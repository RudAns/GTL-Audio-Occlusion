#include "WaveBakeActor.h"
#include "AcousticDoor.h"
#include "AcousticPortal.h"
#include "AcousticRoom.h"
#include "AcousticSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

DEFINE_LOG_CATEGORY_STATIC(LogWaveBake, Log, All);

static constexpr float C_CmPerS = 34300.f;
static constexpr uint32 FlagSolid = 1u, FlagNearWall = 2u, FlagThin = 4u;
// A bake shipped with the project (BakeData/) is used when this machine has not baked yet.
static FString ShippedBakePath(const TCHAR* File) { return FPaths::ProjectDir() / TEXT("BakeData") / File; }

AWaveBakeActor::AWaveBakeActor()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
	ProbeISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Probes"));
	ProbeISM->SetupAttachment(RootComponent);
	ProbeISMActive = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ProbesActive"));
	ProbeISMActive->SetupAttachment(RootComponent);
	SlicePlane = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Slice"));
	SlicePlane->SetupAttachment(RootComponent);
	ProbeStemISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ProbeStems"));
	ProbeStemISM->SetupAttachment(RootComponent);
	VoxelISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Voxels"));
	VoxelISM->SetupAttachment(RootComponent);
	VoxelThinISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("VoxelsThin"));
	VoxelThinISM->SetupAttachment(RootComponent);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cube.Succeeded()) { VoxelISM->SetStaticMesh(Cube.Object); VoxelThinISM->SetStaticMesh(Cube.Object); }
	if (Cyl.Succeeded()) ProbeStemISM->SetStaticMesh(Cyl.Object);
	for (UStaticMeshComponent* C : { (UStaticMeshComponent*)ProbeISM, (UStaticMeshComponent*)ProbeISMActive, SlicePlane.Get(),
		(UStaticMeshComponent*)ProbeStemISM, (UStaticMeshComponent*)VoxelISM, (UStaticMeshComponent*)VoxelThinISM })
	{
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetCanEverAffectNavigation(false);
	}
	if (Sphere.Succeeded()) { ProbeISM->SetStaticMesh(Sphere.Object); ProbeISMActive->SetStaticMesh(Sphere.Object); }
	if (Plane.Succeeded()) SlicePlane->SetStaticMesh(Plane.Object);
	ProbeISM->SetVisibility(false);
	ProbeISMActive->SetVisibility(false);
	SlicePlane->SetVisibility(false);
	ProbeStemISM->SetVisibility(false);
	VoxelISM->SetVisibility(false);
	VoxelThinISM->SetVisibility(false);
}

FVector AWaveBakeActor::SampleWorld(const FIntVector& S) const
{
	const FIntVector C(S.X * Desc.SampleStride + Desc.SampleStride / 2, S.Y * Desc.SampleStride + Desc.SampleStride / 2, S.Z * Desc.SampleStride + Desc.SampleStride / 2);
	return Desc.CellCenter(FIntVector(FMath::Min(C.X, Desc.Dims.X - 1), FMath::Min(C.Y, Desc.Dims.Y - 1), FMath::Min(C.Z, Desc.Dims.Z - 1)));
}

void AWaveBakeActor::BeginPlay()
{
	Super::BeginPlay();
	if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Occlusion/Materials/M_DataSlice.M_DataSlice")))
	{
		SliceMID = UMaterialInstanceDynamic::Create(M, this);
		SlicePlane->SetMaterial(0, SliceMID);
	}
	if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Occlusion/Materials/M_Probe.M_Probe")))
	{
		UMaterialInstanceDynamic* A = UMaterialInstanceDynamic::Create(M, this); A->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.2f, 0.9f, 0.85f) * 3.f);
		UMaterialInstanceDynamic* B = UMaterialInstanceDynamic::Create(M, this); B->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.55f, 0.1f) * 6.f);
		ProbeISM->SetMaterial(0, A); ProbeISMActive->SetMaterial(0, B); ProbeStemISM->SetMaterial(0, A);
	}
	if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Occlusion/Materials/M_Voxel.M_Voxel")))
	{
		UMaterialInstanceDynamic* A = UMaterialInstanceDynamic::Create(M, this); A->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.72f, 0.74f, 0.78f));
		UMaterialInstanceDynamic* B = UMaterialInstanceDynamic::Create(M, this); B->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.42f, 0.08f));
		VoxelISM->SetMaterial(0, A); VoxelThinISM->SetMaterial(0, B);
	}
	Voxelize();
	PlaceProbes();
	if (LoadCache()) UE_LOG(LogWaveBake, Log, TEXT("Loaded wave bake cache (%d probes)"), Probes.Num());
	if (HasData() && LoadReplay()) UE_LOG(LogWaveBake, Log, TEXT("Loaded wave replay (%d frames, probe %d)"), ReplayFrames, ReplayProbe);
	UpdateProbeVisuals();
	if (UAcousticSubsystem* Sys = GetWorld()->GetSubsystem<UAcousticSubsystem>()) Sys->WaveBake = this;
}

void AWaveBakeActor::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Solver) { Solver->Release(); FlushRenderingCommands(); Solver.Reset(); }
	UpdateVoxelView(false);
	Super::EndPlay(Reason);
}

// ------------------------------------------------------------------------------------------------
// Voxelisation of the acoustic proxy geometry (every primitive tagged "Acoustic")
// ------------------------------------------------------------------------------------------------

