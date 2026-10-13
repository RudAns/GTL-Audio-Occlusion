#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OcclusionTypes.h"
#include "AcousticPortal.generated.h"

class UBoxComponent;
class AAcousticRoom;

/** An opening between two rooms (doorway, archway). Local X is the portal normal, Y the width, Z the height. */
UCLASS()
class AUDIOOCCLUSION_API AAcousticPortal : public AActor
{
	GENERATED_BODY()
public:
	AAcousticPortal();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	TObjectPtr<UBoxComponent> Opening;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	TObjectPtr<class AAcousticDoor> Door;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	FString DisplayName = TEXT("门");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	bool bHasDoor = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	float Width = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	float Height = 220.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	float Depth = 30.f;

	/** 0 = closed, 1 = fully open (only meaningful with a door). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	float OpenTarget = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	TObjectPtr<AAcousticRoom> RoomA;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	TObjectPtr<AAcousticRoom> RoomB;

	/** Configure size and door leaf (called by the level builder). */
	void Setup(float InWidth, float InHeight, float InDepth, class AAcousticDoor* InDoor);

	void ToggleDoor();
	void SetOpen(bool bOpen);

	/** Acoustic openness 0..1 derived from the current swing angle. */
	float GetOpenness() const;
	bool IsOpenEnough() const { return GetOpenness() > 0.02f; }
	FVector GetCenter() const;
	FVector GetNormal() const;
	FVector GetRight() const;
	/** The two jamb edges at ear height, used by the edge-diffraction search. */
	void GetEdgePoints(FVector& OutA, FVector& OutB, float Inset = 15.f) const;
	/** Closest point of the opening to P (clamped to the opening rectangle). */
	FVector ClosestPointOnOpening(const FVector& P) const;

	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;

protected:
	float CurrentOpen = 1.f;
	void ApplyLeaf();
};
