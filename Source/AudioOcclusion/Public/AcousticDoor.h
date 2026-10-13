#pragma once

#include "CoreMinimal.h"
#include "Engine/StaticMeshActor.h"
#include "AcousticDoor.generated.h"

/** A swinging door leaf. It is a static mesh actor whose pivot is the hinge, so rotating the actor swings the door;
 *  Steam Audio exports it as a dynamic object and tracks it through the root transform. */
UCLASS()
class AUDIOOCCLUSION_API AAcousticDoor : public AStaticMeshActor
{
	GENERATED_BODY()
public:
	AAcousticDoor();
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Handle;
	UPROPERTY(EditAnywhere) FRotator ClosedRotation = FRotator::ZeroRotator;

	void Setup(float Width, float Height, UStaticMesh* Cube, UMaterialInterface* LeafMat, UMaterialInterface* HandleMat);
	void SetSwing(float Degrees);
};
