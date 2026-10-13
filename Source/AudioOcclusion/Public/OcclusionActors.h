#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "OcclusionActors.generated.h"

class UAcousticEmitterComponent;
class USteamAudioSourceComponent;

/** A placed sound source prop: visual meshes (added by the builder) + emitter + Steam Audio source. */
UCLASS()
class AUDIOOCCLUSION_API AAcousticEmitterActor : public AActor
{
	GENERATED_BODY()
public:
	AAcousticEmitterActor();
	UPROPERTY(VisibleAnywhere) TObjectPtr<UAcousticEmitterComponent> Emitter;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USteamAudioSourceComponent> SteamSource;
};

/** A mannequin that walks a loop of waypoints with a footstep loop attached. */
UCLASS()
class AUDIOOCCLUSION_API APatrolNPC : public ACharacter
{
	GENERATED_BODY()
public:
	APatrolNPC();
	UPROPERTY(EditAnywhere) TArray<FVector> Waypoints;
	UPROPERTY(EditAnywhere) float PauseSeconds = 1.2f;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UAcousticEmitterComponent> Emitter;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USteamAudioSourceComponent> SteamSource;
	/** Starting foot-bone heights above the floor (cm) that arm a foot / count as a plant; refined from the observed walk cycle. */
	float FootLiftCm = 11.f;
	float FootPlantCm = 7.f;
	int32 StepCount = 0;
	float FootMin[2] = { 1e9f, 1e9f };
	float FootMax[2] = { -1e9f, -1e9f };
	bool bFootArmed[2] = { false, false };
	virtual void Tick(float DeltaSeconds) override;
protected:
	int32 Target = 0;
	float Pause = 0.f;
};

UCLASS()
class AUDIOOCCLUSION_API AOcclusionGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	AOcclusionGameMode();
	virtual void StartPlay() override;
};
