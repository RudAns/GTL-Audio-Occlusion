#include "AcousticDoor.h"
#include "Components/StaticMeshComponent.h"

AAcousticDoor::AAcousticDoor()
{
	PrimaryActorTick.bCanEverTick = false;
	UStaticMeshComponent* Leaf = GetStaticMeshComponent();
	Leaf->SetMobility(EComponentMobility::Movable);
	Leaf->SetCollisionProfileName(TEXT("BlockAll"));
	Leaf->ComponentTags.Add(TEXT("Acoustic"));
	Leaf->ComponentTags.Add(TEXT("TL_Door"));
	Handle = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Handle"));
	Handle->SetupAttachment(Leaf);
	Handle->SetMobility(EComponentMobility::Movable);
	Handle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Handle->SetUsingAbsoluteScale(true);
}

void AAcousticDoor::Setup(float Width, float Height, UStaticMesh* Cube, UMaterialInterface* LeafMat, UMaterialInterface* HandleMat)
{
	// Cube = 1 m LevelPrototyping cube with its pivot at a corner, so the actor origin is the hinge.
	UStaticMeshComponent* Leaf = GetStaticMeshComponent();
	Leaf->SetStaticMesh(Cube);
	Leaf->bUseDefaultCollision = false;
	Leaf->SetCollisionProfileName(TEXT("BlockAll"));
	const FVector Scale(0.05f, (Width - 4.f) / 100.f, (Height - 2.f) / 100.f);
	SetActorScale3D(Scale);
	if (LeafMat) Leaf->SetMaterial(0, LeafMat);
	Handle->SetStaticMesh(Cube);
	Handle->SetWorldScale3D(FVector(0.13f, 0.16f, 0.035f));
	// Relative location is expressed in the parent's scaled space.
	Handle->SetRelativeLocation(FVector(-4.f, Width - 28.f, 100.f) / Scale);
	if (HandleMat) Handle->SetMaterial(0, HandleMat);
}

void AAcousticDoor::SetSwing(float Degrees)
{
	SetActorRotation(ClosedRotation.Quaternion() * FRotator(0.f, -Degrees, 0.f).Quaternion());
}
