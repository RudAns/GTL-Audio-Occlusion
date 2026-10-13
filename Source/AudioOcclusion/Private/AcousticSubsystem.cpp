#include "AcousticSubsystem.h"
#include "AcousticEmitterComponent.h"
#include "AcousticRoom.h"
#include "AcousticPortal.h"
#include "WaveBakeActor.h"
#include "Components/AudioComponent.h"
#include "AudioDevice.h"
#include "ActiveSound.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundSubmix.h"
#include "SubmixEffects/AudioMixerSubmixEffectReverb.h"
#include "SubmixEffects/SubmixEffectFilter.h"
#include "SteamAudioOcclusionSettings.h"
#include "SteamAudioSpatializationSettings.h"
#include "SteamAudioSourceComponent.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogAcoustic, Log, All);

static constexpr float SolveRateHz = 15.f;
static constexpr float SpeedOfSound = 343.f;

// --------------------------------------------------------------------------------------------------------------
// Modes
// --------------------------------------------------------------------------------------------------------------

FPipelineConfig UAcousticSubsystem::ConfigForStep(EOcclusionStep S)
{
	FPipelineConfig C;
	const int32 N = (int32)S;
	switch (S)
	{
	case EOcclusionStep::None: break;
	case EOcclusionStep::UENative: C.bNative = true; break;
	case EOcclusionStep::WaveBake: C.Solution = ESolution::WaveBake; C.bReverb = true; C.bSmoothing = true; break;
	case EOcclusionStep::SteamAudio: C.Solution = ESolution::SteamAudio; C.bReverb = true; C.bSmoothing = true; break;
	default:
		C.bMultiRay = N >= (int32)EOcclusionStep::MultiRay;
		C.bPortal = N >= (int32)EOcclusionStep::PortalPath;
		C.bExcess = N >= (int32)EOcclusionStep::ExcessMapping;
		C.bTransmission = N >= (int32)EOcclusionStep::Transmission;
		C.bReverb = N >= (int32)EOcclusionStep::ReverbCoupling;
		C.bSmoothing = N >= (int32)EOcclusionStep::Smoothing;
		break;
	}
	return C;
}

FString UAcousticSubsystem::StepTitle(EOcclusionStep S)
{
	switch (S)
	{
	case EOcclusionStep::None: return TEXT("仅距离衰减");
	case EOcclusionStep::UENative: return TEXT("UE 原生遮挡");
	case EOcclusionStep::MultiRay: return TEXT("多射线比例");
	case EOcclusionStep::PortalPath: return TEXT("房间—门户路径");
	case EOcclusionStep::ExcessMapping: return TEXT("程差频率映射");
	case EOcclusionStep::Transmission: return TEXT("穿墙透射");
	case EOcclusionStep::ReverbCoupling: return TEXT("房间混响耦合");
	case EOcclusionStep::Smoothing: return TEXT("时间平滑");
	case EOcclusionStep::WaveBake: return TEXT("波动烘焙");
	case EOcclusionStep::SteamAudio: return TEXT("Steam Audio");
	default: return TEXT("");
	}
}

FString UAcousticSubsystem::StepExplain(EOcclusionStep S)
{
	switch (S)
	{
	case EOcclusionStep::None: return TEXT("只有距离衰减。隔着墙的声音和身边的一样清楚，方向始终指向声源本身。");
	case EOcclusionStep::UENative: return TEXT("引擎自带：每个声源向听者打一条射线，挡住就整体衰减并低通。沿墙角走动时会在“挡住/没挡住”之间来回跳。");
	case EOcclusionStep::MultiRay: return TEXT("在声源周围取 12 个点分别打射线，按被挡住的比例连续衰减，跳变消失；但声像仍在声源本身，隔一堵墙和隔三个房间无法区分。");
	case EOcclusionStep::PortalPath: return TEXT("在房间—门户图上找出声音实际经过的门洞，把声音移到门口方向、按整条路径的长度衰减。关上门，路径会自动改走别的门。");
	case EOcclusionStep::ExcessMapping: return TEXT("绕射损失按路径比直线多走的距离（程差）分频段计算：拐得越急、离门越远，高频损失越多。对比上一步只按拐弯角度计算。");
	case EOcclusionStep::Transmission: return TEXT("加入穿墙分量：按墙体材质分频段计算传声损失，与门洞路径按能量叠加。木隔墙透声明显，混凝土墙几乎不透。");
	case EOcclusionStep::ReverbCoupling: return TEXT("每个房间有自己的混响，声源只激发所在房间的混响，再经门洞传到你所在的房间。隔壁的声音会带着隔壁的空间感。");
	case EOcclusionStep::Smoothing: return TEXT("参数平滑、路径切换迟滞与交叉淡化。关掉它，关门或绕过门框时能听到明显的跳变。");
	case EOcclusionStep::WaveBake: return TEXT("用 GPU 对整栋房子做波动模拟后烘焙的参数：直达声响度、早期反射响度、首达时间与到达方向。按 K 开始烘焙，P / L / O 查看探针、参数切片和波场回放。");
	case EOcclusionStep::SteamAudio: return TEXT("Valve Steam Audio 4.8.1 官方插件：体积遮挡、频率相关透射与双耳渲染，相当于直接上线的配置。它不知道门洞路径：隔房间的声音只剩穿墙分量（对照组 6 的 B 把自研门户路径叠加上去）。");
	default: return TEXT("");
	}
}