void AWaveBakeActor::Voxelize()
{
	Desc.CellCm = C_CmPerS / (5.f * MaxFrequency);   // 5 samples per wavelength at f_max
	Desc.Origin = DomainMin;
	const FVector Size = DomainMax - DomainMin;
	Desc.Dims = FIntVector(FMath::CeilToInt(Size.X / Desc.CellCm), FMath::CeilToInt(Size.Y / Desc.CellCm), FMath::CeilToInt(Size.Z / Desc.CellCm));
	Desc.Dt = 0.5f * Desc.CellCm / C_CmPerS;          // Courant number 0.5 (3D limit 0.577)
	Desc.NumSteps = FMath::CeilToInt(0.92f / Desc.Dt);
	Desc.SampleStride = 4;
	Desc.StepsPerBin = 6;
	Desc.SliceEvery = 8;
	Flags.Init(0u, Desc.NumCells());
	ThinWallCells = 0;
	const float MinThick = 2.f * Desc.CellCm;
	auto Idx = [this](int32 X, int32 Y, int32 Z) { return (Z * Desc.Dims.Y + Y) * Desc.Dims.X + X; };
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		// Door leaves are dynamic: the bake treats every doorway as open and doors are applied at run time
		// (group 6A). Leaving them out also keeps the geometry hash, and so the cache, independent of door state.
		if (It->IsA<AAcousticDoor>()) continue;
		TArray<UStaticMeshComponent*> Comps;
		It->GetComponents<UStaticMeshComponent>(Comps);
		for (UStaticMeshComponent* C : Comps)
		{
			if (!C->ComponentHasTag(TEXT("Acoustic")) || !C->GetStaticMesh()) continue;
			const FBox Local = C->GetStaticMesh()->GetBoundingBox();
			const FTransform T = C->GetComponentTransform();
			const FVector Scale = T.GetScale3D().GetAbs();
			// Thin-wall fix (report ch. 9.6): grow the thinnest axes to two cells so the wall cannot leak.
			FVector LMin = Local.Min, LMax = Local.Max, GMin = LMin, GMax = LMax;
			for (int32 a = 0; a < 3; ++a)
			{
				const float WorldThick = (LMax[a] - LMin[a]) * Scale[a];
				if (WorldThick < MinThick && Scale[a] > KINDA_SMALL_NUMBER)
				{
					const float Grow = 0.5f * (MinThick - WorldThick) / Scale[a];
					GMin[a] -= Grow; GMax[a] += Grow;
				}
			}
			const FBox WB = FBox(GMin, GMax).TransformBy(T);
			const FIntVector A = Desc.CellOf(WB.Min), B = Desc.CellOf(WB.Max);
			const FTransform Inv = T.Inverse();
			for (int32 Z = A.Z; Z <= B.Z; ++Z)
			for (int32 Y = A.Y; Y <= B.Y; ++Y)
			for (int32 X = A.X; X <= B.X; ++X)
			{
				const FVector L = Inv.TransformPosition(Desc.CellCenter(FIntVector(X, Y, Z)));
				const bool bIn = L.X >= GMin.X && L.X <= GMax.X && L.Y >= GMin.Y && L.Y <= GMax.Y && L.Z >= GMin.Z && L.Z <= GMax.Z;
				if (!bIn) continue;
				uint32& F = Flags[Idx(X, Y, Z)];
				const bool bOrig = L.X >= LMin.X && L.X <= LMax.X && L.Y >= LMin.Y && L.Y <= LMax.Y && L.Z >= LMin.Z && L.Z <= LMax.Z;
				if (!(F & FlagSolid)) { F |= FlagSolid; if (!bOrig) { F |= FlagThin; ++ThinWallCells; } }
			}
		}
	}
	// The ground: everything below z = 0 is solid.
	for (int32 Z = 0; Z < Desc.Dims.Z; ++Z)
	{
		if (Desc.CellCenter(FIntVector(0, 0, Z)).Z > 0.f) break;
		for (int32 Y = 0; Y < Desc.Dims.Y; ++Y) for (int32 X = 0; X < Desc.Dims.X; ++X) Flags[Idx(X, Y, Z)] |= FlagSolid;
	}
	for (int32 Z = 1; Z + 1 < Desc.Dims.Z; ++Z)
	for (int32 Y = 1; Y + 1 < Desc.Dims.Y; ++Y)
	for (int32 X = 1; X + 1 < Desc.Dims.X; ++X)
	{
		const int32 I = Idx(X, Y, Z);
		if (Flags[I] & FlagSolid) continue;
		if ((Flags[I - 1] | Flags[I + 1] | Flags[I - Desc.Dims.X] | Flags[I + Desc.Dims.X] | Flags[I - Desc.Dims.X * Desc.Dims.Y] | Flags[I + Desc.Dims.X * Desc.Dims.Y]) & FlagSolid)
			Flags[I] |= FlagNearWall;
	}
	UE_LOG(LogWaveBake, Log, TEXT("Voxelised %dx%dx%d cells (%.1f cm), %d steps of %.3f ms, thin-wall cells added %d"),
		Desc.Dims.X, Desc.Dims.Y, Desc.Dims.Z, Desc.CellCm, Desc.NumSteps, Desc.Dt * 1000.f, ThinWallCells);
}

void AWaveBakeActor::PlaceProbes()
{
	Probes.Reset();
	auto Idx = [this](int32 X, int32 Y, int32 Z) { return (Z * Desc.Dims.Y + Y) * Desc.Dims.X + X; };
	auto Clear = [&](const FVector& P)
	{
		const FIntVector C = Desc.CellOf(P);
		for (int32 dy = -3; dy <= 3; ++dy) for (int32 dx = -3; dx <= 3; ++dx)
		{
			const int32 xx = FMath::Clamp(C.X + dx, 0, Desc.Dims.X - 1), yy = FMath::Clamp(C.Y + dy, 0, Desc.Dims.Y - 1);
			if (Flags[Idx(xx, yy, C.Z)] & FlagSolid) return false;
		}
		return true;
	};
	auto Add = [&](const FVector& P)
	{
		const FVector At(P.X, P.Y, EarHeight);
		if (At.X < DomainMin.X + 60.f || At.Y < DomainMin.Y + 60.f || At.X > DomainMax.X - 60.f || At.Y > DomainMax.Y - 60.f) return;
		if (!Clear(At)) return;
		const FVector C = Desc.CellCenter(Desc.CellOf(At));
		for (const FWaveProbeData& O : Probes) if (FVector::Dist2D(O.Pos, C) < 90.f) return;   // no near-duplicates
		FWaveProbeData D; D.Pos = C;
		Probes.Add(D);
	};
	// Each room gets its own evenly spread grid (so a 2.3 m corridor still gets a row down its middle);
	// outdoors the field varies slowly, so probes are sparser there.
	for (TActorIterator<AAcousticRoom> It(GetWorld()); It; ++It)
	{
		const FBox B = It->GetBox().Overlap(FBox(DomainMin, DomainMax));
		if (!B.IsValid) continue;
		const float Spacing = It->bOutdoor ? OutdoorProbeSpacing : ProbeSpacing;
		const FVector Size = B.GetSize();
		const int32 NX = FMath::Max(1, FMath::RoundToInt(Size.X / Spacing)), NY = FMath::Max(1, FMath::RoundToInt(Size.Y / Spacing));
		for (int32 j = 0; j < NY; ++j) for (int32 i = 0; i < NX; ++i)
			Add(FVector(B.Min.X + Size.X * (i + 0.5f) / NX, B.Min.Y + Size.Y * (j + 0.5f) / NY, 0.f));
	}
	// Doorways are where the field changes fastest: one probe on each side of every opening.
	for (TActorIterator<AAcousticPortal> It(GetWorld()); It; ++It)
	{
		const FVector N = It->GetNormal().GetSafeNormal2D();
		for (float S : { -75.f, 75.f }) Add(It->GetCenter() + N * S);
	}
	UE_LOG(LogWaveBake, Log, TEXT("Placed %d probes"), Probes.Num());
}

