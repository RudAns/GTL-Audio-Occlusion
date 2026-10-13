#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CaptureDirector.generated.h"

class FCaptureTap;

/** Scripted self-test: walks through views, steps, doors, the wave bake and Steam Audio, saving screenshots,
 *  solver read-outs and short output recordings. Enabled with -OccCapture=<dir> [-OccOnly=a,b]. */
UCLASS()
class AUDIOOCCLUSION_API ACaptureDirector : public AActor
{
	GENERATED_BODY()
public:
	ACaptureDirector();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	FString OutDir;
	TArray<FString> Only;

private:
	struct FShot
	{
		FString Name;
		TFunction<void()> Setup;
		float Wait = 1.5f;
		float RecordSec = 0.f;          // > 0: record the output mix for this long before the screenshot
		TFunction<bool()> WaitUntil;    // optional extra condition
		float Timeout = 240.f;
	};
	TArray<FShot> Shots;
	TSharedPtr<FCaptureTap, ESPMode::ThreadSafe> Tap;
	int32 Index = -1;
	float Timer = 0.f;
	float Elapsed = 0.f;
	int32 Phase = 0;
	void Build();
	void LogState(const FString& Tag);
	void Place(const FVector& Pos, float Yaw, bool bFirstPerson, float TopYaw = -45.f, float Zoom = 1900.f, float Pitch = 0.f);
	void Door(const FString& Name, bool bOpen);
	void Focus(const FString& Name, bool bSolo);
	/** Sends a real key event through the player controller (Enhanced Input path); released before the next shot. */
	void Press(const FKey& K);
	void LogInput(const FString& Tag);
	TArray<FKey> Held;
	class UAcousticSubsystem* Sys() const;
};
