#include "CaptureDirector.h"
#include "AcousticSubsystem.h"
#include "AcousticEmitterComponent.h"
#include "AcousticPortal.h"
#include "AcousticRoom.h"
#include "OcclusionCharacter.h"
#include "OcclusionPlayerController.h"
#include "OcclusionHUDWidget.h"
#include "WaveBakeActor.h"
#include "OcclusionActors.h"
#include "SteamAudioSourceComponent.h"
#include "AudioMixerBlueprintLibrary.h"
#include "EngineUtils.h"
#include "HighResScreenshot.h"
#include "UnrealClient.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "InputKeyEventArgs.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogCapture, Log, All);

#include "ISubmixBufferListener.h"
#include "Sound/SoundSubmix.h"

/** Taps the main output submix so the self-test can write its own WAV and count invalid samples. */
class FCaptureTap : public ISubmixBufferListener
{
public:
	FCriticalSection Lock;
	TArray<float> Data;
	int32 Channels = 2;
	int32 Rate = 48000;
	int64 Buffers = 0;
	bool bRec = false;
	virtual void OnNewSubmixBuffer(const USoundSubmix*, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double) override
	{
		FScopeLock L(&Lock);
		++Buffers;
		Channels = NumChannels; Rate = SampleRate;
		if (bRec) Data.Append(AudioData, NumSamples);
	}
	virtual bool IsRenderingAudio() const override { return false; }
	virtual const FString& GetListenerName() const override { static FString N = TEXT("OcclusionCaptureTap"); return N; }

	FString Finish(const FString& Path)
	{
		TArray<float> D; int32 C, R;
		{ FScopeLock L(&Lock); bRec = false; D = MoveTemp(Data); Data.Reset(); C = Channels; R = Rate; }
		int32 NaNs = 0; double Sum = 0; float Peak = 0;
		TArray<int16> Pcm; Pcm.SetNumUninitialized(D.Num());
		for (int32 i = 0; i < D.Num(); ++i)
		{
			float X = D[i];
			if (!FMath::IsFinite(X)) { ++NaNs; X = 0.f; }
			Peak = FMath::Max(Peak, FMath::Abs(X)); Sum += double(X) * X;
			Pcm[i] = int16(FMath::Clamp(X, -1.f, 1.f) * 32767.f);
		}
		const uint32 Bytes = Pcm.Num() * 2;
		TArray<uint8> W;
		auto U32 = [&W](uint32 V) { W.Append((uint8*)&V, 4); };
		auto U16 = [&W](uint16 V) { W.Append((uint8*)&V, 2); };
		W.Append((const uint8*)"RIFF", 4); U32(36 + Bytes); W.Append((const uint8*)"WAVEfmt ", 8);
		U32(16); U16(1); U16(uint16(C)); U32(uint32(R)); U32(uint32(R * C * 2)); U16(uint16(C * 2)); U16(16);
		W.Append((const uint8*)"data", 4); U32(Bytes); W.Append((uint8*)Pcm.GetData(), Bytes);
		FFileHelper::SaveArrayToFile(W, *Path);
		const float Rms = D.Num() ? FMath::Sqrt(float(Sum / D.Num())) : 0.f;
		return FString::Printf(TEXT("  audio: %d samples (%d ch, %d Hz), rms %.1f dBFS, peak %.1f dBFS, non-finite %d, buffers so far %lld\n"),
			D.Num(), C, R, 20.f * FMath::LogX(10.f, FMath::Max(Rms, 1e-6f)), 20.f * FMath::LogX(10.f, FMath::Max(Peak, 1e-6f)), NaNs, Buffers);
	}
};

ACaptureDirector::ACaptureDirector() { PrimaryActorTick.bCanEverTick = true; }

UAcousticSubsystem* ACaptureDirector::Sys() const { return GetWorld()->GetSubsystem<UAcousticSubsystem>(); }

