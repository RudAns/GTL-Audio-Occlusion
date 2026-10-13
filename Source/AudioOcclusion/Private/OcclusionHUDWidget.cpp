#include "OcclusionHUDWidget.h"
#include "OcclusionPlayerController.h"
#include "OcclusionCharacter.h"
#include "AcousticSubsystem.h"
#include "AcousticEmitterComponent.h"
#include "AcousticRoom.h"
#include "WaveBakeActor.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Framework/Application/SlateApplication.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"

namespace HudStyle
{
	const FLinearColor Panel(0.012f, 0.016f, 0.024f, 0.80f);
	const FLinearColor PanelSoft(0.012f, 0.016f, 0.024f, 0.62f);
	const FLinearColor Text(0.93f, 0.94f, 0.95f, 1.f);
	const FLinearColor Dim(0.62f, 0.66f, 0.70f, 1.f);
	const FLinearColor Accent(1.0f, 0.62f, 0.16f, 1.f);
	const FLinearColor AccentBg(1.0f, 0.55f, 0.10f, 0.22f);
	const FLinearColor Teal(0.25f, 0.85f, 0.78f, 1.f);
	const FLinearColor ChipBg(0.20f, 0.75f, 0.70f, 0.20f);
	const FLinearColor KeyBg(0.85f, 0.88f, 0.92f, 0.95f);
	const FLinearColor KeyText(0.05f, 0.06f, 0.08f, 1.f);
}

static FSlateBrush Rounded(const FLinearColor& C, float R)
{
	return FSlateRoundedBoxBrush(C, R);
}

UTextBlock* UOcclusionHUDWidget::MakeText(const FString& S, int32 Size, const FLinearColor& C, bool bBold)
{
	UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>();
	T->SetText(FText::FromString(S));
	T->SetFont(FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size));
	T->SetColorAndOpacity(FSlateColor(C));
	T->SetShadowOffset(FVector2D(1.f, 1.f));
	T->SetShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.55f));
	return T;
}

UBorder* UOcclusionHUDWidget::MakePanel(const FLinearColor& Bg, float Radius, const FMargin& Pad)
{
	UBorder* B = WidgetTree->ConstructWidget<UBorder>();
	B->SetBrush(Rounded(Bg, Radius));
	B->SetPadding(Pad);
	return B;
}

UWidget* UOcclusionHUDWidget::KeyCap(const FString& Key, const FString& What)
{
	UHorizontalBox* H = WidgetTree->ConstructWidget<UHorizontalBox>();
	UBorder* Cap = MakePanel(HudStyle::KeyBg, 5.f, FMargin(8, 2, 8, 2));
	Cap->SetContent(MakeText(Key, 14, HudStyle::KeyText, true));
	H->AddChildToHorizontalBox(Cap)->SetVerticalAlignment(VAlign_Center);
	UTextBlock* T = MakeText(What, 15, HudStyle::Text);
	UHorizontalBoxSlot* S = H->AddChildToHorizontalBox(T);
	S->SetPadding(FMargin(8, 0, 18, 0)); S->SetVerticalAlignment(VAlign_Center);
	return H;
}

static UCanvasPanelSlot* Place(UCanvasPanel* Root, UWidget* W, const FAnchors& A, const FVector2D& Align, const FVector2D& Pos)
{
	UCanvasPanelSlot* S = Root->AddChildToCanvas(W);
	S->SetAnchors(A); S->SetAlignment(Align); S->SetPosition(Pos); S->SetAutoSize(true);
	return S;
}

void UOcclusionHUDWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildTree();
	SetVisibility(ESlateVisibility::Visible);   // receives mouse clicks; unhandled clicks fall through to the game viewport
}

