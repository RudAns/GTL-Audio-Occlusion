#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "OcclusionPlayerController.generated.h"

class UInputMappingContext;
class UInputAction;
class UOcclusionHUDWidget;
class AOcclusionCharacter;

UCLASS()
class AUDIOOCCLUSION_API AOcclusionPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

	UPROPERTY(Transient) TObjectPtr<UOcclusionHUDWidget> HUD;
	void Toast(const FString& Msg);
	bool bHelp = false;
	bool bHideUI = false;
	/** First person: hold Alt to get the mouse cursor for the control panel. */
	bool bAltCursor = false;
	bool bCursorMode = false;
	void ApplyInputMode();

protected:
	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> Context;
	UPROPERTY(Transient) TObjectPtr<UInputAction> MoveAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> LookAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ZoomAction;
	UPROPERTY(Transient) TMap<FKey, TObjectPtr<UInputAction>> KeyActions;

	UInputAction* MakeAction(const TCHAR* Name, EInputActionValueType Type);
	void BindKey(const FKey& Key, TFunction<void()> Fn);
	TMap<FKey, TFunction<void()>> KeyFns;

	void OnMove(const FInputActionValue& V);
	void OnLook(const FInputActionValue& V);
	void OnZoom(const FInputActionValue& V);
	AOcclusionCharacter* Char() const;
	UPROPERTY(Transient) TObjectPtr<UInputAction> AltAction;
};