void AWaveBakeActor::ListenerProbes(const FVector& L, TArray<int32>& OutIdx, TArray<float>& OutW) const
{
	OutIdx.Reset(); OutW.Reset();
	// Candidates: probes that see the listener and stand in the same room (a probe across a doorway hears a different field).
	const UAcousticSubsystem* Sys = GetWorld()->GetSubsystem<UAcousticSubsystem>();
	const AAcousticRoom* RL = Sys ? Sys->RoomAt(L) : nullptr;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(WaveProbeLOS), false);
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) if (APawn* Pa = PC->GetPawn()) Q.AddIgnoredActor(Pa);
	TArray<TPair<float, int32>> C;
	for (int32 i = 0; i < Probes.Num(); ++i) C.Add({ FVector::DistSquared(Probes[i].Pos, L), i });
	C.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key; });
	for (int32 Pass = 0; Pass < 2 && OutIdx.Num() == 0; ++Pass)
	{
		for (const TPair<float, int32>& E : C)
		{
			if (OutIdx.Num() >= 3) break;
			const FVector& P = Probes[E.Value].Pos;
			if (Pass == 0 && RL && Sys->RoomAt(P) != RL) continue;
			FHitResult H;
			if (GetWorld()->LineTraceSingleByChannel(H, P, L, ECC_GameTraceChannel1, Q)) continue;
			// Keep blending local: drop probes much further away than the nearest one.
			if (OutIdx.Num() > 0 && FMath::Sqrt(E.Key) > FMath::Sqrt(C[0].Key) + ProbeSpacing * 1.2f) break;
			OutIdx.Add(E.Value);
			OutW.Add(1.f / FMath::Max(E.Key, 400.f));
		}
	}
	if (OutIdx.Num() == 0 && C.Num() > 0) { OutIdx.Add(C[0].Value); OutW.Add(1.f); }
	float Sum = 0.f; for (float W : OutW) Sum += W;
	for (float& W : OutW) W /= Sum;
}

int32 AWaveBakeActor::NearestProbe(const FVector& P, bool bRequireLOS) const
{
	if (bRequireLOS)
	{
		TArray<int32> I; TArray<float> W;
		ListenerProbes(P, I, W);
		return I.Num() > 0 ? I[0] : -1;
	}
	int32 Best = -1; float BestD = TNumericLimits<float>::Max();
	for (int32 i = 0; i < Probes.Num(); ++i)
	{
		const float D = FVector::DistSquared(Probes[i].Pos, P);
		if (D < BestD) { Best = i; BestD = D; }
	}
	return Best;
}

bool AWaveBakeActor::Query(const FVector& Listener, const FVector& Source, FWaveQuery& Out) const
{
	if (!HasData() || IsBaking()) return false;
	TArray<int32> Use; TArray<float> Wt;
	ListenerProbes(Listener, Use, Wt);
	if (Use.Num() == 0) return false;
	// Reciprocity: each probe stands in for the listener; its field is read at the source and the results are blended.
	float Arr = 0.f, Lds = 0.f, Ler = 0.f, WSum = 0.f;
	for (int32 k = 0; k < Use.Num(); ++k)
	{
		const FWaveProbeData& P = Probes[Use[k]];
		const float A = SampleField(P, P.ArrivalMs, Source), D = SampleField(P, P.Lds, Source), E = SampleField(P, P.Ler, Source);
		if (A < -1e5f || D < -1e5f) continue;
		Arr += A * Wt[k]; Lds += D * Wt[k]; Ler += E * Wt[k]; WSum += Wt[k];
	}
	if (WSum <= 0.f) return false;
	Arr /= WSum; Lds /= WSum; Ler /= WSum;
	const float Direct = FVector::Dist(Source, Listener) / 100.f;
	Out.ArrivalMs = Arr;
	Out.PathM = FMath::Max(Arr * 0.343f, Direct);
	Out.ExtraLossDb = FMath::Max(-20.f * FMath::LogX(10.f, FMath::Max(Out.PathM, 0.5f)) - Lds, 0.f);
	Out.LerMinusLdsDb = Ler - Lds;
	Out.ProbesUsed = Use; Out.Weights = Wt;

	// Trace the first-arrival path back from the source down the arrival-time gradient of the main probe, and stop
	// as soon as the listener is in plain view: the last bend is the corner the sound actually comes around.
	const FWaveProbeData& P = Probes[Use[0]];
	auto Solid = [this](const FVector& W)
	{
		const FIntVector C = Desc.CellOf(W);
		return (Flags[(C.Z * Desc.Dims.Y + C.Y) * Desc.Dims.X + C.X] & FlagSolid) != 0;
	};
	auto SegmentFree = [&](const FVector& A, const FVector& B)
	{
		const int32 N = FMath::Max(2, FMath::CeilToInt(FVector::Dist(A, B) / (Desc.CellCm * 0.7f)));
		for (int32 k = 1; k < N; ++k) if (Solid(FMath::Lerp(A, B, float(k) / N))) return false;
		return true;
	};
	Out.Path.Reset();
	FVector X = Source;
	Out.Path.Add(X);
	for (int32 i = 0; i < 600; ++i)
	{
		if (SegmentFree(X, Listener)) break;
		const FVector G = ArrivalGradient(P, X);
		if (G.IsNearlyZero()) break;
		const FVector D = -G.GetSafeNormal2D();
		const float Here = SampleField(P, P.ArrivalMs, X);
		bool bMoved = false;
		for (float Ang : { 0.f, 25.f, -25.f, 50.f, -50.f, 75.f, -75.f, 100.f, -100.f })
		{
			const FVector Dir = D.RotateAngleAxis(Ang, FVector::UpVector);
			const FVector NextX = X + Dir * 20.f;
			if (!SegmentFree(X, NextX)) continue;
			const float There = SampleField(P, P.ArrivalMs, NextX);
			if (There < -1e5f || There >= Here + 0.05f) continue;
			X = NextX; bMoved = true; break;
		}
		if (!bMoved) break;
		if (i % 4 == 3) Out.Path.Add(X);
	}
	if (Out.Path.Last() != X) Out.Path.Add(X);
	Out.Path.Add(Listener);
	// Perceived direction: towards the last bend before the listener (or the source when it is in view).
	const FVector Far = Out.Path.Num() >= 2 ? Out.Path[Out.Path.Num() - 2] : Source;
	Out.ArrivalDir = (Far - Listener).GetSafeNormal();
	if (Out.ArrivalDir.IsNearlyZero()) Out.ArrivalDir = (Source - Listener).GetSafeNormal();
	return true;
}

// ------------------------------------------------------------------------------------------------
// Bake state machine
// ------------------------------------------------------------------------------------------------

void AWaveBakeActor::StartBake()
{
	if (IsBaking()) return;
	SavedDoors.Reset();
	for (TActorIterator<AAcousticPortal> It(GetWorld()); It; ++It)
	{
		if (!It->bHasDoor) continue;
		SavedDoors.Add(*It, It->OpenTarget);
		It->SetOpen(true);   // the bake assumes open doors; closed doors are applied at run time (group 6A)
	}
	Phase = EPhase::OpeningDoors;
	PhaseTimer = 1.2f;
	BakeStart = FPlatformTime::Seconds();
}

void AWaveBakeActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	switch (Phase)
	{
	case EPhase::OpeningDoors:
		PhaseTimer -= DeltaSeconds;
		if (PhaseTimer <= 0.f)
		{
			Voxelize();
			PlaceProbes();
			if (Probes.Num() == 0) { Phase = EPhase::Idle; break; }
			Solver = MakeShared<FWaveSolverGPU, ESPMode::ThreadSafe>();
			Solver->Init(Desc, Flags, 0.985f);
			FVector Player = FVector::ZeroVector;
			if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) if (APawn* P = PC->GetPawn()) Player = P->GetActorLocation();
			ReplayProbe = NearestProbe(Player, false);
			CurrentProbe = 0;
			Solver->BeginProbe(Desc.CellOf(Probes[0].Pos), CurrentProbe == ReplayProbe, Desc.CellOf(Probes[0].Pos).Z);
			Phase = EPhase::Simulating;
		}
		break;
	case EPhase::Simulating:
		if (Solver->Advance(StepsPerFrame)) { Solver->RequestReadback(); Phase = EPhase::Reading; }
		break;
	case EPhase::Reading:
		if (Solver->TryFinish(Raw))
		{
			ReduceProbe(CurrentProbe, Raw);
			if (CurrentProbe == ReplayProbe) { ReplaySlices = MoveTemp(Raw.Slices); ReplayFrames = Raw.SliceFrames; }
			++CurrentProbe;
			if (CurrentProbe >= Probes.Num()) { FinishBake(); break; }
			Solver->BeginProbe(Desc.CellOf(Probes[CurrentProbe].Pos), CurrentProbe == ReplayProbe, Desc.CellOf(Probes[CurrentProbe].Pos).Z);
			Phase = EPhase::Simulating;
		}
		break;
	default: break;
	}
	UpdateSlice(DeltaSeconds);
	if (bShowProbes && HasData() && (SliceView == EWaveSliceView::Off || SliceView == EWaveSliceView::Voxels))
	{
		// Show which probes the run-time query is blending for the listener.
		if (UAcousticSubsystem* Sys = GetWorld()->GetSubsystem<UAcousticSubsystem>())
		{
			ProbeISMActive->ClearInstances();
			const int32 N = NearestProbe(Sys->ListenerPos, true);
			if (Probes.IsValidIndex(N))
			{
				ProbeISMActive->AddInstance(FTransform(FRotator::ZeroRotator, Probes[N].Pos, FVector(0.32f)), true);
				DrawDebugLine(GetWorld(), Probes[N].Pos, Sys->ListenerPos, FColor(255, 150, 40), false, -1.f, 0, 2.f);
			}
		}
	}
}

void AWaveBakeActor::ReduceProbe(int32 Index, const FWaveProbeRaw& R)
{
	FWaveProbeData& P = Probes[Index];
	const FIntVector SD = Desc.SampleDims();
	const int32 NS = SD.X * SD.Y * SD.Z, NB = Desc.NumBins();
	const float BinS = Desc.StepsPerBin * Desc.Dt;
	const float FMax = C_CmPerS / (Desc.CellCm * 5.f);
	const float Sigma = FMath::Sqrt(FMath::Loge(10.f)) / (PI * FMax);
	const int32 DSBins = FMath::CeilToInt((0.005f + 4.f * Sigma) / BinS);
	const int32 ERBins = FMath::CeilToInt(0.2f / BinS);
	// Reference: the strongest sample 0.8-1.8 m from the probe gives the free-field level.
	float RefPeak = 0.f, RefR = 100.f; int32 RefS = -1;
	for (int32 s = 0; s < NS; ++s)
	{
		const FIntVector S(s % SD.X, (s / SD.X) % SD.Y, s / (SD.X * SD.Y));
		const float D = FVector::Dist(SampleWorld(S), P.Pos);
		if (D < 80.f || D > 180.f) continue;
		float Pk = 0.f; for (int32 b = 0; b < NB; ++b) Pk = FMath::Max(Pk, R.Env[s * NB + b]);
		if (Pk > RefPeak) { RefPeak = Pk; RefR = D; RefS = s; }
	}
	const float Thr = RefPeak * 1e-5f;   // -50 dB arrival threshold
	P.ArrivalMs.SetNumUninitialized(NS); P.Lds.SetNumUninitialized(NS); P.Ler.SetNumUninitialized(NS);
	float RefDS = 0.f;
	auto Windows = [&](int32 s, int32& OutArr, float& DS, float& ER)
	{
		OutArr = -1; DS = 0.f; ER = 0.f;
		const float* E = &R.Env[s * NB];
		for (int32 b = 0; b < NB; ++b) if (E[b] > Thr) { OutArr = b; break; }
		if (OutArr < 0) return;
		for (int32 b = OutArr; b < FMath::Min(NB, OutArr + DSBins); ++b) DS += E[b];
		for (int32 b = OutArr + DSBins; b < FMath::Min(NB, OutArr + ERBins); ++b) ER += E[b];
	};
	if (RefS >= 0) { int32 A; float ER; Windows(RefS, A, RefDS, ER); }
	RefDS = FMath::Max(RefDS, 1e-20f);
	const float RefDb = -20.f * FMath::LogX(10.f, RefR / 100.f);
	const float T0Ms = 5.f * Sigma * 1000.f;
	for (int32 s = 0; s < NS; ++s)
	{
		int32 A; float DS, ER;
		Windows(s, A, DS, ER);
		if (A < 0) { P.ArrivalMs[s] = -1.f; P.Lds[s] = -120.f; P.Ler[s] = -120.f; continue; }
		P.ArrivalMs[s] = FMath::Max((A + 0.5f) * BinS * 1000.f - T0Ms, 0.f);
		P.Lds[s] = 10.f * FMath::LogX(10.f, FMath::Max(DS, 1e-30f) / RefDS) + RefDb;
		P.Ler[s] = 10.f * FMath::LogX(10.f, FMath::Max(ER, 1e-30f) / RefDS) + RefDb;
	}
	P.RefLds = RefDb;
}

void AWaveBakeActor::FinishBake()
{
	LastBakeSeconds = float(FPlatformTime::Seconds() - BakeStart);
	Phase = EPhase::Idle;
	for (auto& KV : SavedDoors) if (KV.Key) KV.Key->SetOpen(KV.Value > 0.5f);
	if (Solver) { Solver->Release(); Solver.Reset(); }
	SaveCache();
	SaveReplay();
	UpdateProbeVisuals();
	ShownProbe = -1;
	UE_LOG(LogWaveBake, Log, TEXT("Wave bake finished: %d probes in %.1f s"), Probes.Num(), LastBakeSeconds);
}