void UOcclusionHUDWidget::BuildTree()
{
	Root = WidgetTree->ConstructWidget<UCanvasPanel>();
	WidgetTree->RootWidget = Root;

	// ---- top-left: title, mode, active layers ----
	{
		UBorder* P = MakePanel(HudStyle::Panel, 12.f, FMargin(18, 14, 18, 14));
		UVerticalBox* V = WidgetTree->ConstructWidget<UVerticalBox>();
		V->AddChildToVerticalBox(MakeText(TEXT("声音遮挡实验室"), 24, HudStyle::Text, true));
		V->AddChildToVerticalBox(MakeText(TEXT("SOUND OCCLUSION LAB · 遮挡 · 阻挡 · 衍射 · 透射"), 12, HudStyle::Dim));
		ModeLine = MakeText(TEXT(""), 17, HudStyle::Accent, true);
		V->AddChildToVerticalBox(ModeLine)->SetPadding(FMargin(0, 10, 0, 6));
		Chips = WidgetTree->ConstructWidget<UHorizontalBox>();
		V->AddChildToVerticalBox(Chips);
		P->SetContent(V);
		Place(Root, P, FAnchors(0, 0), FVector2D(0, 0), FVector2D(24, 22));
		AllPanels.Add(P);
	}
	// ---- left: control panel (mouse) ----
	{
		ControlPanel = MakePanel(HudStyle::Panel, 12.f, FMargin(12, 12, 12, 12));
		StepList = WidgetTree->ConstructWidget<UVerticalBox>();
		BuildControls(StepList);
		ControlPanel->SetContent(StepList);
		Place(Root, ControlPanel, FAnchors(0, 0), FVector2D(0, 0), FVector2D(24, 196));
		AllPanels.Add(ControlPanel);
	}
	// ---- left: comparison group (replaces step list highlight while active) ----
	{
		GroupPanel = MakePanel(HudStyle::Panel, 12.f, FMargin(16, 12, 16, 12));
		UVerticalBox* V = WidgetTree->ConstructWidget<UVerticalBox>();
		GroupTitle = MakeText(TEXT(""), 18, HudStyle::Accent, true);
		GroupQuestion = MakeText(TEXT(""), 14, HudStyle::Dim);
		V->AddChildToVerticalBox(GroupTitle);
		V->AddChildToVerticalBox(GroupQuestion)->SetPadding(FMargin(0, 2, 0, 8));
		for (int32 i = 0; i < 3; ++i)
		{
			UBorder* Row = MakePanel(FLinearColor(0, 0, 0, 0), 7.f, FMargin(10, 5, 12, 5));
			UTextBlock* T = MakeText(TEXT(""), 16, HudStyle::Text);
			Row->SetContent(T);
			V->AddChildToVerticalBox(Row)->SetPadding(FMargin(0, 1, 0, 1));
			GroupRows.Add(Row); GroupTexts.Add(T);
			FHudClick C;
			C.Row = Row; C.Label = T; C.Owner = GroupPanel;
			C.Fn = [this, i]() { if (UAcousticSubsystem* S = Sys()) S->SetVariant(i); };
			C.On = [this, i]() { UAcousticSubsystem* S = Sys(); return S && S->GetVariant() == i; };
			C.Enabled = []() { return true; };
			Clicks.Add(MoveTemp(C));
		}
		{
			USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(); Box->SetWidthOverride(300.f);
			UTextBlock* How = MakeText(TEXT("对照组是预先配好的两三套方案。站在同一个位置点 A / B（或按 B）来回切换，对比听感；左侧勾选显示当前这套方案的组成。"), 13, HudStyle::Dim);
			How->SetAutoWrapText(true);
			Box->SetContent(How);
			V->AddChildToVerticalBox(Box)->SetPadding(FMargin(0, 8, 0, 0));
		}
		V->AddChildToVerticalBox(MakeText(TEXT("B 切换方案　·　G 下一组　·　再点组号退出"), 13, HudStyle::Text))->SetPadding(FMargin(0, 6, 0, 0));
		GroupPanel->SetContent(V);
		Place(Root, GroupPanel, FAnchors(0, 0), FVector2D(0, 0), FVector2D(340, 196));
	}
	// ---- right: focused emitter card ----
	{
		UBorder* P = MakePanel(HudStyle::Panel, 12.f, FMargin(18, 14, 18, 14));
		UVerticalBox* V = WidgetTree->ConstructWidget<UVerticalBox>();
		UHorizontalBox* Head = WidgetTree->ConstructWidget<UHorizontalBox>();
		EmitterDot = MakePanel(HudStyle::Accent, 7.f, FMargin(7, 7, 7, 7));
		Head->AddChildToHorizontalBox(EmitterDot)->SetVerticalAlignment(VAlign_Center);
		EmitterName = MakeText(TEXT(""), 20, HudStyle::Text, true);
		Head->AddChildToHorizontalBox(EmitterName)->SetPadding(FMargin(10, 0, 0, 0));
		V->AddChildToVerticalBox(Head);
		EmitterInfo = MakeText(TEXT(""), 14, HudStyle::Dim);
		V->AddChildToVerticalBox(EmitterInfo)->SetPadding(FMargin(0, 6, 0, 8));
		const TCHAR* Labels[6] = { TEXT("路径 · 低 250 Hz"), TEXT("路径 · 中 1 kHz"), TEXT("路径 · 高 4 kHz"), TEXT("穿墙 · 低"), TEXT("穿墙 · 中"), TEXT("穿墙 · 高") };
		for (int32 i = 0; i < 6; ++i)
		{
			if (i == 3) V->AddChildToVerticalBox(WidgetTree->ConstructWidget<USpacer>())->SetPadding(FMargin(0, 3, 0, 3));
			UHorizontalBox* H = WidgetTree->ConstructWidget<UHorizontalBox>();
			USizeBox* Lab = WidgetTree->ConstructWidget<USizeBox>(); Lab->SetWidthOverride(126.f);
			UTextBlock* LabT = MakeText(Labels[i], 13, HudStyle::Dim);
			Lab->SetContent(LabT);
			BandLabels.Add(LabT);
			H->AddChildToHorizontalBox(Lab)->SetVerticalAlignment(VAlign_Center);
			UBorder* Track = MakePanel(FLinearColor(1, 1, 1, 0.08f), 4.f, FMargin(0));
			USizeBox* TrackBox = WidgetTree->ConstructWidget<USizeBox>(); TrackBox->SetWidthOverride(150.f); TrackBox->SetHeightOverride(10.f);
			UHorizontalBox* TrackH = WidgetTree->ConstructWidget<UHorizontalBox>();
			USizeBox* Bar = WidgetTree->ConstructWidget<USizeBox>(); Bar->SetHeightOverride(10.f); Bar->SetWidthOverride(150.f);
			UBorder* Fill = MakePanel(i < 3 ? HudStyle::Accent : FLinearColor(0.47f, 0.6f, 1.f, 1.f), 4.f, FMargin(0));
			Bar->SetContent(Fill);
			TrackH->AddChildToHorizontalBox(Bar);
			Track->SetContent(TrackH);
			TrackBox->SetContent(Track);
			H->AddChildToHorizontalBox(TrackBox)->SetVerticalAlignment(VAlign_Center);
			UTextBlock* Val = MakeText(TEXT(""), 13, HudStyle::Text);
			H->AddChildToHorizontalBox(Val)->SetPadding(FMargin(10, 0, 0, 0));
			V->AddChildToVerticalBox(H)->SetPadding(FMargin(0, 2, 0, 2));
			BandBars.Add(Bar); BandTexts.Add(Val);
		}
		EmitterNote = MakeText(TEXT(""), 14, HudStyle::Text);
		EmitterNote->SetAutoWrapText(true);
		USizeBox* NoteBox = WidgetTree->ConstructWidget<USizeBox>(); NoteBox->SetWidthOverride(360.f); NoteBox->SetContent(EmitterNote);
		V->AddChildToVerticalBox(NoteBox)->SetPadding(FMargin(0, 10, 0, 0));
		V->AddChildToVerticalBox(MakeText(TEXT("Tab 换声源　·　R 只听这个"), 13, HudStyle::Dim))->SetPadding(FMargin(0, 8, 0, 0));
		P->SetContent(V);
		// Right column: emitter card, then the wave bake visualisation panel under it.
		UVerticalBox* RightCol = WidgetTree->ConstructWidget<UVerticalBox>();
		RightCol->AddChildToVerticalBox(P)->SetHorizontalAlignment(HAlign_Right);
		Place(Root, RightCol, FAnchors(1, 0), FVector2D(1, 0), FVector2D(-24, 22));
		BuildVizPanel();
		RightCol->AddChildToVerticalBox(VizPanel)->SetPadding(FMargin(0, 10, 0, 0));
		AllPanels.Add(P);
	}
	// ---- bottom: explanation ----
	{
		UBorder* P = MakePanel(HudStyle::Panel, 12.f, FMargin(22, 14, 22, 14));
		Explain = MakeText(TEXT(""), 18, HudStyle::Text);
		Explain->SetAutoWrapText(true);
		USizeBox* SB = WidgetTree->ConstructWidget<USizeBox>(); SB->SetWidthOverride(860.f); SB->SetContent(Explain);
		P->SetContent(SB);
		Place(Root, P, FAnchors(0.5f, 1.f), FVector2D(0.5f, 1.f), FVector2D(0, -78));
		AllPanels.Add(P);
	}
	// ---- bottom strip: key hints ----
	{
		KeysBox = MakePanel(HudStyle::PanelSoft, 10.f, FMargin(16, 8, 10, 8));
		UHorizontalBox* H = WidgetTree->ConstructWidget<UHorizontalBox>();
		H->AddChildToHorizontalBox(KeyCap(TEXT("WASD"), TEXT("移动")));
		H->AddChildToHorizontalBox(KeyCap(TEXT("V"), TEXT("切换视角")));
		H->AddChildToHorizontalBox(KeyCap(TEXT("Q E"), TEXT("旋转 45°")));
		H->AddChildToHorizontalBox(KeyCap(TEXT("F"), TEXT("开关门")));
		H->AddChildToHorizontalBox(KeyCap(TEXT("鼠标"), TEXT("点选处理层")));
		H->AddChildToHorizontalBox(KeyCap(TEXT("1–0"), TEXT("预设")));
		H->AddChildToHorizontalBox(KeyCap(TEXT("K"), TEXT("重新烘焙（高耗时）")));
		H->AddChildToHorizontalBox(KeyCap(TEXT("F1"), TEXT("全部按键")));
		KeysBox->SetContent(H);
		Place(Root, KeysBox, FAnchors(0.5f, 1.f), FVector2D(0.5f, 1.f), FVector2D(0, -20));
		AllPanels.Add(KeysBox);
	}
	// ---- top-right under the card: status ----
	{
		StatusLine = MakeText(TEXT(""), 15, FLinearColor(0.86f, 0.89f, 0.92f, 1.f));
		UBorder* StatusBox = MakePanel(HudStyle::Panel, 9.f, FMargin(14, 6, 14, 6));
		StatusBox->SetContent(StatusLine);
		Place(Root, StatusBox, FAnchors(1, 1), FVector2D(1, 1), FVector2D(-20, -150));
		AllPanels.Add(StatusBox);
	}
	// ---- toast ----
	{
		ToastBox = MakePanel(FLinearColor(0.05f, 0.05f, 0.06f, 0.88f), 10.f, FMargin(22, 10, 22, 10));
		ToastText = MakeText(TEXT(""), 20, HudStyle::Accent, true);
		ToastBox->SetContent(ToastText);
		Place(Root, ToastBox, FAnchors(0.5f, 0.f), FVector2D(0.5f, 0.f), FVector2D(0, 34));
		ToastBox->SetVisibility(ESlateVisibility::Collapsed);
	}
	// ---- bake progress ----
	{
		BakeBox = MakePanel(HudStyle::Panel, 10.f, FMargin(18, 10, 18, 12));
		UVerticalBox* V = WidgetTree->ConstructWidget<UVerticalBox>();
		BakeText = MakeText(TEXT(""), 15, HudStyle::Text, true);
		V->AddChildToVerticalBox(BakeText);
		UBorder* Track = MakePanel(FLinearColor(1, 1, 1, 0.1f), 4.f, FMargin(0));
		USizeBox* TB = WidgetTree->ConstructWidget<USizeBox>(); TB->SetWidthOverride(420.f); TB->SetHeightOverride(10.f);
		UHorizontalBox* TH = WidgetTree->ConstructWidget<UHorizontalBox>();
		BakeBar = WidgetTree->ConstructWidget<USizeBox>(); BakeBar->SetHeightOverride(10.f); BakeBar->SetWidthOverride(0.f);
		BakeBar->SetContent(MakePanel(HudStyle::Teal, 4.f, FMargin(0)));
		TH->AddChildToHorizontalBox(BakeBar); Track->SetContent(TH); TB->SetContent(Track);
		V->AddChildToVerticalBox(TB)->SetPadding(FMargin(0, 8, 0, 0));
		BakeBox->SetContent(V);
		Place(Root, BakeBox, FAnchors(0.5f, 0.f), FVector2D(0.5f, 0.f), FVector2D(0, 100));
		BakeBox->SetVisibility(ESlateVisibility::Collapsed);
	}
	// ---- help overlay ----
	{
		HelpBox = MakePanel(FLinearColor(0.01f, 0.012f, 0.018f, 0.92f), 14.f, FMargin(30, 22, 30, 22));
		UVerticalBox* V = WidgetTree->ConstructWidget<UVerticalBox>();
		V->AddChildToVerticalBox(MakeText(TEXT("全部按键"), 22, HudStyle::Text, true))->SetPadding(FMargin(0, 0, 0, 12));
		const TCHAR* Rows[][2] = {
			{ TEXT("WASD / 方向键"), TEXT("移动（俯视角下相对镜头方向）") },
			{ TEXT("鼠标"), TEXT("点选左侧处理层；第一人称下转头，按住 Alt 出鼠标点选") },
			{ TEXT("V"), TEXT("切换第一人称 / 俯视视角") },
			{ TEXT("Q / E · 滚轮"), TEXT("俯视角下按 45° 旋转镜头 · 缩放") },
			{ TEXT("F"), TEXT("开关最近的门") },
			{ TEXT("1 – 0"), TEXT("预设组合：1 不处理 · 2 UE 原生 · 3~8 自研管线逐层叠加 · 9 波动烘焙 · 0 Steam") },
			{ TEXT("G / B"), TEXT("下一个对照组 / 在 A、B（C）之间切换（也可以用鼠标点）") },
			{ TEXT("Tab / R"), TEXT("切换观察的声源 / 只听这个声源") },
			{ TEXT("H"), TEXT("显示或隐藏射线、路径与虚拟声像") },
			{ TEXT("K"), TEXT("重新烘焙 GPU 波动场（高耗时，约 1~3 分钟；启动时自动读取上次的结果，工程里也附带了一份）") },
			{ TEXT("P / L / O"), TEXT("烘焙可视化：探针点阵 / 切换体素与参数切片 / 波场回放") },
			{ TEXT("F1 / F3 / Esc"), TEXT("关闭本页 / 隐藏界面 / 退出") } };
		for (auto& R : Rows)
		{
			UHorizontalBox* H = WidgetTree->ConstructWidget<UHorizontalBox>();
			USizeBox* K = WidgetTree->ConstructWidget<USizeBox>(); K->SetWidthOverride(180.f);
			K->SetContent(MakeText(R[0], 16, HudStyle::Accent, true));
			H->AddChildToHorizontalBox(K);
			H->AddChildToHorizontalBox(MakeText(R[1], 16, HudStyle::Text));
			V->AddChildToVerticalBox(H)->SetPadding(FMargin(0, 3, 0, 3));
		}
		HelpBox->SetContent(V);
		Place(Root, HelpBox, FAnchors(0.5f, 0.5f), FVector2D(0.5f, 0.5f), FVector2D(0, 0));
		HelpBox->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UOcclusionHUDWidget::ShowToast(const FString& Msg)
{
	if (!ToastBox) return;
	ToastText->SetText(FText::FromString(Msg));
	ToastBox->SetVisibility(ESlateVisibility::HitTestInvisible);
	ToastTime = 2.2f;
}

void UOcclusionHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (ToastTime > 0.f)
	{
		ToastTime -= InDeltaTime;
		ToastBox->SetRenderOpacity(FMath::Clamp(ToastTime / 0.4f, 0.f, 1.f));
		if (ToastTime <= 0.f) ToastBox->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UOcclusionHUDWidget::Refresh(AOcclusionPlayerController* PC)
{
	UAcousticSubsystem* Sys = PC && PC->GetWorld() ? PC->GetWorld()->GetSubsystem<UAcousticSubsystem>() : nullptr;
	if (!Sys || !Root) return;
	const ESlateVisibility Vis = PC->bHideUI ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible;
	for (UWidget* W : AllPanels) W->SetVisibility(Vis);
	HelpBox->SetVisibility(PC->bHelp && !PC->bHideUI ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

	// Mode line + chips
	const int32 G = Sys->GetGroup();
	if (G >= 0)
	{
		const FComparisonGroup& Grp = Sys->GetGroups()[G];
		ModeLine->SetText(FText::FromString(FString::Printf(TEXT("对照组 %s　·　%c：%s"), *Grp.Title, TCHAR('A' + Sys->GetVariant()), *Grp.Labels[Sys->GetVariant()])));
		GroupPanel->SetVisibility(Vis);
		GroupTitle->SetText(FText::FromString(FString::Printf(TEXT("对照组 %s"), *Grp.Title)));
		GroupQuestion->SetText(FText::FromString(FString::Printf(TEXT("要听出的差别：%s"), *Grp.Question)));
		for (int32 i = 0; i < GroupRows.Num(); ++i)
		{
			const bool bHas = i < Grp.Labels.Num();
			GroupRows[i]->SetVisibility(bHas ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
			if (!bHas) continue;
			GroupTexts[i]->SetText(FText::FromString(FString::Printf(TEXT("%c　%s"), TCHAR('A' + i), *Grp.Labels[i])));
		}
	}
	else
	{
		const int32 S = Sys->MatchingStep();
		ModeLine->SetText(FText::FromString(S >= 0
			? FString::Printf(TEXT("预设 %d　·　%s"), (S + 1) % 10, *UAcousticSubsystem::StepTitle((EOcclusionStep)S))
			: FString(TEXT("自由组合"))));
		GroupPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
	UpdateControls();
	UpdateViz(PC);
	const TArray<FString> Layers = Sys->ActiveLayerNames();
	const FString Joined = FString::Join(Layers, TEXT("|"));
	if (Joined != LastChips)
	{
	LastChips = Joined;
	Chips->ClearChildren();
	for (const FString& N : Layers)
	{
		UBorder* C = MakePanel(HudStyle::ChipBg, 9.f, FMargin(9, 2, 9, 3));
		C->SetContent(MakeText(N, 13, HudStyle::Teal, true));
		Chips->AddChildToHorizontalBox(C)->SetPadding(FMargin(0, 0, 6, 0));
	}
	}

	// Explanation
	if (G >= 0)
	{
		const FComparisonGroup& Grp = Sys->GetGroups()[G];
		const FPipelineConfig& C = Sys->GetConfig();
		const EOcclusionStep Rep = UAcousticSubsystem::RepresentativeStep(C);
		Explain->SetText(FText::FromString(FString::Printf(TEXT("【%c】%s"), TCHAR('A' + Sys->GetVariant()), *UAcousticSubsystem::StepExplain(Rep))));
	}
	else
	{
		const int32 M = Sys->MatchingStep();
		Explain->SetText(FText::FromString(UAcousticSubsystem::StepExplain(M >= 0 ? (EOcclusionStep)M : UAcousticSubsystem::RepresentativeStep(Sys->GetConfig()))));
	}

	// Focused emitter
	if (UAcousticEmitterComponent* E = Sys->GetFocused())
	{
		const FAcousticResult& R = E->Last;
		EmitterName->SetText(FText::FromString(E->DisplayName + (E->bSolo ? TEXT("　· 只听") : TEXT(""))));
		EmitterDot->SetBrush(FSlateRoundedBoxBrush(E->Color, 7.f));
		const AAcousticRoom* RS = Sys->RoomAt(E->GetSourceLocation());
		FString Info = FString::Printf(TEXT("所在：%s　·　直线 %.1f m"), RS ? *RS->DisplayName : TEXT("—"), R.DirectM);
		if (R.PathPoints.Num() > 2) Info += FString::Printf(TEXT("\n路径 %.1f m　·　程差 %.1f m　·　拐角 %.0f°"), R.PathLengthM, R.ExcessM, R.BendDeg);
		EmitterInfo->SetText(FText::FromString(Info));
		const bool bSteam = Sys->GetConfig().Solution == ESolution::SteamAudio;
		const TCHAR* BandName[3] = { TEXT("低"), TEXT("中"), TEXT("高") };
		for (int32 b = 3; b < 6; ++b)
			BandLabels[b]->SetText(FText::FromString((bSteam ? FString(TEXT("Steam 直达 · ")) : FString(TEXT("穿墙 · "))) + BandName[b - 3]));
		for (int32 b = 0; b < 6; ++b)
		{
			const float Db = b < 3 ? E->AppliedPath.GainDb[b] : (bSteam ? E->SteamDb[b - 3] : E->AppliedTrans.GainDb[b - 3]);
			const bool bAud = b < 3 ? (R.bPathAudible || Sys->GetConfig().bNative) : (bSteam || R.bTransAudible);
			const float Lin = bAud ? FMath::Clamp(1.f + Db / 40.f, 0.f, 1.f) : 0.f;
			BandBars[b]->SetWidthOverride(FMath::Max(150.f * Lin, 1.f));
			BandTexts[b]->SetText(FText::FromString(bAud ? FString::Printf(TEXT("%+.0f dB"), Db) : FString(TEXT("—"))));
		}
		EmitterNote->SetText(FText::FromString(R.Note));
	}

	// Status
	if (AOcclusionCharacter* C = Cast<AOcclusionCharacter>(PC->GetPawn()))
	{
		StatusLine->SetText(FText::FromString(FString::Printf(TEXT("%s　·　你在：%s　·　求解 %.2f ms"),
			C->View == EViewMode::FirstPerson ? TEXT("第一人称") : TEXT("俯视"),
			Sys->ListenerRoom ? *Sys->ListenerRoom->DisplayName : TEXT("—"), Sys->SolveMs)));
	}

	// Bake progress
	if (AWaveBakeActor* WB = Sys->WaveBake)
	{
		if (WB->IsBaking())
		{
			BakeBox->SetVisibility(Vis);
			BakeText->SetText(FText::FromString(WB->GetStatusText()));
			BakeBar->SetWidthOverride(420.f * WB->GetProgress());
		}
		else BakeBox->SetVisibility(ESlateVisibility::Collapsed);
	}
}

// ------------------------------------------------------------------------------------------------
// Control panel: pick the solution (radio), its sub-layers (check boxes) and the shared layers.
// The hierarchy mirrors the actual dependencies: transmission and the diffraction mapping only
// exist on top of the room/portal path, and the shared layers apply to every solution.
// ------------------------------------------------------------------------------------------------

UAcousticSubsystem* UOcclusionHUDWidget::Sys() const
{
	return GetWorld() ? GetWorld()->GetSubsystem<UAcousticSubsystem>() : nullptr;
}

int32 UOcclusionHUDWidget::AddClick(UVerticalBox* V, int32 Indent, bool bRadio, const FString& Label, const FString& Key,
	TFunction<void()> Fn, TFunction<bool()> On, TFunction<bool()> Enabled)
{
	FHudClick C;
	C.Row = MakePanel(FLinearColor(0, 0, 0, 0), 6.f, FMargin(6, 3, 8, 3));
	UHorizontalBox* H = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (Indent > 0)
	{
		USizeBox* Sp = WidgetTree->ConstructWidget<USizeBox>(); Sp->SetWidthOverride(22.f * Indent);
		H->AddChildToHorizontalBox(Sp);
	}
	USizeBox* BoxSize = WidgetTree->ConstructWidget<USizeBox>(); BoxSize->SetWidthOverride(16.f); BoxSize->SetHeightOverride(16.f);
	C.Box = MakePanel(FLinearColor(0, 0, 0, 0), bRadio ? 8.f : 3.f, FMargin(0));
	BoxSize->SetContent(C.Box);
	H->AddChildToHorizontalBox(BoxSize)->SetVerticalAlignment(VAlign_Center);
	C.Label = MakeText(Label, Indent > 0 ? 15 : 16, HudStyle::Text, Indent == 0);
	UHorizontalBoxSlot* LS = H->AddChildToHorizontalBox(C.Label);
	LS->SetPadding(FMargin(9, 0, 10, 0)); LS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); LS->SetVerticalAlignment(VAlign_Center);
	if (!Key.IsEmpty())
	{
		UBorder* Cap = MakePanel(FLinearColor(1, 1, 1, 0.10f), 4.f, FMargin(5, 0, 5, 0));
		Cap->SetContent(MakeText(Key, 12, HudStyle::Dim, true));
		H->AddChildToHorizontalBox(Cap)->SetVerticalAlignment(VAlign_Center);
	}
	USizeBox* RowBox = WidgetTree->ConstructWidget<USizeBox>(); RowBox->SetWidthOverride(BuildWidth);
	RowBox->SetContent(H);
	C.Row->SetContent(RowBox);
	V->AddChildToVerticalBox(C.Row)->SetPadding(FMargin(0, 1, 0, 1));
	C.Owner = BuildOwner ? BuildOwner : ControlPanel.Get();
	C.bRadio = bRadio;
	C.Fn = MoveTemp(Fn); C.On = MoveTemp(On); C.Enabled = MoveTemp(Enabled);
	return Clicks.Add(MoveTemp(C));
}

namespace
{
	enum class ESol : uint8 { None, Native, Custom, Wave, Steam };
	ESol SolOf(const FPipelineConfig& C)
	{
		if (C.Solution == ESolution::WaveBake) return ESol::Wave;
		if (C.Solution == ESolution::SteamAudio) return ESol::Steam;
		if (C.bNative) return ESol::Native;
		if (C.bMultiRay || C.bPortal || C.bTransmission) return ESol::Custom;
		return ESol::None;
	}
	void ClearSolution(FPipelineConfig& C)
	{
		C.Solution = ESolution::Custom;
		C.bNative = C.bMultiRay = C.bPortal = C.bExcess = C.bTransmission = false;
		C.bDynamicDoors = C.bSteamPlusPortal = false;
	}
}

void UOcclusionHUDWidget::BuildControls(UVerticalBox* V)
{
	LastCustom.bMultiRay = LastCustom.bPortal = LastCustom.bExcess = LastCustom.bTransmission = true;

	auto Header = [&](const TCHAR* Title, const TCHAR* Sub, float Top)
	{
		UHorizontalBox* H = WidgetTree->ConstructWidget<UHorizontalBox>();
		H->AddChildToHorizontalBox(MakeText(Title, 15, HudStyle::Accent, true))->SetVerticalAlignment(VAlign_Bottom);
		H->AddChildToHorizontalBox(MakeText(Sub, 12, HudStyle::Dim))->SetPadding(FMargin(8, 0, 0, 1));
		V->AddChildToVerticalBox(H)->SetPadding(FMargin(4, Top, 0, 4));
	};
	auto Cur = [this]() { UAcousticSubsystem* S = Sys(); return S ? S->GetConfig() : FPipelineConfig(); };
	auto Apply = [this](const FPipelineConfig& C)
	{
		if (UAcousticSubsystem* S = Sys())
		{
			S->SetConfig(C);
			if (SolOf(C) == ESol::Custom) { LastCustom.bMultiRay = C.bMultiRay; LastCustom.bPortal = C.bPortal; LastCustom.bExcess = C.bExcess; LastCustom.bTransmission = C.bTransmission; }
		}
	};
	auto Always = []() { return true; };
	// Switch to the custom pipeline (restoring the last layer set) before changing one of its layers.
	auto EnsureCustom = [this](FPipelineConfig& C)
	{
		if (SolOf(C) == ESol::Custom) return;
		ClearSolution(C);
		C.bMultiRay = LastCustom.bMultiRay; C.bPortal = LastCustom.bPortal; C.bExcess = LastCustom.bExcess; C.bTransmission = LastCustom.bTransmission;
	};

	ControlHint = MakeText(TEXT(""), 12, HudStyle::Dim);
	Header(TEXT("处理方案"), TEXT("单选，子项可多选"), 0.f);

	AddClick(V, 0, true, TEXT("不处理遮挡"), TEXT("1"),
		[=]() { FPipelineConfig C = Cur(); ClearSolution(C); Apply(C); },
		[=]() { return SolOf(Cur()) == ESol::None; }, Always);
	AddClick(V, 0, true, TEXT("UE 原生遮挡"), TEXT("2"),
		[=]() { FPipelineConfig C = Cur(); ClearSolution(C); C.bNative = true; Apply(C); },
		[=]() { return SolOf(Cur()) == ESol::Native; }, Always);
	AddClick(V, 0, true, TEXT("自研管线"), TEXT("3~8"),
		[=]() { FPipelineConfig C = Cur(); EnsureCustom(C); if (!C.bMultiRay && !C.bPortal) { C.bMultiRay = C.bPortal = C.bExcess = C.bTransmission = true; } Apply(C); },
		[=]() { return SolOf(Cur()) == ESol::Custom; }, Always);
	auto CustomOn = [=]() { return SolOf(Cur()) == ESol::Custom; };
	AddClick(V, 1, false, TEXT("多射线遮挡"), TEXT(""),
		[=]() { FPipelineConfig C = Cur(); EnsureCustom(C); C.bMultiRay = !C.bMultiRay; Apply(C); },
		[=]() { return CustomOn() && Cur().bMultiRay; }, CustomOn);
	AddClick(V, 1, false, TEXT("房间—门户路径"), TEXT(""),
		[=]() { FPipelineConfig C = Cur(); EnsureCustom(C); C.bPortal = !C.bPortal; Apply(C); },
		[=]() { return CustomOn() && Cur().bPortal; }, CustomOn);
	auto PortalOn = [=]() { return CustomOn() && Cur().bPortal; };
	AddClick(V, 2, true, TEXT("衍射：按拐角角度"), TEXT(""),
		[=]() { FPipelineConfig C = Cur(); EnsureCustom(C); C.bPortal = true; C.bExcess = false; Apply(C); },
		[=]() { return PortalOn() && !Cur().bExcess; }, PortalOn);
	AddClick(V, 2, true, TEXT("衍射：按程差分频段"), TEXT(""),
		[=]() { FPipelineConfig C = Cur(); EnsureCustom(C); C.bPortal = true; C.bExcess = true; Apply(C); },
		[=]() { return PortalOn() && Cur().bExcess; }, PortalOn);
	AddClick(V, 2, false, TEXT("穿墙透射"), TEXT(""),
		[=]() { FPipelineConfig C = Cur(); EnsureCustom(C); C.bPortal = true; C.bTransmission = !C.bTransmission; Apply(C); },
		[=]() { return PortalOn() && Cur().bTransmission; }, PortalOn);
	AddClick(V, 0, true, TEXT("波动烘焙"), TEXT("9"),
		[=]() { FPipelineConfig C = Cur(); ClearSolution(C); C.Solution = ESolution::WaveBake; Apply(C); },
		[=]() { return SolOf(Cur()) == ESol::Wave; }, Always);
	AddClick(V, 1, false, TEXT("叠加关门衰减"), TEXT(""),
		[=]() { FPipelineConfig C = Cur(); const bool b = !(SolOf(C) == ESol::Wave && C.bDynamicDoors); ClearSolution(C); C.Solution = ESolution::WaveBake; C.bDynamicDoors = b; Apply(C); },
		[=]() { const FPipelineConfig C = Cur(); return SolOf(C) == ESol::Wave && C.bDynamicDoors; }, [=]() { return SolOf(Cur()) == ESol::Wave; });
	AddClick(V, 0, true, TEXT("Steam Audio"), TEXT("0"),
		[=]() { FPipelineConfig C = Cur(); ClearSolution(C); C.Solution = ESolution::SteamAudio; Apply(C); },
		[=]() { return SolOf(Cur()) == ESol::Steam; }, Always);
	AddClick(V, 1, false, TEXT("叠加自研门户路径"), TEXT(""),
		[=]() { FPipelineConfig C = Cur(); const bool b = !(SolOf(C) == ESol::Steam && C.bSteamPlusPortal); ClearSolution(C); C.Solution = ESolution::SteamAudio;
			C.bSteamPlusPortal = b; C.bPortal = b; C.bExcess = b; Apply(C); },
		[=]() { const FPipelineConfig C = Cur(); return SolOf(C) == ESol::Steam && C.bSteamPlusPortal; }, [=]() { return SolOf(Cur()) == ESol::Steam; });

	Header(TEXT("通用层"), TEXT("对所有方案生效"), 10.f);
	AddClick(V, 0, false, TEXT("房间混响耦合"), TEXT(""),
		[=]() { FPipelineConfig C = Cur(); C.bReverb = !C.bReverb; Apply(C); },
		[=]() { return Cur().bReverb; }, Always);
	AddClick(V, 0, false, TEXT("时间平滑"), TEXT(""),
		[=]() { FPipelineConfig C = Cur(); C.bSmoothing = !C.bSmoothing; Apply(C); },
		[=]() { return Cur().bSmoothing; }, Always);

	Header(TEXT("对照组"), TEXT("点组号进入，再点退出"), 10.f);
	{
		UHorizontalBox* H = WidgetTree->ConstructWidget<UHorizontalBox>();
		for (int32 g = 0; g < 6; ++g)
		{
			FHudClick C;
			C.Row = MakePanel(FLinearColor(1, 1, 1, 0.08f), 6.f, FMargin(0));
			USizeBox* SB = WidgetTree->ConstructWidget<USizeBox>(); SB->SetWidthOverride(36.f); SB->SetHeightOverride(28.f);
			C.Label = MakeText(FString::FromInt(g + 1), 15, HudStyle::Text, true);
			UBorder* Center = MakePanel(FLinearColor(0, 0, 0, 0), 0.f, FMargin(0));
			Center->SetHorizontalAlignment(HAlign_Center); Center->SetVerticalAlignment(VAlign_Center);
			Center->SetContent(C.Label);
			SB->SetContent(Center);
			C.Row->SetContent(SB);
			C.Row->SetHorizontalAlignment(HAlign_Center); C.Row->SetVerticalAlignment(VAlign_Center);
			H->AddChildToHorizontalBox(C.Row)->SetPadding(FMargin(0, 0, 6, 0));
			C.Owner = ControlPanel;
			C.Fn = [this, g]() { if (UAcousticSubsystem* S = Sys()) { if (S->GetGroup() == g) S->SetConfig(S->GetConfig()); else S->SetGroup(g); } };
			C.On = [this, g]() { UAcousticSubsystem* S = Sys(); return S && S->GetGroup() == g; };
			C.Enabled = Always;
			Clicks.Add(MoveTemp(C));
		}
		V->AddChildToVerticalBox(H)->SetPadding(FMargin(6, 0, 0, 6));
	}
	USizeBox* HintBox = WidgetTree->ConstructWidget<USizeBox>(); HintBox->SetWidthOverride(262.f);
	ControlHint->SetAutoWrapText(true);
	HintBox->SetContent(ControlHint);
	V->AddChildToVerticalBox(HintBox)->SetPadding(FMargin(6, 4, 0, 0));
}

void UOcclusionHUDWidget::UpdateControls()
{
	for (int32 i = 0; i < Clicks.Num(); ++i)
	{
		FHudClick& C = Clicks[i];
		const bool bOn = C.On ? C.On() : false;
		const bool bEn = C.Enabled ? C.Enabled() : true;
		const bool bHov = (i == Hover);
		if (C.Box)
		{
			const FLinearColor Fill = bOn ? (C.bRadio ? HudStyle::Accent : HudStyle::Teal) : FLinearColor(0, 0, 0, 0);
			const FLinearColor Edge = bOn ? Fill : (bEn ? HudStyle::Text * FLinearColor(1, 1, 1, 0.7f) : HudStyle::Dim * FLinearColor(1, 1, 1, 0.45f));
			C.Box->SetBrush(FSlateRoundedBoxBrush(Fill, C.bRadio ? 8.f : 3.f, Edge, 1.6f));
			C.Row->SetBrush(FSlateRoundedBoxBrush(bHov ? FLinearColor(1, 1, 1, 0.10f) : (bOn && C.bRadio ? HudStyle::AccentBg : FLinearColor(0, 0, 0, 0)), 6.f));
		}
		else
		{
			C.Row->SetBrush(FSlateRoundedBoxBrush(bOn ? HudStyle::AccentBg : (bHov ? FLinearColor(1, 1, 1, 0.16f) : FLinearColor(1, 1, 1, 0.07f)), 6.f));
		}
		C.Label->SetColorAndOpacity(FSlateColor(bOn && (C.bRadio || !C.Box) ? HudStyle::Accent : (bEn ? HudStyle::Text : HudStyle::Dim * FLinearColor(1, 1, 1, 0.6f))));
	}
	if (UAcousticSubsystem* S = Sys())
	{
		ControlHint->SetText(FText::FromString(S->GetGroup() >= 0
			? FString(TEXT("对照组进行中：上面的勾选显示当前方案的组成；改任何一项会退出对照。"))
			: FString(TEXT("灰色子项属于未选中的方案，点它会切到对应方案。数字键 1~0 是预设组合。"))));
	}
}

int32 UOcclusionHUDWidget::ClickAt(const FVector2D& ScreenPos) const
{
	for (int32 i = 0; i < Clicks.Num(); ++i)
	{
		const FHudClick& C = Clicks[i];
		if (!C.Row || (C.Owner && !C.Owner->IsVisible()) || !C.Row->IsVisible()) continue;
		if (C.Row->GetCachedGeometry().IsUnderLocation(ScreenPos)) return i;
	}
	return INDEX_NONE;
}

FReply UOcclusionHUDWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const int32 I = ClickAt(InMouseEvent.GetScreenSpacePosition());
		if (Clicks.IsValidIndex(I) && Clicks[I].Fn)
		{
			Clicks[I].Fn();
			UpdateControls();
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

FReply UOcclusionHUDWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Hover = ClickAt(InMouseEvent.GetScreenSpacePosition());
	return FReply::Unhandled();
}

void UOcclusionHUDWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Hover = INDEX_NONE;
	Super::NativeOnMouseLeave(InMouseEvent);
}

bool UOcclusionHUDWidget::SimulateClick(const FString& Label)
{
	const FHudClick* Found = nullptr;
	for (const FHudClick& C : Clicks) if (C.Label && C.Label->GetText().ToString() == Label) { Found = &C; break; }
	if (!Found) for (const FHudClick& C : Clicks) if (C.Label && C.Label->GetText().ToString().StartsWith(Label)) { Found = &C; break; }
	if (Found)
	{
		const FHudClick& C = *Found;
		const FVector2D P = C.Row->GetCachedGeometry().GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f));
		FSlateApplication& App = FSlateApplication::Get();
		App.SetCursorPos(P);
		const TSet<FKey> NoKeys;
		const FPointerEvent Down(App.GetUserIndexForMouse(), FSlateApplication::CursorPointerIndex, P, P, NoKeys, EKeys::LeftMouseButton, 0, FModifierKeysState());
		const bool bHandled = App.ProcessMouseButtonDownEvent(nullptr, Down);
		const TSet<FKey> NoKeys2;
		const FPointerEvent Up(App.GetUserIndexForMouse(), FSlateApplication::CursorPointerIndex, P, P, NoKeys2, EKeys::LeftMouseButton, 0, FModifierKeysState());
		App.ProcessMouseButtonUpEvent(Up);
		return bHandled;
	}
	return false;
}

// ------------------------------------------------------------------------------------------------
// Wave bake visualisation panel (right column): pick the view with the mouse and read its legend.
// ------------------------------------------------------------------------------------------------

void UOcclusionHUDWidget::BuildVizPanel()
{
	VizPanel = MakePanel(HudStyle::Panel, 12.f, FMargin(14, 12, 14, 12));
	UVerticalBox* V = WidgetTree->ConstructWidget<UVerticalBox>();
	BuildOwner = VizPanel; BuildWidth = 300.f;
	auto WB = [this]() { UAcousticSubsystem* S = Sys(); return S ? S->WaveBake.Get() : nullptr; };
	auto Always = []() { return true; };
	auto HasData = [WB]() { AWaveBakeActor* W = WB(); return W && W->HasData(); };

	{
		UHorizontalBox* H = WidgetTree->ConstructWidget<UHorizontalBox>();
		H->AddChildToHorizontalBox(MakeText(TEXT("烘焙可视化"), 15, HudStyle::Accent, true))->SetVerticalAlignment(VAlign_Bottom);
		H->AddChildToHorizontalBox(MakeText(TEXT("点选查看"), 12, HudStyle::Dim))->SetPadding(FMargin(8, 0, 0, 1));
		V->AddChildToVerticalBox(H)->SetPadding(FMargin(4, 0, 0, 4));
	}
	AddClick(V, 0, false, TEXT("重新烘焙（高耗时，约 1~3 分钟）"), TEXT("K"),
		[this, WB]() { if (AWaveBakeActor* W = WB()) { if (W->IsBaking()) return; W->StartBake(); ShowToast(TEXT("开始重新烘焙：约 1~3 分钟，期间按自研管线播放")); } },
		[WB]() { AWaveBakeActor* W = WB(); return W && W->IsBaking(); },
		[WB]() { AWaveBakeActor* W = WB(); return W && !W->IsBaking(); });
	AddClick(V, 0, false, TEXT("显示全部探针"), TEXT("P"),
		[WB]() { if (AWaveBakeActor* W = WB()) W->SetProbesVisible(!W->AreProbesVisible()); },
		[WB]() { AWaveBakeActor* W = WB(); return W && W->AreProbesVisible(); }, Always);
	struct FView { EWaveSliceView V; const TCHAR* Label; const TCHAR* Key; };
	const FView Views[] = {
		{ EWaveSliceView::Off, TEXT("不显示切片"), TEXT("") },
		{ EWaveSliceView::Voxels, TEXT("体素：求解器眼中的墙"), TEXT("L") },
		{ EWaveSliceView::Arrival, TEXT("首达时间"), TEXT("") },
		{ EWaveSliceView::Lds, TEXT("直达声响度"), TEXT("") },
		{ EWaveSliceView::Ler, TEXT("早期反射响度"), TEXT("") },
		{ EWaveSliceView::Ratio, TEXT("直达被挡程度"), TEXT("") },
		{ EWaveSliceView::Replay, TEXT("波场回放"), TEXT("O") } };
	for (const FView& Vw : Views)
	{
		const EWaveSliceView Mode = Vw.V;
		AddClick(V, 0, true, Vw.Label, Vw.Key,
			[this, WB, Mode]() { if (AWaveBakeActor* W = WB()) ShowToast(W->SetSliceView(Mode)); },
			[WB, Mode]() { AWaveBakeActor* W = WB(); return W && W->GetSliceView() == Mode; },
			Mode == EWaveSliceView::Off || Mode == EWaveSliceView::Voxels ? TFunction<bool()>(Always)
				: Mode == EWaveSliceView::Replay ? TFunction<bool()>([WB]() { AWaveBakeActor* W = WB(); return W && W->HasReplay(); }) : TFunction<bool()>(HasData));
	}
	BuildOwner = nullptr; BuildWidth = 262.f;

	// Legend
	LegendTitle = MakeText(TEXT(""), 15, HudStyle::Text, true);
	V->AddChildToVerticalBox(LegendTitle)->SetPadding(FMargin(4, 10, 0, 4));
	{
		UVerticalBox* Scale = WidgetTree->ConstructWidget<UVerticalBox>();
		RampBar = WidgetTree->ConstructWidget<UHorizontalBox>();
		ReplayBar = WidgetTree->ConstructWidget<UHorizontalBox>();
		const int32 Seg = 30;
		for (int32 i = 0; i < Seg; ++i)
		{
			const float T = (i + 0.5f) / Seg;
			USizeBox* A = WidgetTree->ConstructWidget<USizeBox>(); A->SetWidthOverride(296.f / Seg); A->SetHeightOverride(12.f);
			A->SetContent(MakePanel(AWaveBakeActor::RampColor(T), 0.f, FMargin(0)));
			RampBar->AddChildToHorizontalBox(A);
			const float S = T * 2.f - 1.f, M = FMath::Pow(FMath::Abs(S), 0.6f);
			const FLinearColor RC = S >= 0.f ? FLinearColor(FColor(255, uint8(160 + 60 * (1 - M)), uint8(80 * (1 - M))))
			                                : FLinearColor(FColor(uint8(60 * (1 - M)), uint8(140 + 60 * (1 - M)), 255));
			USizeBox* B = WidgetTree->ConstructWidget<USizeBox>(); B->SetWidthOverride(296.f / Seg); B->SetHeightOverride(12.f);
			B->SetContent(MakePanel(RC * FLinearColor(1, 1, 1, 0.25f + 0.75f * M), 0.f, FMargin(0)));
			ReplayBar->AddChildToHorizontalBox(B);
		}
		Scale->AddChildToVerticalBox(RampBar);
		Scale->AddChildToVerticalBox(ReplayBar);
		UHorizontalBox* Ends = WidgetTree->ConstructWidget<UHorizontalBox>();
		LegendLo = MakeText(TEXT(""), 12, HudStyle::Dim);
		LegendHi = MakeText(TEXT(""), 12, HudStyle::Dim);
		UHorizontalBoxSlot* LS = Ends->AddChildToHorizontalBox(LegendLo); LS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Ends->AddChildToHorizontalBox(LegendHi);
		Scale->AddChildToVerticalBox(Ends)->SetPadding(FMargin(0, 2, 0, 0));
		LegendScale = Scale;
		V->AddChildToVerticalBox(Scale)->SetPadding(FMargin(4, 0, 0, 6));
	}
	{
		USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(); Box->SetWidthOverride(300.f);
		LegendRead = MakeText(TEXT(""), 13, HudStyle::Dim);
		LegendRead->SetAutoWrapText(true);
		Box->SetContent(LegendRead);
		V->AddChildToVerticalBox(Box)->SetPadding(FMargin(4, 0, 0, 0));
	}
	VizPanel->SetContent(V);

	// Floating tag above the probe the slice comes from.
	ProbeTag = MakePanel(FLinearColor(0.05f, 0.05f, 0.06f, 0.85f), 7.f, FMargin(9, 3, 9, 4));
	ProbeTagText = MakeText(TEXT(""), 13, HudStyle::Accent, true);
	ProbeTag->SetContent(ProbeTagText);
	UCanvasPanelSlot* TS = Root->AddChildToCanvas(ProbeTag);
	TS->SetAutoSize(true); TS->SetAlignment(FVector2D(0.5f, 1.f));
	ProbeTag->SetVisibility(ESlateVisibility::Collapsed);
}

void UOcclusionHUDWidget::UpdateViz(AOcclusionPlayerController* PC)
{
	UAcousticSubsystem* S = Sys();
	AWaveBakeActor* W = S ? S->WaveBake.Get() : nullptr;
	const bool bShow = W && !PC->bHideUI && (S->GetConfig().Solution == ESolution::WaveBake || W->GetSliceView() != EWaveSliceView::Off || W->AreProbesVisible());
	VizPanel->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	ProbeTag->SetVisibility(ESlateVisibility::Collapsed);
	if (!bShow) return;
	const AWaveBakeActor::FLegend L = W->GetLegend();
	LegendTitle->SetText(FText::FromString(L.Title));
	LegendRead->SetText(FText::FromString(L.Read));
	LegendLo->SetText(FText::FromString(L.Lo));
	LegendHi->SetText(FText::FromString(L.Hi));
	LegendScale->SetVisibility(L.bRamp || L.bReplay ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	RampBar->SetVisibility(L.bReplay ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	ReplayBar->SetVisibility(L.bReplay ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

	FVector P; int32 Index = -1;
	FVector2D Screen;
	if (W->GetShownProbe(P, Index) && UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, P + FVector(0, 0, 40.f), Screen, false))
	{
		ProbeTagText->SetText(FText::FromString(W->GetSliceView() == EWaveSliceView::Replay
			? FString::Printf(TEXT("探针 #%d · 脉冲从这里发出"), Index + 1)
			: FString::Printf(TEXT("探针 #%d · 切片数据来自这里"), Index + 1)));
		if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(ProbeTag->Slot)) CS->SetPosition(Screen);
		ProbeTag->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}