void UAcousticSubsystem::BuildGroups()
{
	Groups.Reset();
	auto C = [](EOcclusionStep S) { return ConfigForStep(S); };
	{
		FComparisonGroup G; G.Title = TEXT("1 · 基线"); G.Question = TEXT("二值切换与边缘抖动");
		G.Labels = { TEXT("仅距离衰减"), TEXT("UE 原生遮挡") };
		G.Configs = { C(EOcclusionStep::None), C(EOcclusionStep::UENative) };
		Groups.Add(G);
	}
	{
		FComparisonGroup G; G.Title = TEXT("2 · 遮挡量"); G.Question = TEXT("参数是否连续");
		G.Labels = { TEXT("UE 原生单射线"), TEXT("多射线比例 + 平滑") };
		FPipelineConfig B = C(EOcclusionStep::MultiRay); B.bSmoothing = true;
		G.Configs = { C(EOcclusionStep::UENative), B };
		Groups.Add(G);
	}
	{
		FComparisonGroup G; G.Title = TEXT("3 · 路径"); G.Question = TEXT("声音是否从正确的门口传来，关门后是否改道");
		G.Labels = { TEXT("多射线比例"), TEXT("房间—门户路径") };
		FPipelineConfig A = C(EOcclusionStep::MultiRay); A.bSmoothing = true;
		FPipelineConfig B = C(EOcclusionStep::PortalPath); B.bSmoothing = true;
		G.Configs = { A, B };
		Groups.Add(G);
	}
	{
		FComparisonGroup G; G.Title = TEXT("4 · 衍射映射"); G.Question = TEXT("同样的拐角，远近不同时衰减是否不同");
		G.Labels = { TEXT("按角度（Wwise 式）"), TEXT("按程差分频段") };
		FPipelineConfig A = C(EOcclusionStep::PortalPath); A.bSmoothing = true;
		FPipelineConfig B = C(EOcclusionStep::ExcessMapping); B.bSmoothing = true;
		G.Configs = { A, B };
		Groups.Add(G);
	}
	{
		FComparisonGroup G; G.Title = TEXT("5 · 方案级"); G.Question = TEXT("同一位置的听感、开销与制作成本");
		G.Labels = { TEXT("自研管线（全开）"), TEXT("波动烘焙"), TEXT("Steam Audio") };
		G.Configs = { C(EOcclusionStep::Smoothing), C(EOcclusionStep::WaveBake), C(EOcclusionStep::SteamAudio) };
		Groups.Add(G);
	}
	{
		FComparisonGroup G; G.Title = TEXT("6 · 组合"); G.Question = TEXT("不同技术叠加后是否一致、有无重复计算");
		G.Labels = { TEXT("波动烘焙 + 动态门户"), TEXT("Steam Audio + 自研门户路径") };
		FPipelineConfig A = C(EOcclusionStep::WaveBake); A.bDynamicDoors = true;
		FPipelineConfig B = C(EOcclusionStep::SteamAudio); B.bSteamPlusPortal = true; B.bPortal = true; B.bExcess = true;
		G.Configs = { A, B };
		Groups.Add(G);
	}
}

void UAcousticSubsystem::SetStep(EOcclusionStep S)
{
	Step = S; Group = -1; Variant = 0;
	Config = ConfigForStep(S);
}

void UAcousticSubsystem::SetGroup(int32 Index)
{
	if (Groups.Num() == 0) BuildGroups();
	if (!Groups.IsValidIndex(Index)) { SetStep(Step); return; }
	Group = Index; Variant = 0;
	Config = Groups[Group].Configs[0];
}

void UAcousticSubsystem::SetVariant(int32 V)
{
	if (!Groups.IsValidIndex(Group) || !Groups[Group].Configs.IsValidIndex(V)) return;
	Variant = V;
	Config = Groups[Group].Configs[Variant];
}

void UAcousticSubsystem::SetConfig(const FPipelineConfig& C)
{
	Group = -1; Variant = 0;
	Config = C;
	const int32 M = MatchingStep();
	if (M >= 0) Step = (EOcclusionStep)M;
}

bool UAcousticSubsystem::SameConfig(const FPipelineConfig& A, const FPipelineConfig& B)
{
	return A.Solution == B.Solution && A.bNative == B.bNative && A.bMultiRay == B.bMultiRay && A.bPortal == B.bPortal && A.bExcess == B.bExcess
		&& A.bTransmission == B.bTransmission && A.bReverb == B.bReverb && A.bSmoothing == B.bSmoothing && A.bDynamicDoors == B.bDynamicDoors
		&& A.bSteamPlusPortal == B.bSteamPlusPortal;
}

int32 UAcousticSubsystem::MatchingStep() const
{
	for (int32 i = 0; i < (int32)EOcclusionStep::Count; ++i)
		if (SameConfig(Config, ConfigForStep((EOcclusionStep)i))) return i;
	return -1;
}

EOcclusionStep UAcousticSubsystem::RepresentativeStep(const FPipelineConfig& C)
{
	if (C.Solution == ESolution::WaveBake) return EOcclusionStep::WaveBake;
	if (C.Solution == ESolution::SteamAudio) return EOcclusionStep::SteamAudio;
	if (C.bNative) return EOcclusionStep::UENative;
	if (C.bTransmission) return EOcclusionStep::Transmission;
	if (C.bExcess && C.bPortal) return EOcclusionStep::ExcessMapping;
	if (C.bPortal) return EOcclusionStep::PortalPath;
	if (C.bMultiRay) return EOcclusionStep::MultiRay;
	if (C.bSmoothing) return EOcclusionStep::Smoothing;
	if (C.bReverb) return EOcclusionStep::ReverbCoupling;
	return EOcclusionStep::None;
}

void UAcousticSubsystem::CycleVariant()
{
	if (!Groups.IsValidIndex(Group)) return;
	Variant = (Variant + 1) % Groups[Group].Configs.Num();
	Config = Groups[Group].Configs[Variant];
}

TArray<FString> UAcousticSubsystem::ActiveLayerNames() const
{
	TArray<FString> N;
	if (Config.Solution == ESolution::WaveBake) { N.Add(TEXT("波动烘焙")); if (Config.bDynamicDoors) N.Add(TEXT("动态门户损失")); }
	else if (Config.Solution == ESolution::SteamAudio) { N.Add(TEXT("Steam Audio 遮挡+透射")); if (Config.bSteamPlusPortal) N.Add(TEXT("自研门户路径")); }
	else
	{
		if (Config.bNative) N.Add(TEXT("UE 原生射线"));
		if (Config.bMultiRay) N.Add(Config.bPortal ? TEXT("同房间多射线阻挡") : TEXT("多射线比例"));
		if (Config.bPortal) N.Add(TEXT("房间—门户路径"));
		if (Config.bPortal) N.Add(Config.bExcess ? TEXT("程差映射") : TEXT("角度映射"));
		if (Config.bTransmission) N.Add(TEXT("穿墙透射"));
	}
	if (Config.bReverb) N.Add(TEXT("房间混响"));
	if (Config.bSmoothing) N.Add(TEXT("平滑"));
	if (N.Num() == 0) N.Add(TEXT("仅距离衰减"));
	return N;
}

// --------------------------------------------------------------------------------------------------------------
// Setup
// --------------------------------------------------------------------------------------------------------------

void UAcousticSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	BuildGroups();
	SetStep(EOcclusionStep::Smoothing);
	Gather();
}

void UAcousticSubsystem::Deinitialize()
{
	Emitters.Reset();
	Super::Deinitialize();
}

void UAcousticSubsystem::RegisterEmitter(UAcousticEmitterComponent* E) { if (E) Emitters.AddUnique(E); }
void UAcousticSubsystem::UnregisterEmitter(UAcousticEmitterComponent* E) { Emitters.Remove(E); }

TArray<UAcousticEmitterComponent*> UAcousticSubsystem::GetEmitters() const
{
	TArray<UAcousticEmitterComponent*> Out;
	for (const auto& E : Emitters) if (E) Out.Add(E);
	return Out;
}