float AWaveBakeActor::GetProgress() const
{
	if (!IsBaking() || Probes.Num() == 0) return 0.f;
	const float Inner = Solver ? float(Solver->GetStep()) / FMath::Max(Desc.NumSteps, 1) : 0.f;
	return FMath::Clamp((CurrentProbe + Inner) / Probes.Num(), 0.f, 1.f);
}

FString AWaveBakeActor::GetStatusText() const
{
	if (Phase == EPhase::OpeningDoors) return TEXT("GPU 波动烘焙：先把所有门打开……");
	if (!IsBaking())
		return HasData() ? FString::Printf(TEXT("波动烘焙数据：%d 个探针，%d×%d×%d 格（%.1f cm），烘焙用时 %.0f s"), Probes.Num(), Desc.Dims.X, Desc.Dims.Y, Desc.Dims.Z, Desc.CellCm, LastBakeSeconds)
			: FString(TEXT("尚无波动烘焙数据，按 K 开始烘焙"));
	const double El = FPlatformTime::Seconds() - BakeStart;
	const float Pr = GetProgress();
	const float Eta = Pr > 0.02f ? float(El / Pr - El) : 0.f;
	return FString::Printf(TEXT("GPU 波动烘焙　探针 %d / %d　·　%d×%d×%d 格（%.1f cm）　·　已用 %.0f s，剩余约 %.0f s"),
		FMath::Min(CurrentProbe + 1, Probes.Num()), Probes.Num(), Desc.Dims.X, Desc.Dims.Y, Desc.Dims.Z, Desc.CellCm, El, Eta);
}

// ------------------------------------------------------------------------------------------------
// Query
// ------------------------------------------------------------------------------------------------


float AWaveBakeActor::SampleField(const FWaveProbeData& P, const TArray<float>& Field, const FVector& W) const
{
	const FIntVector SD = Desc.SampleDims();
	const FVector G = (W - Desc.Origin) / (Desc.CellCm * Desc.SampleStride) - FVector(0.5f);
	const int32 X0 = FMath::Clamp(FMath::FloorToInt(G.X), 0, SD.X - 2), Y0 = FMath::Clamp(FMath::FloorToInt(G.Y), 0, SD.Y - 2), Z0 = FMath::Clamp(FMath::FloorToInt(G.Z), 0, SD.Z - 2);
	const float FX = FMath::Clamp(G.X - X0, 0.f, 1.f), FY = FMath::Clamp(G.Y - Y0, 0.f, 1.f), FZ = FMath::Clamp(G.Z - Z0, 0.f, 1.f);
	float Sum = 0.f, WSum = 0.f;
	for (int32 c = 0; c < 8; ++c)
	{
		const int32 dx = c & 1, dy = (c >> 1) & 1, dz = (c >> 2) & 1;
		const int32 S = ((Z0 + dz) * SD.Y + (Y0 + dy)) * SD.X + (X0 + dx);
		const float V = Field[S];
		if (P.ArrivalMs[S] < 0.f) continue;            // skip samples inside walls / unreached
		const float Wt = (dx ? FX : 1 - FX) * (dy ? FY : 1 - FY) * (dz ? FZ : 1 - FZ) + 1e-4f;
		Sum += V * Wt; WSum += Wt;
	}
	return WSum > 0.f ? Sum / WSum : -1e6f;
}

FVector AWaveBakeActor::ArrivalGradient(const FWaveProbeData& P, const FVector& W) const
{
	const float H = Desc.CellCm * Desc.SampleStride * 0.75f;
	FVector G;
	for (int32 a = 0; a < 3; ++a)
	{
		FVector D = FVector::ZeroVector; D[a] = H;
		const float A = SampleField(P, P.ArrivalMs, W + D), B = SampleField(P, P.ArrivalMs, W - D);
		G[a] = (A < -1e5f || B < -1e5f) ? 0.f : (A - B) / (2.f * H);
	}
	G.Z *= 0.3f;   // paths stay near ear height
	return G;
}


// ------------------------------------------------------------------------------------------------
// Cache
// ------------------------------------------------------------------------------------------------

FString AWaveBakeActor::CachePath() const { return FPaths::ProjectSavedDir() / TEXT("WaveBake/bake.bin"); }


uint32 AWaveBakeActor::GeometryHash() const
{
	uint32 H = GetTypeHash(Desc.Dims);
	H = HashCombine(H, GetTypeHash(Desc.CellCm));
	H = HashCombine(H, FCrc::MemCrc32(Flags.GetData(), Flags.Num() * sizeof(uint32)));
	return H;
}

bool AWaveBakeActor::SaveCache() const
{
	FBufferArchive Ar;
	uint32 Magic = 0x57415645, Ver = 2, Hash = GeometryHash();
	int32 N = Probes.Num();
	Ar << Magic << Ver << Hash << N;
	for (const FWaveProbeData& P : Probes)
	{
		FVector Pos = P.Pos; float Ref = P.RefLds;
		TArray<float> A = P.ArrivalMs, L = P.Lds, E = P.Ler;
		Ar << Pos << Ref << A << L << E;
	}
	return FFileHelper::SaveArrayToFile(Ar, *CachePath());
}

bool AWaveBakeActor::LoadCache()
{
	for (const FString& Path : { CachePath(), ShippedBakePath(TEXT("bake.bin")) })
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path)) continue;
		FMemoryReader Ar(Bytes);
		uint32 Magic = 0, Ver = 0, Hash = 0; int32 N = 0;
		Ar << Magic << Ver << Hash << N;
		if (Magic != 0x57415645 || Ver != 2 || Hash != GeometryHash() || N != Probes.Num()) continue;
		for (FWaveProbeData& P : Probes)
		{
			Ar << P.Pos << P.RefLds << P.ArrivalMs << P.Lds << P.Ler;
		}
		if (!Ar.IsError()) return true;
	}
	for (FWaveProbeData& P : Probes) { P.ArrivalMs.Reset(); P.Lds.Reset(); P.Ler.Reset(); }
	return false;
}

// ------------------------------------------------------------------------------------------------
// Visualisation
// ------------------------------------------------------------------------------------------------

void AWaveBakeActor::UpdateProbeVisuals()
{
	ProbeISM->ClearInstances();
	ProbeStemISM->ClearInstances();
	for (const FWaveProbeData& P : Probes)
	{
		ProbeISM->AddInstance(FTransform(FRotator::ZeroRotator, P.Pos, FVector(0.26f)), true);
		// A thin stem down to the floor so the ear-height position reads in the top-down view.
		ProbeStemISM->AddInstance(FTransform(FRotator::ZeroRotator, FVector(P.Pos.X, P.Pos.Y, P.Pos.Z * 0.5f), FVector(0.025f, 0.025f, P.Pos.Z / 100.f)), true);
	}
	ProbeISM->SetVisibility(bShowProbes);
	ProbeStemISM->SetVisibility(bShowProbes);
	ProbeISMActive->SetVisibility(bShowProbes || (SliceView != EWaveSliceView::Off && SliceView != EWaveSliceView::Voxels));
}

