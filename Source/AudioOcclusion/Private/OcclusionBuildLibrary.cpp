#include "OcclusionBuildLibrary.h"
#include "AcousticRoom.h"
#include "AcousticPortal.h"
#include "AcousticDoor.h"
#include "AcousticEmitterComponent.h"
#include "OcclusionActors.h"
#include "WaveBakeActor.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/BrushComponent.h"
#include "Atmosphere/AtmosphericFog.h"
#include "GameFramework/PlayerStart.h"
#include "Sound/SoundWave.h"
#include "EngineUtils.h"
#include "SteamAudioGeometryComponent.h"
#include "SteamAudioDynamicObjectComponent.h"
#include "SteamAudioStaticMeshActor.h"
#include "Async/Async.h"

#if WITH_EDITOR
namespace SteamAudio
{
	bool STEAMAUDIO_API ExportStaticGeometryForLevel(UWorld* World, ULevel* Level, FString FileName, bool bExportOBJ);
	bool STEAMAUDIO_API ExportDynamicObject(USteamAudioDynamicObjectComponent* DynamicObject, FString FileName, bool bExportOBJ);
}
#endif

DEFINE_LOG_CATEGORY_STATIC(LogSoundHouse, Log, All);

namespace
{
	const TCHAR* MatPath = TEXT("/Game/Occlusion/Materials/");
	const TCHAR* SAMatPath = TEXT("/Game/Occlusion/SteamAudio/");