UAcousticEmitterComponent* UAcousticSubsystem::GetFocused() const
{
	return Emitters.IsValidIndex(FocusEmitter) ? Emitters[FocusEmitter].Get() : nullptr;
}

void UAcousticSubsystem::CycleFocus(int32 Dir)
{
	if (Emitters.Num() == 0) return;
	FocusEmitter = (FocusEmitter + Dir + Emitters.Num()) % Emitters.Num();
	const bool bAnySolo = Emitters.ContainsByPredicate([](const auto& E) { return E && E->bSolo; });
	if (bAnySolo) for (auto& E : Emitters) if (E) E->bSolo = (E == Emitters[FocusEmitter]);
}

void UAcousticSubsystem::ToggleSolo()
{
	UAcousticEmitterComponent* F = GetFocused();
	if (!F) return;
	const bool bNew = !F->bSolo;
	for (auto& E : Emitters) if (E) E->bSolo = false;
	F->bSolo = bNew;
}

void UAcousticSubsystem::Gather()
{
	Rooms.Reset(); Portals.Reset();
	for (TActorIterator<AAcousticRoom> It(GetWorld()); It; ++It) Rooms.Add(*It);
	for (TActorIterator<AAcousticPortal> It(GetWorld()); It; ++It) Portals.Add(*It);
	for (TActorIterator<AWaveBakeActor> It(GetWorld()); It; ++It) { WaveBake = *It; break; }
	Rooms.Sort([](const AAcousticRoom& A, const AAcousticRoom& B) { return A.Priority > B.Priority; });
	bGathered = true;
	UE_LOG(LogAcoustic, Log, TEXT("Acoustic world: %d rooms, %d portals"), Rooms.Num(), Portals.Num());
}

void UAcousticSubsystem::InitAudioGraph()
{
	UWorld* W = GetWorld();
	FAudioDevice* Device = W ? W->GetAudioDeviceRaw() : nullptr;
	// Steam Audio attenuation: same distance curve as the custom voices, plus the Steam Audio occlusion plugin.
	SteamAttenuation = NewObject<USoundAttenuation>(this, TEXT("SteamAttenuation"));
	FSoundAttenuationSettings& A = SteamAttenuation->Attenuation;
	A.bAttenuate = true; A.bSpatialize = true;
	A.SpatializationAlgorithm = ESoundSpatializationAlgorithm::SPATIALIZATION_HRTF;
	A.AttenuationShape = EAttenuationShape::Sphere;
	A.AttenuationShapeExtents = FVector(150.f, 0.f, 0.f);
	A.FalloffDistance = 4500.f;
	A.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
	A.dBAttenuationAtMax = -60.f;
	A.bEnableOcclusion = true;
	USteamAudioOcclusionSettings* Occ = NewObject<USteamAudioOcclusionSettings>(this, TEXT("SteamOcclusionSettings"));
	Occ->bApplyOcclusion = true;
	Occ->bApplyTransmission = true;
	Occ->TransmissionType = ETransmissionType::FREQUENCY_DEPENDENT;
	A.PluginSettings.OcclusionPluginSettingsArray.Add(Occ);
	USteamAudioSpatializationSettings* Spat = NewObject<USteamAudioSpatializationSettings>(this, TEXT("SteamSpatializationSettings"));
	A.PluginSettings.SpatializationPluginSettingsArray.Add(Spat);

	// One reverb submix per indoor room: [reverb -> low-pass]. The low-pass and the output volume model the coupling to the listener's room.
	for (AAcousticRoom* R : Rooms)
	{
		if (!R || R->bOutdoor || FParse::Param(FCommandLine::Get(), TEXT("OccNoRoomReverb"))) continue;
		USoundSubmix* Sub = NewObject<USoundSubmix>(this, *FString::Printf(TEXT("Reverb_%s"), *R->GetName()));
		USubmixEffectReverbPreset* Rev = NewObject<USubmixEffectReverbPreset>(Sub);
		FSubmixEffectReverbSettings RS;
		RS.DecayTime = R->DecayTime;
		RS.WetLevel = 1.f; RS.DryLevel = 0.f;
		RS.Density = 0.6f; RS.Diffusion = 0.85f;
		RS.ReflectionsGain = 0.12f; RS.LateGain = 1.0f;
		Rev->SetSettings(RS);
		USubmixEffectFilterPreset* Flt = NewObject<USubmixEffectFilterPreset>(Sub);
		FSubmixEffectFilterSettings FS; FS.FilterType = ESubmixFilterType::LowPass; FS.FilterFrequency = 20000.f; FS.FilterQ = 0.7f;
		Flt->SetSettings(FS);
		Sub->SubmixEffectChain.Add(Rev);
		Sub->SubmixEffectChain.Add(Flt);
		if (Device) Device->RegisterSoundSubmix(Sub, true);
		R->ReverbSubmix = Sub;
		RoomFilters.Add(R, Flt);
		RoomCouplingDb.Add(R, 0.f);
	}
	for (auto& E : Emitters) if (E) E->StartVoices(SteamAttenuation);
	bAudioStarted = true;
}

// --------------------------------------------------------------------------------------------------------------
// Geometry queries
// --------------------------------------------------------------------------------------------------------------

AAcousticRoom* UAcousticSubsystem::RoomAt(const FVector& P) const
{
	for (AAcousticRoom* R : Rooms) if (R && R->Contains(P)) return R;
	return nullptr;
}

AAcousticPortal* UAcousticSubsystem::NearestDoor(const FVector& P, float MaxDist) const
{
	AAcousticPortal* Best = nullptr; float BestD = MaxDist;
	for (AAcousticPortal* Po : Portals)
	{
		if (!Po || !Po->bHasDoor) continue;
		const float D = FVector::Dist2D(Po->GetCenter(), P);
		if (D < BestD) { BestD = D; Best = Po; }
	}
	return Best;
}

static FCollisionQueryParams AcousticParams(const AActor* Ignore, const UWorld* W)
{
	FCollisionQueryParams Q(SCENE_QUERY_STAT(AcousticTrace), false);
	if (Ignore) Q.AddIgnoredActor(Ignore);
	if (W) if (APlayerController* PC = W->GetFirstPlayerController()) if (APawn* P = PC->GetPawn()) Q.AddIgnoredActor(P);
	return Q;
}

bool UAcousticSubsystem::TraceBlocked(const FVector& A, const FVector& B, const AActor* IgnoreA) const
{
	FHitResult H;
	return GetWorld()->LineTraceSingleByChannel(H, A, B, ECC_GameTraceChannel1, AcousticParams(IgnoreA, GetWorld()));
}

