#pragma once

#include "CoreMinimal.h"
#include "RenderGraphResources.h"

class FRHIGPUBufferReadback;

/** Voxel grid + GPU FDTD runner for one probe at a time (report ch. 9.6). All sizes in cells. */
struct OCCLUSIONWAVE_API FWaveGridDesc
{
	FIntVector Dims = FIntVector(0);
	FVector Origin = FVector::ZeroVector;    // world position (cm) of cell (0,0,0) corner
	float CellCm = 13.7f;
	float Dt = 0.f;                           // seconds per step
	int32 SampleStride = 4;                   // coarse listener grid stride (cells)
	int32 StepsPerBin = 4;
	int32 NumSteps = 4000;
	int32 SliceEvery = 8;
	FIntVector SampleDims() const { return FIntVector(FMath::DivideAndRoundUp(Dims.X, SampleStride), FMath::DivideAndRoundUp(Dims.Y, SampleStride), FMath::DivideAndRoundUp(Dims.Z, SampleStride)); }
	int32 NumBins() const { return FMath::DivideAndRoundUp(NumSteps, StepsPerBin); }
	int32 NumCells() const { return Dims.X * Dims.Y * Dims.Z; }
	FVector CellCenter(const FIntVector& C) const { return Origin + (FVector(C) + 0.5f) * CellCm; }
	FIntVector CellOf(const FVector& P) const
	{
		const FVector L = (P - Origin) / CellCm;
		return FIntVector(FMath::Clamp(FMath::FloorToInt(L.X), 0, Dims.X - 1), FMath::Clamp(FMath::FloorToInt(L.Y), 0, Dims.Y - 1), FMath::Clamp(FMath::FloorToInt(L.Z), 0, Dims.Z - 1));
	}
};

/** Output of one probe simulation, read back to the CPU. */
struct OCCLUSIONWAVE_API FWaveProbeRaw
{
	TArray<float> Env;       // SampleCount * NumBins energy envelope
	TArray<float> Slices;    // optional: frames * Dims.X * Dims.Y
	int32 SliceFrames = 0;
};

/** Drives the compute shaders over several frames. Game-thread API; work is enqueued to the render thread. */
class OCCLUSIONWAVE_API FWaveSolverGPU : public TSharedFromThis<FWaveSolverGPU, ESPMode::ThreadSafe>
{
public:
	~FWaveSolverGPU();
	void Init(const FWaveGridDesc& InDesc, const TArray<uint32>& InFlags, float InWallLoss);
	/** Start a probe: clears fields and accumulators. */
	void BeginProbe(const FIntVector& SrcCell, bool bCaptureSlices, int32 SliceZ);
	/** Advance up to N steps this frame. Returns true when the probe has finished all steps. */
	bool Advance(int32 N);
	/** After Advance returned true: start the GPU->CPU copy. Poll with TryFinish. */
	void RequestReadback();
	bool TryFinish(FWaveProbeRaw& Out);
	int32 GetStep() const { return Step; }
	const FWaveGridDesc& GetDesc() const { return Desc; }
	void Release();

private:
	FWaveGridDesc Desc;
	float WallLoss = 0.99f;
	int32 Step = 0;
	FIntVector Src;
	bool bSlices = false;
	int32 SliceZ = 0;
	TArray<uint32> Flags;
	bool bFlagsUploaded = false;

	// Render-thread owned
	TRefCountPtr<FRDGPooledBuffer> PA, PB, FlagBuf, EnvBuf, SliceBuf;
	bool bAIsCur = true;
	FRHIGPUBufferReadback* EnvReadback = nullptr;
	FRHIGPUBufferReadback* SliceReadback = nullptr;
	bool bReadbackRequested = false;
	FThreadSafeBool bReadbackEnqueued = false;
};
