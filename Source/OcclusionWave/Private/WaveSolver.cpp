#include "WaveSolver.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIGPUReadback.h"
#include "RenderingThread.h"
#include "ShaderCompilerCore.h"
#include "DataDrivenShaderPlatformInfo.h"

class FWaveStepCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FWaveStepCS);
	SHADER_USE_PARAMETER_STRUCT(FWaveStepCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FUintVector3, Dims)
		SHADER_PARAMETER(float, Lambda2)
		SHADER_PARAMETER(FUintVector3, SrcCell)
		SHADER_PARAMETER(float, SrcValue)
		SHADER_PARAMETER(float, WallLoss)
		SHADER_PARAMETER(uint32, SpongeWidth)
		SHADER_PARAMETER(float, SpongeMax)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, PCur)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, PPrevNext)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, Flags)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P) { return IsFeatureLevelSupported(P.Platform, ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FWaveStepCS, "/OcclusionWave/WaveFDTD.usf", "StepCS", SF_Compute);

class FWaveAccumCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FWaveAccumCS);
	SHADER_USE_PARAMETER_STRUCT(FWaveAccumCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FUintVector3, Dims)
		SHADER_PARAMETER(FUintVector3, SampleDims)
		SHADER_PARAMETER(uint32, SampleStride)
		SHADER_PARAMETER(uint32, Bin)
		SHADER_PARAMETER(uint32, NumBins)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, PNow)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, Env)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P) { return IsFeatureLevelSupported(P.Platform, ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FWaveAccumCS, "/OcclusionWave/WaveFDTD.usf", "AccumCS", SF_Compute);

class FWaveSliceCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FWaveSliceCS);
	SHADER_USE_PARAMETER_STRUCT(FWaveSliceCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FUintVector3, Dims)
		SHADER_PARAMETER(uint32, SliceZ)
		SHADER_PARAMETER(uint32, Frame)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, PNow)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, Slices)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P) { return IsFeatureLevelSupported(P.Platform, ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FWaveSliceCS, "/OcclusionWave/WaveFDTD.usf", "SliceCS", SF_Compute);

static FUintVector3 U3(const FIntVector& V) { return FUintVector3((uint32)V.X, (uint32)V.Y, (uint32)V.Z); }

FWaveSolverGPU::~FWaveSolverGPU()
{
	delete EnvReadback; delete SliceReadback;
}

void FWaveSolverGPU::Init(const FWaveGridDesc& InDesc, const TArray<uint32>& InFlags, float InWallLoss)
{
	Desc = InDesc; Flags = InFlags; WallLoss = InWallLoss; bFlagsUploaded = false;
}

void FWaveSolverGPU::Release()
{
	TSharedPtr<FWaveSolverGPU, ESPMode::ThreadSafe> Self = AsShared();
	ENQUEUE_RENDER_COMMAND(WaveRelease)([Self](FRHICommandListImmediate&)
	{
		Self->PA.SafeRelease(); Self->PB.SafeRelease(); Self->FlagBuf.SafeRelease(); Self->EnvBuf.SafeRelease(); Self->SliceBuf.SafeRelease();
	});
}

void FWaveSolverGPU::BeginProbe(const FIntVector& SrcCell, bool bCaptureSlices, int32 InSliceZ)
{
	Src = SrcCell; Step = 0; bSlices = bCaptureSlices; SliceZ = InSliceZ;
	bReadbackRequested = false; bReadbackEnqueued = false;
	TSharedPtr<FWaveSolverGPU, ESPMode::ThreadSafe> Self = AsShared();
	const bool bUploadFlags = !bFlagsUploaded;
	bFlagsUploaded = true;
	ENQUEUE_RENDER_COMMAND(WaveBegin)([Self, bUploadFlags](FRHICommandListImmediate& RHICmdList)
	{
		FRDGBuilder GB(RHICmdList);
		const FWaveGridDesc& D = Self->Desc;
		const int32 NC = D.NumCells();
		const FIntVector SD = D.SampleDims();
		const int32 NS = SD.X * SD.Y * SD.Z;
		auto Make = [&GB](TRefCountPtr<FRDGPooledBuffer>& Pooled, uint32 Bytes, uint32 Count, const TCHAR* Name) -> FRDGBufferRef
		{
			if (!Pooled.IsValid() || Pooled->GetSize() != Bytes * Count)
			{
				FRDGBufferRef B = GB.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(Bytes, Count), Name);
				Pooled = GB.ConvertToExternalBuffer(B);
				return B;
			}
			return GB.RegisterExternalBuffer(Pooled);
		};
		FRDGBufferRef A = Make(Self->PA, 4, NC, TEXT("Wave.PA"));
		FRDGBufferRef B = Make(Self->PB, 4, NC, TEXT("Wave.PB"));
		FRDGBufferRef F = Make(Self->FlagBuf, 4, NC, TEXT("Wave.Flags"));
		FRDGBufferRef E = Make(Self->EnvBuf, 4, NS * D.NumBins(), TEXT("Wave.Env"));
		const int32 Frames = D.NumSteps / D.SliceEvery + 1;
		FRDGBufferRef S = Make(Self->SliceBuf, 4, Self->bSlices ? Frames * D.Dims.X * D.Dims.Y : 1, TEXT("Wave.Slices"));
		AddClearUAVPass(GB, GB.CreateUAV(A), 0u);
		AddClearUAVPass(GB, GB.CreateUAV(B), 0u);
		AddClearUAVPass(GB, GB.CreateUAV(E), 0u);
		AddClearUAVPass(GB, GB.CreateUAV(S), 0u);
		if (bUploadFlags) GB.QueueBufferUpload(F, Self->Flags.GetData(), Self->Flags.Num() * sizeof(uint32), ERDGInitialDataFlags::None);
		GB.Execute();
		Self->bAIsCur = true;
	});
}