void AWaveBakeActor::ToggleProbes()
{
	SetProbesVisible(!bShowProbes);
}

void AWaveBakeActor::SetProbesVisible(bool bOn)
{
	bShowProbes = bOn;
	UpdateProbeVisuals();
}

FString AWaveBakeActor::CycleSliceView()
{
	static const EWaveSliceView Order[] = { EWaveSliceView::Off, EWaveSliceView::Voxels, EWaveSliceView::Arrival, EWaveSliceView::Lds, EWaveSliceView::Ler, EWaveSliceView::Ratio };
	int32 I = 0; for (int32 k = 0; k < 6; ++k) if (Order[k] == SliceView) I = k;
	EWaveSliceView Next = Order[(I + 1) % 6];
	if (!HasData() && Next >= EWaveSliceView::Arrival) Next = EWaveSliceView::Off;
	return SetSliceView(Next);
}

FString AWaveBakeActor::ToggleReplay()
{
	return SetSliceView(SliceView == EWaveSliceView::Replay ? EWaveSliceView::Off : EWaveSliceView::Replay);
}

FString AWaveBakeActor::SetSliceView(EWaveSliceView V)
{
	if (V >= EWaveSliceView::Arrival && V != EWaveSliceView::Replay && !HasData()) return TEXT("尚无烘焙数据：先按 K 烘焙");
	if (V == EWaveSliceView::Replay && ReplayFrames <= 0) return TEXT("还没有波场回放数据：先按 K 烘焙一次");
	SliceView = V;
	ShownProbe = -1; SliceRefresh = 0.f; ReplayTime = 0.f;
	UpdateVoxelView(V == EWaveSliceView::Voxels);
	UpdateProbeVisuals();
	return GetLegend().Title;
}

bool AWaveBakeActor::GetShownProbe(FVector& OutPos, int32& OutIndex) const
{
	const int32 I = SliceView == EWaveSliceView::Replay ? ReplayProbe : ShownProbe;
	if (!Probes.IsValidIndex(I) || SliceView == EWaveSliceView::Off || SliceView == EWaveSliceView::Voxels) return false;
	OutPos = Probes[I].Pos; OutIndex = I;
	return true;
}

AWaveBakeActor::FLegend AWaveBakeActor::GetLegend() const
{
	FLegend L;
	switch (SliceView)
	{
	case EWaveSliceView::Voxels:
		L.Title = TEXT("体素：求解器眼中的房子");
		L.bRamp = false;
		L.Read = FString::Printf(TEXT("场景墙体已隐藏，方块就是参与波动计算的实心格（%.1f cm，500 Hz 每波长 5 格）。橙色是比两格还薄、被加厚的墙，防止声音从缝里漏过去。门板不参与烘焙，门洞按打开处理；家具也不在网格里。"), Desc.CellCm);
		break;
	case EWaveSliceView::Arrival:
		L.Title = TEXT("首达时间"); L.Lo = TEXT("60 ms 以上"); L.Hi = TEXT("0 ms");
		L.Read = TEXT("从发光的探针发出一个脉冲，声音最早到达每一点的时间。等值线每 5 ms 一条（约 1.7 m 声程），绕过门洞时等值线跟着拐弯，这就是绕射的路径。");
		break;
	case EWaveSliceView::Lds:
		L.Title = TEXT("直达声响度"); L.Lo = TEXT("−45 dB"); L.Hi = TEXT("0 dB");
		L.Read = TEXT("首达后很短时间窗内的能量，相对 1 m 处自由场。等值线每 6 dB 一条。墙后面迅速变暗，门口方向亮一些，这就是遮挡和衍射的量。");
		break;
	case EWaveSliceView::Ler:
		L.Title = TEXT("早期反射响度"); L.Lo = TEXT("−45 dB"); L.Hi = TEXT("0 dB");
		L.Read = TEXT("首达之后约 80 ms 内反射声的能量。它比直达声衰减得慢，所以隔着墙听到的主要是反射和混响。");
		break;
	case EWaveSliceView::Ratio:
		L.Title = TEXT("直达被挡程度（反射 − 直达）"); L.Lo = TEXT("−5 dB"); L.Hi = TEXT("+25 dB");
		L.Read = TEXT("早期反射比直达声高多少。暗处能直接看到探针；越亮说明直达声被挡得越多，听起来越“闷”、越远。运行时就用这个值决定湿干比。");
		break;
	case EWaveSliceView::Replay:
		L.Title = TEXT("波场回放"); L.Lo = TEXT("负压"); L.Hi = TEXT("正压"); L.bReplay = true;
		L.Read = FString::Printf(TEXT("烘焙时从发光探针发出的脉冲，在耳朵高度的声压，慢放约 250 倍、循环播放。看波前在门洞处弯折、在墙上反射，烘焙参数就是从这些波形里统计出来的。（共 %d 帧）"), ReplayFrames);
		break;
	default:
		L.Title = TEXT("关闭切片"); L.bRamp = false;
		L.Read = HasData() ? TEXT("选一个视图查看烘焙结果。") : TEXT("尚无烘焙数据：按 K 开始烘焙（约 1~3 分钟）。");
		break;
	}
	return L;
}

FLinearColor AWaveBakeActor::RampColor(float T)
{
	T = FMath::Clamp(T, 0.f, 1.f);
	static const FLinearColor C[5] = { FLinearColor(0.02f, 0.03f, 0.18f), FLinearColor(0.05f, 0.25f, 0.5f), FLinearColor(0.05f, 0.55f, 0.5f), FLinearColor(0.45f, 0.8f, 0.3f), FLinearColor(1.f, 0.9f, 0.3f) };
	const float K = T * 4.f; const int32 I = FMath::Min(3, FMath::FloorToInt(K)); const float F = K - I;
	return FLinearColor::LerpUsingHSV(C[I], C[I + 1], F);
}

static FColor Ramp(float T)
{
	return AWaveBakeActor::RampColor(T).ToFColor(true);
}

