#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "OcclusionCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class USceneComponent;
class USteamAudioListenerComponent;

UENUM(BlueprintType)
enum class EViewMode : uint8 { FirstPerson, TopDown };

/** Player: first-person or top-down camera. The audio listener always sits at the character's ears. */
UCLASS()
class AUDIOOCCLUSION_API AOcclusionCharacter : public ACharacter
{
	GENERATED_BODY()
public:
	AOcclusionCharacter();

	UPROPERTY(VisibleAnywhere) TObjectPtr<USpringArmComponent> Boom;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> TopCamera;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> FPCamera;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Ears;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USteamAudioListenerComponent> SteamListener;

	EViewMode View = EViewMode::TopDown;
	float TopYaw = -45.f;          // camera yaw in top-down, 45-degree steps
	float TopYawTarget = -45.f;
	float TopPitch = -55.f;
	float TopZoom = 1900.f;
	float TopZoomTarget = 1900.f;

	void SetView(EViewMode V);
	void ToggleView() { SetView(View == EViewMode::FirstPerson ? EViewMode::TopDown : EViewMode::FirstPerson); }
	void RotateTop(int32 Dir) { TopYawTarget += 45.f * Dir; }
	void Zoom(float Dir) { TopZoomTarget = FMath::Clamp(TopZoomTarget - Dir * 300.f, 900.f, 4600.f); }
	FRotator GetMoveBasis() const;
	FVector GetEarLocation() const;

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PossessedBy(AController* NewController) override;
};