float UAcousticSubsystem::MultiRayFraction(const UAcousticEmitterComponent* E, const FVector& S, const FVector& L, FAcousticResult* Out) const
{
	// 12 icosahedron directions, 40 cm around the source; samples poking through a wall are discarded (as Steam Audio does).
	static const FVector Dirs[12] = {
		FVector(0, 0.526f, 0.851f), FVector(0, -0.526f, 0.851f), FVector(0, 0.526f, -0.851f), FVector(0, -0.526f, -0.851f),
		FVector(0.526f, 0.851f, 0), FVector(-0.526f, 0.851f, 0), FVector(0.526f, -0.851f, 0), FVector(-0.526f, -0.851f, 0),
		FVector(0.851f, 0, 0.526f), FVector(-0.851f, 0, 0.526f), FVector(0.851f, 0, -0.526f), FVector(-0.851f, 0, -0.526f) };
	const AActor* Owner = E ? E->GetOwner() : nullptr;
	int32 Valid = 0, Blocked = 0;
	for (const FVector& D : Dirs)
	{
		const FVector P = S + D * 40.f;
		if (TraceBlocked(S, P, Owner)) continue;
		++Valid;
		const bool bB = TraceBlocked(P, L, Owner);
		Blocked += bB ? 1 : 0;
		if (Out) { Out->Rays.Add(TPair<FVector, FVector>(P, L)); Out->RayBlocked.Add(bB); }
	}
	return Valid > 0 ? float(Blocked) / Valid : (TraceBlocked(S, L, Owner) ? 1.f : 0.f);
}

static EAcousticMaterial MaterialOf(const UPrimitiveComponent* C)
{
	if (!C) return EAcousticMaterial::Concrete;
	if (C->ComponentHasTag(TEXT("TL_Wood"))) return EAcousticMaterial::WoodPartition;
	if (C->ComponentHasTag(TEXT("TL_Glass"))) return EAcousticMaterial::Glass;
	if (C->ComponentHasTag(TEXT("TL_Door"))) return EAcousticMaterial::Door;
	if (C->ComponentHasTag(TEXT("TL_Furniture"))) return EAcousticMaterial::Furniture;
	return EAcousticMaterial::Concrete;
}

FBand3 UAcousticSubsystem::TransmissionLoss(const FVector& S, const FVector& L, const AActor* Ignore, TArray<FVector>* OutHits) const
{
	// Walk the straight line, adding each crossed element once (dB sum = product of transmission coefficients), capped by flanking at 60 dB.
	FBand3 TL;
	FCollisionQueryParams Q = AcousticParams(Ignore, GetWorld());
	FVector From = S;
	for (int32 i = 0; i < 10; ++i)
	{
		FHitResult H;
		if (!GetWorld()->LineTraceSingleByChannel(H, From, L, ECC_GameTraceChannel1, Q)) break;
		UPrimitiveComponent* C = H.GetComponent();
		TL = TL + TransmissionLossDb(MaterialOf(C));
		if (OutHits) OutHits->Add(H.ImpactPoint);
		if (C) Q.AddIgnoredComponent(C);
		From = H.ImpactPoint;
	}
	for (int32 b = 0; b < 3; ++b) TL[b] = FMath::Min(TL[b], 60.f);
	return TL;
}

bool UAcousticSubsystem::FindPortalPath(const FVector& S, AAcousticRoom* RS, const FVector& L, AAcousticRoom* RL, TArray<FVector>& OutPts, TArray<AAcousticPortal*>& OutPortals) const
{
	OutPts.Reset(); OutPortals.Reset();
	if (!RS || !RL) return false;
	struct FNode { FVector P; AAcousticRoom* A; AAcousticRoom* B; AAcousticPortal* Portal; };
	TArray<FNode> N;
	N.Add({ S, RS, RS, nullptr });
	N.Add({ L, RL, RL, nullptr });
	for (AAcousticPortal* Po : Portals)
	{
		if (!Po || !Po->IsOpenEnough() || !Po->RoomA || !Po->RoomB) continue;
		N.Add({ Po->GetCenter() + FVector(0, 0, 160.f - Po->Height * 0.5f), Po->RoomA, Po->RoomB, Po });
	}
	auto Share = [](const FNode& X, const FNode& Y)
	{
		return X.A == Y.A || X.A == Y.B || X.B == Y.A || X.B == Y.B;
	};
	const int32 Num = N.Num();
	TArray<float> Dist; Dist.Init(TNumericLimits<float>::Max(), Num);
	TArray<int32> Prev; Prev.Init(-1, Num);
	TArray<bool> Done; Done.Init(false, Num);
	Dist[0] = 0.f;
	for (int32 It = 0; It < Num; ++It)
	{
		int32 U = -1;
		for (int32 i = 0; i < Num; ++i) if (!Done[i] && (U < 0 || Dist[i] < Dist[U])) U = i;
		if (U < 0 || Dist[U] == TNumericLimits<float>::Max()) break;
		Done[U] = true;
		if (U == 1) break;
		for (int32 V = 0; V < Num; ++V)
		{
			if (Done[V] || V == U) continue;
			// Start node only connects through its own room; the listener node likewise.
			if (!Share(N[U], N[V])) continue;
			if (U == 0 && V == 1 && RS != RL) continue;
			const float D = Dist[U] + FVector::Dist(N[U].P, N[V].P);
			if (D < Dist[V]) { Dist[V] = D; Prev[V] = U; }
		}
	}
	if (Prev[1] < 0) return false;
	TArray<int32> Chain;
	for (int32 V = 1; V >= 0; V = Prev[V]) Chain.Insert(V, 0);
	for (int32 V : Chain) { OutPts.Add(N[V].P); if (N[V].Portal) OutPortals.Add(N[V].Portal); }
	// Pull the path taut through each aperture: intersect the neighbour segment with the portal plane, clamp to the opening.
	for (int32 Pass = 0; Pass < 3; ++Pass)
	{
		for (int32 i = 1; i + 1 < OutPts.Num(); ++i)
		{
			AAcousticPortal* Po = OutPortals[i - 1];
			const FVector A = OutPts[i - 1], B = OutPts[i + 1];
			const FVector Nrm = Po->GetNormal();
			const float DA = FVector::DotProduct(A - Po->GetCenter(), Nrm), DB = FVector::DotProduct(B - Po->GetCenter(), Nrm);
			FVector X = OutPts[i];
			if (FMath::Abs(DA - DB) > 1.f) { const float T = DA / (DA - DB); X = A + (B - A) * FMath::Clamp(T, 0.f, 1.f); }
			OutPts[i] = Po->ClosestPointOnOpening(X);
		}
	}
	return true;
}