void ACaptureDirector::Place(const FVector& Pos, float Yaw, bool bFP, float TopYaw, float Zoom, float Pitch)
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	AOcclusionCharacter* C = PC ? Cast<AOcclusionCharacter>(PC->GetPawn()) : nullptr;
	if (!C) return;
	C->SetActorLocation(Pos, false, nullptr, ETeleportType::TeleportPhysics);
	C->SetActorRotation(FRotator(0, Yaw, 0));
	C->TopYaw = C->TopYawTarget = TopYaw;
	C->TopZoom = C->TopZoomTarget = Zoom;
	C->SetView(bFP ? EViewMode::FirstPerson : EViewMode::TopDown);
	PC->SetControlRotation(FRotator(Pitch, Yaw, 0));
}

void ACaptureDirector::Door(const FString& Name, bool bOpen)
{
	for (TActorIterator<AAcousticPortal> It(GetWorld()); It; ++It) if (It->DisplayName == Name) It->SetOpen(bOpen);
}

void ACaptureDirector::Focus(const FString& Name, bool bSolo)
{
	UAcousticSubsystem* S = Sys();
	TArray<UAcousticEmitterComponent*> Es = S->GetEmitters();
	for (int32 i = 0; i < Es.Num(); ++i)
	{
		Es[i]->bSolo = false;
		if (Es[i]->DisplayName.Contains(Name)) { S->FocusEmitter = i; Es[i]->bSolo = bSolo; }
	}
}

void ACaptureDirector::BeginPlay()
{
	Super::BeginPlay();
	IFileManager::Get().MakeDirectory(*OutDir, true);
	FApp::SetUnfocusedVolumeMultiplier(1.f);   // the off-screen capture window never has focus
	Build();
}

void ACaptureDirector::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Tap.IsValid())
		if (FAudioDevice* D = GetWorld()->GetAudioDeviceRaw()) D->UnregisterSubmixBufferListener(Tap.ToSharedRef(), D->GetMainSubmixObject());
	Super::EndPlay(Reason);
}

void ACaptureDirector::Press(const FKey& K)
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	FViewport* VP = GEngine->GameViewport ? GEngine->GameViewport->Viewport : nullptr;
	if (!PC) return;
	PC->InputKey(FInputKeyEventArgs(VP, IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(), K, IE_Pressed, FPlatformTime::Cycles64()));
	Held.Add(K);
}

void ACaptureDirector::LogInput(const FString& Tag)
{
	UAcousticSubsystem* S = Sys();
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	AOcclusionCharacter* C = PC ? Cast<AOcclusionCharacter>(PC->GetPawn()) : nullptr;
	FString Doors;
	for (TActorIterator<AAcousticPortal> It(GetWorld()); It; ++It) Doors += FString::Printf(TEXT("%s=%.2f "), *It->DisplayName, It->GetOpenness());
	const UAcousticEmitterComponent* F = S->GetFocused();
	const AOcclusionPlayerController* HPC = Cast<AOcclusionPlayerController>(PC);
	const FString Out = FString::Printf(TEXT("== %s | pawn %s | view %s | topYaw %.0f | ctrlYaw %.0f | step %s | group %d variant %d | focus %s solo %d | debug %d | help %d hideUI %d | cursor %d | layers %s | %s\n"), *Tag,
		C ? *C->GetActorLocation().ToCompactString() : TEXT("-"), C ? (C->View == EViewMode::FirstPerson ? TEXT("FP") : TEXT("Top")) : TEXT("-"),
		C ? C->TopYawTarget : 0.f, PC ? PC->GetControlRotation().Yaw : 0.f, *UAcousticSubsystem::StepTitle(S->GetStep()), S->GetGroup(), S->GetVariant(),
		F ? *F->DisplayName : TEXT("-"), F ? int32(F->bSolo) : 0, int32(S->bDebugDraw), HPC ? int32(HPC->bHelp) : -1, HPC ? int32(HPC->bHideUI) : -1, PC ? int32(PC->bShowMouseCursor) : -1, *FString::Join(S->ActiveLayerNames(), TEXT("+")), *Doors);
	FFileHelper::SaveStringToFile(Out, *(OutDir / TEXT("readout.txt")), FFileHelper::EEncodingOptions::ForceUTF8, &IFileManager::Get(), FILEWRITE_Append);
}