bool FWaveSolverGPU::Advance(int32 N)
{
	const int32 Count = FMath::Min(N, Desc.NumSteps - Step);
	if (Count <= 0) return true;
	TSharedPtr<FWaveSolverGPU, ESPMode::ThreadSafe> Self = AsShared();
	const int32 StartStep = Step;
	// Gaussian pulse: sigma = sqrt(ln10) / (pi * fmax), t0 = 5 sigma (Raghuvanshi & Snyder 2014, eq. 1).
	const double FMax = 343.0 / (Desc.CellCm * 0.01 * 5.0);
	const double Sigma = FMath::Sqrt(FMath::Loge(10.0)) / (PI * FMax);
	const double T0 = 5.0 * Sigma;
	TArray<float> SrcSamples;
	SrcSamples.SetNum(Count);
	for (int32 k = 0; k < Count; ++k)
	{
		const double T = (StartStep + k) * Desc.Dt;
		SrcSamples[k] = (float)FMath::Exp(-FMath::Square((T - T0) / Sigma));
	}
	ENQUEUE_RENDER_COMMAND(WaveAdvance)([Self, StartStep, Count, SrcSamples](FRHICommandListImmediate& RHICmdList)
	{
		FRDGBuilder GB(RHICmdList);
		const FWaveGridDesc& D = Self->Desc;
		FRDGBufferRef A = GB.RegisterExternalBuffer(Self->PA);
		FRDGBufferRef B = GB.RegisterExternalBuffer(Self->PB);
		FRDGBufferRef F = GB.RegisterExternalBuffer(Self->FlagBuf);
		FRDGBufferRef E = GB.RegisterExternalBuffer(Self->EnvBuf);
		FRDGBufferRef S = GB.RegisterExternalBuffer(Self->SliceBuf);
		FRDGBufferSRVRef FSrv = GB.CreateSRV(F);
		FRDGBufferUAVRef EUav = GB.CreateUAV(E);
		FRDGBufferUAVRef SUav = GB.CreateUAV(S);
		FRDGBufferSRVRef ASrv = GB.CreateSRV(A), BSrv = GB.CreateSRV(B);
		FRDGBufferUAVRef AUav = GB.CreateUAV(A), BUav = GB.CreateUAV(B);
		TShaderMapRef<FWaveStepCS> StepCS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FWaveAccumCS> AccumCS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FWaveSliceCS> SliceCS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		const FIntVector SD = D.SampleDims();
		const int32 NS = SD.X * SD.Y * SD.Z;
		bool bACur = Self->bAIsCur;
		for (int32 k = 0; k < Count; ++k)
		{
			const int32 StepIdx = StartStep + k;
			FWaveStepCS::FParameters* P = GB.AllocParameters<FWaveStepCS::FParameters>();
			P->Dims = U3(D.Dims);
			P->Lambda2 = 0.25f;
			P->SrcCell = U3(Self->Src);
			P->SrcValue = SrcSamples[k];
			P->WallLoss = Self->WallLoss;
			P->SpongeWidth = 10;
			P->SpongeMax = 0.08f;
			P->PCur = bACur ? ASrv : BSrv;
			P->PPrevNext = bACur ? BUav : AUav;
			P->Flags = FSrv;
			FComputeShaderUtils::AddPass(GB, RDG_EVENT_NAME("WaveStep"), StepCS, P, FComputeShaderUtils::GetGroupCount(D.Dims, FIntVector(8, 8, 4)));
			bACur = !bACur;   // the buffer just written holds the new current field

			FWaveAccumCS::FParameters* AP = GB.AllocParameters<FWaveAccumCS::FParameters>();
			AP->Dims = U3(D.Dims); AP->SampleDims = U3(SD); AP->SampleStride = D.SampleStride;
			AP->Bin = StepIdx / D.StepsPerBin; AP->NumBins = D.NumBins();
			AP->PNow = bACur ? ASrv : BSrv; AP->Env = EUav;
			FComputeShaderUtils::AddPass(GB, RDG_EVENT_NAME("WaveAccum"), AccumCS, AP, FIntVector(FMath::DivideAndRoundUp(NS, 64), 1, 1));

			if (Self->bSlices && (StepIdx % D.SliceEvery) == 0)
			{
				FWaveSliceCS::FParameters* SP = GB.AllocParameters<FWaveSliceCS::FParameters>();
				SP->Dims = U3(D.Dims); SP->SliceZ = Self->SliceZ; SP->Frame = StepIdx / D.SliceEvery;
				SP->PNow = bACur ? ASrv : BSrv; SP->Slices = SUav;
				FComputeShaderUtils::AddPass(GB, RDG_EVENT_NAME("WaveSlice"), SliceCS, SP, FComputeShaderUtils::GetGroupCount(FIntVector(D.Dims.X, D.Dims.Y, 1), FIntVector(8, 8, 1)));
			}
		}
		Self->bAIsCur = bACur;
		GB.Execute();
	});
	Step += Count;
	return Step >= Desc.NumSteps;
}

