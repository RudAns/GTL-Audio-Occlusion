#include "OcclusionPlayerController.h"
#include "OcclusionCharacter.h"
#include "OcclusionHUDWidget.h"
#include "AcousticSubsystem.h"
#include "AcousticPortal.h"
#include "AcousticEmitterComponent.h"
#include "WaveBakeActor.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputModifiers.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/KismetSystemLibrary.h"

UInputAction* AOcclusionPlayerController::MakeAction(const TCHAR* Name, EInputActionValueType Type)
{
	UInputAction* A = NewObject<UInputAction>(this, Name);
	A->ValueType = Type;
	return A;
}

void AOcclusionPlayerController::BindKey(const FKey& Key, TFunction<void()> Fn)
{
	UInputAction* A = MakeAction(*FString::Printf(TEXT("IA_%s"), *Key.GetFName().ToString()), EInputActionValueType::Boolean);
	Context->MapKey(A, Key);
	KeyActions.Add(Key, A);
	KeyFns.Add(Key, MoveTemp(Fn));
}

AOcclusionCharacter* AOcclusionPlayerController::Char() const { return Cast<AOcclusionCharacter>(GetPawn()); }

void AOcclusionPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	Context = NewObject<UInputMappingContext>(this, TEXT("IMC_Occlusion"));
	MoveAction = MakeAction(TEXT("IA_Move"), EInputActionValueType::Axis2D);
	LookAction = MakeAction(TEXT("IA_Look"), EInputActionValueType::Axis2D);
	ZoomAction = MakeAction(TEXT("IA_Zoom"), EInputActionValueType::Axis1D);

	auto MapMove = [this](const FKey& K, bool bSwizzle, bool bNegate)
	{
		FEnhancedActionKeyMapping& M = Context->MapKey(MoveAction, K);
		if (bSwizzle) { UInputModifierSwizzleAxis* S = NewObject<UInputModifierSwizzleAxis>(this); S->Order = EInputAxisSwizzle::YXZ; M.Modifiers.Add(S); }
		if (bNegate) { M.Modifiers.Add(NewObject<UInputModifierNegate>(this)); }
	};
	MapMove(EKeys::W, true, false); MapMove(EKeys::S, true, true);
	MapMove(EKeys::D, false, false); MapMove(EKeys::A, false, true);
	MapMove(EKeys::Up, true, false); MapMove(EKeys::Down, true, true);
	MapMove(EKeys::Right, false, false); MapMove(EKeys::Left, false, true);
	{
		FEnhancedActionKeyMapping& M = Context->MapKey(LookAction, EKeys::Mouse2D);
		UInputModifierNegate* N = NewObject<UInputModifierNegate>(this); N->bX = false; N->bY = true; N->bZ = false;
		M.Modifiers.Add(N);
	}
	Context->MapKey(ZoomAction, EKeys::MouseWheelAxis);
	AltAction = MakeAction(TEXT("IA_Cursor"), EInputActionValueType::Boolean);
	Context->MapKey(AltAction, EKeys::LeftAlt);

	UAcousticSubsystem* Sys = GetWorld()->GetSubsystem<UAcousticSubsystem>();
	const FKey StepKeys[10] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine, EKeys::Zero };
	for (int32 i = 0; i < 10; ++i)
	{
		BindKey(StepKeys[i], [this, Sys, i]() { Sys->SetStep((EOcclusionStep)i); Toast(FString::Printf(TEXT("第 %d 步 · %s"), i + 1, *UAcousticSubsystem::StepTitle((EOcclusionStep)i))); });
	}
	BindKey(EKeys::V, [this]() { if (Char()) { Char()->ToggleView(); ApplyInputMode(); Toast(Char()->View == EViewMode::FirstPerson ? TEXT("第一人称视角") : TEXT("俯视视角")); } });
	BindKey(EKeys::Q, [this]() { if (Char() && Char()->View == EViewMode::TopDown) Char()->RotateTop(-1); });
	BindKey(EKeys::E, [this]() { if (Char() && Char()->View == EViewMode::TopDown) Char()->RotateTop(1); });
	BindKey(EKeys::F, [this, Sys]()
	{
		if (!Char()) return;
		if (AAcousticPortal* D = Sys->NearestDoor(Char()->GetActorLocation(), 260.f))
		{
			D->ToggleDoor();
			Toast(FString::Printf(TEXT("%s · %s"), *D->DisplayName, D->OpenTarget > 0.5f ? TEXT("开门") : TEXT("关门")));
		}
	});
	BindKey(EKeys::G, [this, Sys]()
	{
		const int32 N = Sys->GetGroups().Num();
		const int32 Next = Sys->GetGroup() + 1;
		if (Next >= N) { Sys->SetStep(Sys->GetStep()); Toast(TEXT("退出对照，回到分步模式")); }
		else { Sys->SetGroup(Next); Toast(FString::Printf(TEXT("对照组 %s · 当前 A"), *Sys->GetGroups()[Next].Title)); }
	});
	BindKey(EKeys::B, [this, Sys]()
	{
		if (Sys->GetGroup() < 0) { Toast(TEXT("先按 G 进入对照组")); return; }
		Sys->CycleVariant();
		const FComparisonGroup& G = Sys->GetGroups()[Sys->GetGroup()];
		Toast(FString::Printf(TEXT("切换到 %c · %s"), TCHAR('A' + Sys->GetVariant()), *G.Labels[Sys->GetVariant()]));
	});
	BindKey(EKeys::Tab, [this, Sys]() { Sys->CycleFocus(1); if (UAcousticEmitterComponent* F = Sys->GetFocused()) Toast(FString::Printf(TEXT("观察声源：%s"), *F->DisplayName)); });
	BindKey(EKeys::R, [this, Sys]() { Sys->ToggleSolo(); if (UAcousticEmitterComponent* F = Sys->GetFocused()) Toast(F->bSolo ? FString::Printf(TEXT("只听：%s"), *F->DisplayName) : FString(TEXT("恢复全部声源"))); });
	BindKey(EKeys::H, [this, Sys]() { Sys->bDebugDraw = !Sys->bDebugDraw; Toast(Sys->bDebugDraw ? TEXT("显示射线与路径") : TEXT("隐藏射线与路径")); });
	BindKey(EKeys::K, [this, Sys]()
	{
		if (!Sys->WaveBake) return;
		if (Sys->WaveBake->IsBaking()) { Toast(TEXT("正在烘焙中，请稍候")); return; }
		Sys->WaveBake->StartBake(); Toast(TEXT("开始重新烘焙：约 1~3 分钟，期间按自研管线播放"));
	});
	BindKey(EKeys::P, [this, Sys]() { if (Sys->WaveBake) { Sys->WaveBake->ToggleProbes(); } });
	BindKey(EKeys::L, [this, Sys]() { if (Sys->WaveBake) { Toast(Sys->WaveBake->CycleSliceView()); } });
	BindKey(EKeys::O, [this, Sys]() { if (Sys->WaveBake) { Toast(Sys->WaveBake->ToggleReplay()); } });
	BindKey(EKeys::F1, [this]() { bHelp = !bHelp; });
	BindKey(EKeys::F3, [this]() { bHideUI = !bHideUI; });
	BindKey(EKeys::Escape, [this]() { UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false); });

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AOcclusionPlayerController::OnMove);
		EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &AOcclusionPlayerController::OnLook);
		EIC->BindAction(ZoomAction, ETriggerEvent::Triggered, this, &AOcclusionPlayerController::OnZoom);
		EIC->BindActionValueLambda(AltAction, ETriggerEvent::Started, [this](const FInputActionValue&) { bAltCursor = true; ApplyInputMode(); });
		EIC->BindActionValueLambda(AltAction, ETriggerEvent::Completed, [this](const FInputActionValue&) { bAltCursor = false; ApplyInputMode(); });
		for (auto& KV : KeyActions)
		{
			const FKey K = KV.Key;
			EIC->BindActionValueLambda(KV.Value, ETriggerEvent::Started, [this, K](const FInputActionValue&) { if (TFunction<void()>* F = KeyFns.Find(K)) (*F)(); });
		}
	}
}

void AOcclusionPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (ULocalPlayer* LP = GetLocalPlayer())
		if (UEnhancedInputLocalPlayerSubsystem* S = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			S->AddMappingContext(Context, 0);
	HUD = CreateWidget<UOcclusionHUDWidget>(this, UOcclusionHUDWidget::StaticClass());
	if (HUD) HUD->AddToViewport(0);
	ApplyInputMode();
}

void AOcclusionPlayerController::ApplyInputMode()
{
	// Top-down: the cursor is always there for the control panel. First person: mouse look, Alt brings the cursor back.
	const bool bCursor = bAltCursor || !Char() || Char()->View == EViewMode::TopDown;
	if (bCursor)
	{
		FInputModeGameAndUI M;
		M.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		M.SetHideCursorDuringCapture(false);
		SetInputMode(M);
	}
	else
	{
		FInputModeGameOnly M;
		SetInputMode(M);
	}
	bShowMouseCursor = bCursor;
	bCursorMode = bCursor;
}

void AOcclusionPlayerController::OnMove(const FInputActionValue& V)
{
	AOcclusionCharacter* C = Char(); if (!C) return;
	const FVector2D In = V.Get<FVector2D>();
	const FRotator Basis = C->GetMoveBasis();
	C->AddMovementInput(FRotationMatrix(Basis).GetUnitAxis(EAxis::X), In.Y);
	C->AddMovementInput(FRotationMatrix(Basis).GetUnitAxis(EAxis::Y), In.X);
}

void AOcclusionPlayerController::OnLook(const FInputActionValue& V)
{
	AOcclusionCharacter* C = Char(); if (!C || C->View != EViewMode::FirstPerson || bAltCursor) return;
	const FVector2D In = V.Get<FVector2D>();
	AddYawInput(In.X);
	AddPitchInput(In.Y);
}

void AOcclusionPlayerController::OnZoom(const FInputActionValue& V)
{
	if (AOcclusionCharacter* C = Char()) C->Zoom(V.Get<float>());
}

void AOcclusionPlayerController::Toast(const FString& Msg)
{
	if (HUD) HUD->ShowToast(Msg);
}

void AOcclusionPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	// Views can also change from code (self-test, possession); keep the cursor mode in sync.
	if (bCursorMode != (bAltCursor || !Char() || Char()->View == EViewMode::TopDown)) ApplyInputMode();
	if (HUD) HUD->Refresh(this);
}