float UAcousticSubsystem::KurzeAndersonDb(float ExcessM, float FreqHz)
{
	// Closed-form fit of Maekawa's barrier chart (report ch. 1, 6.2), capped at 24 dB.
	const float Nf = 2.f * ExcessM * FreqHz / SpeedOfSound;
	if (Nf <= 0.f) return 0.f;
	const float T = FMath::Sqrt(2.f * PI * Nf);
	return FMath::Min(5.f + 20.f * FMath::LogX(10.f, T / FMath::Tanh(T)), 24.f);
}

static float PathLengthCm(const TArray<FVector>& P)
{
	float L = 0.f; for (int32 i = 1; i < P.Num(); ++i) L += FVector::Dist(P[i - 1], P[i]); return L;
}

static float BendDegrees(const TArray<FVector>& P)
{
	float Sum = 0.f;
	for (int32 i = 1; i + 1 < P.Num(); ++i)
	{
		const FVector A = (P[i] - P[i - 1]).GetSafeNormal(), B = (P[i + 1] - P[i]).GetSafeNormal();
		Sum += FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(A, B), -1.f, 1.f)));
	}
	return Sum;
}

// --------------------------------------------------------------------------------------------------------------
// Solve
// --------------------------------------------------------------------------------------------------------------

void UAcousticSubsystem::UpdateListener()
{
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		FVector Loc, Fwd, Right;
		PC->GetAudioListenerPosition(Loc, Fwd, Right);
		ListenerPos = Loc; ListenerFwd = Fwd;
	}
	ListenerRoom = RoomAt(ListenerPos);
}

void UAcousticSubsystem::Solve(UAcousticEmitterComponent* E, FAcousticResult& R)
{
	const FVector S = E->GetSourceLocation();
	R = FAcousticResult();
	R.VoicePos = S;
	R.DirectM = FVector::Dist(S, ListenerPos) / 100.f;
	AAcousticRoom* RS = RoomAt(S);
	R.SourceRoom = Rooms.IndexOfByKey(RS);
	if (Config.Solution == ESolution::WaveBake) { SolveWave(E, R); return; }
	if (Config.Solution == ESolution::SteamAudio)
	{
		R.Note = TEXT("Steam Audio 负责直达路径的遮挡与透射");
		if (Config.bSteamPlusPortal)
		{
			// Steam Audio renders the direct path at the real position; our solver adds the doorway path when rooms differ.
			FAcousticResult Tmp = R; SolveCustom(E, Tmp);
			if (RS && ListenerRoom && RS != ListenerRoom && Tmp.PathPoints.Num() > 2)
			{
				R.VoicePos = Tmp.VoicePos; R.PathGainDb = Tmp.PathGainDb; R.bPathAudible = Tmp.bPathAudible;
				R.PathPoints = Tmp.PathPoints; R.PathLengthM = Tmp.PathLengthM; R.ExcessM = Tmp.ExcessM; R.BendDeg = Tmp.BendDeg; R.PortalCount = Tmp.PortalCount;
				R.Note = TEXT("Steam Audio 直达路径 + 自研门户路径");
			}
			else R.bPathAudible = false;
		}
		else R.bPathAudible = false;
		return;
	}
	SolveCustom(E, R);
}

void UAcousticSubsystem::SolveCustom(UAcousticEmitterComponent* E, FAcousticResult& R)
{
	const FVector S = E->GetSourceLocation();
	const FVector L = ListenerPos;
	const AActor* Owner = E->GetOwner();
	AAcousticRoom* RS = RoomAt(S);
	AAcousticRoom* RL = ListenerRoom;
	if (Config.bNative)
	{
		R.bNativeBlocked = TraceBlocked(S, L, Owner);
		R.Note = R.bNativeBlocked ? TEXT("引擎原生：射线被挡 → 整体 −12 dB、低通 600 Hz（二值）") : TEXT("引擎原生：射线未被挡 → 不处理");
		return;
	}
	if (!Config.bMultiRay && !Config.bPortal) { R.Note = TEXT("只有距离衰减"); return; }

	if (!Config.bPortal)
	{
		const float F = MultiRayFraction(E, S, L, &R);
		R.OccludedFraction = F;
		R.PathGainDb = FBand3(-10.f * F, -18.f * F, -26.f * F);
		R.Note = FString::Printf(TEXT("12 条射线中 %.0f%% 被挡，按比例衰减；声像仍在声源处"), F * 100.f);
		return;
	}

	const bool bSame = (RS == RL) || !RS || !RL;
	bool bDirectClear = true;
	if (bSame)
	{
		const float F = Config.bMultiRay ? MultiRayFraction(E, S, L, &R) : 0.f;
		R.OccludedFraction = F;
		// Obstruction inside one space only affects the direct sound (report ch. 1): milder than occlusion.
		R.PathGainDb = FBand3(-3.f * F, -8.f * F, -15.f * F);
		R.PathPoints = { S, L };
		R.PathLengthM = R.DirectM;
		bDirectClear = F < 0.99f;
		R.Note = F > 0.01f ? FString::Printf(TEXT("同一空间：直达声被挡 %.0f%%，只衰减直达声"), F * 100.f) : TEXT("同一空间，视线通畅");
	}
	else
	{
		TArray<FVector> Pts; TArray<AAcousticPortal*> Ps;
		if (FindPortalPath(S, RS, L, RL, Pts, Ps))
		{
			R.PathPoints = Pts;
			R.PortalCount = Ps.Num();
			const float Len = PathLengthCm(Pts);
			R.PathLengthM = Len / 100.f;
			R.ExcessM = FMath::Max(Len - FVector::Dist(S, L), 0.f) / 100.f;
			R.BendDeg = BendDegrees(Pts);
			FBand3 G;
			const bool bLOS = !TraceBlocked(S, L, Owner);   // the straight line already passes through the openings: lit zone
			if (bLOS) { R.ExcessM = 0.f; R.BendDeg = 0.f; }
			if (bLOS) G = FBand3::Splat(0.f);
			else if (Config.bExcess)
			{
				for (int32 b = 0; b < 3; ++b) G[b] = -KurzeAndersonDb(R.ExcessM, GBandHz[b]);
			}
			else
			{
				const float C = FMath::Min(R.BendDeg, 270.f) / 180.f;   // Wwise-style: one coefficient from the bend angle
				G = FBand3(-4.f * C, -10.f * C, -18.f * C);
			}
			for (AAcousticPortal* Po : Ps) G = G + FBand3::Splat(10.f * FMath::LogX(10.f, FMath::Max(Po->GetOpenness(), 0.03f)));
			R.PathGainDb = G;
			// Virtual source: seen from the listener along the last segment, at the full path distance.
			const FVector Dir = (Pts[Pts.Num() - 2] - L).GetSafeNormal();
			R.VoicePos = L + Dir * Len;
			R.Note = FString::Printf(TEXT("经 %d 个门洞，路径 %.1f m，比直线多 %.1f m，累计拐角 %.0f°"), Ps.Num(), R.PathLengthM, R.ExcessM, R.BendDeg);
		}
		else
		{
			R.bPathAudible = false;
			R.Note = Config.bTransmission ? TEXT("门都关着：只剩穿墙透射") : TEXT("没有开着的门可以通过（透射尚未启用）");
		}
		bDirectClear = false;
	}

	if (Config.bTransmission && !bDirectClear)
	{
		TArray<FVector> Hits;
		const FBand3 TL = TransmissionLoss(S, L, Owner, &Hits);
		R.TransHits = Hits;
		if (Hits.Num() > 0 && TL[1] < 59.f)
		{
			R.TransGainDb = TL * -1.f;
			R.bTransAudible = true;
		}
	}
}

