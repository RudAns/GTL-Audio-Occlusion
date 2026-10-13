#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AcousticRoom.generated.h"

class UBoxComponent;
class USoundSubmix;

/** An acoustic space: a box volume with a reverb character. Rooms are the nodes of the room-portal graph. */
UCLASS()
class AUDIOOCCLUSION_API AAcousticRoom : public AActor
{
	GENERATED_BODY()
public:
	AAcousticRoom();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	TObjectPtr<UBoxComponent> Bounds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	FString DisplayName = TEXT("房间");

	/** Late reverb decay (s), used for the room's reverb submix and the HUD. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	float DecayTime = 0.8f;

	/** Reverb send level of sources inside this room, in dB. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	float WetLevelDb = -10.f;

	/** Outdoor spaces have no reverb submix of their own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	bool bOutdoor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	int32 Priority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	FLinearColor Tint = FLinearColor(0.2f, 0.6f, 0.6f);

	bool Contains(const FVector& P) const;
	FBox GetBox() const;

	/** Runtime: this room's reverb submix (created by the subsystem). */
	UPROPERTY(Transient)
	TObjectPtr<USoundSubmix> ReverbSubmix;
};