void FWaveSolverGPU::RequestReadback()
{
	if (bReadbackRequested) return;
	bReadbackRequested = true;
	TSharedPtr<FWaveSolverGPU, ESPMode::ThreadSafe> Self = AsShared();
	ENQUEUE_RENDER_COMMAND(WaveReadback)([Self](FRHICommandListImmediate& RHICmdList)
	{
		if (!Self->EnvReadback) Self->EnvReadback = new FRHIGPUBufferReadback(TEXT("Wave.EnvReadback"));
		if (!Self->SliceReadback) Self->SliceReadback = new FRHIGPUBufferReadback(TEXT("Wave.SliceReadback"));
		FRDGBuilder GB(RHICmdList);
		FRDGBufferRef E = GB.RegisterExternalBuffer(Self->EnvBuf);
		AddEnqueueCopyPass(GB, Self->EnvReadback, E, Self->EnvBuf->GetSize());
		if (Self->bSlices)
		{
			FRDGBufferRef S = GB.RegisterExternalBuffer(Self->SliceBuf);
			AddEnqueueCopyPass(GB, Self->SliceReadback, S, Self->SliceBuf->GetSize());
		}
		GB.Execute();
		Self->bReadbackEnqueued = true;
	});
}

bool FWaveSolverGPU::TryFinish(FWaveProbeRaw& Out)
{
	if (!bReadbackEnqueued) return false;
	bool bDone = false;
	TSharedPtr<FWaveSolverGPU, ESPMode::ThreadSafe> Self = AsShared();
	FWaveProbeRaw* OutPtr = &Out;
	// Poll on the render thread and copy out when ready.
	ENQUEUE_RENDER_COMMAND(WaveFinish)([Self, OutPtr, &bDone](FRHICommandListImmediate&)
	{
		const bool bEnvReady = Self->EnvReadback && Self->EnvReadback->IsReady();
		const bool bSliceReady = !Self->bSlices || (Self->SliceReadback && Self->SliceReadback->IsReady());
		if (!bEnvReady || !bSliceReady) return;
		const uint32 EnvBytes = Self->EnvBuf->GetSize();
		OutPtr->Env.SetNumUninitialized(EnvBytes / 4);
		const void* E = Self->EnvReadback->Lock(EnvBytes);
		FMemory::Memcpy(OutPtr->Env.GetData(), E, EnvBytes);
		Self->EnvReadback->Unlock();
		if (Self->bSlices)
		{
			const uint32 SB = Self->SliceBuf->GetSize();
			OutPtr->Slices.SetNumUninitialized(SB / 4);
			const void* S = Self->SliceReadback->Lock(SB);
			FMemory::Memcpy(OutPtr->Slices.GetData(), S, SB);
			Self->SliceReadback->Unlock();
			OutPtr->SliceFrames = Self->Desc.NumSteps / Self->Desc.SliceEvery + 1;
		}
		bDone = true;
	});
	FlushRenderingCommands();
	return bDone;
}