void UAcousticSubsystem::SolveWave(UAcousticEmitterComponent* E, FAcousticResult& R)
{
	const FVector S = E->GetSourceLocation();
	FWaveQuery Q;
	if (!WaveBake || !WaveBake->Query(ListenerPos, S, Q))
	{
		R.Note = WaveBake && WaveBake->IsBaking() ? TEXT("正在烘焙……完成前按自研管线播放") : TEXT("尚无烘焙数据：按 K 开始烘焙");
		FPipelineConfig Saved = Config; Config = ConfigForStep(EOcclusionStep::Smoothing);
		SolveCustom(E, R);
		Config = Saved;
		return;
	}
	// Distance is applied by placing the voice at the arrival-time distance; the bake adds the excess loss of the low band.
	const float Extra = FMath::Clamp(Q.ExtraLossDb, 0.f, 40.f);
	// Extrapolate to 1 kHz / 4 kHz: find the excess path that explains the 250 Hz loss, then evaluate Kurze-Anderson higher up.
	float Lo = 0.f, Hi = 20.f;
	for (int32 i = 0; i < 24; ++i) { const float M = 0.5f * (Lo + Hi); (KurzeAndersonDb(M, 250.f) < Extra) ? Lo = M : Hi = M; }
	const float Eq = Extra < 0.5f ? 0.f : 0.5f * (Lo + Hi);
	FBand3 G(-Extra, -FMath::Max(Extra, KurzeAndersonDb(Eq, 1000.f)), -FMath::Max(Extra, KurzeAndersonDb(Eq, 4000.f)));
	if (Config.bDynamicDoors)
	{
		// Doors were open during the bake: closed doors on the arrival path add their portal loss.
		for (AAcousticPortal* Po : Portals)
		{
			if (!Po || !Po->bHasDoor) continue;
			for (int32 i = 1; i < Q.Path.Num(); ++i)
			{
				const FVector A = Q.Path[i - 1], B = Q.Path[i];
				const FVector N = Po->GetNormal();
				const float DA = FVector::DotProduct(A - Po->GetCenter(), N), DB = FVector::DotProduct(B - Po->GetCenter(), N);
				// Where the segment pierces the door plane (the last segment can be long: it runs straight to the listener).
				const FVector Hit = DA * DB < 0.f ? A + (B - A) * (DA / (DA - DB)) : FVector::ZeroVector;
				if (DA * DB < 0.f && FMath::Abs(FVector::DotProduct(Hit - Po->GetCenter(), Po->GetRight())) < Po->Width * 0.5f + 30.f)
				{
					const FBand3 DoorTL = TransmissionLossDb(EAcousticMaterial::Door);
					const float Open = Po->GetOpenness();
					for (int32 b = 0; b < 3; ++b)
						G[b] += 10.f * FMath::LogX(10.f, FMath::Lerp(FMath::Pow(10.f, -DoorTL[b] / 10.f), 1.f, Open));
					break;
				}
			}
		}
	}
	R.PathGainDb = G;
	R.PathPoints = Q.Path;
	R.PortalCount = 0;
	R.BendDeg = BendDegrees(Q.Path);
	R.PathLengthM = Q.PathM;
	R.ExcessM = FMath::Max(Q.PathM - R.DirectM, 0.f);
	R.VoicePos = ListenerPos + Q.ArrivalDir * Q.PathM * 100.f;
	R.WetDb = FMath::Clamp(Q.LerMinusLdsDb - 14.f, -30.f, -4.f);
	R.Note = FString::Printf(TEXT("烘焙：首达 %.1f ms，直达声比自由场低 %.1f dB，早期反射比直达声高 %.1f dB"), Q.ArrivalMs, Extra, Q.LerMinusLdsDb);
}

// --------------------------------------------------------------------------------------------------------------
// Apply
// --------------------------------------------------------------------------------------------------------------

/**
 * Reverb send that behaves like a pre-distance-attenuation send: the room reverb is fed by the source level, not by the
 * level at the listener. UE 5.8 reads a mono pre-attenuation buffer as stereo for HRTF-spatialised sources, which fills
 * the send with garbage (NaN), so the send is taken after distance attenuation and the attenuation is divided back out.
 */
static void SetRoomSend(UAudioComponent* V, USoundSubmixBase* Sub, float Level, const FVector& ListenerPos)
{
	if (!V || !Sub) return;
	if (Level > 0.f)
	{
		const FSoundAttenuationSettings* A = V->bOverrideAttenuation ? &V->AttenuationOverrides
			: (V->AttenuationSettings ? &V->AttenuationSettings->Attenuation : nullptr);
		const float G = A ? A->Evaluate(FTransform(V->GetComponentLocation()), ListenerPos, 1.f) : 1.f;
		Level = FMath::Min(Level / FMath::Max(G, 0.02f), 50.f);
	}
	FSoundSubmixSendInfo Info;
	Info.SoundSubmix = Sub;
	Info.SendLevel = FMath::IsFinite(Level) ? Level : 0.f;
	Info.DisableManualSendClamp = true;
	Info.SendStage = ESubmixSendStage::PostDistanceAttenuation;
	if (FAudioDevice* D = V->GetAudioDevice())
	{
		D->SendCommandToActiveSounds(V->GetAudioComponentID(), [Info](FActiveSound& AS) { AS.SetSubmixSend(Info); });
	}
}

