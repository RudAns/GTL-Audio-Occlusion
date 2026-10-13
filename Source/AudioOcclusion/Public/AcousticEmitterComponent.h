#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "OcclusionTypes.h"
#include "AcousticEmitterComponent.generated.h"

class UAudioComponent;
class USoundBase;
class USourceEffectEQPreset;
class USoundEffectSourcePresetChain;
class USoundAttenuation;

/**
 * A sound source handled by the demo. It owns three synchronised voices that play the same loop:
 * the path voice (placed at the real or virtual position), the transmission voice (always at the real
 * position) and the Steam Audio voice. Only the voices of the active solution are audible.
 */
UCLASS(ClassGroup = (Audio), meta = (BlueprintSpawnableComponent))
class AUDIOOCCLUSION_API UAcousticEmitterComponent : public USceneComponent
{
	GENERATED_BODY()
public:
	UAcousticEmitterComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	TObjectPtr<USoundBase> Sound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	FString DisplayName = TEXT("声源");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	FLinearColor Color = FLinearColor(1.f, 0.6f, 0.2f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	float BaseVolume = 1.f;

	/** Distance where the 1/r roll-off starts (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	float InnerRadius = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	float FalloffDistance = 4500.f;

	UPROPERTY(Transient) TObjectPtr<UAudioComponent> PathVoice;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> TransVoice;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> SteamVoice;
	UPROPERTY(Transient) TObjectPtr<USourceEffectEQPreset> PathEQ;
	UPROPERTY(Transient) TObjectPtr<USourceEffectEQPreset> TransEQ;
	UPROPERTY(Transient) TObjectPtr<USoundEffectSourcePresetChain> PathChain;
	UPROPERTY(Transient) TObjectPtr<USoundEffectSourcePresetChain> TransChain;

	/** One-shot source (footsteps): the voices are re-triggered with TriggerOneShot instead of looping. */
	bool bOneShot = false;
	void TriggerOneShot(float Pitch = 1.f);
	bool bMuted = false;
	bool bSolo = false;

	// Smoothed state
	FAppliedVoice AppliedPath;
	FAppliedVoice AppliedTrans;
	float AppliedWetDb = -60.f;
	FBand3 SteamDb = FBand3::Splat(0.f);   // Steam Audio direct-path gain per band, for the HUD
	FAcousticResult Last;
	TArray<FVector> PrevPathPoints;
	float PathSwitchTimer = 0.f;
	int32 SolveSlot = 0;

	FVector GetSourceLocation() const { return GetComponentLocation(); }

	void StartVoices(USoundAttenuation* SteamAttenuation);
	void SetNativeOcclusion(bool bEnable);
	/** Apply band gains (dB, relative) to a voice: mid -> volume, low/high -> peaking EQ, deep high cut -> LPF. */
	static void ApplyBands(UAudioComponent* Voice, USourceEffectEQPreset* EQ, const FBand3& GainDb, float Base);
	void SetVoiceAudible(UAudioComponent* Voice, bool bAudible);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

protected:
	UAudioComponent* MakeVoice(const TCHAR* Name);
};
