#pragma once

#include "CoreMinimal.h"
#include "OcclusionTypes.generated.h"

/** Three bands used throughout the demo (centre frequencies 250 Hz, 1 kHz, 4 kHz). */
struct FBand3
{
	float V[3] = { 0.f, 0.f, 0.f };
	FBand3() = default;
	FBand3(float A, float B, float C) { V[0] = A; V[1] = B; V[2] = C; }
	static FBand3 Splat(float X) { return FBand3(X, X, X); }
	float& operator[](int32 I) { return V[I]; }
	float operator[](int32 I) const { return V[I]; }
	FBand3 operator+(const FBand3& O) const { return FBand3(V[0] + O.V[0], V[1] + O.V[1], V[2] + O.V[2]); }
	FBand3 operator-(const FBand3& O) const { return FBand3(V[0] - O.V[0], V[1] - O.V[1], V[2] - O.V[2]); }
	FBand3 operator*(float S) const { return FBand3(V[0] * S, V[1] * S, V[2] * S); }
};

static constexpr float GBandHz[3] = { 250.f, 1000.f, 4000.f };

/** The ten step switches (report ch. 9.1). Steps 1-8 build up one custom pipeline; 9 and 10 swap the whole solution. */
UENUM(BlueprintType)
enum class EOcclusionStep : uint8
{
	None = 0,        // 1  distance attenuation only
	UENative,        // 2  UE attenuation occlusion: one trace, binary
	MultiRay,        // 3  fraction of rays blocked
	PortalPath,      // 4  rooms & portals, sound re-positioned at the doorway
	ExcessMapping,   // 5  diffraction loss from excess path length per band (vs. angle)
	Transmission,    // 6  through-wall component per material, energy sum with the path
	ReverbCoupling,  // 7  each room's reverb fed by its own sources, heard through portals
	Smoothing,       // 8  parameter smoothing, path hysteresis and cross-fade
	WaveBake,        // 9  GPU wave bake parameters
	SteamAudio,      // 10 Steam Audio occlusion / transmission / pathing
	Count UMETA(Hidden)
};

UENUM(BlueprintType)
enum class EAcousticMaterial : uint8
{
	Concrete,
	WoodPartition,
	Glass,
	Door,
	Furniture
};

/** Per-band transmission loss in dB (field-incidence mass law with flanking caps, report ch. 1 and 6.4). */
inline FBand3 TransmissionLossDb(EAcousticMaterial M)
{
	switch (M)
	{
	case EAcousticMaterial::Concrete:      return FBand3(42.f, 52.f, 60.f);
	case EAcousticMaterial::WoodPartition: return FBand3(14.f, 22.f, 30.f);
	case EAcousticMaterial::Glass:         return FBand3(18.f, 26.f, 30.f);
	case EAcousticMaterial::Door:          return FBand3(16.f, 22.f, 27.f);
	default:                               return FBand3(4.f, 8.f, 12.f);
	}
}

inline const TCHAR* AcousticMaterialName(EAcousticMaterial M)
{
	switch (M)
	{
	case EAcousticMaterial::Concrete:      return TEXT("混凝土墙");
	case EAcousticMaterial::WoodPartition: return TEXT("木隔墙");
	case EAcousticMaterial::Glass:         return TEXT("玻璃");
	case EAcousticMaterial::Door:          return TEXT("木门");
	default:                               return TEXT("家具");
	}
}

/** What the solver hands to the renderer for one emitter, before smoothing. */
struct FAcousticResult
{
	// Main (path) voice
	FVector VoicePos = FVector::ZeroVector;   // where the main voice is placed (virtual source)
	FBand3 PathGainDb;                        // excess loss over the distance curve, per band
	bool bPathAudible = true;
	// Transmission voice (through walls, at the real position)
	FBand3 TransGainDb = FBand3::Splat(-120.f);
	bool bTransAudible = false;
	// Reverb
	float WetDb = -12.f;
	int32 SourceRoom = INDEX_NONE;
	// Read-outs for the HUD and debug draw
	float PathLengthM = 0.f;
	float DirectM = 0.f;
	float ExcessM = 0.f;
	float BendDeg = 0.f;
	float OccludedFraction = 0.f;
	bool bNativeBlocked = false;
	int32 PortalCount = 0;
	TArray<FVector> PathPoints;               // source ... listener
	TArray<TPair<FVector, FVector>> Rays;     // multi-ray debug
	TArray<bool> RayBlocked;
	TArray<FVector> TransHits;
	FString Note;
};

/** Smoothed values actually applied to audio components. */
struct FAppliedVoice
{
	FBand3 GainDb = FBand3::Splat(0.f);
	FVector Pos = FVector::ZeroVector;
	bool bInit = false;
};