void ACaptureDirector::Build()
{
	auto Step = [this](EOcclusionStep S) { Sys()->SetStep(S); };
	const FVector HallSpot(300, 300, 100);
	// 01-02: top-down overview and first person, full custom pipeline
	Shots.Add({ TEXT("01_overview_topdown"), [=, this]() { Step(EOcclusionStep::Smoothing); Place(FVector(420, 700, 100), 0, false, -50.f, 3000.f); Focus(TEXT("收音机"), false); }, 2.5f });
	Shots.Add({ TEXT("02_firstperson_hall"), [=, this]() { Place(FVector(250, 900, 100), -20, true, -45.f, 1900.f, -4.f); }, 2.f });
	// 03-05: pillar obstruction with the test speaker: none / native / multi-ray
	Shots.Add({ TEXT("03_step1_none_pillar"), [=, this]() { Step(EOcclusionStep::None); Place(FVector(330, 690, 100), 0, false, 0.f, 2300.f); Focus(TEXT("测试音箱"), true); }, 1.5f, 2.5f });
	Shots.Add({ TEXT("04_step2_native_pillar"), [=, this]() { Step(EOcclusionStep::UENative); }, 1.5f, 2.5f });
	Shots.Add({ TEXT("05_step3_multiray_pillar"), [=, this]() { Step(EOcclusionStep::MultiRay); }, 1.5f, 2.5f });
	// 06-09: radio in the music room heard from the hall: portal path, excess mapping, closed door + transmission, reverb
	Shots.Add({ TEXT("06_step4_portal_radio"), [=, this]() { Step(EOcclusionStep::PortalPath); Door(TEXT("琴房门"), true); Place(HallSpot, 0, false, -45.f, 2600.f); Focus(TEXT("收音机"), true); }, 2.f, 2.5f });
	Shots.Add({ TEXT("07_step5_excess_radio"), [=, this]() { Step(EOcclusionStep::ExcessMapping); }, 1.5f, 2.5f });
	Shots.Add({ TEXT("08_step6_door_closed_transmission"), [=, this]() { Step(EOcclusionStep::Transmission); Door(TEXT("琴房门"), false); }, 2.5f, 2.5f });
	Shots.Add({ TEXT("09_step8_full_door_open"), [=, this]() { Step(EOcclusionStep::Smoothing); Door(TEXT("琴房门"), true); }, 3.f, 2.5f });
	// 10: wood partition: machine heard from the music room
	Shots.Add({ TEXT("10_wood_partition_machine"), [=, this]() { Step(EOcclusionStep::Smoothing); Door(TEXT("工坊门"), false); Place(FVector(2200, 600, 100), 90, false, -45.f, 2000.f); Focus(TEXT("机床"), true); }, 2.f, 2.5f });
	// 11: first person in the corridor looking at the music room door
	Shots.Add({ TEXT("11_firstperson_corridor"), [=, this]() { Place(FVector(1525, 1200, 100), -90, true, -45.f, 1900.f, -2.f); Focus(TEXT("收音机"), false); }, 2.f });
	// 12: comparison group 3, variant B
	Shots.Add({ TEXT("12_group3_B"), [=, this]() { Sys()->SetGroup(2); Sys()->CycleVariant(); Place(HallSpot, 0, false, -45.f, 2600.f); Focus(TEXT("收音机"), false); }, 2.f });
	// 13-17: GPU wave bake, then visualisations
	Shots.Add({ TEXT("13_wavebake_running"), [=, this]() { Step(EOcclusionStep::WaveBake); if (Sys()->WaveBake) Sys()->WaveBake->StartBake(); Place(FVector(600, 700, 100), 0, false, -45.f, 3200.f); }, 6.f });
	{
		FShot S; S.Name = TEXT("14_wavebake_done_lds"); S.Setup = [=, this]() {};
		S.WaitUntil = [this]() { return Sys()->WaveBake && !Sys()->WaveBake->IsBaking(); };
		S.Wait = 1.f; S.Timeout = 600.f;
		Shots.Add(S);
	}
	Shots.Add({ TEXT("15_slice_lds"), [=, this]() { Place(FVector(450, 650, 100), 0, false, -45.f, 4400.f); if (AWaveBakeActor* W = Sys()->WaveBake) { for (int32 n = 0; n < 8 && W->GetSliceView() != EWaveSliceView::Lds; ++n) W->CycleSliceView(); W->ToggleProbes(); } Focus(TEXT("收音机"), true); }, 2.f, 2.5f });
	Shots.Add({ TEXT("15b_slice_arrival"), [=, this]() { if (AWaveBakeActor* W = Sys()->WaveBake) W->SetSliceView(EWaveSliceView::Arrival); }, 1.5f });
	Shots.Add({ TEXT("16_slice_ratio"), [=, this]() { if (AWaveBakeActor* W = Sys()->WaveBake) { for (int32 n = 0; n < 8 && W->GetSliceView() != EWaveSliceView::Ratio; ++n) W->CycleSliceView(); } }, 1.5f });
	Shots.Add({ TEXT("17_slice_voxels"), [=, this]() { if (AWaveBakeActor* W = Sys()->WaveBake) { for (int32 n = 0; n < 8 && W->GetSliceView() != EWaveSliceView::Voxels; ++n) W->CycleSliceView(); } }, 1.5f });
	Shots.Add({ TEXT("18_wave_replay"), [=, this]() { if (AWaveBakeActor* W = Sys()->WaveBake) W->ToggleReplay(); }, 1.2f });
	// 18b: wave bake heard from the corridor: machine behind the workshop door (path must bend at the doorway and end at the listener)
	Shots.Add({ TEXT("18b_wave_corridor_machine"), [=, this]() { if (AWaveBakeActor* W = Sys()->WaveBake) { W->SetSliceView(EWaveSliceView::Off); W->SetProbesVisible(true); }
		Step(EOcclusionStep::WaveBake); Door(TEXT("工坊门"), true); Place(FVector(1525, 1330, 100), 0, false, -45.f, 2200.f); Focus(TEXT("机床"), false); }, 2.0f });
	// 19-20: Steam Audio
	Shots.Add({ TEXT("19_steamaudio_radio"), [=, this]() { if (AWaveBakeActor* W = Sys()->WaveBake) { for (int32 n = 0; n < 8 && W->GetSliceView() != EWaveSliceView::Off; ++n) W->CycleSliceView(); if (W->GetSliceView() == EWaveSliceView::Off && false) {} W->ToggleProbes(); }
		Step(EOcclusionStep::SteamAudio); Door(TEXT("琴房门"), false); Place(HallSpot, 0, false, -45.f, 2600.f); Focus(TEXT("收音机"), true); }, 2.5f, 2.5f });
	Shots.Add({ TEXT("20_group6_B_steam_plus_portal"), [=, this]() { Door(TEXT("琴房门"), true); Sys()->SetGroup(5); Sys()->CycleVariant(); }, 2.5f, 2.5f });
	Shots.Add({ TEXT("21_help"), [=, this]() { Sys()->SetStep(EOcclusionStep::Smoothing); if (AOcclusionPlayerController* PC = Cast<AOcclusionPlayerController>(GetWorld()->GetFirstPlayerController())) PC->bHelp = true; }, 1.f });
	// 21b: patrol footsteps, solo, heard from the hall while the NPC walks past
	Shots.Add({ TEXT("21b_footsteps"), [=, this]() { if (AOcclusionPlayerController* PC = Cast<AOcclusionPlayerController>(GetWorld()->GetFirstPlayerController())) PC->bHelp = false;
		Sys()->SetStep(EOcclusionStep::Smoothing); Place(FVector(700, 550, 100), 0, false, -45.f, 2600.f); Focus(TEXT("巡逻者"), true); }, 1.0f, 5.0f });
	// 25: switch-by-switch audio checks (radio solo, listener in the hall); compare the "audio:" lines in readout.txt
	{
		auto Cfg = [this](TFunction<void(FPipelineConfig&)> Edit) { FPipelineConfig C; Edit(C); Sys()->SetConfig(C); };
		const FVector Hall(300, 300, 100);
		Shots.Add({ TEXT("25a_none"), [=, this]() { Place(Hall, 0, false, -45.f, 2600.f); Focus(TEXT("收音机"), true); Door(TEXT("琴房门"), true); Door(TEXT("工坊门"), false);
			Cfg([](FPipelineConfig& C) {}); }, 1.5f, 2.0f });
		Shots.Add({ TEXT("25b_native_blocked"), [=, this]() { Cfg([](FPipelineConfig& C) { C.bNative = true; }); }, 1.5f, 2.0f });
		Shots.Add({ TEXT("25c_portal_excess_trans"), [=, this]() { Cfg([](FPipelineConfig& C) { C.bMultiRay = C.bPortal = C.bExcess = C.bTransmission = true; }); }, 1.5f, 2.0f });
		Shots.Add({ TEXT("25d_plus_reverb"), [=, this]() { Cfg([](FPipelineConfig& C) { C.bMultiRay = C.bPortal = C.bExcess = C.bTransmission = C.bReverb = true; }); }, 2.0f, 2.0f });
		Shots.Add({ TEXT("25e_plus_smoothing"), [=, this]() { Cfg([](FPipelineConfig& C) { C.bMultiRay = C.bPortal = C.bExcess = C.bTransmission = C.bReverb = C.bSmoothing = true; }); }, 2.0f, 2.0f });
		Shots.Add({ TEXT("25f_wave_door_closed"), [=, this]() { Door(TEXT("琴房门"), false); Cfg([](FPipelineConfig& C) { C.Solution = ESolution::WaveBake; C.bReverb = C.bSmoothing = true; }); }, 2.5f, 2.0f });
		Shots.Add({ TEXT("25g_wave_door_closed_dyn"), [=, this]() { Cfg([](FPipelineConfig& C) { C.Solution = ESolution::WaveBake; C.bReverb = C.bSmoothing = C.bDynamicDoors = true; }); }, 2.5f, 2.0f });
		Shots.Add({ TEXT("25h_steam_door_closed"), [=, this]() { Cfg([](FPipelineConfig& C) { C.Solution = ESolution::SteamAudio; C.bReverb = C.bSmoothing = true; }); }, 2.5f, 2.0f });
		Shots.Add({ TEXT("25i_steam_door_open"), [=, this]() { Door(TEXT("琴房门"), true); }, 2.5f, 2.0f });
		Shots.Add({ TEXT("25j_steam_plus_portal_open"), [=, this]() { Cfg([](FPipelineConfig& C) { C.Solution = ESolution::SteamAudio; C.bReverb = C.bSmoothing = C.bSteamPlusPortal = C.bPortal = C.bExcess = true; }); }, 2.5f, 2.0f });
		Shots.Add({ TEXT("25k_reverb_only_none"), [=, this]() { Cfg([](FPipelineConfig& C) { C.bReverb = true; }); }, 2.0f, 2.0f });
	}
	// 22: real key input through the controller (Enhanced Input), each step logged with the resulting state
	Shots.Add({ TEXT("22a_input_start"), [=, this]() { if (AOcclusionPlayerController* PC = Cast<AOcclusionPlayerController>(GetWorld()->GetFirstPlayerController())) PC->bHelp = false;
		Sys()->SetGroup(-1); Place(FVector(380, 700, 100), 0, false, -45.f, 2200.f); LogInput(TEXT("22a before")); Press(EKeys::W); }, 1.0f });
	Shots.Add({ TEXT("22b_input_rotate"), [=, this]() { LogInput(TEXT("22b after W 1s")); Press(EKeys::E); }, 1.0f });
	Shots.Add({ TEXT("22c_input_firstperson"), [=, this]() { LogInput(TEXT("22c after E")); Press(EKeys::V); }, 1.0f });
	Shots.Add({ TEXT("22d_input_step2"), [=, this]() { LogInput(TEXT("22d after V")); Press(EKeys::Two); }, 0.8f });
	Shots.Add({ TEXT("22e_input_group"), [=, this]() { LogInput(TEXT("22e after 2")); Press(EKeys::G); }, 0.8f });
	Shots.Add({ TEXT("22f_input_variant"), [=, this]() { LogInput(TEXT("22f after G")); Press(EKeys::B); }, 0.8f });
	Shots.Add({ TEXT("22g_input_focus_solo"), [=, this]() { LogInput(TEXT("22g after B")); Press(EKeys::Tab); }, 0.5f });
	Shots.Add({ TEXT("22h_input_solo"), [=, this]() { LogInput(TEXT("22h after Tab")); Press(EKeys::R); }, 0.5f });
	Shots.Add({ TEXT("22i_input_debug"), [=, this]() { LogInput(TEXT("22i after R")); Press(EKeys::H); }, 0.5f });
	Shots.Add({ TEXT("22j_input_door"), [=, this]() { LogInput(TEXT("22j after H")); Place(FVector(360, 1080, 100), 90, false, -45.f, 1800.f); Press(EKeys::F); }, 2.0f });
	Shots.Add({ TEXT("22k_input_back_topdown"), [=, this]() { LogInput(TEXT("22k after F at study door")); Press(EKeys::V); Press(EKeys::Q); }, 1.2f });
	Shots.Add({ TEXT("22l_input_help_f1"), [=, this]() { LogInput(TEXT("22l after V,Q")); Press(EKeys::F1); }, 1.0f });
	Shots.Add({ TEXT("22m_input_hide_ui_f3"), [=, this]() { LogInput(TEXT("22m after F1")); Press(EKeys::F1); Press(EKeys::F3); }, 1.0f });
	Shots.Add({ TEXT("22n_input_gap"), [=, this]() { LogInput(TEXT("22n after F1,F3")); }, 0.3f });   // a key released and pressed in the same frame is not a new press
	Shots.Add({ TEXT("22o_input_show_ui_f3"), [=, this]() { Press(EKeys::F3); }, 0.5f });
	Shots.Add({ TEXT("22p_input_end"), [=, this]() { LogInput(TEXT("22p after F3")); }, 0.2f });
	// 23: mouse clicks on the control panel, through Slate hit testing
	auto Click = [this](const TCHAR* L) {
		AOcclusionPlayerController* PC = Cast<AOcclusionPlayerController>(GetWorld()->GetFirstPlayerController());
		const bool bOk = PC && PC->HUD && PC->HUD->SimulateClick(L);
		LogInput(FString::Printf(TEXT("click %s -> %s"), L, bOk ? TEXT("handled") : TEXT("NOT HANDLED")));
	};
	Shots.Add({ TEXT("23a_mouse_custom"), [=, this]() { if (AOcclusionPlayerController* PC = Cast<AOcclusionPlayerController>(GetWorld()->GetFirstPlayerController())) { PC->bHelp = false; PC->bHideUI = false; }
		Step(EOcclusionStep::None); Place(FVector(300, 300, 100), 0, false, -45.f, 2600.f); Focus(TEXT("收音机"), true); }, 0.6f });
	Shots.Add({ TEXT("23b_mouse_custom_clicked"), [=, this]() { Click(TEXT("自研管线")); }, 1.0f });
	Shots.Add({ TEXT("23c_mouse_trans_off"), [=, this]() { Click(TEXT("穿墙透射")); }, 0.6f });
	Shots.Add({ TEXT("23d_mouse_angle"), [=, this]() { Click(TEXT("衍射：按拐角角度")); }, 0.6f });
	Shots.Add({ TEXT("23e_mouse_portal_off"), [=, this]() { Click(TEXT("房间—门户路径")); }, 0.6f });
	Shots.Add({ TEXT("23f_mouse_steam_plus"), [=, this]() { Click(TEXT("叠加自研门户路径")); }, 1.0f });
	Shots.Add({ TEXT("23g_mouse_reverb_off"), [=, this]() { Click(TEXT("房间混响耦合")); }, 0.6f });
	Shots.Add({ TEXT("23h_mouse_group3"), [=, this]() { Click(TEXT("3")); }, 1.0f });
	Shots.Add({ TEXT("23i_mouse_group3_B"), [=, this]() { Click(TEXT("B")); }, 1.0f });
	Shots.Add({ TEXT("23j_mouse_group_exit"), [=, this]() { Click(TEXT("3")); }, 0.6f });
	Shots.Add({ TEXT("23k_fp_alt"), [=, this]() { Place(FVector(300, 300, 100), 0, true, -45.f, 2600.f); }, 0.6f });
	Shots.Add({ TEXT("23l_fp_alt_down"), [=, this]() { LogInput(TEXT("fp before alt")); Press(EKeys::LeftAlt); }, 0.6f });
	Shots.Add({ TEXT("23m_end"), [=, this]() { LogInput(TEXT("fp alt held")); }, 0.3f });
}

