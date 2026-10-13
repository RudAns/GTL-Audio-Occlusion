#include "AcousticPortal.h"
#include "AcousticRoom.h"
#include "AcousticDoor.h"
#include "Components/BoxComponent.h"
#include "EngineUtils.h"

AAcousticPortal::AAcousticPortal()
{
	PrimaryActorTick.bCanEverTick = true;
	Opening = CreateDefaultSubobject<UBoxComponent>(TEXT("Opening"));
	Opening->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Opening->SetCanEverAffectNavigation(false);
	Opening->ShapeColor = FColor(255, 160, 40);
	RootComponent = Opening;
}

void AAcousticPortal::Setup(float InWidth, float InHeight, float InDepth, AAcousticDoor* InDoor)
{
	Width = InWidth; Height = InHeight; Depth = InDepth;
	Door = InDoor; bHasDoor = (InDoor != nullptr);
	Opening->SetBoxExtent(FVector(Depth * 0.5f, Width * 0.5f, Height * 0.5f));
	if (Door)
	{
		// Hinge on the -Y jamb at floor level, in the middle of the wall depth.
		Door->ClosedRotation = GetActorRotation();
		Door->SetActorLocation(GetActorTransform().TransformPosition(FVector(-2.5f, -Width * 0.5f + 2.f, -Height * 0.5f)));
	}
	else OpenTarget = 1.f;
	CurrentOpen = OpenTarget;
	ApplyLeaf();
}

void AAcousticPortal::BeginPlay()
{
	Super::BeginPlay();
	bHasDoor = (Door != nullptr);
	CurrentOpen = bHasDoor ? OpenTarget : 1.f;
	ApplyLeaf();
	// Resolve the two rooms by probing 60 cm to each side of the opening.
	if (!RoomA || !RoomB)
	{
		const FVector C = GetCenter(), N = GetNormal();
		const FVector PA = C - N * 60.f, PB = C + N * 60.f;
		for (TActorIterator<AAcousticRoom> It(GetWorld()); It; ++It)
		{
			if (!RoomA && It->Contains(PA)) RoomA = *It;
			if (!RoomB && It->Contains(PB)) RoomB = *It;
		}
	}
}

void AAcousticPortal::ToggleDoor() { if (bHasDoor) OpenTarget = OpenTarget > 0.5f ? 0.f : 1.f; }
void AAcousticPortal::SetOpen(bool bOpen) { if (bHasDoor) OpenTarget = bOpen ? 1.f : 0.f; }

float AAcousticPortal::GetOpenness() const
{
	if (!bHasDoor) return 1.f;
	// Swing angle 0..100 deg; acoustically fully open from about 70 deg.
	const float Angle = CurrentOpen * 100.f;
	return FMath::Clamp(FMath::Sin(FMath::DegreesToRadians(FMath::Min(Angle, 70.f) * (90.f / 70.f))), 0.f, 1.f);
}

void AAcousticPortal::ApplyLeaf()
{
	if (Door) Door->SetSwing(CurrentOpen * 100.f);
}

void AAcousticPortal::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bHasDoor) return;
	const float Prev = CurrentOpen;
	CurrentOpen = FMath::FInterpConstantTo(CurrentOpen, OpenTarget, DeltaSeconds, 1.25f);
	if (Prev != CurrentOpen) ApplyLeaf();
}

FVector AAcousticPortal::GetCenter() const { return Opening->GetComponentLocation(); }
FVector AAcousticPortal::GetNormal() const { return Opening->GetForwardVector(); }
FVector AAcousticPortal::GetRight() const { return Opening->GetRightVector(); }

void AAcousticPortal::GetEdgePoints(FVector& OutA, FVector& OutB, float Inset) const
{
	const FVector C = GetCenter();
	const FVector R = GetRight();
	const float Half = FMath::Max(Width * 0.5f - Inset, 5.f);
	const float Ear = 160.f - Height * 0.5f;
	OutA = C - R * Half + FVector(0, 0, Ear);
	OutB = C + R * Half + FVector(0, 0, Ear);
}

FVector AAcousticPortal::ClosestPointOnOpening(const FVector& P) const
{
	const FTransform T = Opening->GetComponentTransform();
	FVector L = T.InverseTransformPosition(P);
	L.X = 0.f;
	L.Y = FMath::Clamp(L.Y, -Width * 0.5f + 10.f, Width * 0.5f - 10.f);
	L.Z = FMath::Clamp(L.Z, -Height * 0.5f + 10.f, Height * 0.5f - 10.f);
	return T.TransformPosition(L);
}
