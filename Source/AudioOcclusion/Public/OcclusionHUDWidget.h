#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "AcousticSubsystem.h"
#include "OcclusionHUDWidget.generated.h"

class UCanvasPanel;
class UTextBlock;
class UVerticalBox;
class UHorizontalBox;
class UBorder;
class USizeBox;
class AOcclusionPlayerController;
class UAcousticSubsystem;

/** One clickable row (radio, checkbox or chip) on the control panel. */
struct FHudClick
{
	UBorder* Row = nullptr;
	UBorder* Box = nullptr;          // indicator (radio dot / check box); null for chips and group rows
	UTextBlock* Label = nullptr;
	UWidget* Owner = nullptr;        // panel that must be visible for the row to take clicks
	bool bRadio = false;
	TFunction<void()> Fn;
	TFunction<bool()> On;
	TFunction<bool()> Enabled;
};

/** Game-style HUD built entirely in C++ (no UMG asset). */
UCLASS()
class AUDIOOCCLUSION_API UOcclusionHUDWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	void Refresh(AOcclusionPlayerController* PC);
	void ShowToast(const FString& Msg);
	/** Self-test: click the row with this label through Slate (real hit test). Returns true if the HUD took the click. */
	bool SimulateClick(const FString& Label);

protected:
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Root;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ModeLine;
	UPROPERTY(Transient) TObjectPtr<UHorizontalBox> Chips;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> StepList;
	UPROPERTY(Transient) TObjectPtr<UBorder> ControlPanel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ControlHint;
	UPROPERTY(Transient) TObjectPtr<UBorder> GroupPanel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> GroupTitle;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> GroupQuestion;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> GroupRows;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> GroupTexts;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Explain;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> EmitterName;
	UPROPERTY(Transient) TObjectPtr<UBorder> EmitterDot;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> EmitterInfo;
	UPROPERTY(Transient) TArray<TObjectPtr<USizeBox>> BandBars;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> BandTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> BandLabels;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> EmitterNote;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusLine;
	UPROPERTY(Transient) TObjectPtr<UBorder> ToastBox;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ToastText;
	UPROPERTY(Transient) TObjectPtr<UBorder> BakeBox;
	UPROPERTY(Transient) TObjectPtr<USizeBox> BakeBar;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BakeText;
	UPROPERTY(Transient) TObjectPtr<UBorder> HelpBox;
	UPROPERTY(Transient) TObjectPtr<UBorder> KeysBox;
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> AllPanels;

	UPROPERTY(Transient) TObjectPtr<UBorder> VizPanel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> LegendTitle;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> LegendRead;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> LegendLo;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> LegendHi;
	UPROPERTY(Transient) TObjectPtr<UWidget> LegendScale;
	UPROPERTY(Transient) TObjectPtr<UHorizontalBox> RampBar;
	UPROPERTY(Transient) TObjectPtr<UHorizontalBox> ReplayBar;
	UPROPERTY(Transient) TObjectPtr<UBorder> ProbeTag;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ProbeTagText;
	UWidget* BuildOwner = nullptr;
	float BuildWidth = 262.f;
	void BuildVizPanel();
	void UpdateViz(class AOcclusionPlayerController* PC);
	TArray<FHudClick> Clicks;
	int32 Hover = -1;
	FPipelineConfig LastCustom;
	UAcousticSubsystem* Sys() const;
	void BuildControls(UVerticalBox* V);
	int32 AddClick(UVerticalBox* V, int32 Indent, bool bRadio, const FString& Label, const FString& Key, TFunction<void()> Fn, TFunction<bool()> On, TFunction<bool()> Enabled);
	void UpdateControls();
	int32 ClickAt(const FVector2D& ScreenPos) const;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;

	float ToastTime = 0.f;
	FString LastChips;
	UTextBlock* MakeText(const FString& S, int32 Size, const FLinearColor& C, bool bBold = false);
	UBorder* MakePanel(const FLinearColor& Bg, float Radius, const FMargin& Pad);
	UWidget* KeyCap(const FString& Key, const FString& What);
	void BuildTree();
};
