#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WaveSolver.h"
#include "WaveBakeActor.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UTexture2D;
class UMaterialInstanceDynamic;
class AAcousticPortal;

/** Result of a run-time query of the baked field (listener, source). */
struct FWaveQuery
{
	float ExtraLossDb = 0.f;      // direct sound below the free field at the same path length
	float LerMinusLdsDb = 0.f;
	float ArrivalMs = 0.f;
	float PathM = 0.f;
	FVector ArrivalDir = FVector::ForwardVector;
	TArray<FVector> Path;          // source ... listener, traced down the arrival-time gradient
	TArray<int32> ProbesUsed;
	TArray<float> Weights;
};

/** Per-probe baked parameters on the coarse listener grid. */
struct FWaveProbeData
{
	FVector Pos;
	TArray<float> ArrivalMs;   // < 0: no energy arrived
	TArray<float> Lds;         // dB re free field at 1 m
	TArray<float> Ler;
	float RefLds = 0.f;
};

UENUM()
enum class EWaveSliceView : uint8 { Off, Voxels, Arrival, Lds, Ler, Ratio, Replay };

/** GPU wave bake of the whole house (report ch. 9.6) and its visualisation: probes, voxels, parameter slices, wave replay. */
UCLASS()
class AUDIOOCCLUSION_API AWaveBakeActor : public AActor
{
	GENERATED_BODY()
public:
	AWaveBakeActor();

	UPROPERTY(EditAnywhere, Category = "Wave") FVector DomainMin = FVector(-1250.f, -250.f, -20.f);
	UPROPERTY(EditAnywhere, Category = "Wave") FVector DomainMax = FVector(2650.f, 2250.f, 400.f);
	UPROPERTY(EditAnywhere, Category = "Wave") float MaxFrequency = 500.f;
	UPROPERTY(EditAnywhere, Category = "Wave") float ProbeSpacing = 260.f;
	UPROPERTY(EditAnywhere, Category = "Wave") float OutdoorProbeSpacing = 500.f;
	UPROPERTY(EditAnywhere, Category = "Wave") float EarHeight = 160.f;
	UPROPERTY(EditAnywhere, Category = "Wave") int32 StepsPerFrame = 300;

	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> ProbeISM;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> ProbeISMActive;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> SlicePlane;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> ProbeStemISM;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> VoxelISM;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> VoxelThinISM;

	void StartBake();
	bool IsBaking() const { return Phase != EPhase::Idle; }
	bool HasData() const { return Probes.Num() > 0 && Probes[0].Lds.Num() > 0; }
	float GetProgress() const;
	FString GetStatusText() const;
	bool Query(const FVector& Listener, const FVector& Source, FWaveQuery& Out) const;

	void ToggleProbes();
	FString CycleSliceView();
	FString ToggleReplay();
	EWaveSliceView GetSliceView() const { return SliceView; }
	/** Switch the visualisation; returns a short message for the HUD toast. */
	FString SetSliceView(EWaveSliceView V);
	void SetProbesVisible(bool bOn);
	bool AreProbesVisible() const { return bShowProbes; }
	bool HasReplay() const { return ReplayFrames > 0; }
	int32 GetReplayProbe() const { return ReplayProbe; }
	/** Probe whose field the slice shows (or that emitted the replayed pulse). */
	bool GetShownProbe(FVector& OutPos, int32& OutIndex) const;
	float GetCellCm() const { return Desc.CellCm; }
	int32 NumProbes() const { return Probes.Num(); }
	static FLinearColor RampColor(float T);
	struct FLegend { FString Title, Lo, Hi, Read; bool bRamp = true; bool bReplay = false; };
	FLegend GetLegend() const;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	enum class EPhase : uint8 { Idle, OpeningDoors, Simulating, Reading };
	EPhase Phase = EPhase::Idle;
	float PhaseTimer = 0.f;
	int32 CurrentProbe = 0;
	double BakeStart = 0.0;
	float LastBakeSeconds = 0.f;
	int32 ReplayProbe = -1;

	FWaveGridDesc Desc;
	TArray<uint32> Flags;
	int32 ThinWallCells = 0;
	TArray<FWaveProbeData> Probes;
	TArray<float> ReplaySlices;
	int32 ReplayFrames = 0;
	float ReplayTime = 0.f;
	TSharedPtr<FWaveSolverGPU, ESPMode::ThreadSafe> Solver;
	FWaveProbeRaw Raw;
	TMap<TObjectPtr<AAcousticPortal>, float> SavedDoors;

	EWaveSliceView SliceView = EWaveSliceView::Off;
	bool bShowProbes = false;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> SliceTex;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> SliceMID;
	TArray<FColor> SlicePixels;
	int32 ShownProbe = -1;
	float SliceRefresh = 0.f;

	void Voxelize();
	void PlaceProbes();
	void ReduceProbe(int32 Index, const FWaveProbeRaw& R);
	void FinishBake();
	bool SaveCache() const;
	bool LoadCache();
	FString CachePath() const;
	uint32 GeometryHash() const;
	int32 NearestProbe(const FVector& P, bool bRequireLOS) const;
	/** Up to three probes that see the listener from the same room, with normalised inverse-square weights. */
	void ListenerProbes(const FVector& L, TArray<int32>& OutIdx, TArray<float>& OutW) const;
	float SampleField(const FWaveProbeData& P, const TArray<float>& Field, const FVector& W) const;
	FVector ArrivalGradient(const FWaveProbeData& P, const FVector& W) const;
	void UpdateProbeVisuals();
	void UpdateVoxelView(bool bOn);
	bool bVoxelsShown = false;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> HiddenForVoxels;
	bool SaveReplay() const;
	bool LoadReplay();
	void UpdateSlice(float Dt);
	FVector SampleWorld(const FIntVector& S) const;
};