void UAcousticSubsystem::ApplyResult(UAcousticEmitterComponent* E, const FAcousticResult& R, float Dt)
{
	if (!E->PathVoice) return;
	const bool bAnySolo = Emitters.ContainsByPredicate([](const auto& X) { return X && X->bSolo; });
	const bool bSilenced = E->bMuted || (bAnySolo && !E->bSolo);
	const float Base = bSilenced ? 0.f : E->BaseVolume;
	const bool bSteam = Config.Solution == ESolution::SteamAudio;

	const bool bNative = Config.Solution == ESolution::Custom && Config.bNative;
	E->SetNativeOcclusion(bNative);

	// Smoothing: one-pole in dB (tau 0.2 s) and an angular/position glide for the virtual source.
	const float K = Config.bSmoothing ? (1.f - FMath::Exp(-Dt / 0.2f)) : 1.f;
	auto Glide = [&](FAppliedVoice& V, const FBand3& Target, const FVector& TargetPos)
	{
		if (!V.bInit || !Config.bSmoothing) { V.GainDb = Target; V.Pos = TargetPos; V.bInit = true; return; }
		for (int32 b = 0; b < 3; ++b) V.GainDb[b] += (Target[b] - V.GainDb[b]) * K;
		// Interpolate direction and distance separately so the image swings around the listener instead of cutting through walls.
		const FVector D0 = V.Pos - ListenerPos, D1 = TargetPos - ListenerPos;
		const float L0 = D0.Size(), L1 = D1.Size();
		const FQuat Q0 = FQuat::FindBetweenNormals(FVector::ForwardVector, D0.GetSafeNormal());
		const FQuat Q1 = FQuat::FindBetweenNormals(FVector::ForwardVector, D1.GetSafeNormal());
		const FQuat Q = FQuat::Slerp(Q0, Q1, K);
		V.Pos = ListenerPos + Q.RotateVector(FVector::ForwardVector) * FMath::Lerp(L0, L1, K);
		if (V.Pos.ContainsNaN() || L0 < 1.f || L1 < 1.f) V.Pos = FMath::Lerp(V.Pos.ContainsNaN() ? TargetPos : V.Pos, TargetPos, K);
		if (V.Pos.ContainsNaN()) V.Pos = TargetPos;
		for (int32 b = 0; b < 3; ++b) if (!FMath::IsFinite(V.GainDb[b])) V.GainDb[b] = Target[b];
	};

	// Guard against non-finite solver output: one NaN reaching a voice poisons every filter downstream of it.
	auto Finite = [](const FBand3& B) { return FMath::IsFinite(B[0]) && FMath::IsFinite(B[1]) && FMath::IsFinite(B[2]); };
	auto FinitePos = [](const FVector& P) { return !P.ContainsNaN(); };
	if (!Finite(R.PathGainDb) || !Finite(R.TransGainDb) || !FinitePos(R.VoicePos) || !FMath::IsFinite(R.WetDb))
	{
		static int32 Warned = 0;
		if (Warned++ < 10) UE_LOG(LogTemp, Warning, TEXT("Acoustics: non-finite result for %s (path %f %f %f, trans %f, pos %s, wet %f, note %s)"), *E->DisplayName,
			R.PathGainDb[0], R.PathGainDb[1], R.PathGainDb[2], R.TransGainDb[1], *R.VoicePos.ToString(), R.WetDb, *R.Note);
	}
	const FBand3 PathTarget = R.bPathAudible && Finite(R.PathGainDb) ? R.PathGainDb : FBand3::Splat(-80.f);
	const FBand3 TransTarget = R.bTransAudible && Finite(R.TransGainDb) ? R.TransGainDb : FBand3::Splat(-80.f);
	Glide(E->AppliedPath, PathTarget, FinitePos(R.VoicePos) ? R.VoicePos : E->GetSourceLocation());
	Glide(E->AppliedTrans, TransTarget, E->GetSourceLocation());

	if (bSteam)
	{
		E->SteamVoice->SetVolumeMultiplier(FMath::Max(Base, 0.0005f));
		if (const USteamAudioSourceComponent* SA = E->GetOwner()->FindComponentByClass<USteamAudioSourceComponent>())
		{
			const float T[3] = { SA->TransmissionLowValue, SA->TransmissionMidValue, SA->TransmissionHighValue };
			for (int32 b = 0; b < 3; ++b)
			{
				const float G = SA->OcclusionValue + (1.f - SA->OcclusionValue) * T[b];   // Steam Audio direct effect (report ch. 5.1)
				E->SteamDb[b] = 20.f * FMath::LogX(10.f, FMath::Max(G, 1e-4f));
			}
		}
		if (Config.bSteamPlusPortal && R.bPathAudible)
		{
			E->PathVoice->SetWorldLocation(E->AppliedPath.Pos);
			UAcousticEmitterComponent::ApplyBands(E->PathVoice, E->PathEQ, E->AppliedPath.GainDb, Base);
		}
		else E->PathVoice->SetVolumeMultiplier(0.0005f);
		E->TransVoice->SetVolumeMultiplier(0.0005f);
	}
	else
	{
		E->SteamVoice->SetVolumeMultiplier(0.0005f);
		E->PathVoice->SetWorldLocation(E->AppliedPath.Pos);
		UAcousticEmitterComponent::ApplyBands(E->PathVoice, E->PathEQ, E->AppliedPath.GainDb, Base);
		E->TransVoice->SetWorldLocation(E->GetSourceLocation());
		UAcousticEmitterComponent::ApplyBands(E->TransVoice, E->TransEQ, E->AppliedTrans.GainDb, Base);
		// The engine applies native occlusion itself; show its effect on the bars (-12 dB, 600 Hz low-pass).
		if (bNative) E->AppliedPath.GainDb = R.bNativeBlocked ? FBand3(-12.f, -14.f, -32.f) : FBand3::Splat(0.f);
	}

	// Reverb: each source excites its own room; the room's submix carries the coupling to the listener.
	const float WetTarget = Config.bReverb && FMath::IsFinite(R.WetDb) ? R.WetDb : -80.f;
	E->AppliedWetDb += (WetTarget - E->AppliedWetDb) * (Config.bSmoothing ? K : 1.f);
	USoundSubmixBase* Sub = Rooms.IsValidIndex(R.SourceRoom) && Rooms[R.SourceRoom] ? Rooms[R.SourceRoom]->ReverbSubmix.Get() : nullptr;
	const float WetLin = bSilenced ? 0.f : FMath::Pow(10.f, E->AppliedWetDb / 20.f);
	for (AAcousticRoom* Ro : Rooms)
	{
		if (!Ro || !Ro->ReverbSubmix) continue;
		const float Lv = (Ro->ReverbSubmix == Sub) ? WetLin : 0.f;
		SetRoomSend(bSteam ? E->SteamVoice.Get() : E->PathVoice.Get(), Ro->ReverbSubmix, Lv, ListenerPos);
		SetRoomSend(bSteam ? E->PathVoice.Get() : E->SteamVoice.Get(), Ro->ReverbSubmix, 0.f, ListenerPos);
	}
}

