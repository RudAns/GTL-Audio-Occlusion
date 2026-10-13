#include "OcclusionCharacter.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "SteamAudioListenerComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Animation/AnimInstance.h"
#include "EngineUtils.h"

AOcclusionCharacter::AOcclusionCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	GetCapsuleComponent()->InitCapsuleSize(38.f, 92.f);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> SkelMesh(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"));
	static ConstructorHelpers::FClassFinder<UAnimInstance> Anim(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed"));
	if (SkelMesh.Succeeded()) GetMesh()->SetSkeletalMesh(SkelMesh.Object);
	if (Anim.Succeeded()) GetMesh()->SetAnimInstanceClass(Anim.Class);
	GetMesh()->SetRelativeLocationAndRotation(FVector(0, 0, -92.f), FRotator(0, -90.f, 0));
	GetMesh()->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);

	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0, 540.f, 0);
	GetCharacterMovement()->MaxWalkSpeed = 380.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;

	Boom = CreateDefaultSubobject<USpringArmComponent>(TEXT("Boom"));
	Boom->SetupAttachment(RootComponent);
	Boom->bDoCollisionTest = false;
	Boom->bUsePawnControlRotation = false;
	Boom->SetUsingAbsoluteRotation(true);
	Boom->TargetArmLength = TopZoom;
	Boom->bEnableCameraLag = true;
	Boom->CameraLagSpeed = 8.f;

	TopCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("TopCamera"));
	TopCamera->SetupAttachment(Boom);
	TopCamera->SetFieldOfView(50.f);

	FPCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FPCamera"));
	FPCamera->SetupAttachment(RootComponent);
	FPCamera->SetRelativeLocation(FVector(10.f, 0.f, 70.f));
	FPCamera->bUsePawnControlRotation = true;
	FPCamera->SetFieldOfView(90.f);

	Ears = CreateDefaultSubobject<USceneComponent>(TEXT("Ears"));
	Ears->SetupAttachment(RootComponent);
	Ears->SetRelativeLocation(FVector(0, 0, 68.f));   // about 1.6 m above the floor

	SteamListener = CreateDefaultSubobject<USteamAudioListenerComponent>(TEXT("SteamListener"));
}

void AOcclusionCharacter::BeginPlay()
{
	Super::BeginPlay();
	SetView(View);
}

void AOcclusionCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	SetView(View);
}

void AOcclusionCharacter::SetView(EViewMode V)
{
	View = V;
	const bool bFP = (V == EViewMode::FirstPerson);
	FPCamera->SetActive(bFP);
	TopCamera->SetActive(!bFP);
	GetMesh()->SetOwnerNoSee(bFP);
	bUseControllerRotationYaw = bFP;
	GetCharacterMovement()->bOrientRotationToMovement = !bFP;
	// The ceiling is a cut-away: visible from inside in first person, hidden from above.
	for (TActorIterator<AActor> It(GetWorld()); It; ++It) if (It->ActorHasTag(TEXT("Ceiling"))) It->SetActorHiddenInGame(!bFP);
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		// The listener stays at the ears in both views; orientation follows the camera (report ch. 9.7, third-person pitfall).
		PC->SetAudioListenerOverride(Ears, FVector::ZeroVector, FRotator::ZeroRotator);
		if (bFP) PC->SetControlRotation(FRotator(0.f, TopYaw + 0.f, 0.f));
	}
}

FRotator AOcclusionCharacter::GetMoveBasis() const
{
	if (View == EViewMode::FirstPerson && Controller) return FRotator(0.f, Controller->GetControlRotation().Yaw, 0.f);
	return FRotator(0.f, TopYaw, 0.f);
}

FVector AOcclusionCharacter::GetEarLocation() const { return Ears->GetComponentLocation(); }

void AOcclusionCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TopYaw = FMath::FInterpTo(TopYaw, TopYawTarget, DeltaSeconds, 8.f);
	TopZoom = FMath::FInterpTo(TopZoom, TopZoomTarget, DeltaSeconds, 8.f);
	Boom->SetWorldRotation(FRotator(TopPitch, TopYaw, 0.f));
	Boom->TargetArmLength = TopZoom;
	// Listener orientation: camera yaw in both views, so left/right on screen matches left/right in the headphones.
	const float Yaw = (View == EViewMode::FirstPerson && Controller) ? Controller->GetControlRotation().Yaw : TopYaw;
	Ears->SetWorldRotation(FRotator(0.f, Yaw, 0.f));
}