	UMaterialInterface* Mat(const TCHAR* Name)
	{
		UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("%s%s.%s"), MatPath, Name, Name));
		if (!M) UE_LOG(LogSoundHouse, Warning, TEXT("Missing material %s"), Name);
		return M;
	}

	struct FBuilder
	{
		UWorld* W;
		UStaticMesh* Cube;      // LevelPrototyping SM_Cube: 100 cm, pivot at min corner
		UStaticMesh* Chamfer;   // SM_ChamferCube: 100 cm, centred
		UStaticMesh* Cyl;       // SM_Cylinder: 100 cm diameter, 100 tall, pivot at bottom centre
		UStaticMesh* Sphere;
		int32 Count = 0;
		FString Folder = TEXT("SoundHouse");

		template<class T> T* Spawn(const FVector& Loc, const FRotator& Rot, const FString& Label, const FString& Sub)
		{
			FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			T* A = W->SpawnActor<T>(T::StaticClass(), Loc, Rot, P);
#if WITH_EDITOR
			if (A) { A->SetActorLabel(Label); A->SetFolderPath(FName(*(Folder / Sub))); }
#endif
			++Count;
			return A;
		}

		/** Axis-aligned box from min to max corner (cm). Acoustic boxes get the tags read by the solvers and a Steam Audio geometry component. */
		AStaticMeshActor* Box(const FVector& Min, const FVector& Max, UMaterialInterface* M, const FString& Label, const FString& Sub,
			bool bAcoustic, const TCHAR* TLTag = nullptr, const TCHAR* SAMat = TEXT("SA_Concrete"), bool bCollide = true)
		{
			AStaticMeshActor* A = Spawn<AStaticMeshActor>(Min, FRotator::ZeroRotator, Label, Sub);
			UStaticMeshComponent* C = A->GetStaticMeshComponent();
			C->SetMobility(EComponentMobility::Static);
			C->SetStaticMesh(Cube);
			C->bUseDefaultCollision = false;   // otherwise the mesh default profile is re-applied on load and custom responses are lost
			C->SetCollisionProfileName(TEXT("BlockAll"));
			A->SetActorScale3D((Max - Min) / 100.f);
			if (M) C->SetMaterial(0, M);
			if (!bCollide) C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			if (bAcoustic)
			{
				C->ComponentTags.Add(TEXT("Acoustic"));
				if (TLTag) C->ComponentTags.Add(TLTag);
				USteamAudioGeometryComponent* G = NewObject<USteamAudioGeometryComponent>(A, TEXT("SteamAudioGeometry"));
				G->Material = FSoftObjectPath(FString::Printf(TEXT("%s%s.%s"), SAMatPath, SAMat, SAMat));
				A->AddInstanceComponent(G);
				G->RegisterComponent();
			}
			else
			{
				C->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
			}
			return A;
		}

		AStaticMeshActor* Prop(UStaticMesh* Mesh, const FVector& Loc, const FVector& Scale, UMaterialInterface* M, const FString& Label, const FRotator& Rot = FRotator::ZeroRotator, bool bCollide = true, bool bCastShadow = true)
		{
			AStaticMeshActor* A = Spawn<AStaticMeshActor>(Loc, Rot, Label, TEXT("Props"));
			UStaticMeshComponent* C = A->GetStaticMeshComponent();
			C->SetMobility(EComponentMobility::Static);
			C->SetStaticMesh(Mesh);
			C->bUseDefaultCollision = false;
			C->SetCollisionProfileName(TEXT("BlockAll"));
			A->SetActorScale3D(Scale);
			if (M) C->SetMaterial(0, M);
			C->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
			C->SetCastShadow(bCastShadow);
			if (!bCollide) C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			return A;
		}

		struct FOpen { float A0, A1, Z0, Z1; };

		/** Wall along X (constant Y), from X0 to X1, with openings given in X. */
		void WallX(float Y, float X0, float X1, float Thick, float H, TArray<FOpen> Opens, UMaterialInterface* M, const FString& Label, const TCHAR* TL, const TCHAR* SA)
		{
			Opens.Sort([](const FOpen& A, const FOpen& B) { return A.A0 < B.A0; });
			float Cur = X0; int32 i = 0;
			for (const FOpen& O : Opens)
			{
				if (O.A0 > Cur) Box(FVector(Cur, Y - Thick / 2, 0), FVector(O.A0, Y + Thick / 2, H), M, FString::Printf(TEXT("%s_%d"), *Label, i++), TEXT("Walls"), true, TL, SA);
				if (O.Z0 > 0.f) Box(FVector(O.A0, Y - Thick / 2, 0), FVector(O.A1, Y + Thick / 2, O.Z0), M, FString::Printf(TEXT("%s_%d"), *Label, i++), TEXT("Walls"), true, TL, SA);
				if (O.Z1 < H) Box(FVector(O.A0, Y - Thick / 2, O.Z1), FVector(O.A1, Y + Thick / 2, H), M, FString::Printf(TEXT("%s_%d"), *Label, i++), TEXT("Walls"), true, TL, SA);
				Cur = O.A1;
			}
			if (X1 > Cur) Box(FVector(Cur, Y - Thick / 2, 0), FVector(X1, Y + Thick / 2, H), M, FString::Printf(TEXT("%s_%d"), *Label, i++), TEXT("Walls"), true, TL, SA);
		}

		/** Wall along Y (constant X). */
		void WallY(float X, float Y0, float Y1, float Thick, float H, TArray<FOpen> Opens, UMaterialInterface* M, const FString& Label, const TCHAR* TL, const TCHAR* SA)
		{
			Opens.Sort([](const FOpen& A, const FOpen& B) { return A.A0 < B.A0; });
			float Cur = Y0; int32 i = 0;
			for (const FOpen& O : Opens)
			{
				if (O.A0 > Cur) Box(FVector(X - Thick / 2, Cur, 0), FVector(X + Thick / 2, O.A0, H), M, FString::Printf(TEXT("%s_%d"), *Label, i++), TEXT("Walls"), true, TL, SA);
				if (O.Z0 > 0.f) Box(FVector(X - Thick / 2, O.A0, 0), FVector(X + Thick / 2, O.A1, O.Z0), M, FString::Printf(TEXT("%s_%d"), *Label, i++), TEXT("Walls"), true, TL, SA);
				if (O.Z1 < H) Box(FVector(X - Thick / 2, O.A0, O.Z1), FVector(X + Thick / 2, O.A1, H), M, FString::Printf(TEXT("%s_%d"), *Label, i++), TEXT("Walls"), true, TL, SA);
				Cur = O.A1;
			}
			if (Y1 > Cur) Box(FVector(X - Thick / 2, Cur, 0), FVector(X + Thick / 2, Y1, H), M, FString::Printf(TEXT("%s_%d"), *Label, i++), TEXT("Walls"), true, TL, SA);
		}

		/** Glass pane filling an opening of a wall along Y or X. */
		void Glass(bool bAlongY, float Fixed, float A0, float A1, float Z0, float Z1, const FString& Label)
		{
			const FVector Mn = bAlongY ? FVector(Fixed - 3, A0, Z0) : FVector(A0, Fixed - 3, Z0);
			const FVector Mx = bAlongY ? FVector(Fixed + 3, A1, Z1) : FVector(A1, Fixed + 3, Z1);
			Box(Mn, Mx, Mat(TEXT("MI_Glass")), Label, TEXT("Windows"), true, TEXT("TL_Glass"), TEXT("SA_Glass"));
			// Thin frame
			UMaterialInterface* F = Mat(TEXT("MI_Frame"));
			const FVector T = bAlongY ? FVector(16, 0, 0) : FVector(0, 16, 0);
			auto Bar = [&](const FVector& B0, const FVector& B1) { Box(B0, B1, F, Label + TEXT("_Frame"), TEXT("Windows"), false); };
			if (bAlongY)
			{
				Bar(FVector(Fixed - 8, A0, Z0 - 4), FVector(Fixed + 8, A1, Z0));
				Bar(FVector(Fixed - 8, A0, Z1), FVector(Fixed + 8, A1, Z1 + 4));
				Bar(FVector(Fixed - 8, (A0 + A1) / 2 - 2, Z0), FVector(Fixed + 8, (A0 + A1) / 2 + 2, Z1));
			}
			else
			{
				Bar(FVector(A0, Fixed - 8, Z0 - 4), FVector(A1, Fixed + 8, Z0));
				Bar(FVector(A0, Fixed - 8, Z1), FVector(A1, Fixed + 8, Z1 + 4));
				Bar(FVector((A0 + A1) / 2 - 2, Fixed - 8, Z0), FVector((A0 + A1) / 2 + 2, Fixed + 8, Z1));
			}
		}

		AAcousticRoom* Room(const FString& Name, const FVector& Min, const FVector& Max, float Decay, float WetDb, bool bOutdoor, const FLinearColor& Tint, int32 Priority = 0)
		{
			AAcousticRoom* R = Spawn<AAcousticRoom>((Min + Max) * 0.5f, FRotator::ZeroRotator, TEXT("Room_") + Name, TEXT("Acoustics"));
			R->Bounds->SetBoxExtent((Max - Min) * 0.5f);
			R->DisplayName = Name; R->DecayTime = Decay; R->WetLevelDb = WetDb; R->bOutdoor = bOutdoor; R->Tint = Tint; R->Priority = Priority;
			return R;
		}

		/** Doorway in a wall. bAlongY: wall runs along Y (normal = X). Door frame trims are added around it. */
		AAcousticPortal* Portal(const FString& Name, bool bAlongY, float Fixed, float A0, float A1, float H, float WallThick, bool bDoor, bool bStartOpen)
		{
			const float Width = A1 - A0;
			const FVector Center = bAlongY ? FVector(Fixed, (A0 + A1) / 2, H / 2) : FVector((A0 + A1) / 2, Fixed, H / 2);
			const FRotator Rot = bAlongY ? FRotator::ZeroRotator : FRotator(0, 90.f, 0);   // portal local X = wall normal
			AAcousticPortal* P = Spawn<AAcousticPortal>(Center, Rot, TEXT("Portal_") + Name, TEXT("Acoustics"));
			P->DisplayName = Name;
			P->OpenTarget = bStartOpen ? 1.f : 0.f;
			AAcousticDoor* D = nullptr;
			if (bDoor)
			{
				D = Spawn<AAcousticDoor>(Center, Rot, TEXT("Door_") + Name, TEXT("Doors"));
				D->Setup(Width, H, Cube, Mat(TEXT("MI_DoorWood")), Mat(TEXT("MI_Metal")));
				USteamAudioDynamicObjectComponent* SD = NewObject<USteamAudioDynamicObjectComponent>(D, TEXT("SteamAudioDynamic"));
				D->AddInstanceComponent(SD); SD->RegisterComponent();
				USteamAudioGeometryComponent* G = NewObject<USteamAudioGeometryComponent>(D, TEXT("SteamAudioGeometry"));
				G->Material = FSoftObjectPath(FString::Printf(TEXT("%sSA_Door.SA_Door"), SAMatPath));
				D->AddInstanceComponent(G); G->RegisterComponent();
			}
			P->Setup(Width, H, WallThick + 10.f, D);
			// Frame trims (non-acoustic): two jambs and a head, dark charcoal.
			UMaterialInterface* F = Mat(TEXT("MI_Frame"));
			const float T = WallThick / 2 + 3;
			if (bAlongY)
			{
				Box(FVector(Fixed - T, A0 - 7, 0), FVector(Fixed + T, A0, H + 7), F, Name + TEXT("_Jamb"), TEXT("Trim"), false);
				Box(FVector(Fixed - T, A1, 0), FVector(Fixed + T, A1 + 7, H + 7), F, Name + TEXT("_Jamb"), TEXT("Trim"), false);
				Box(FVector(Fixed - T, A0 - 7, H), FVector(Fixed + T, A1 + 7, H + 7), F, Name + TEXT("_Head"), TEXT("Trim"), false);
			}
			else
			{
				Box(FVector(A0 - 7, Fixed - T, 0), FVector(A0, Fixed + T, H + 7), F, Name + TEXT("_Jamb"), TEXT("Trim"), false);
				Box(FVector(A1, Fixed - T, 0), FVector(A1 + 7, Fixed + T, H + 7), F, Name + TEXT("_Jamb"), TEXT("Trim"), false);
				Box(FVector(A0 - 7, Fixed - T, H), FVector(A1 + 7, Fixed + T, H + 7), F, Name + TEXT("_Head"), TEXT("Trim"), false);
			}
			return P;
		}

		void Lamp(const FVector& Pos, const FLinearColor& Col, float Intensity, float Radius, const FString& Label, bool bShade = true)
		{
			APointLight* L = Spawn<APointLight>(Pos, FRotator::ZeroRotator, Label, TEXT("Lights"));
			UPointLightComponent* C = Cast<UPointLightComponent>(L->GetLightComponent());
			C->SetMobility(EComponentMobility::Movable);
			C->SetIntensityUnits(ELightUnits::Candelas);
			C->SetIntensity(Intensity);
			C->SetLightColor(Col);
			C->SetAttenuationRadius(Radius);
			C->SetSourceRadius(8.f);
			C->SetCastShadows(true);
			if (bShade)
			{
				Prop(Cyl, Pos + FVector(0, 0, 6), FVector(0.42f, 0.42f, 0.14f), Mat(TEXT("MI_Glow_Warm")), Label + TEXT("_Shade"), FRotator::ZeroRotator, false, false);
				Prop(Cyl, Pos + FVector(0, 0, 20), FVector(0.02f, 0.02f, 0.9f), Mat(TEXT("MI_Frame")), Label + TEXT("_Cord"), FRotator::ZeroRotator, false, false);
			}
		}

		AAcousticEmitterActor* Emitter(const FString& Name, const FVector& Pos, const TCHAR* SoundPath, const FLinearColor& Col, float Volume)
		{
			AAcousticEmitterActor* E = Spawn<AAcousticEmitterActor>(Pos, FRotator::ZeroRotator, TEXT("Emitter_") + Name, TEXT("Sound"));
			E->Emitter->DisplayName = Name;
			E->Emitter->Color = Col;
			E->Emitter->BaseVolume = Volume;
			E->Emitter->Sound = LoadObject<USoundWave>(nullptr, SoundPath);
			if (!E->Emitter->Sound) UE_LOG(LogSoundHouse, Warning, TEXT("Missing sound %s"), SoundPath);
			return E;
		}
	};
}

