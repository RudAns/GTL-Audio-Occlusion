#include "AcousticRoom.h"
#include "Components/BoxComponent.h"

AAcousticRoom::AAcousticRoom()
{
	PrimaryActorTick.bCanEverTick = false;
	Bounds = CreateDefaultSubobject<UBoxComponent>(TEXT("Bounds"));
	Bounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Bounds->SetBoxExtent(FVector(200.f));
	Bounds->ShapeColor = FColor(60, 200, 200);
	Bounds->SetCanEverAffectNavigation(false);
	RootComponent = Bounds;
}

FBox AAcousticRoom::GetBox() const
{
	const FVector C = Bounds->GetComponentLocation();
	const FVector E = Bounds->GetScaledBoxExtent();
	return FBox(C - E, C + E);
}

bool AAcousticRoom::Contains(const FVector& P) const
{
	return GetBox().IsInsideOrOn(P);
}
