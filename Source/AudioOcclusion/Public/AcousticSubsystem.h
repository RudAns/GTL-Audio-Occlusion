#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "OcclusionTypes.h"
#include "AcousticSubsystem.generated.h"

class UAcousticEmitterComponent;
class AAcousticRoom;
class AAcousticPortal;
class USoundAttenuation;
class USoundSubmix;
class USubmixEffectReverbPreset;
class USubmixEffectFilterPreset;
class AWaveBakeActor;

UENUM(BlueprintType)
enum class ESolution : uint8 { Custom, WaveBake, SteamAudio };

/** Which layers are active. Steps produce cumulative configs; comparison groups produce explicit ones. */
USTRUCT(BlueprintType)
struct FPipelineConfig
{
	GENERATED_BODY()
	UPROPERTY() ESolution Solution = ESolution::Custom;
	UPROPERTY() bool bNative = false;
	UPROPERTY() bool bMultiRay = false;
	UPROPERTY() bool bPortal = false;
	UPROPERTY() bool bExcess = false;
	UPROPERTY() bool bTransmission = false;
	UPROPERTY() bool bReverb = false;
	UPROPERTY() bool bSmoothing = false;
	UPROPERTY() bool bDynamicDoors = false;   // wave bake + door loss on top (group 6A)
	UPROPERTY() bool bSteamPlusPortal = false; // Steam Audio direct path + custom portal paths (group 6B)
};

struct FComparisonGroup
{
	FString Title;
	FString Question;
	TArray<FString> Labels;          // A, B, (C)
	TArray<FPipelineConfig> Configs;
};

UCLASS()
class AUDIOOCCLUSION_API UAcousticSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAcousticSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickableInEditor() const override { return false; }

	void RegisterEmitter(UAcousticEmitterComponent* E);
	void UnregisterEmitter(UAcousticEmitterComponent* E);

	// ---- modes ----
	void SetStep(EOcclusionStep Step);
	EOcclusionStep GetStep() const { return Step; }
	void SetGroup(int32 Index);             // -1 = step mode
	int32 GetGroup() const { return Group; }
	void CycleVariant();                    // A -> B (-> C)
	void SetVariant(int32 V);
	/** Free combination picked with the mouse; leaves group mode. */
	void SetConfig(const FPipelineConfig& C);
	/** Preset step whose configuration equals the current one, or -1 for a free combination. */
	int32 MatchingStep() const;
	/** The most specific layer in a configuration, used to pick the explanation text. */
	static EOcclusionStep RepresentativeStep(const FPipelineConfig& C);
	static bool SameConfig(const FPipelineConfig& A, const FPipelineConfig& B);
	int32 GetVariant() const { return Variant; }
	const TArray<FComparisonGroup>& GetGroups() const { return Groups; }
	const FPipelineConfig& GetConfig() const { return Config; }
	static FPipelineConfig ConfigForStep(EOcclusionStep S);
	static FString StepTitle(EOcclusionStep S);
	static FString StepExplain(EOcclusionStep S);
	TArray<FString> ActiveLayerNames() const;

	// ---- debug / HUD ----
	bool bDebugDraw = true;
	int32 FocusEmitter = 0;
	TArray<UAcousticEmitterComponent*> GetEmitters() const;
	UAcousticEmitterComponent* GetFocused() const;
	void CycleFocus(int32 Dir);
	void ToggleSolo();
	FVector ListenerPos = FVector::ZeroVector;
	FVector ListenerFwd = FVector::ForwardVector;
	AAcousticRoom* ListenerRoom = nullptr;
	float SolveMs = 0.f;
	const TArray<TObjectPtr<AAcousticRoom>>& GetRooms() const { return Rooms; }
	const TArray<TObjectPtr<AAcousticPortal>>& GetPortals() const { return Portals; }
	AAcousticRoom* RoomAt(const FVector& P) const;
	AAcousticPortal* NearestDoor(const FVector& P, float MaxDist) const;

	UPROPERTY(Transient) TObjectPtr<AWaveBakeActor> WaveBake;

	// ---- solver building blocks (public for the wave-bake comparison) ----
	static float KurzeAndersonDb(float ExcessM, float FreqHz);
	bool TraceBlocked(const FVector& A, const FVector& B, const AActor* IgnoreA) const;
	float MultiRayFraction(const UAcousticEmitterComponent* E, const FVector& S, const FVector& L, FAcousticResult* Out) const;
	bool FindPortalPath(const FVector& S, AAcousticRoom* RS, const FVector& L, AAcousticRoom* RL, TArray<FVector>& OutPts, TArray<AAcousticPortal*>& OutPortals) const;
	FBand3 TransmissionLoss(const FVector& S, const FVector& L, const AActor* Ignore, TArray<FVector>* OutHits) const;

protected:
	void Gather();
	void InitAudioGraph();
	void UpdateListener();
	void Solve(UAcousticEmitterComponent* E, FAcousticResult& R);
	void SolveCustom(UAcousticEmitterComponent* E, FAcousticResult& R);
	void SolveWave(UAcousticEmitterComponent* E, FAcousticResult& R);
	void ApplyResult(UAcousticEmitterComponent* E, const FAcousticResult& R, float Dt);
	void UpdateRoomCoupling(float Dt);
	void DrawDebug(UAcousticEmitterComponent* E, const FAcousticResult& R) const;
	void BuildGroups();

	UPROPERTY(Transient) TArray<TObjectPtr<AAcousticRoom>> Rooms;
	UPROPERTY(Transient) TArray<TObjectPtr<AAcousticPortal>> Portals;
	UPROPERTY(Transient) TArray<TObjectPtr<UAcousticEmitterComponent>> Emitters;
	UPROPERTY(Transient) TObjectPtr<USoundAttenuation> SteamAttenuation;
	UPROPERTY(Transient) TMap<TObjectPtr<AAcousticRoom>, TObjectPtr<USubmixEffectFilterPreset>> RoomFilters;
	TMap<AAcousticRoom*, float> RoomCouplingDb;

	EOcclusionStep Step = EOcclusionStep::Smoothing;
	int32 Group = -1;
	int32 Variant = 0;
	FPipelineConfig Config;
	TArray<FComparisonGroup> Groups;
	float SolveAccum = 0.f;
	bool bGathered = false;
	bool bAudioStarted = false;
};