void AWaveBakeActor::UpdateVoxelView(bool bOn)
{
	if (bOn == bVoxelsShown) return;
	bVoxelsShown = bOn;
	if (bOn)
	{
		// Hide the walls the solver sees (tagged Acoustic, taller than a slab) so the voxel cubes stand in for them.
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			TArray<UStaticMeshComponent*> Comps;
			It->GetComponents<UStaticMeshComponent>(Comps);
			for (UStaticMeshComponent* C : Comps)
			{
				if (!C->ComponentHasTag(TEXT("Acoustic")) || C->bHiddenInGame) continue;
				if (C->Bounds.BoxExtent.Z * 2.f < 60.f) continue;   // floor and ceiling slabs stay
				C->SetHiddenInGame(true);
				HiddenForVoxels.Add(C);
			}
		}
		VoxelISM->ClearInstances();
		VoxelThinISM->ClearInstances();
		const int32 Z0 = Desc.CellOf(FVector(0, 0, 1.f)).Z + 1;
		const int32 Z1 = Desc.CellOf(FVector(0, 0, EarHeight)).Z;
		auto Idx = [this](int32 X, int32 Y, int32 Z) { return (Z * Desc.Dims.Y + Y) * Desc.Dims.X + X; };
		auto Solid = [&](int32 X, int32 Y, int32 Z)
		{
			if (X < 0 || Y < 0 || Z < 0 || X >= Desc.Dims.X || Y >= Desc.Dims.Y || Z >= Desc.Dims.Z) return true;
			return (Flags[Idx(X, Y, Z)] & FlagSolid) != 0;
		};
		const float S = Desc.CellCm / 100.f * 0.94f;
		TArray<FTransform> Solid_T, Thin_T;
		for (int32 Z = Z0; Z <= Z1; ++Z)
			for (int32 Y = 0; Y < Desc.Dims.Y; ++Y)
				for (int32 X = 0; X < Desc.Dims.X; ++X)
				{
					const uint32 F = Flags[Idx(X, Y, Z)];
					if (!(F & FlagSolid)) continue;
					// Only cells with an air neighbour (or the top layer) are visible; skip the hidden interior.
					const bool bSurface = Z == Z1 || !Solid(X + 1, Y, Z) || !Solid(X - 1, Y, Z) || !Solid(X, Y + 1, Z) || !Solid(X, Y - 1, Z);
					if (!bSurface) continue;
					const FVector C = Desc.CellCenter(FIntVector(X, Y, Z));
					((F & FlagThin) ? Thin_T : Solid_T).Add(FTransform(FRotator::ZeroRotator, C, FVector(S)));
				}
		VoxelISM->AddInstances(Solid_T, false);
		VoxelThinISM->AddInstances(Thin_T, false);
		UE_LOG(LogWaveBake, Log, TEXT("Voxel view: %d solid, %d thickened cells shown"), Solid_T.Num(), Thin_T.Num());
	}
	else
	{
		for (const TWeakObjectPtr<UPrimitiveComponent>& C : HiddenForVoxels) if (C.IsValid()) C->SetHiddenInGame(false);
		HiddenForVoxels.Reset();
		VoxelISM->ClearInstances();
		VoxelThinISM->ClearInstances();
	}
	VoxelISM->SetVisibility(bOn);
	VoxelThinISM->SetVisibility(bOn);
}

void AWaveBakeActor::UpdateSlice(float Dt)
{
	const bool bOn = SliceView != EWaveSliceView::Off && SliceView != EWaveSliceView::Voxels;
	SlicePlane->SetVisibility(bOn);
	if (!bOn || !SliceMID) return;
	// The texture has 2x2 pixels per voxel so the iso-lines of the (bilinear) field come out smooth.
	const int32 SS = 2;
	const int32 W = Desc.Dims.X, H = Desc.Dims.Y, TW = W * SS, TH = H * SS;
	if (!SliceTex || SliceTex->GetSizeX() != TW || SliceTex->GetSizeY() != TH)
	{
		SliceTex = UTexture2D::CreateTransient(TW, TH, PF_B8G8R8A8);
		SliceTex->Filter = TF_Bilinear;
		SliceTex->SRGB = true;
		SliceTex->UpdateResource();
		SliceMID->SetTextureParameterValue(TEXT("Data"), SliceTex);
		const FVector Size = FVector(W, H, 0) * Desc.CellCm;
		SlicePlane->SetWorldLocation(FVector(Desc.Origin.X + Size.X * 0.5f, Desc.Origin.Y + Size.Y * 0.5f, 3.f));
		SlicePlane->SetWorldScale3D(FVector(Size.X / 100.f, Size.Y / 100.f, 1.f));
		SlicePixels.SetNumZeroed(TW * TH);
	}
	bool bDirty = false;
	const int32 EarZ = Desc.CellOf(FVector(0, 0, EarHeight)).Z;
	auto Idx = [this](int32 X, int32 Y, int32 Z) { return (Z * Desc.Dims.Y + Y) * Desc.Dims.X + X; };
	if (SliceView == EWaveSliceView::Replay)
	{
		ReplayTime += Dt;
		const int32 Frame = FMath::FloorToInt(ReplayTime * 60.f) % FMath::Max(ReplayFrames, 1);
		const float* F = &ReplaySlices[Frame * W * H];
		float Mx = 1e-6f; for (int32 i = 0; i < W * H; ++i) Mx = FMath::Max(Mx, FMath::Abs(F[i]));
		Mx = FMath::Max(Mx * 0.6f, 1e-4f);
		for (int32 ty = 0; ty < TH; ++ty) for (int32 tx = 0; tx < TW; ++tx)
		{
			const int32 x = tx / SS, y = ty / SS, I = ty * TW + tx;
			if (Flags[Idx(x, y, EarZ)] & FlagSolid) { SlicePixels[I] = FColor(30, 34, 40, 235); continue; }
			const float V = FMath::Clamp(F[y * W + x] / Mx, -1.f, 1.f);
			const float A = FMath::Pow(FMath::Abs(V), 0.6f);
			SlicePixels[I] = V >= 0.f ? FColor(255, uint8(160 + 60 * (1 - A)), uint8(80 * (1 - A)), uint8(40 + 200 * A))
			                          : FColor(uint8(60 * (1 - A)), uint8(140 + 60 * (1 - A)), 255, uint8(40 + 200 * A));
		}
		bDirty = true;
	}
	else
	{
		SliceRefresh -= Dt;
		int32 Want = -1;
		if (UAcousticSubsystem* Sys = GetWorld()->GetSubsystem<UAcousticSubsystem>()) Want = NearestProbe(Sys->ListenerPos, true);
		if (SliceRefresh <= 0.f && Want != ShownProbe)
		{
			SliceRefresh = 0.25f;
			ShownProbe = Want;
			// Field value per pixel (NaN = wall / no data), then colour + iso-lines.
			TArray<float> Val; Val.Init(NAN, TW * TH);
			float Lo = 0.f, Hi = 1.f, Step = 1.f;
			switch (SliceView)
			{
			case EWaveSliceView::Arrival: Lo = 60.f; Hi = 0.f; Step = 5.f; break;
			case EWaveSliceView::Lds: case EWaveSliceView::Ler: Lo = -45.f; Hi = 0.f; Step = 6.f; break;
			case EWaveSliceView::Ratio: Lo = -5.f; Hi = 25.f; Step = 5.f; break;
			default: break;
			}
			if (Probes.IsValidIndex(ShownProbe))
			{
				const FWaveProbeData& P = Probes[ShownProbe];
				const float Zc = Desc.CellCenter(FIntVector(0, 0, EarZ)).Z;
				for (int32 ty = 0; ty < TH; ++ty) for (int32 tx = 0; tx < TW; ++tx)
				{
					if (Flags[Idx(tx / SS, ty / SS, EarZ)] & FlagSolid) continue;
					const FVector Wp(Desc.Origin.X + (tx + 0.5f) / SS * Desc.CellCm, Desc.Origin.Y + (ty + 0.5f) / SS * Desc.CellCm, Zc);
					const float Arr = SampleField(P, P.ArrivalMs, Wp);
					if (Arr < -1e5f) continue;
					float V = 0.f;
					switch (SliceView)
					{
					case EWaveSliceView::Arrival: V = Arr; break;
					case EWaveSliceView::Lds: V = SampleField(P, P.Lds, Wp); break;
					case EWaveSliceView::Ler: V = SampleField(P, P.Ler, Wp); break;
					case EWaveSliceView::Ratio: V = SampleField(P, P.Ler, Wp) - SampleField(P, P.Lds, Wp); break;
					default: break;
					}
					Val[ty * TW + tx] = V;
				}
			}
			for (int32 ty = 0; ty < TH; ++ty) for (int32 tx = 0; tx < TW; ++tx)
			{
				const int32 I = ty * TW + tx;
				const float V = Val[I];
				if (FMath::IsNaN(V)) { SlicePixels[I] = (Flags[Idx(tx / SS, ty / SS, EarZ)] & FlagSolid) ? FColor(22, 25, 31, 235) : FColor(0, 0, 0, 0); continue; }
				FColor C = Ramp((V - Lo) / (Hi - Lo)); C.A = 220;
				// Iso-line where the band index changes towards the right or lower neighbour.
				const int32 B = FMath::FloorToInt(V / Step);
				bool bEdge = false;
				if (tx + 1 < TW && !FMath::IsNaN(Val[I + 1]) && FMath::FloorToInt(Val[I + 1] / Step) != B) bEdge = true;
				if (ty + 1 < TH && !FMath::IsNaN(Val[I + TW]) && FMath::FloorToInt(Val[I + TW] / Step) != B) bEdge = true;
				if (bEdge) C = FColor(uint8(C.R * 0.35f), uint8(C.G * 0.35f), uint8(C.B * 0.35f), 240);
				SlicePixels[I] = C;
			}
			bDirty = true;
			// The probe whose field is shown glows orange.
			ProbeISMActive->ClearInstances();
			if (Probes.IsValidIndex(ShownProbe)) ProbeISMActive->AddInstance(FTransform(FRotator::ZeroRotator, Probes[ShownProbe].Pos, FVector(0.42f)), true);
		}
	}
	if (SliceView == EWaveSliceView::Replay && ProbeISMActive->GetInstanceCount() == 0 && Probes.IsValidIndex(ReplayProbe))
		ProbeISMActive->AddInstance(FTransform(FRotator::ZeroRotator, Probes[ReplayProbe].Pos, FVector(0.42f)), true);
	if (bDirty && SliceTex && SliceTex->GetResource())
	{
		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, TW, TH);
		const int32 Bytes = TW * TH * 4;
		uint8* Copy = (uint8*)FMemory::Malloc(Bytes);
		FMemory::Memcpy(Copy, SlicePixels.GetData(), Bytes);
		SliceTex->UpdateTextureRegions(0, 1, Region, TW * 4, 4, Copy, [](uint8* Data, const FUpdateTextureRegion2D* R) { FMemory::Free(Data); delete R; });
	}
}

