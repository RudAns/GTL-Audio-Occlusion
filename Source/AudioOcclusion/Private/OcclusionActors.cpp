#include "OcclusionActors.h"
#include "AcousticEmitterComponent.h"
#include "OcclusionCharacter.h"
#include "OcclusionPlayerController.h"
#include "SteamAudioSourceComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Animation/AnimInstance.h"
#include "CaptureDirector.h"
#include "Misc/CommandLine.h"

static void ConfigureSteamSource(USteamAudioSourceComponent* S)
{
	S->bSimulateOcclusion = true;
	S->OcclusionType = EOcclusionType::VOLUMETRIC;
	S->OcclusionRadius = 0.4f;
	S->OcclusionSamples = 16;
	S->bSimulateTransmission = true;
	S->MaxTransmissionSurfaces = 3;
}

AAcousticEmitterActor::AAcousticEmitterActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Emitter = CreateDefaultSubobject<UAcousticEmitterComponent>(TEXT("Emitter"));
	Emitter->SetupAttachment(RootComponent);
	SteamSource = CreateDefaultSubobject<USteamAudioSourceComponent>(TEXT("SteamSource"));
	ConfigureSteamSource(SteamSource);
}

APatrolNPC::APatrolNPC()
{
	PrimaryActorTick.bCanEverTick = true;
	GetCapsuleComponent()->InitCapsuleSize(36.f, 92.f);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> SkelMesh(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	static ConstructorHelpers::FClassFinder<UAnimInstance> Anim(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed"));
	if (SkelMesh.Succeeded()) GetMesh()->SetSkeletalMesh(SkelMesh.Object);
	if (Anim.Succeeded()) GetMesh()->SetAnimInstanceClass(Anim.Class);
	GetMesh()->SetRelativeLocationAndRotation(FVector(0, 0, -92.f), FRotator(0, -90.f, 0));
	GetMesh()->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0, 300.f, 0);
	GetCharacterMovement()->MaxWalkSpeed = 150.f;
	bUseControllerRotationYaw = false;
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	Emitter = CreateDefaultSubobject<UAcousticEmitterComponent>(TEXT("Emitter"));
	Emitter->SetupAttachment(RootComponent);
	Emitter->SetRelativeLocation(FVector(0, 0, -62.f));   // footsteps: about 30 cm above the floor, clear of the floor slab for the ray tests
	Emitter->bOneShot = true;
	SteamSource = CreateDefaultSubobject<USteamAudioSourceComponent>(TEXT("SteamSource"));
	ConfigureSteamSource(SteamSource);
}

void APatrolNPC::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Footsteps follow the animation: a step fires when a foot bone comes back down to the floor.
	{
		const float Ground = GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		static const FName Feet[2] = { TEXT("foot_l"), TEXT("foot_r") };
		const bool bMoving = GetVelocity().Size2D() > 20.f;
		for (int32 i = 0; i < 2; ++i)
		{
			const float Hcm = GetMesh()->GetSocketLocation(Feet[i]).Z - Ground;
			if (bMoving) { FootMin[i] = FMath::Min(FootMin[i], Hcm); FootMax[i] = FMath::Max(FootMax[i], Hcm); }
			// Thresholds adapt to the walk cycle's foot height range once it is known (ankle bone, not the sole).
			const float Range = FootMax[i] - FootMin[i];
			const float Lift = Range > 6.f ? FootMin[i] + 0.6f * Range : FootLiftCm;
			const float Plant = Range > 6.f ? FootMin[i] + 0.25f * Range : FootPlantCm;
			if (Hcm > Lift) bFootArmed[i] = true;
			else if (bFootArmed[i] && Hcm < Plant && bMoving)
			{
				bFootArmed[i] = false;
				++StepCount;
				if (Emitter) Emitter->TriggerOneShot(FMath::FRandRange(0.9f, 1.1f));
			}
		}
	}
	if (Waypoints.Num() < 2) return;
	if (Pause > 0.f) { Pause -= DeltaSeconds; return; }
	const FVector To = Waypoints[Target] - GetActorLocation();
	if (To.Size2D() < 40.f) { Target = (Target + 1) % Waypoints.Num(); Pause = PauseSeconds; return; }
	AddMovementInput(To.GetSafeNormal2D(), 1.f);
}

AOcclusionGameMode::AOcclusionGameMode()
{
	DefaultPawnClass = AOcclusionCharacter::StaticClass();
	PlayerControllerClass = AOcclusionPlayerController::StaticClass();
}

void AOcclusionGameMode::StartPlay()
{
	Super::StartPlay();
	FString Dir;
	if (FParse::Value(FCommandLine::Get(), TEXT("OccCapture="), Dir))
	{
		ACaptureDirector* D = GetWorld()->SpawnActorDeferred<ACaptureDirector>(ACaptureDirector::StaticClass(), FTransform::Identity);
		D->OutDir = Dir;
		FString Only;
		if (FParse::Value(FCommandLine::Get(), TEXT("OccOnly="), Only, false)) Only.Replace(TEXT(","), TEXT("+")).ParseIntoArray(D->Only, TEXT("+"));
		D->FinishSpawning(FTransform::Identity);
	}
}
