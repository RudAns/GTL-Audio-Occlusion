#include "AcousticEmitterComponent.h"
#include "AcousticSubsystem.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundEffectSource.h"
#include "SourceEffects/SourceEffectEQ.h"

UAcousticEmitterComponent::UAcousticEmitterComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

static FSoundAttenuationSettings MakeAttenuation(float Inner, float Falloff)
{
	FSoundAttenuationSettings A;
	A.bAttenuate = true;
	A.bSpatialize = true;
	A.SpatializationAlgorithm = ESoundSpatializationAlgorithm::SPATIALIZATION_HRTF;   // Steam Audio binaural renderer
	A.AttenuationShape = EAttenuationShape::Sphere;
	A.AttenuationShapeExtents = FVector(Inner, 0.f, 0.f);
	A.FalloffDistance = Falloff;
	A.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
	A.dBAttenuationAtMax = -60.f;
	A.bEnableOcclusion = false;
	A.bEnableReverbSend = false;
	A.bEnableLogFrequencyScaling = true;
	A.OcclusionTraceChannel = ECC_GameTraceChannel1;      // "Acoustic" channel
	A.OcclusionLowPassFilterFrequency = 600.f;            // values the engine needs to actually do something (report ch. 9.2)
	A.OcclusionVolumeAttenuation = 0.25f;
	A.OcclusionInterpolationTime = 0.1f;
	return A;
}

UAudioComponent* UAcousticEmitterComponent::MakeVoice(const TCHAR* Name)
{
	AActor* Owner = GetOwner();
	UAudioComponent* V = NewObject<UAudioComponent>(Owner, Name);
	V->bAutoActivate = false;
	V->bAutoDestroy = false;
	V->bStopWhenOwnerDestroyed = true;
	V->bAllowSpatialization = true;
	V->bOverrideAttenuation = true;
	V->AttenuationOverrides = MakeAttenuation(InnerRadius, FalloffDistance);
	V->SetUsingAbsoluteLocation(true);
	V->SetupAttachment(Owner->GetRootComponent());
	V->RegisterComponent();
	V->SetSound(Sound);
	V->SetWorldLocation(GetComponentLocation());
	return V;
}

void UAcousticEmitterComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UAcousticSubsystem* Sys = GetWorld()->GetSubsystem<UAcousticSubsystem>())
	{
		Sys->RegisterEmitter(this);
	}
}

void UAcousticEmitterComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UAcousticSubsystem* Sys = GetWorld() ? GetWorld()->GetSubsystem<UAcousticSubsystem>() : nullptr)
	{
		Sys->UnregisterEmitter(this);
	}
	Super::EndPlay(Reason);
}