void ACaptureDirector::LogState(const FString& Tag)
{
	UAcousticSubsystem* S = Sys();
	FString Out = FString::Printf(TEXT("== %s | listener %s room=%s | layers: %s\n"), *Tag, *S->ListenerPos.ToString(),
		S->ListenerRoom ? *S->ListenerRoom->DisplayName : TEXT("-"), *FString::Join(S->ActiveLayerNames(), TEXT(", ")));
	for (UAcousticEmitterComponent* E : S->GetEmitters())
	{
		const FAcousticResult& R = E->Last;
		const USteamAudioSourceComponent* SA = E->GetOwner()->FindComponentByClass<USteamAudioSourceComponent>();
		Out += FString::Printf(TEXT("  %s: path[%s] %.1f/%.1f/%.1f dB trans[%s] %.1f/%.1f/%.1f dB wet %.1f | direct %.1fm path %.1fm excess %.1fm bend %.0f portals %d | virt-dist %.1fm | steam occ %.2f trans %.3f/%.3f/%.3f | %s\n"),
			*E->DisplayName, R.bPathAudible ? TEXT("on") : TEXT("off"), E->AppliedPath.GainDb[0], E->AppliedPath.GainDb[1], E->AppliedPath.GainDb[2],
			R.bTransAudible ? TEXT("on") : TEXT("off"), E->AppliedTrans.GainDb[0], E->AppliedTrans.GainDb[1], E->AppliedTrans.GainDb[2], E->AppliedWetDb,
			R.DirectM, R.PathLengthM, R.ExcessM, R.BendDeg, R.PortalCount, FVector::Dist(E->AppliedPath.Pos, S->ListenerPos) / 100.f,
			SA ? SA->OcclusionValue : -1.f, SA ? SA->TransmissionLowValue : -1.f, SA ? SA->TransmissionMidValue : -1.f, SA ? SA->TransmissionHighValue : -1.f, *R.Note);
	}
	if (UAcousticEmitterComponent* F = S->GetFocused())
	{
		FHitResult H; FCollisionQueryParams Q(SCENE_QUERY_STAT(CapDbg), false); Q.AddIgnoredActor(F->GetOwner());
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) if (PC->GetPawn()) Q.AddIgnoredActor(PC->GetPawn());
		if (GetWorld()->LineTraceSingleByChannel(H, F->GetSourceLocation(), S->ListenerPos, ECC_GameTraceChannel1, Q))
			Out += FString::Printf(TEXT("  focus first hit: %s / %s at %s\n"), *GetNameSafe(H.GetActor()), *GetNameSafe(H.GetComponent()), *H.ImpactPoint.ToString());
	}
	for (TActorIterator<APatrolNPC> It(GetWorld()); It; ++It)
		Out += FString::Printf(TEXT("  npc: steps %d, foot height L %.1f..%.1f R %.1f..%.1f cm, speed %.0f\n"), It->StepCount, It->FootMin[0], It->FootMax[0], It->FootMin[1], It->FootMax[1], It->GetVelocity().Size2D());
	if (S->WaveBake) Out += FString::Printf(TEXT("  wave: baking=%d data=%d %s\n"), S->WaveBake->IsBaking(), S->WaveBake->HasData(), *S->WaveBake->GetStatusText());
	UE_LOG(LogCapture, Display, TEXT("%s"), *Out);
	FFileHelper::SaveStringToFile(Out, *(OutDir / TEXT("readout.txt")), FFileHelper::EEncodingOptions::ForceUTF8, &IFileManager::Get(), FILEWRITE_Append);
}

void ACaptureDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Timer -= DeltaSeconds;
	Elapsed += DeltaSeconds;
	if (Index < 0)
	{
		if (Elapsed < 3.f) return;   // let the world settle and voices start
		Index = 0; Phase = 0;
	}
	if (Only.Num() > 0)
	{
		// Nothing selected remains: stop now instead of running the remaining setups (a skipped bake would block quitting).
		bool bAny = false;
		for (int32 i = Index; i < Shots.Num() && !bAny; ++i) bAny = Only.ContainsByPredicate([&](const FString& O) { return Shots[i].Name.StartsWith(O); });
		if (!bAny && Phase == 0) Index = Shots.Num();
	}
	while (Index < Shots.Num() && Only.Num() > 0 && !Only.ContainsByPredicate([&](const FString& O) { return Shots[Index].Name.StartsWith(O); }))
	{
		// Skipped shots still run their setup so later shots keep the right state.
		// (except the bake start, which would keep the GPU busy for minutes)
		if (Shots[Index].Setup && !Shots[Index].Name.Contains(TEXT("wavebake_running")) && !Shots[Index].Name.StartsWith(TEXT("22")) && !Shots[Index].Name.StartsWith(TEXT("23"))) Shots[Index].Setup();
		++Index;
	}
	if (Index >= Shots.Num())
	{
		UE_LOG(LogCapture, Display, TEXT("Capture finished"));
		UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
		SetActorTickEnabled(false);
		return;
	}
	FShot& S = Shots[Index];
	switch (Phase)
	{
	case 0:
		if (Held.Num() > 0)
		{
			if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
				for (const FKey& K : Held)
					PC->InputKey(FInputKeyEventArgs(GEngine->GameViewport ? GEngine->GameViewport->Viewport : nullptr, IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(), K, IE_Released, FPlatformTime::Cycles64()));
			Held.Reset();
		}
		if (S.Setup) S.Setup();
		Timer = S.Wait; Elapsed = 0.f; Phase = 1;
		break;
	case 1:
		if (Timer > 0.f) break;
		if (S.WaitUntil && !S.WaitUntil() && Elapsed < S.Timeout) break;
		if (S.RecordSec > 0.f)
		{
			if (!Tap.IsValid())
			{
				Tap = MakeShared<FCaptureTap, ESPMode::ThreadSafe>();
				if (FAudioDevice* D = GetWorld()->GetAudioDeviceRaw()) D->RegisterSubmixBufferListener(Tap.ToSharedRef(), D->GetMainSubmixObject());
			}
			{ FScopeLock L(&Tap->Lock); Tap->Data.Reset(); Tap->bRec = true; }
			Timer = S.RecordSec; Phase = 2;
		}
		else Phase = 3;
		break;
	case 2:
		if (Timer > 0.f) break;
		{
			const FString Stats = Tap->Finish(OutDir / (S.Name + TEXT(".wav")));
			FFileHelper::SaveStringToFile(TEXT("== ") + S.Name + TEXT("\n") + Stats, *(OutDir / TEXT("readout.txt")), FFileHelper::EEncodingOptions::ForceUTF8, &IFileManager::Get(), FILEWRITE_Append);
		}
		Phase = 3;
		break;
	case 3:
		LogState(S.Name);
		FScreenshotRequest::RequestScreenshot(OutDir / (S.Name + TEXT(".png")), true, false);
		Timer = 0.6f; Phase = 4;
		break;
	case 4:
		if (Timer > 0.f) break;
		++Index; Phase = 0;
		break;
	}
}