void UAcousticSubsystem::UpdateRoomCoupling(float Dt)
{
	for (AAcousticRoom* R : Rooms)
	{
		if (!R || !R->ReverbSubmix) continue;
		float TargetDb = 0.f, Cut = 20000.f;
		if (R != ListenerRoom)
		{
			const FVector C = R->GetBox().GetCenter() * FVector(1, 1, 0) + FVector(0, 0, 160.f);
			TArray<FVector> Pts; TArray<AAcousticPortal*> Ps;
			if (ListenerRoom && FindPortalPath(C, R, ListenerPos, ListenerRoom, Pts, Ps))
			{
				// Diffuse energy leaving through the openings: about 6 dB per portal crossed, scaled by how open each door is.
				TargetDb = 0.f;
				for (AAcousticPortal* Po : Ps) TargetDb += -6.f + 10.f * FMath::LogX(10.f, FMath::Max(Po->GetOpenness(), 0.03f));
				const float Ex = FMath::Max(PathLengthCm(Pts) - FVector::Dist(C, ListenerPos), 0.f) / 100.f;
				Cut = FMath::Clamp(9000.f / (1.f + Ex * 0.6f), 900.f, 20000.f);
			}
			else
			{
				const FBand3 TL = TransmissionLoss(C, ListenerPos, nullptr, nullptr);
				TargetDb = -TL[1];
				Cut = 500.f;
			}
		}
		float& Cur = RoomCouplingDb.FindOrAdd(R);
		const float K = Config.bSmoothing ? (1.f - FMath::Exp(-Dt / 0.3f)) : 1.f;
		if (!FMath::IsFinite(TargetDb)) TargetDb = -60.f;
		if (!FMath::IsFinite(Cut)) Cut = 20000.f;
		Cur += (TargetDb - Cur) * K;
		if (!FMath::IsFinite(Cur)) Cur = TargetDb;
		R->ReverbSubmix->SetSubmixOutputVolume(GetWorld(), FMath::Pow(10.f, Cur / 20.f));
		if (USubmixEffectFilterPreset* F = RoomFilters.FindRef(R))
		{
			FSubmixEffectFilterSettings S = F->Settings; S.FilterFrequency = Cut; F->SetSettings(S);
		}
	}
}

// --------------------------------------------------------------------------------------------------------------
// Tick
// --------------------------------------------------------------------------------------------------------------

void UAcousticSubsystem::Tick(float DeltaTime)
{
	UWorld* W = GetWorld();
	if (!W || !W->IsGameWorld()) return;
	if (!bGathered) Gather();
	if (!bAudioStarted && W->GetTimeSeconds() > 0.2f) InitAudioGraph();
	if (!bAudioStarted) return;
	UpdateListener();

	SolveAccum += DeltaTime;
	const bool bSolve = SolveAccum >= 1.f / SolveRateHz;
	if (bSolve)
	{
		const double T0 = FPlatformTime::Seconds();
		for (auto& E : Emitters) if (E) Solve(E, E->Last);
		SolveMs = float((FPlatformTime::Seconds() - T0) * 1000.0);
		UpdateRoomCoupling(SolveAccum);
		SolveAccum = 0.f;
	}
	for (auto& E : Emitters)
	{
		if (!E) continue;
		// Without smoothing, parameters only move on solve ticks (15 Hz), which is what makes the steps audible.
		if (Config.bSmoothing || bSolve) ApplyResult(E, E->Last, Config.bSmoothing ? DeltaTime : 1.f);
		if (bDebugDraw) DrawDebug(E, E->Last);
	}
}

void UAcousticSubsystem::DrawDebug(UAcousticEmitterComponent* E, const FAcousticResult& R) const
{
	UWorld* W = GetWorld();
	const bool bFocus = (E == GetFocused());
	const FColor Col = E->Color.ToFColor(true);
	const FVector S = E->GetSourceLocation();
	if (!bFocus) { DrawDebugSphere(W, S, 18.f, 8, Col, false, -1.f, 0, 1.5f); return; }
	auto Clip = [this](FVector A, FVector B) -> TPair<FVector, FVector>
	{
		const float Keep = 90.f;   // never draw within 90 cm of the ears (first-person view)
		if (FVector::Dist(B, ListenerPos) < Keep) B = ListenerPos + (A - ListenerPos).GetSafeNormal() * Keep;
		if (FVector::Dist(A, ListenerPos) < Keep) A = ListenerPos + (B - ListenerPos).GetSafeNormal() * Keep;
		return TPair<FVector, FVector>(A, B);
	};
	for (int32 i = 0; i < R.Rays.Num(); ++i)
	{
		const bool bB = R.RayBlocked.IsValidIndex(i) && R.RayBlocked[i];
		const TPair<FVector, FVector> Seg = Clip(R.Rays[i].Key, R.Rays[i].Value);
		DrawDebugLine(W, Seg.Key, Seg.Value, bB ? FColor(230, 70, 60) : FColor(70, 220, 120), false, -1.f, 0, 1.2f);
	}
	if (Config.Solution == ESolution::Custom && Config.bNative)
	{
		const bool bB = TraceBlocked(S, ListenerPos, E->GetOwner());
		const TPair<FVector, FVector> Seg = Clip(S, ListenerPos);
		DrawDebugLine(W, Seg.Key, Seg.Value, bB ? FColor(230, 70, 60) : FColor(70, 220, 120), false, -1.f, 0, 2.5f);
	}
	for (int32 i = 1; i < R.PathPoints.Num(); ++i)
	{
		const TPair<FVector, FVector> Seg = Clip(R.PathPoints[i - 1], R.PathPoints[i]);
		DrawDebugLine(W, Seg.Key, Seg.Value, FColor(255, 170, 40), false, -1.f, 0, 2.5f);
		if (i + 1 < R.PathPoints.Num()) DrawDebugSphere(W, R.PathPoints[i], 10.f, 8, FColor(255, 170, 40), false, -1.f, 0, 2.f);
	}
	for (const FVector& H : R.TransHits) DrawDebugBox(W, H, FVector(6.f), FColor(120, 150, 255), false, -1.f, 0, 2.f);
	if (R.bTransAudible) { const TPair<FVector, FVector> Seg = Clip(S, ListenerPos); DrawDebugLine(W, Seg.Key, Seg.Value, FColor(120, 150, 255), false, -1.f, 0, 1.f); }
	if (FVector::Dist(E->AppliedPath.Pos, S) > 30.f && E->AppliedPath.bInit)
	{
		DrawDebugSphere(W, E->AppliedPath.Pos, 22.f, 10, FColor(255, 200, 80), false, -1.f, 0, 2.f);
		const TPair<FVector, FVector> Seg = Clip(ListenerPos, E->AppliedPath.Pos);
		DrawDebugLine(W, Seg.Key, Seg.Value, FColor(255, 200, 80, 120), false, -1.f, 0, 1.f);
	}
	DrawDebugSphere(W, S, 26.f, 10, Col, false, -1.f, 0, 3.f);
}