void UAcousticEmitterComponent::StartVoices(USoundAttenuation* SteamAttenuation)
{
	if (!Sound || PathVoice) return;
	PathVoice = MakeVoice(TEXT("PathVoice"));
	TransVoice = MakeVoice(TEXT("TransVoice"));
	SteamVoice = MakeVoice(TEXT("SteamVoice"));
	if (SteamAttenuation)
	{
		SteamVoice->bOverrideAttenuation = false;
		SteamVoice->AttenuationSettings = SteamAttenuation;
	}
	// The Steam Audio occlusion plugin reads the source position from the owner actor, so its voice stays at the owner.
	SteamVoice->SetUsingAbsoluteLocation(false);
	SteamVoice->SetRelativeLocation(FVector::ZeroVector);

	auto MakeEQ = [this](const TCHAR* N, TObjectPtr<USourceEffectEQPreset>& OutEQ, TObjectPtr<USoundEffectSourcePresetChain>& OutChain)
	{
		OutEQ = NewObject<USourceEffectEQPreset>(this, N);
		FSourceEffectEQSettings S;
		FSourceEffectEQBand Low;  Low.Frequency = 180.f;  Low.Bandwidth = 2.f; Low.GainDb = 0.f; Low.bEnabled = true;
		FSourceEffectEQBand High; High.Frequency = 4500.f; High.Bandwidth = 2.f; High.GainDb = 0.f; High.bEnabled = true;
		S.EQBands = { Low, High };
		OutEQ->SetSettings(S);
		OutChain = NewObject<USoundEffectSourcePresetChain>(this);
		FSourceEffectChainEntry Entry; Entry.Preset = OutEQ; Entry.bBypass = false;
		OutChain->Chain.Add(Entry);
	};
	MakeEQ(TEXT("PathEQ"), PathEQ, PathChain);
	MakeEQ(TEXT("TransEQ"), TransEQ, TransChain);
	if (!FParse::Param(FCommandLine::Get(), TEXT("OccNoEQ")))
	{
		PathVoice->SetSourceEffectChain(PathChain);
		TransVoice->SetSourceEffectChain(TransChain);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("OccPanning")))
	{
		for (UAudioComponent* V : { PathVoice.Get(), TransVoice.Get() }) V->AttenuationOverrides.SpatializationAlgorithm = ESoundSpatializationAlgorithm::SPATIALIZATION_Default;
	}

	PathVoice->SetVolumeMultiplier(BaseVolume);
	TransVoice->SetVolumeMultiplier(0.001f);
	SteamVoice->SetVolumeMultiplier(0.001f);
	if (bOneShot) return;   // started by TriggerOneShot
	// Start all three on the same frame so the loops stay aligned.
	PathVoice->Play(0.f);
	TransVoice->Play(0.f);
	if (!FParse::Param(FCommandLine::Get(), TEXT("OccNoSteamVoice"))) SteamVoice->Play(0.f);
}

void UAcousticEmitterComponent::TriggerOneShot(float Pitch)
{
	// All three voices restart together; their gains, filters and positions are kept up to date by the subsystem.
	for (UAudioComponent* V : { PathVoice.Get(), TransVoice.Get(), SteamVoice.Get() })
	{
		if (!V) continue;
		V->SetPitchMultiplier(Pitch);
		V->Play(0.f);
	}
}

void UAcousticEmitterComponent::SetNativeOcclusion(bool bEnable)
{
	if (!PathVoice) return;
	if (PathVoice->AttenuationOverrides.bEnableOcclusion != bEnable)
	{
		FSoundAttenuationSettings A = PathVoice->AttenuationOverrides;
		A.bEnableOcclusion = bEnable;
		PathVoice->AdjustAttenuation(A);
	}
}

void UAcousticEmitterComponent::ApplyBands(UAudioComponent* Voice, USourceEffectEQPreset* EQ, const FBand3& GainDb, float Base)
{
	if (!Voice) return;
	const float Mid = FMath::Clamp(GainDb[1], -80.f, 12.f);
	Voice->SetVolumeMultiplier(FMath::Max(Base * FMath::Pow(10.f, Mid / 20.f), 0.0005f));
	const float LowRel = FMath::Clamp(GainDb[0] - Mid, -30.f, 20.f);
	const float HighRel = FMath::Clamp(GainDb[2] - Mid, -40.f, 12.f);
	if (EQ)
	{
		FSourceEffectEQSettings S = EQ->Settings;
		if (S.EQBands.Num() >= 2)
		{
			S.EQBands[0].GainDb = LowRel;
			S.EQBands[1].GainDb = FMath::Max(HighRel, -30.f);
			EQ->SetSettings(S);
		}
	}
	// One-pole LPF for the deep high cut that a single peaking band cannot reach.
	if (HighRel < -10.f)
	{
		const float Fc = FMath::Clamp(20000.f * FMath::Pow(2.f, (HighRel + 10.f) / 4.f), 400.f, 20000.f);
		Voice->SetLowPassFilterEnabled(true);
		Voice->SetLowPassFilterFrequency(Fc);
	}
	else
	{
		Voice->SetLowPassFilterEnabled(false);
	}
}

void UAcousticEmitterComponent::SetVoiceAudible(UAudioComponent* Voice, bool bAudible)
{
	if (Voice && !bAudible) Voice->SetVolumeMultiplier(0.0005f);
}
