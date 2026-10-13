#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "OcclusionBuildLibrary.generated.h"

/** Editor-side level construction, called from Tools/build_level.py. */
UCLASS()
class AUDIOOCCLUSION_API UOcclusionBuildLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Spawns the whole sound house (geometry, rooms, portals, doors, emitters, lights, NPC, wave bake actor) into World. */
	UFUNCTION(BlueprintCallable, Category = "Occlusion")
	static int32 BuildSoundHouse(UObject* WorldContext);

	/** Exports Steam Audio static geometry and door dynamic objects for the current level (editor only). */
	UFUNCTION(BlueprintCallable, Category = "Occlusion")
	static bool ExportSteamAudio(UObject* WorldContext, const FString& StaticAssetPath, const FString& DoorAssetFolder);
};