// Replay frames are quantised to 8 bits with one scale per frame (about 30 MB) so the replay survives a restart.
bool AWaveBakeActor::SaveReplay() const
{
	if (ReplayFrames <= 0) return false;
	const int32 N = Desc.Dims.X * Desc.Dims.Y;
	if (ReplaySlices.Num() < ReplayFrames * N) return false;
	FBufferArchive Ar;
	uint32 Magic = 0x52504C59, Hash = GeometryHash();
	int32 Frames = ReplayFrames, Probe = ReplayProbe, Cells = N;
	Ar << Magic << Hash << Frames << Probe << Cells;
	TArray<int8> Q; Q.SetNumUninitialized(N);
	for (int32 f = 0; f < Frames; ++f)
	{
		const float* F = &ReplaySlices[f * N];
		float Mx = 1e-12f; for (int32 i = 0; i < N; ++i) Mx = FMath::Max(Mx, FMath::Abs(F[i]));
		for (int32 i = 0; i < N; ++i) Q[i] = int8(FMath::Clamp(FMath::RoundToInt(F[i] / Mx * 127.f), -127, 127));
		Ar << Mx;
		Ar.Serialize(Q.GetData(), N);
	}
	return FFileHelper::SaveArrayToFile(Ar, *(FPaths::ProjectSavedDir() / TEXT("WaveBake/replay.bin")));
}

bool AWaveBakeActor::LoadReplay()
{
	TArray<uint8> Bytes;
	bool bFound = false;
	for (const FString& Path : { FPaths::ProjectSavedDir() / TEXT("WaveBake/replay.bin"), ShippedBakePath(TEXT("replay.bin")) })
	{
		if (!FFileHelper::LoadFileToArray(Bytes, *Path)) continue;
		FMemoryReader Peek(Bytes);
		uint32 M = 0, Hh = 0; Peek << M << Hh;
		if (M == 0x52504C59 && Hh == GeometryHash()) { bFound = true; break; }
	}
	if (!bFound) return false;
	FMemoryReader Ar(Bytes);
	uint32 Magic = 0, Hash = 0; int32 Frames = 0, Probe = -1, Cells = 0;
	Ar << Magic << Hash << Frames << Probe << Cells;
	if (Magic != 0x52504C59 || Hash != GeometryHash() || Cells != Desc.Dims.X * Desc.Dims.Y || Frames <= 0 || Probe >= Probes.Num()) return false;
	ReplaySlices.SetNumUninitialized(Frames * Cells);
	TArray<int8> Q; Q.SetNumUninitialized(Cells);
	for (int32 f = 0; f < Frames; ++f)
	{
		float Mx = 0.f; Ar << Mx;
		Ar.Serialize(Q.GetData(), Cells);
		float* F = &ReplaySlices[f * Cells];
		for (int32 i = 0; i < Cells; ++i) F[i] = Q[i] / 127.f * Mx;
	}
	ReplayFrames = Frames; ReplayProbe = Probe;
	return !Ar.IsError();
}