int32 UOcclusionBuildLibrary::BuildSoundHouse(UObject* WorldContext)
{
	UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!W) return 0;
	FBuilder B;
	B.W = W;
	B.Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/LevelPrototyping/Meshes/SM_Cube.SM_Cube"));
	B.Chamfer = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/LevelPrototyping/Meshes/SM_ChamferCube.SM_ChamferCube"));
	B.Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/LevelPrototyping/Meshes/SM_Cylinder.SM_Cylinder"));
	B.Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!B.Cube || !B.Chamfer || !B.Cyl) { UE_LOG(LogSoundHouse, Error, TEXT("LevelPrototyping meshes missing")); return 0; }

	const float H = 320.f;              // wall height
	UMaterialInterface* Plaster = Mat(TEXT("MI_Plaster"));
	UMaterialInterface* Exterior = Mat(TEXT("MI_Exterior"));
	UMaterialInterface* Partition = Mat(TEXT("MI_Partition"));

	// ---------------- ground, floors, ceiling ----------------
	B.Box(FVector(-1500, -600, -30), FVector(3000, 2600, -1), Mat(TEXT("MI_Grass")), TEXT("Ground"), TEXT("Ground"), true, nullptr, TEXT("SA_Ground"));
	B.Box(FVector(-15, -15, -10), FVector(2615, 2015, 1), Mat(TEXT("MI_Floor")), TEXT("Floor"), TEXT("Ground"), true, nullptr, TEXT("SA_Concrete"));
	B.Box(FVector(-1100, 300, -2), FVector(-60, 1700, 2), Mat(TEXT("MI_Stone")), TEXT("Patio"), TEXT("Ground"), false);
	for (const auto& R : { TArray<float>{ 0.f, 0.f, 1400.f, 1200.f }, TArray<float>{ 0.f, 1200.f, 1400.f, 2000.f }, TArray<float>{ 1400.f, 0.f, 1650.f, 2000.f }, TArray<float>{ 1650.f, 0.f, 2600.f, 900.f }, TArray<float>{ 1650.f, 900.f, 2600.f, 2000.f } })
	{
		AStaticMeshActor* C = B.Box(FVector(R[0] - 15, R[1] - 15, H), FVector(R[2] + 15, R[3] + 15, H + 20), Mat(TEXT("MI_Ceiling")), TEXT("Ceiling"), TEXT("Ceiling"), true, nullptr, TEXT("SA_Concrete"));
		C->GetStaticMeshComponent()->ComponentTags.Add(TEXT("Ceiling"));
		C->Tags.Add(TEXT("Ceiling"));
		C->GetStaticMeshComponent()->bCastHiddenShadow = true;   // keeps the interior lighting identical when the ceiling is hidden from above
	}

	// ---------------- walls ----------------
	using O = FBuilder::FOpen;
	// Exterior (concrete, 30 cm)
	B.WallY(0.f, -15.f, 2015.f, 30.f, H, { O{ 450, 570, 0, 220 }, O{ 750, 1050, 90, 230 }, O{ 1500, 1700, 90, 230 } }, Exterior, TEXT("Wall_West"), nullptr, TEXT("SA_Concrete"));
	B.WallX(0.f, 15.f, 2615.f, 30.f, H, {}, Exterior, TEXT("Wall_North"), nullptr, TEXT("SA_Concrete"));
	B.WallX(2000.f, 15.f, 2615.f, 30.f, H, {}, Exterior, TEXT("Wall_South"), nullptr, TEXT("SA_Concrete"));
	B.WallY(2600.f, 15.f, 1985.f, 30.f, H, {}, Exterior, TEXT("Wall_East"), nullptr, TEXT("SA_Concrete"));
	// Interior concrete (20 cm)
	B.WallY(1400.f, 15.f, 1985.f, 20.f, H, { O{ 200, 440, 0, 260 }, O{ 1600, 1720, 0, 220 } }, Plaster, TEXT("Wall_HallCorridor"), nullptr, TEXT("SA_Concrete"));
	B.WallX(1200.f, 15.f, 1390.f, 20.f, H, { O{ 300, 420, 0, 220 }, O{ 800, 1100, 90, 230 } }, Plaster, TEXT("Wall_HallStudy"), nullptr, TEXT("SA_Concrete"));
	B.WallY(1650.f, 15.f, 1985.f, 20.f, H, { O{ 380, 500, 0, 220 }, O{ 1450, 1570, 0, 220 } }, Plaster, TEXT("Wall_CorridorRooms"), nullptr, TEXT("SA_Concrete"));
	// Wooden partition between music room and workshop (12 cm): transmission demo
	B.WallX(900.f, 1660.f, 2585.f, 12.f, H, {}, Partition, TEXT("Wall_WoodPartition"), TEXT("TL_Wood"), TEXT("SA_Wood"));
	// Windows (glass)
	B.Glass(true, 0.f, 750, 1050, 90, 230, TEXT("Window_Hall"));
	B.Glass(true, 0.f, 1500, 1700, 90, 230, TEXT("Window_Study"));
	B.Glass(false, 1200.f, 800, 1100, 90, 230, TEXT("Window_HallStudy"));
	// Hall pillar: obstruction inside one room
	B.Box(FVector(770, 570, 0), FVector(830, 630, H), Mat(TEXT("MI_Column")), TEXT("Pillar"), TEXT("Walls"), true, TEXT("TL_Furniture"), TEXT("SA_Concrete"));
	// Skirting boards in each room's accent colour (visual only)
	auto Skirt = [&](float X0, float Y0, float X1, float Y1, const TCHAR* M)
	{
		UMaterialInterface* Mi = Mat(M);
		B.Box(FVector(X0, Y0, 0), FVector(X1, Y0 + 2, 14), Mi, TEXT("Skirt"), TEXT("Trim"), false, nullptr, nullptr, false);
		B.Box(FVector(X0, Y1 - 2, 0), FVector(X1, Y1, 14), Mi, TEXT("Skirt"), TEXT("Trim"), false, nullptr, nullptr, false);
		B.Box(FVector(X0, Y0, 0), FVector(X0 + 2, Y1, 14), Mi, TEXT("Skirt"), TEXT("Trim"), false, nullptr, nullptr, false);
		B.Box(FVector(X1 - 2, Y0, 0), FVector(X1, Y1, 14), Mi, TEXT("Skirt"), TEXT("Trim"), false, nullptr, nullptr, false);
	};
	Skirt(15, 15, 1390, 1190, TEXT("MI_Accent_Hall"));
	Skirt(15, 1210, 1390, 1985, TEXT("MI_Accent_Study"));
	Skirt(1410, 15, 1640, 1985, TEXT("MI_Accent_Corridor"));
	Skirt(1660, 15, 2585, 894, TEXT("MI_Accent_Music"));
	Skirt(1660, 906, 2585, 1985, TEXT("MI_Accent_Workshop"));

	// ---------------- rooms & portals ----------------
	AAcousticRoom* Hall = B.Room(TEXT("客厅"), FVector(10, 10, 0), FVector(1390, 1190, H), 0.9f, -12.f, false, FLinearColor(0.2f, 0.7f, 0.7f));
	AAcousticRoom* Study = B.Room(TEXT("书房"), FVector(10, 1210, 0), FVector(1390, 1990, H), 0.5f, -16.f, false, FLinearColor(0.35f, 0.7f, 0.3f));
	AAcousticRoom* Corr = B.Room(TEXT("走廊"), FVector(1410, 10, 0), FVector(1640, 1990, H), 1.6f, -9.f, false, FLinearColor(0.6f, 0.6f, 0.65f));
	AAcousticRoom* Music = B.Room(TEXT("琴房"), FVector(1660, 10, 0), FVector(2590, 894, H), 1.2f, -11.f, false, FLinearColor(0.3f, 0.45f, 0.9f));
	AAcousticRoom* Shop = B.Room(TEXT("工坊"), FVector(1660, 906, 0), FVector(2590, 1990, H), 1.0f, -12.f, false, FLinearColor(0.95f, 0.55f, 0.2f));
	AAcousticRoom* Yard = B.Room(TEXT("庭院"), FVector(-1500, -600, 0), FVector(-15, 2600, 600), 0.3f, -30.f, true, FLinearColor(0.4f, 0.7f, 0.35f), -1);
	(void)Hall; (void)Study; (void)Corr; (void)Music; (void)Shop; (void)Yard;
	B.Portal(TEXT("前门"), true, 0.f, 450, 570, 220, 30, true, true);
	B.Portal(TEXT("拱门"), true, 1400.f, 200, 440, 260, 20, false, true);
	B.Portal(TEXT("书房门"), false, 1200.f, 300, 420, 220, 20, true, true);
	B.Portal(TEXT("书房侧门"), true, 1400.f, 1600, 1720, 220, 20, true, false);
	B.Portal(TEXT("琴房门"), true, 1650.f, 380, 500, 220, 20, true, true);
	B.Portal(TEXT("工坊门"), true, 1650.f, 1450, 1570, 220, 20, true, false);

	// ---------------- furniture ----------------
	UMaterialInterface* Wood = Mat(TEXT("MI_Wood")), *WoodDark = Mat(TEXT("MI_WoodDark")), *Metal = Mat(TEXT("MI_Metal")), *FabricA = Mat(TEXT("MI_FabricTeal")), *FabricB = Mat(TEXT("MI_FabricRust"));
	// Hall: sofa, coffee table, rug, sideboard
	B.Prop(B.Chamfer, FVector(330, 1000, 25), FVector(2.2f, 0.85f, 0.5f), FabricA, TEXT("Sofa_Seat"));
	B.Prop(B.Chamfer, FVector(330, 1110, 60), FVector(2.2f, 0.25f, 0.9f), FabricA, TEXT("Sofa_Back"));
	B.Prop(B.Chamfer, FVector(330, 800, 22), FVector(1.2f, 0.6f, 0.08f), WoodDark, TEXT("CoffeeTable"));
	B.Prop(B.Cyl, FVector(330, 800, 0), FVector(0.12f, 0.12f, 0.2f), Metal, TEXT("CoffeeTable_Leg"));
	B.Box(FVector(120, 650, 1), FVector(560, 1060, 2), Mat(TEXT("MI_Rug")), TEXT("Rug"), TEXT("Props"), false, nullptr, nullptr, false);
	B.Prop(B.Chamfer, FVector(1200, 1120, 45), FVector(2.4f, 0.45f, 0.9f), Wood, TEXT("Sideboard"));
	B.Prop(B.Chamfer, FVector(100, 300, 75), FVector(0.5f, 1.6f, 1.5f), Wood, TEXT("Bookcase_Hall"));
	// Test speaker stand behind the pillar
	// Study: desk, chair, shelves
	B.Prop(B.Chamfer, FVector(700, 1850, 74), FVector(1.6f, 0.7f, 0.06f), Wood, TEXT("Desk"));
	B.Prop(B.Chamfer, FVector(700, 1850, 37), FVector(1.5f, 0.6f, 0.72f), WoodDark, TEXT("Desk_Base"));
	B.Prop(B.Chamfer, FVector(700, 1720, 45), FVector(0.5f, 0.5f, 0.1f), FabricB, TEXT("Chair"));
	B.Prop(B.Chamfer, FVector(1250, 1600, 100), FVector(0.4f, 2.4f, 2.0f), Wood, TEXT("Shelf_Study"));
	// Music room: piano block, speaker cabinets
	B.Prop(B.Chamfer, FVector(2050, 150, 60), FVector(1.6f, 0.6f, 1.2f), Mat(TEXT("MI_Piano")), TEXT("Piano"));
	B.Prop(B.Chamfer, FVector(2450, 250, 40), FVector(0.8f, 0.5f, 0.8f), WoodDark, TEXT("RadioCabinet"));
	B.Box(FVector(1900, 450, 1), FVector(2400, 800, 2), Mat(TEXT("MI_RugBlue")), TEXT("Rug_Music"), TEXT("Props"), false, nullptr, nullptr, false);
	// Workshop: workbench, machine
	B.Prop(B.Chamfer, FVector(2450, 1500, 45), FVector(0.8f, 2.2f, 0.9f), WoodDark, TEXT("Workbench"));
	B.Prop(B.Chamfer, FVector(2200, 1650, 55), FVector(1.1f, 1.1f, 1.1f), Mat(TEXT("MI_Machine")), TEXT("Machine_Body"));
	B.Prop(B.Cyl, FVector(2200, 1650, 110), FVector(0.6f, 0.6f, 0.35f), Metal, TEXT("Machine_Drum"));
	B.Prop(B.Cyl, FVector(1900, 1850, 0), FVector(0.6f, 0.6f, 0.9f), Mat(TEXT("MI_Barrel")), TEXT("Barrel"));
	// Corridor: bench and plant pots
	B.Prop(B.Chamfer, FVector(1600, 1000, 22), FVector(0.45f, 1.4f, 0.44f), Wood, TEXT("Bench_Corridor"));
	B.Prop(B.Cyl, FVector(1450, 700, 0), FVector(0.4f, 0.4f, 0.5f), Mat(TEXT("MI_Pot")), TEXT("Pot"));
	B.Prop(B.Sphere, FVector(1450, 700, 75), FVector(0.55f, 0.55f, 0.6f), Mat(TEXT("MI_Leaves")), TEXT("Plant"), FRotator::ZeroRotator, false);
	// Extra dressing (visual only, ignored by acoustic traces)
	{
		UMaterialInterface* Glow = Mat(TEXT("MI_Glow_Warm"));
		auto Armchair = [&](const FVector& P, float Yaw, UMaterialInterface* F, const TCHAR* L)
		{
			const FRotator R(0, Yaw, 0);
			B.Prop(B.Chamfer, P + FVector(0, 0, 22), FVector(0.85f, 0.8f, 0.44f), F, L, R);
			B.Prop(B.Chamfer, P + R.RotateVector(FVector(-36, 0, 55)), FVector(0.18f, 0.8f, 0.7f), F, L, R);
			for (float S : { -36.f, 36.f }) B.Prop(B.Chamfer, P + R.RotateVector(FVector(0, S, 40)), FVector(0.8f, 0.1f, 0.36f), F, L, R);
		};
		auto FloorLamp = [&](const FVector& P, const TCHAR* L)
		{
			B.Prop(B.Cyl, P, FVector(0.3f, 0.3f, 0.03f), Metal, L);
			B.Prop(B.Cyl, P, FVector(0.03f, 0.03f, 1.45f), Metal, L);
			B.Prop(B.Cyl, P + FVector(0, 0, 140), FVector(0.4f, 0.4f, 0.32f), Glow, L, FRotator::ZeroRotator, false, false);
		};
		auto Painting = [&](const FVector& A, const FVector& Bv, const TCHAR* M, const TCHAR* L)
		{
			B.Box(A, Bv, Mat(TEXT("MI_Frame")), L, TEXT("Props"), false, nullptr, nullptr, false);
			const FVector In = (Bv - A).GetAbs().X < 6.f ? FVector(Bv.X > 1000.f ? -1 : 1, 0, 0) : FVector(0, Bv.Y > 1000.f ? -1 : 1, 0);
			const FVector Inset(In.X == 0 ? 8 : 0, In.Y == 0 ? 8 : 0, 8);
			B.Box(A + Inset + In * 1.5f, Bv - Inset + In * 1.5f, Mat(M), L, TEXT("Props"), false, nullptr, nullptr, false);
		};
		auto Plant = [&](const FVector& P, float S, const TCHAR* L)
		{
			B.Prop(B.Cyl, P, FVector(0.36f, 0.36f, 0.45f) * S, Mat(TEXT("MI_Pot")), L);
			B.Prop(B.Sphere, P + FVector(0, 0, 70 * S), FVector(0.6f, 0.6f, 0.75f) * S, Mat(TEXT("MI_Leaves")), L, FRotator::ZeroRotator, false);
		};
		// Hall
		Armchair(FVector(600, 860, 0), 180.f, FabricB, TEXT("Armchair_Hall"));
		FloorLamp(FVector(80, 1120, 0), TEXT("FloorLamp_Hall"));
		Painting(FVector(180, 15, 120), FVector(440, 19, 230), TEXT("MI_Accent_Music"), TEXT("Painting_Hall_1"));
		Painting(FVector(520, 15, 140), FVector(660, 19, 220), TEXT("MI_Accent_Workshop"), TEXT("Painting_Hall_2"));
		Plant(FVector(1330, 1130, 0), 1.1f, TEXT("Plant_Hall"));
		Plant(FVector(60, 60, 0), 0.9f, TEXT("Plant_Hall_2"));
		// Study
		B.Box(FVector(420, 1560, 1), FVector(1000, 1960, 2), Mat(TEXT("MI_RugBlue")), TEXT("Rug_Study"), TEXT("Props"), false, nullptr, nullptr, false);
		Armchair(FVector(260, 1480, 0), 45.f, FabricA, TEXT("Armchair_Study"));
		FloorLamp(FVector(120, 1340, 0), TEXT("FloorLamp_Study"));
		B.Prop(B.Chamfer, FVector(640, 1262, 95), FVector(1.8f, 0.4f, 1.9f), WoodDark, TEXT("Shelf_Study_2"));
		B.Prop(B.Cyl, FVector(560, 1870, 77), FVector(0.14f, 0.14f, 0.12f), Glow, TEXT("DeskLamp"), FRotator::ZeroRotator, false, false);
		// Music room
		B.Prop(B.Chamfer, FVector(2050, 245, 22), FVector(0.9f, 0.35f, 0.44f), WoodDark, TEXT("PianoBench"));
		B.Prop(B.Cyl, FVector(2250, 420, 0), FVector(0.03f, 0.03f, 1.1f), Metal, TEXT("MusicStand"));
		B.Prop(B.Chamfer, FVector(2250, 420, 115), FVector(0.5f, 0.04f, 0.35f), Mat(TEXT("MI_Frame")), TEXT("MusicStand"), FRotator(0, 0, 20));
		Armchair(FVector(2400, 640, 0), 210.f, FabricA, TEXT("Armchair_Music"));
		Plant(FVector(1720, 60, 0), 1.0f, TEXT("Plant_Music"));
		Painting(FVector(2581, 300, 130), FVector(2585, 560, 230), TEXT("MI_Accent_Hall"), TEXT("Painting_Music"));
		// Workshop
		for (const FVector& C : { FVector(1760, 1120, 25), FVector(1815, 1120, 25), FVector(1787, 1122, 75), FVector(1760, 1180, 25) })
			B.Prop(B.Chamfer, C, FVector(0.5f, 0.5f, 0.5f), Wood, TEXT("Crate"), FRotator(0, C.X * 0.3f, 0));
		B.Prop(B.Chamfer, FVector(2150, 1950, 90), FVector(2.0f, 0.4f, 1.8f), Metal, TEXT("Shelf_Workshop"));
		B.Box(FVector(2581, 1100, 110), FVector(2585, 1400, 220), Mat(TEXT("MI_WoodDark")), TEXT("ToolBoard"), TEXT("Props"), false, nullptr, nullptr, false);
		// Corridor runner
		B.Box(FVector(1465, 80, 1), FVector(1585, 1920, 2), Mat(TEXT("MI_Rug")), TEXT("Rug_Corridor"), TEXT("Props"), false, nullptr, nullptr, false);
		Painting(FVector(1636, 700, 130), FVector(1640, 860, 220), TEXT("MI_Accent_Study"), TEXT("Painting_Corridor"));
	}
	// Courtyard: fire pit ring, benches, trees, flower beds, street lights
	for (int32 k = 0; k < 10; ++k)
	{
		const float A = k * 36.f;
		B.Prop(B.Chamfer, FVector(-600, 1000, 15) + FRotator(0, A, 0).RotateVector(FVector(70, 0, 0)), FVector(0.35f, 0.25f, 0.3f), Mat(TEXT("MI_Stone")), TEXT("FirePit_Stone"), FRotator(0, A, 0));
	}
	B.Prop(B.Cyl, FVector(-600, 1000, 0), FVector(0.9f, 0.9f, 0.12f), Mat(TEXT("MI_Fire")), TEXT("FireBed"), FRotator::ZeroRotator, false);
	B.Prop(B.Chamfer, FVector(-600, 760, 22), FVector(1.6f, 0.4f, 0.42f), Wood, TEXT("Bench_Yard_1"));
	B.Prop(B.Chamfer, FVector(-850, 1000, 22), FVector(0.4f, 1.6f, 0.42f), Wood, TEXT("Bench_Yard_2"), FRotator::ZeroRotator);
	if (UStaticMesh* Tree = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/ArchVis/SampleScene/Tree/HillTree_02.HillTree_02")))
	{
		B.Prop(Tree, FVector(-1150, 250, 0), FVector(0.55f), nullptr, TEXT("Tree_1"), FRotator(0, 40, 0), false);
		B.Prop(Tree, FVector(-1050, 1850, 0), FVector(0.48f), nullptr, TEXT("Tree_2"), FRotator(0, 160, 0), false);
	}
	if (UStaticMesh* Bed = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Building/Geometry/SM_FlowerBed.SM_FlowerBed")))
	{
		B.Prop(Bed, FVector(-200, 1000, 0), FVector(0.6f, 1.0f, 1.f), nullptr, TEXT("FlowerBed_1"));
		B.Prop(Bed, FVector(-1300, 1100, 0), FVector(0.6f, 1.2f, 1.f), nullptr, TEXT("FlowerBed_2"));
	}
	if (UStaticMesh* SL = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Building/Geometry/SM_StreetLight.SM_StreetLight")))
	{
		B.Prop(SL, FVector(-250, 380, 0), FVector(0.8f), nullptr, TEXT("StreetLight_1"));
		B.Prop(SL, FVector(-250, 1650, 0), FVector(0.8f), nullptr, TEXT("StreetLight_2"));
	}

	// ---------------- sound sources ----------------
	B.Emitter(TEXT("收音机 · 音乐"), FVector(2450, 250, 105), TEXT("/Game/Occlusion/Audio/SW_Radio.SW_Radio"), FLinearColor(0.35f, 0.55f, 1.f), 0.9f);
	B.Prop(B.Chamfer, FVector(2450, 250, 95), FVector(0.45f, 0.22f, 0.28f), Mat(TEXT("MI_RadioBody")), TEXT("Radio"));
	B.Prop(B.Cyl, FVector(2450, 236, 88), FVector(0.14f, 0.14f, 0.02f), Mat(TEXT("MI_Glow_Cool")), TEXT("Radio_Dial"), FRotator(90, 0, 0), false);
	B.Emitter(TEXT("机床 · 嗡鸣"), FVector(2200, 1650, 120), TEXT("/Game/Occlusion/Audio/SW_Machine.SW_Machine"), FLinearColor(1.f, 0.55f, 0.15f), 0.85f);
	B.Emitter(TEXT("篝火"), FVector(-600, 1000, 40), TEXT("/Game/XRFramework/Audio/Fire01.Fire01"), FLinearColor(1.f, 0.35f, 0.1f), 1.0f);
	B.Emitter(TEXT("座钟 · 滴答"), FVector(150, 1950, 180), TEXT("/Game/Occlusion/Audio/SW_Clock.SW_Clock"), FLinearColor(0.45f, 0.85f, 0.35f), 0.8f);
	B.Prop(B.Chamfer, FVector(150, 1945, 110), FVector(0.45f, 0.3f, 2.2f), WoodDark, TEXT("Clock_Case"));
	B.Prop(B.Cyl, FVector(150, 1928, 180), FVector(0.32f, 0.32f, 0.03f), Mat(TEXT("MI_ClockFace")), TEXT("Clock_Face"), FRotator(90, 0, 0), false);
	B.Emitter(TEXT("测试音箱"), FVector(1100, 600, 120), TEXT("/Game/Occlusion/Audio/SW_TestBurst.SW_TestBurst"), FLinearColor(0.95f, 0.85f, 0.25f), 0.8f);
	B.Prop(B.Chamfer, FVector(1100, 600, 120), FVector(0.32f, 0.3f, 0.5f), Mat(TEXT("MI_Speaker")), TEXT("Speaker_Box"));
	B.Prop(B.Cyl, FVector(1100, 600, 0), FVector(0.06f, 0.06f, 0.95f), Metal, TEXT("Speaker_Stand"));
	B.Prop(B.Cyl, FVector(1084, 600, 128), FVector(0.2f, 0.2f, 0.02f), Mat(TEXT("MI_Frame")), TEXT("Speaker_Cone"), FRotator(90, 0, 90), false);

	// ---------------- patrol NPC (footsteps) ----------------
	{
		APatrolNPC* N = B.Spawn<APatrolNPC>(FVector(300, 300, 100), FRotator::ZeroRotator, TEXT("PatrolNPC"), TEXT("Sound"));
		N->Waypoints = { FVector(300, 300, 100), FVector(1150, 300, 100), FVector(1525, 320, 100), FVector(1525, 1300, 100), FVector(1525, 320, 100), FVector(1150, 300, 100) };
		N->Emitter->DisplayName = TEXT("巡逻者 · 脚步");
		N->Emitter->Color = FLinearColor(0.85f, 0.5f, 0.95f);
		N->Emitter->BaseVolume = 0.55f;
		N->Emitter->Sound = LoadObject<USoundWave>(nullptr, TEXT("/Game/Occlusion/Audio/SW_Step.SW_Step"));
	}

	// ---------------- lights & sky ----------------
	{
		ADirectionalLight* Sun = B.Spawn<ADirectionalLight>(FVector(0, 0, 1000), FRotator(-38.f, -130.f, 0), TEXT("Sun"), TEXT("Lights"));
		UDirectionalLightComponent* C = Cast<UDirectionalLightComponent>(Sun->GetLightComponent());
		C->SetMobility(EComponentMobility::Movable);
		C->SetIntensity(9.f);
		C->SetLightColor(FLinearColor(1.f, 0.82f, 0.62f));
		C->SetAtmosphereSunLight(true);
		AActor* Sky = B.Spawn<AActor>(FVector::ZeroVector, FRotator::ZeroRotator, TEXT("SkyAtmosphere"), TEXT("Lights"));
		USkyAtmosphereComponent* SA = NewObject<USkyAtmosphereComponent>(Sky, TEXT("SkyAtmosphere"));
		Sky->SetRootComponent(SA); Sky->AddInstanceComponent(SA); SA->RegisterComponent();
		ASkyLight* SL = B.Spawn<ASkyLight>(FVector(0, 0, 600), FRotator::ZeroRotator, TEXT("SkyLight"), TEXT("Lights"));
		USkyLightComponent* SLC = SL->GetLightComponent();
		SLC->SetMobility(EComponentMobility::Movable);
		SLC->bRealTimeCapture = true;
		SLC->SetIntensity(1.f);
		AExponentialHeightFog* Fog = B.Spawn<AExponentialHeightFog>(FVector(0, 0, -100), FRotator::ZeroRotator, TEXT("Fog"), TEXT("Lights"));
		Fog->GetComponent()->SetFogDensity(0.012f);
		Fog->GetComponent()->SetVolumetricFog(false);
		APostProcessVolume* PP = B.Spawn<APostProcessVolume>(FVector::ZeroVector, FRotator::ZeroRotator, TEXT("PostProcess"), TEXT("Lights"));
		PP->bUnbound = true;
		PP->Settings.bOverride_AutoExposureMethod = true; PP->Settings.AutoExposureMethod = AEM_Histogram;
		PP->Settings.bOverride_AutoExposureMinBrightness = true; PP->Settings.AutoExposureMinBrightness = -1.f;
		PP->Settings.bOverride_AutoExposureMaxBrightness = true; PP->Settings.AutoExposureMaxBrightness = 5.f;
		PP->Settings.bOverride_AutoExposureBias = true; PP->Settings.AutoExposureBias = 0.3f;
		PP->Settings.bOverride_AutoExposureSpeedUp = true; PP->Settings.AutoExposureSpeedUp = 4.f;
		PP->Settings.bOverride_AutoExposureSpeedDown = true; PP->Settings.AutoExposureSpeedDown = 2.f;
		PP->Settings.bOverride_VignetteIntensity = true; PP->Settings.VignetteIntensity = 0.35f;
		PP->Settings.bOverride_BloomIntensity = true; PP->Settings.BloomIntensity = 0.45f;
	}
	const FLinearColor Warm(1.f, 0.78f, 0.55f), Neutral(1.f, 0.9f, 0.8f), Cool(0.75f, 0.85f, 1.f);
	B.Lamp(FVector(350, 850, 260), Warm, 160.f, 1100.f, TEXT("Lamp_Hall_1"));
	B.Lamp(FVector(1050, 350, 260), Warm, 140.f, 1100.f, TEXT("Lamp_Hall_2"));
	B.Lamp(FVector(700, 1600, 260), Warm, 130.f, 900.f, TEXT("Lamp_Study"));
	B.Lamp(FVector(1525, 400, 290), Neutral, 70.f, 800.f, TEXT("Lamp_Corridor_1"));
	B.Lamp(FVector(1525, 1500, 290), Neutral, 70.f, 800.f, TEXT("Lamp_Corridor_2"));
	B.Lamp(FVector(2150, 450, 260), Cool, 150.f, 1000.f, TEXT("Lamp_Music"));
	B.Lamp(FVector(2150, 1450, 260), Warm, 160.f, 1000.f, TEXT("Lamp_Workshop"));
	B.Lamp(FVector(-600, 1000, 80), FLinearColor(1.f, 0.45f, 0.15f), 380.f, 900.f, TEXT("Fire_Light"), false);
	B.Lamp(FVector(-250, 380, 330), Warm, 90.f, 900.f, TEXT("StreetLamp_1"), false);
	B.Lamp(FVector(-250, 1650, 330), Warm, 90.f, 900.f, TEXT("StreetLamp_2"), false);

	// ---------------- player start & wave bake ----------------
	B.Spawn<APlayerStart>(FVector(380, 700, 100), FRotator(0, 0, 0), TEXT("PlayerStart"), TEXT("Game"));
	B.Spawn<AWaveBakeActor>(FVector::ZeroVector, FRotator::ZeroRotator, TEXT("WaveBake"), TEXT("Acoustics"));

	UE_LOG(LogSoundHouse, Log, TEXT("Sound house built: %d actors"), B.Count);
	return B.Count;
}

bool UOcclusionBuildLibrary::ExportSteamAudio(UObject* WorldContext, const FString& StaticAssetPath, const FString& DoorAssetFolder)
{
#if WITH_EDITOR
	UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!W) return false;
	auto RunPumped = [](TFunction<bool()> Fn) -> bool
	{
		TFuture<bool> F = Async(EAsyncExecution::Thread, MoveTemp(Fn));
		while (!F.IsReady())
		{
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
			FPlatformProcess::Sleep(0.005f);
		}
		return F.Get();
	};
	ULevel* Level = W->PersistentLevel;
	const bool bStatic = RunPumped([W, Level, StaticAssetPath]() { return SteamAudio::ExportStaticGeometryForLevel(W, Level, StaticAssetPath, false); });
	UE_LOG(LogSoundHouse, Log, TEXT("Steam Audio static export: %s"), bStatic ? TEXT("ok") : TEXT("FAILED"));
	int32 Doors = 0;
	for (TActorIterator<AAcousticDoor> It(W); It; ++It)
	{
		USteamAudioDynamicObjectComponent* D = It->FindComponentByClass<USteamAudioDynamicObjectComponent>();
		if (!D) continue;
		const FString Name = FString::Printf(TEXT("%s/DO_%s.DO_%s"), *DoorAssetFolder, *It->GetName(), *It->GetName());
		const bool bOk = RunPumped([D, Name]() { return SteamAudio::ExportDynamicObject(D, Name, false); });
		Doors += bOk ? 1 : 0;
	}
	UE_LOG(LogSoundHouse, Log, TEXT("Steam Audio dynamic objects exported: %d"), Doors);
	return bStatic;
#else
	return false;
#endif
}
