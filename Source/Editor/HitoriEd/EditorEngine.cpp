#include "EnginePCH.h"

#include "Editor/HitoriEd/EditorEngine.h"

#include "Core/EngineStatics.h"
#include "Core/EngineTimer.h"
#include "Launch/LaunchEngineLoop.h"
#include "Core/StatOverlay.h"
#include "Input/InputSystem.h"


#include "Render/GeometryGenerator.h"

#include "Engine/World.h"
#include "Engine/Level.h"

#include "Render/Renderer.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
// 피킹 결과와 View별 Billboard 행렬 공급에 사용한다.
#include "Collision/HitResult.h"
#include "Components/BillboardComponent.h"
#include "GameFramework/Actor/LightActorBase.h"

#include "Asset/AssetManager.h"
#include "Render/RenderResourceManager.h"

#include "Render/RenderCommand.h"
#include "Editor/Outliner/OutlinerPanel.h"
#include "Editor/HitoriEd/EditorFileUtils.h"
#include "UObject/UObjectIterator.h"

#include "Core/EngineLog.h"
#include "Core/Stats/LightweightStats.h"

// 임시 Shadow 연결을 위한 include
// TODO : FSceneRenderer 도입 후 옮겨야함
#include "ProjectedShadowInfo.h"
#include <utility>
#include <algorithm>
#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLightSceneProxy.h"
#include "Core/Stats/ShadowStats.h"
#include "Math/Frustum.h"

namespace
{
	DECLARE_CYCLE_STAT("Viewport Update", STAT_ViewportUpdate);
	DECLARE_CYCLE_STAT("World Tick", STAT_WorldTick);
	DECLARE_CYCLE_STAT("Editor Tick", STAT_EditorTick);
	DECLARE_CYCLE_STAT("Prepare Render Views", STAT_PrepareRenderViews);
	DECLARE_CYCLE_STAT("Build Render Queue", STAT_BuildRenderQueue);
	DECLARE_CYCLE_STAT("ImGui", STAT_ImGui);
}

#include "Core/SplashScreen.h"

FEngineConfig UEditorEngine::GetConfig() const
{
	FEngineConfig Desc;
	Desc.Title = L"Hitori Engine";
	Desc.Width = 1920;
	Desc.Height = 1080;
	Desc.bBorderless = false;
	Desc.SyncInterval = 0;
	// View는 각자 깊이 버퍼를 쓰고 백버퍼에는 ImGui만 그린다.
	Desc.bCreateDepthBuffer = false;
	Desc.bExitOnEscape = false;
	// 파일이 없으면 검은 배경에 상태 텍스트만 표시된다.
	Desc.SplashImage = "Resources/Splash.png";
	return Desc;
}

// 렌더 자원·월드·에디터와 MultipleViewports 연결을 초기화한다.
// Device·Window·Swapchain·AssetManager는 FEngineLoop가 먼저 만들어 둔다.
bool UEditorEngine::Init()
{
	if (!Super::Init())
		return false;

	FWorldContext& InitialWorldContext = CreateNewWorldContext(EWorldType::Editor);
	InitialWorldContext.SetCurrentWorld(UWorld::CreateWorld(EWorldType::Editor, true));
	EditorWorld = InitialWorldContext.World();

	MainWindow = GetEngineLoop().GetMainWindow();
	MainWindowSC = GetEngineLoop().GetSwapchain();
	Renderer = GetEngineLoop().GetRenderer();
	FRenderDevice* RenderDevice = GetEngineLoop().GetRenderDevice();

	EditorUI = MakeUnique<FEditorUI>();
	EditorUI->Init();

	EditorUI->SetNewSceneCallback([this]() { CreateNewScene(); });
	EditorUI->SetOpenSceneCallback([this]() { OpenScene(); });
	EditorUI->SetSaveSceneCallback([this]() { SaveCurrentScene(); });
	EditorUI->SetSaveSceneAsCallback([this]() { SaveSceneAs(); });

	OutputLogPanel = EditorUI->AddEditorPanel<FOutputLogPanel>();
	FLog::AddSink(OutputLogPanel);
	OutputLogPanel->SetCommandHandler(
		[this](const FString& CommandLine, FString& OutMessage)
		{
			if (ExecShadowCommand(CommandLine, OutMessage))
				return true;

			if (ExecShadowResolutionCommand(CommandLine, OutMessage))
				return true;

			return false;
		}
	);
	HTR_LOG(Info, "Editor Initialize...");

	HTR_LOG(Info, "Initialize ImGui...");
	ImGuiRenderer = MakeUnique<FImGuiRenderer>();
	if (!ImGuiRenderer->Init(MainWindow->GetHandle(), RenderDevice->GetDevice(), RenderDevice->GetContext()))
	{
		HTR_LOG(Error, "Failed To Initialize ImGui!");
	}
	HTR_LOG(Info, "Initialize ImGui Success!");

	GridRenderer = MakeUnique<FGridRenderer>();
	GridRenderer->Init(Renderer);

	GizmoRenderer = MakeUnique<FGizmoRenderer>();
	GizmoRenderer->Init(Renderer);

	Gizmo = MakeUnique<FGizmo>();

	FogRenderer = MakeUnique<FFogRenderer>();
	FogRenderer->Init(Renderer);

	FXAARenderer = MakeUnique<FFXAARenderer>();
	FXAARenderer->Init();

	DepthViewRenderer = MakeUnique<FDepthViewRenderer>();
	DepthViewRenderer->Init();

	ShadowViewRenderer = MakeUnique<FShadowViewRenderer>();
	ShadowViewRenderer->Init();

	ToneMapRenderer = MakeUnique<FToneMapRenderer>();
	ToneMapRenderer->Init();


	// 필요한 Panel들 추가후 raw pointer 반환(소유권 = EditorUI)
	DetailsPanel = EditorUI->AddEditorPanel<FDetailsPanel>();
	// 패널에는 조회 대상만 전달하고, 그림자 작업이나 타깃을 보관하지 않는다.
	DetailsPanel->SetRenderer(Renderer);

	DetailsPanel->SetShadowCascadeDebugCallbacks(
		[this](uint32 ViewId)
		{
			if (ViewId == 0 || ViewId > FMultipleViewportsAdapter::MaxViewCount)
				return false;

			return MultipleViewportsAdapter.GetViewShowFlags(static_cast<int32>(ViewId - 1)).IsEnabled(EEngineShowFlag::ShadowCascades);
		},
		[this](uint32 ViewId, bool bEnabled)
		{
			if (ViewId == 0 || ViewId > FMultipleViewportsAdapter::MaxViewCount)
				return;

			// 다른 View 플래그는 유지하고 색상 표시만 변경한다.
			const int32 ViewIndex = static_cast<int32>(ViewId - 1);
			FEngineShowFlags Flags = MultipleViewportsAdapter.GetViewShowFlags(ViewIndex);
			Flags.SetEnabled(EEngineShowFlag::ShadowCascades, bEnabled);
			MultipleViewportsAdapter.SetViewShowFlags(ViewIndex, Flags);
		});

	EditorControlsPanel = EditorUI->AddEditorPanel<FEditorControlsPanel>();
	ViewportsPanel = EditorUI->AddEditorPanel<FViewportsPanel>();
	ContentDrawerPanel = EditorUI->AddEditorPanel<FContentDrawerPanel>();

	// OutLine
	OutlineRenderer = MakeUnique<FOutlineRenderer>();
	OutlineRenderer->Init(Renderer);

	SettingsPanel = EditorUI->AddEditorPanel<FSettingsPanel>();

	//ToolbarPanel->SetPlayCallback([this]() { RequestPlaySession(); });
	//ToolbarPanel->SetStopCallback([this]() { RequestEndPlayMap(); });
	//ToolbarPanel->SetIsPlayingQuery([this]() { return PlayWorld != nullptr; });

	Outline = MakeUnique<FOutline>();

	SystemFont = UAssetManager::GetAssetByPath<UFont>("Assets/Fonts/Pretendard.json");

	TextRenderer = MakeUnique<FTextRenderer>();
	TextRenderer->Init();

	// 투영 행렬 생성 
	MultipleViewportsAdapter.InitializeFromWorld(*GetActiveWorld());
	// 화면 나눔 비율 설정 가져오기
	MultipleViewportsAdapter.SetSplitRatio({
		SettingsPanel->GetSettings().MultipleViewportsHorizontal,
		SettingsPanel->GetSettings().MultipleViewportsVertical });
	// SingleView에 사용할 인덱스 설정
	MultipleViewportsAdapter.SetSingleViewIndex(
		SettingsPanel->GetSettings().MultipleViewportsSingleViewIndex);
	// 뷰포트 레이아웃 설정
	MultipleViewportsAdapter.SetLayoutMode(
		SettingsPanel->GetSettings().bMultipleViewportsSingle
		? ELayoutMode::Single
		: ELayoutMode::QuadSplit);
	GetActiveWorld()->GetMainCamera()->GetCameraComponent()->SetExternalInputManaged(true);

	/// 삭제 예정
	//SceneManager = EditorUI->AddEditorPanel<FSceneManager>();
	//SceneManager->SetWorld(World);

	OutlinerPanel = EditorUI->AddEditorPanel<FOutlinerPanel>();
	OutlinerPanel->SetWorld(GetActiveWorld());
	OutlinerPanel->SetSelectionCallback(
		[this](USceneComponent* Root)
		{
			Gizmo->SetTarget(Root, true);
			DetailsPanel->SetTarget(Root ? Root->GetOwner() : nullptr);

			UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Root);
			if (!Primitive && Root && Root->GetOwner())
			{
				for (UActorComponent* C : Root->GetOwner()->GetComponents())
					if ((Primitive = Cast<UPrimitiveComponent>(C))) break;
			}
			Outline->SetTarget(Primitive);
		});
	DetailsPanel->SetSelectionCallback(
		[this](UActorComponent* Component)
		{
			if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
			{
				AActor* Owner = SceneComponent->GetOwner();
				if (Owner && SceneComponent == Owner->GetRootComponent())
				{
					Gizmo->SetTarget(SceneComponent, true);
				}
				else
				{
					Gizmo->SetTarget(SceneComponent);
				}
			}
			else
			{
				Gizmo->SetTarget(nullptr);
			}
		}
	);

	OutlinerPanel->SetDeleteActorCallback(
		[this](AActor* Actor)
		{
			DeleteActor(Actor);
		}
	);
	DetailsPanel->SetDeleteComponentCallback(
		[this](UActorComponent* Component)
		{
			DeleteComponent(Component);
		}
	);

	LineBatcher = MakeUnique<FLineBatcher>();
	LineBatcher->Init(Renderer, GetActiveWorld());

	DetailsPanel->SetWorld(GetActiveWorld());

	EditorControlsPanel->SetWorld(GetActiveWorld());
	EditorControlsPanel->SetIsPlayingQuery([this]() { return IsPlaying(); });
	EditorControlsPanel->SetGizmo(Gizmo.get());
	EditorControlsPanel->SetViewportAdapter(&MultipleViewportsAdapter);

	SettingsPanel->SetWorld(GetActiveWorld());
	SettingsPanel->SetViewportAdapter(&MultipleViewportsAdapter);
	ViewportsPanel->SetViewportAdapter(&MultipleViewportsAdapter);

	SkyboxRenderer = MakeUnique<FSkyboxRenderer>();
	SkyboxRenderer->Init("Assets/SkySphere/Sky.hdr");

	const ImVec4 White = { 1, 1, 1, 1 };
	const ImVec4 Green = { 0.40f, 0.80f, 0.30f, 1.0f };

	Save = { "Save",   "Save Current Scene",UAssetManager::GetAssetByPath<UTexture2D>("Assets/Icons/Toolbar/Save.png"),  White, { EKeyCode::S, true } };

	Play = { "Play",   "Play World",        UAssetManager::GetAssetByPath<UTexture2D>("Assets/Icons/Toolbar/Play.png"),  Green, { EKeyCode::P, false, true } };

	Play.CanExecute = [this]()->bool {return !PlayWorld; };
	Play.Execute = [this]() { RequestPlaySession(); };

	Pause = { "Pause",  "Pause",			UAssetManager::GetAssetByPath<UTexture2D>("Assets/Icons/Toolbar/Pause.png"), White, { EKeyCode::Pause } };

	Pause.CanExecute = [this]()->bool {return !bIsPaused && PlayWorld; };
	Pause.Execute = [this]() { bIsPaused = true; };


	Resume = { "Resume", "Resume",          UAssetManager::GetAssetByPath<UTexture2D>("Assets/Icons/Toolbar/Play.png"),  White, { EKeyCode::Pause } };

	Resume.CanExecute = [this]()->bool {return bIsPaused && PlayWorld; };
	Resume.Execute = [this]() { bIsPaused = false; };

	StepFrame = { "Step", "Step One Frame", UAssetManager::GetAssetByPath<UTexture2D>("Assets/Icons/Toolbar/Step.png"),  White, {} };

	StepFrame.CanExecute = [this]()->bool {return bIsPaused && PlayWorld; };
	StepFrame.Execute = [this]() { bStepRequested= true; };


	Stop = { "Stop",   "Stop",				UAssetManager::GetAssetByPath<UTexture2D>("Assets/Icons/Toolbar/Stop.png"),  White, { EKeyCode::Escape } };

	Stop.CanExecute = [this]()->bool {return PlayWorld; };
	Stop.Execute = [this]() { RequestEndPlayMap(); };

	EditorUI->GetToolbar()->SetCommands({ Play, Pause, Resume, StepFrame, Stop });
	EditorUI->GetToolbar()->SetPlayModeBinding(
		[this]() { return PlayMode; },
		[this](EPlayModeType Mode) { PlayMode = Mode; },
		[this]() { return !IsPlaying(); });

	return true;
}

// 프레임 시작·View 상태·월드 갱신 후 View별 오프스크린 렌더와 UI 합성을 진행한다.
// 입력·창 메시지는 FEngineLoop가 먼저 처리하고 Present는 호출 직후에 한다.
void UEditorEngine::Tick(const float DeltaTime)
{
	// PIE 창의 닫기 버튼은 앱이 아니라 PIE 세션만 끝낸다.
	if (PIEWindow && PIEWindow->ConsumeCloseRequest())
		RequestEndPlayMap();
	if (bRequestEndPlayMapQueued) { bRequestEndPlayMapQueued = false; EndPlayMap(); }
	if (bPlaySessionRequested) { bPlaySessionRequested = false; if (!PlayWorld) StartPlayInEditorSession(); }

	BeginFrame(DeltaTime);
	// 임시: 선택된 Actor를 복제해서 결과를 로그로 확인 (PIE 연결 후 삭제)
	if (!IsPlaying() && !ImGui::GetIO().WantTextInput && FInputSystem::IsKeyPressed(EKeyCode::F9))
	{
		UWorld* PIEWorld = UWorld::CreateWorld(EWorldType::PIE, false);
		FObjectDuplicationParameters Params(EditorWorld, nullptr);
		Params.DuplicationSeed.Add(EditorWorld, PIEWorld);
		Params.DuplicationSeed.Add(EditorWorld->GetPersistentLevel(), PIEWorld->GetPersistentLevel());
		Params.PortFlags = EPropertyPortFlags::PPF_DuplicateForPIE;
		StaticDuplicateObjectEx(Params);

		HTR_LOG(Info, "[DupTest] actors {} -> {}",
			EditorWorld->GetPersistentLevel()->GetActors().Num(),
			PIEWorld->GetPersistentLevel()->GetActors().Num());
		for (AActor* A : PIEWorld->GetPersistentLevel()->GetActors())
			HTR_LOG(Info, "[DupTest]   {} world={} level={} root owner ok={}",
				A->GetName(), A->GetWorld() == PIEWorld, A->GetLevel() == PIEWorld->GetPersistentLevel(),
				A->GetRootComponent() && A->GetRootComponent()->GetOwner() == A);
	}
	UpdateMultipleViewportState(DeltaTime);
	TickWorldAndEditor(DeltaTime);
	RenderMultipleViewports();
	EndFrame();
}

// 기존 Directional 설정을 변경하여 다음 프레임의 그림자 준비에 반영한다.
bool UEditorEngine::ExecShadowCommand(const FString& CommandLine, FString& OutMessage)
{
	std::istringstream Input(CommandLine);
	FString Command, Action, Value, Extra;
	Input >> Command >> Action >> Value >> Extra;

	// 다른 명령은 기존 콘솔 처리 경로에서 계속 판정한다.
	if (_stricmp(Command.c_str(), "CSM") != 0)
		return false;

	// 잘못된 입력은 아래 검증에서 설정 변경 없이 이 메시지를 반환한다.
	OutMessage = "Usage: CSM | CSM ON | CSM OFF | CSM SET <0-4>";
	if (!Extra.empty())
		return true;

	// -1은 개수 변경이 없는 토글·ON·OFF 명령을 의미한다.
	int32 RequestedCount = -1;
	if (_stricmp(Action.c_str(), "SET") == 0)
	{
		// 현재 지원 범위인 한 자리 정수 0~4만 허용한다.
		if (Value.size() != 1 || Value[0] < '0'	|| Value[0] > '0' + UDirectionalLightComponent::MaxShadowCascades)
			return true;

		RequestedCount = Value[0] - '0';
	}
	else
	{
		// SET 이외에는 인자가 없거나 ON/OFF만 사용할 수 있다.
		if (!Value.empty() || (!Action.empty() && _stricmp(Action.c_str(), "ON") != 0 && _stricmp(Action.c_str(), "OFF") != 0))
			return true;
	}

	// 평소에는 EditorWorld, PIE 중에는 PlayWorld를 대상으로 한다.
	UWorld* World = GetActiveWorld();
	const FDirectionalLightSceneProxy* Primary = World->GetScene().GetPrimaryDirectionalLight();

	if (!Primary)
	{
		OutMessage = "No active Directional Light in the current world.";
		return true;
	}

	// 실제 Primary의 Owner에서 수정 가능한 동일 Component를 찾는다.
	const ULightComponent* PrimaryComponent = Primary->GetLightComponent();
	UDirectionalLightComponent* Light = nullptr;
	for (UActorComponent* Component : PrimaryComponent->GetOwner()->GetComponents())
	{
		if (Component == PrimaryComponent)
		{
			Light = Cast<UDirectionalLightComponent>(Component);
			break;
		}
	}

	if (!Light)
	{
		OutMessage = "Directional Light component was not found.";
		return true;
	}

	if (RequestedCount >= 0)
	{
		// SET 0은 마지막 개수를 보존하며 CSM만 비활성화한다.
		if (RequestedCount > 0)
			Light->SetCascadeCount(RequestedCount);

		Light->SetCSMEnabled(RequestedCount > 0);
	}
	else if (Action.empty())
	{
		// 단독 CSM 입력은 기존 STAT 명령처럼 현재 상태를 반전한다.
		Light->SetCSMEnabled(!Light->IsCSMEnabled());
	}
	else
	{
		// 입력 검증을 통과했으므로 남은 Action은 ON 또는 OFF다.
		Light->SetCSMEnabled(_stricmp(Action.c_str(), "ON") == 0);
	}

	// OFF에서는 출력된 개수가 저장된 설정값임을 명시한다.
	const bool bEnabled = Light->IsCSMEnabled();
	OutMessage = std::format(
		"CSM: {}, {}: {} (World: {})",
		bEnabled ? "ON" : "OFF",
		bEnabled ? "Cascades" : "Saved Cascades",
		Light->GetCascadeCount(),
		World->IsPlayInEditor() ? "PIE" : "Editor"
	);
	return true;
}

bool UEditorEngine::ExecShadowResolutionCommand(const FString& CommandLine, FString& OutMessage)
{
	std::istringstream Input(CommandLine);
	FString Command, Extra;
	Input >> Command;

	// 다른 명령은 기존 콘솔 처리 경로에서 계속 판정한다.
	if (_stricmp(Command.c_str(), "ShadowResolution") != 0)
		return false;

	// 잘못된 입력은 아래 검증에서 설정 변경 없이 이 메시지를 반환한다.
	OutMessage = "Usage: ShadowResolution <128 | 256 | 512 | 1024 | 2048 | 4096>";
	int32 Resolution = 0;
	if (!(Input >> Resolution) || (Input >> Extra) || !Renderer->SetShadowMapResolution(Resolution))
		return true;
	OutMessage = std::format("ShadowMap global maximum: {}", Resolution);

	return true;
}

// DeltaTime을 패널에 전달하고 에디터 단축키를 처리한다.
void UEditorEngine::BeginFrame(const float DeltaTime)
{
	FStatOverlay::Tick(DeltaTime);
	EditorControlsPanel->FEditorControlsPanel::DeltaTime = DeltaTime;

	if (!ImGui::GetIO().WantTextInput && FInputSystem::IsKeyPressed(EKeyCode::Delete))
		DeleteActor(OutlinerPanel->GetSelectedActor());
}

// 패널의 Layout·Preset 요청과 입력을 Adapter에 반영한다.
void UEditorEngine::UpdateMultipleViewportState(const float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_ViewportUpdate);

	const FVector2 ViewportSize = ViewportsPanel->GetContentSize();
	const FVector2 LocalMousePosition = ViewportsPanel->GetLocalMousePosition();

	ELayoutMode RequestedLayout{};
	int32 RequestedSingleViewIndex = MultipleViewportsAdapter.GetSingleViewIndex();
	if (ViewportsPanel->ConsumeLayoutRequest(RequestedLayout, RequestedSingleViewIndex))
	{
		if (RequestedLayout == ELayoutMode::Single)
			MultipleViewportsAdapter.SetSingleViewIndex(RequestedSingleViewIndex);
		MultipleViewportsAdapter.SetLayoutMode(RequestedLayout);

		FEditorSettings& Settings = SettingsPanel->GetMutableSettings();
		Settings.bMultipleViewportsSingle = RequestedLayout == ELayoutMode::Single;
		Settings.MultipleViewportsSingleViewIndex = RequestedSingleViewIndex;
	}

	int32 PresetViewIndex = InvalidViewIndex;
	EMultipleViewportsCameraPreset RequestedPreset = EMultipleViewportsCameraPreset::Perspective;
	if (ViewportsPanel->ConsumeCameraPresetRequest(PresetViewIndex, RequestedPreset))
		MultipleViewportsAdapter.ApplyCameraPreset(PresetViewIndex, RequestedPreset);
	MultipleViewportsAdapter.UpdateLayout(ViewportSize);

	const float HorizontalDrag = ViewportsPanel->ConsumeHorizontalDrag();
	const float VerticalDrag = ViewportsPanel->ConsumeVerticalDrag();
	if (HorizontalDrag != 0.0f)
		MultipleViewportsAdapter.ApplySplitterDrag(EDragAxis::Horizontal, HorizontalDrag, ViewportSize);
	if (VerticalDrag != 0.0f)
		MultipleViewportsAdapter.ApplySplitterDrag(EDragAxis::Vertical, VerticalDrag, ViewportSize);
	if (HorizontalDrag != 0.0f || VerticalDrag != 0.0f)
	{
		MultipleViewportsAdapter.UpdateLayout(ViewportSize);
		const FSplitRatio Ratio = MultipleViewportsAdapter.GetSplitRatio();
		SettingsPanel->GetMutableSettings().MultipleViewportsHorizontal = Ratio.Horizontal;
		SettingsPanel->GetMutableSettings().MultipleViewportsVertical = Ratio.Vertical;
	}

	MultipleViewportsAdapter.UpdateInput(
		DeltaTime,
		LocalMousePosition,
		SettingsPanel->GetSettings().CameraSpeed,
		SettingsPanel->GetSettings().MouseSensitivity);
	// 겹친 창은 Hover 선택에서 제외하고 우클릭 Capture를 우선한다.
	if (ViewportsPanel->IsHovered() || MultipleViewportsAdapter.GetCapturedViewIndex() != InvalidViewIndex)
		MultipleViewportsAdapter.SetEditorViewIndex(MultipleViewportsAdapter.GetActiveViewIndex());
}

// 월드를 한 번 Tick한 뒤 에디터와 피킹을 갱신한다.
void UEditorEngine::TickWorldAndEditor(const float DeltaTime)
{
	// 월드 상태는 프레임마다 정확히 한 번 갱신한다.

	{
		SCOPE_CYCLE_COUNTER(STAT_WorldTick);
		if (bStepRequested && bIsPaused)
		{
			GetActiveWorld()->Tick(1.0f / 60.0f);
			bStepRequested = false;
		}
		else if(!bIsPaused)
			GetActiveWorld()->Tick(DeltaTime);

	}
	{
		SCOPE_CYCLE_COUNTER(STAT_EditorTick);
		EditorUI->Tick(DeltaTime);
	}
	// 렌더 준비는 View마다 그릴 월드가 다르므로 RenderMultipleViewports에서 한다.
	// PIE 중에도 PIE가 아닌 View에서는 EditorWorld 편집을 계속한다.
	UpdateGizmoAndPicking();
	//SCOPE_CYCLE_COUNTER(STAT_UpdateAllTransforms);
	EditorWorld->GetScene().UpdateAllTransforms();
	if (PlayWorld && bIsPaused)
		PlayWorld->GetScene().UpdateAllTransforms();
}

// 활성 World를 먼저 준비하고 그림자를 생성한 뒤 View별 카메라 수집·렌더를 수행한다.
void UEditorEngine::RenderMultipleViewports()
{
	// PIE 창 크기를 외부 View에 먼저 알려야 카메라 Override의 직교 폭 보정이 맞는다.
	bPIEWindowRendered = false;
	const bool bRenderPIEWindow = IsPIEInWindow() && UpdatePIEWindowTargets();

	// 월드 Tick으로 움직인 게임 카메라를 같은 프레임에 반영한다.
	UpdatePIEViewCamera();

	UWorld* PreparedWorld = nullptr;
	Renderer->SetLightDebugMode(SettingsPanel->GetSettings().LightDebugMode);
	Renderer->SetLightCullingMode(static_cast<ELightCullingMode>(SettingsPanel->GetSettings().LightCullingMode));

	// TODO : FSceneRenderer 도입전 임시
	// World 갱신 여부와 그림자 필요 여부를 별도로 관리한다.
	TArray<UWorld*> PreparedWorlds;
	TMap<UWorld*, TArray<int32>> ShadowViews;
	const FEngineShowFlags& ShowFlags = SettingsPanel->GetSettings().ShowFlags;
	const bool bDrawPrimitives = ShowFlags.IsEnabled(EEngineShowFlag::Primitives);
	const bool bDynamicShadows = ShowFlags.IsEnabled(EEngineShowFlag::DynamicShadows);
	const float MinScreenRadius = std::max(0.0f, GMinScreenRadiusForShadowCaster);
	const float MinScreenRadiusSquared = MinScreenRadius * MinScreenRadius;

	const auto PrepareView = [&](int32 ViewIndex)
		{
			if (!MultipleViewportsAdapter.IsViewActive(ViewIndex))
				return;

			UWorld* ViewWorld = GetWorldForView(ViewIndex);

			// Transform·BVH 갱신과 동적 데이터 준비는 활성 World마다 한 번 수행한다.
			if (PreparedWorlds.Find(ViewWorld) == INDEX_NONE)
			{
				SCOPE_CYCLE_COUNTER(STAT_PrepareRenderViews);
				ViewWorld->BeginRenderViews();
				PreparedWorlds.Add(ViewWorld);
			}

			// 이번 그림자 수신 연결 범위는 Lambert·Blinn-Phong이다.
			const EViewMode ViewMode = MultipleViewportsAdapter.GetViewMode(ViewIndex);
			const bool bNeedsShadow = bDrawPrimitives && bDynamicShadows
				&& (ViewMode == EViewMode::Lit_Lambert || ViewMode == EViewMode::Lit_BlinnPhong);

			// 앞 View가 Unlit이어도 뒤의 Lit View에서 같은 World를 등록할 수 있다.
			if (bNeedsShadow)
				ShadowViews[ViewWorld].Add(ViewIndex);
		};

	// 패널 View는 계속 4개만 순회한다.
	for (int32 ViewIndex = 0; ViewIndex < 4; ++ViewIndex)
		PrepareView(ViewIndex);

	// 별도 PIE는 패널과 분리하여 준비하며, 활성 여부는 PrepareView에서 확인한다.
	if (bRenderPIEWindow)
		PrepareView(PIEViewIndex);

	// TODO: FSceneRenderer 분리 시 World별 View 그룹 구성과 패스 조정을 이동한다.
	TArray<FProjectedShadowInfo> ShadowInfos;

	// World 공유 작업과 View별 Directional 작업을 같은 프레임 목록에 모은다.
	for (UWorld* World : PreparedWorlds)
	{
		const TArray<int32>* ViewIndices = ShadowViews.Find(World);
		if (!ViewIndices)
			continue;
		SCOPE_CYCLE_COUNTER(ShadowStats::STAT_SetupCPU);
		// Directional에서도 같은 Context를 사용하도록 World 처리 동안 유지한다.
		TArray<FLODViewContext> ActiveShadowViews;
		{
			// 같은 World의 여러 View가 공유하는 지역광 준비 비용을 모은다.
			SCOPE_CYCLE_COUNTER(ShadowStats::SetupCPUStats[ShadowStats::Local]);
			ActiveShadowViews.Reserve(ViewIndices->Num());
			TArray<FFrustumPlanes> ActiveViewFrustums;
			ActiveViewFrustums.Reserve(ViewIndices->Num());
			
			for (int32 ViewIndex : *ViewIndices)
			{
				const FLODViewContext View = MultipleViewportsAdapter.GetLODViewContext(ViewIndex);
				ActiveShadowViews.Add(View);
				ActiveViewFrustums.Add(ExtractFrustumPlanes(View.ViewProjection));
			}

			World->GatherLocalProjectedShadows(ShadowInfos, ActiveViewFrustums, ActiveShadowViews, MinScreenRadiusSquared);
		}

		const FDirectionalLightSceneProxy* Directional = World->GetScene().GetPrimaryDirectionalLight();
		if (!Directional || !Directional->ShouldCastShadow())
			continue;

		const auto* Light = static_cast<const UDirectionalLightComponent*>(Directional->GetLightComponent());
		const float ShadowDistance = Light->GetShadowDistance();

		// 각 작업을 설정한 뒤 재사용하는 임시 계산 버퍼다.
		TArray<float> SplitDistances;
		TArray<FVector> ReceiverCorners;
		ReceiverCorners.Reserve(8);

		for (int32 ViewOffset = 0; ViewOffset < ViewIndices->Num(); ++ViewOffset)
		{
			const int32 ViewIndex = (*ViewIndices)[ViewOffset];
			const FLODViewContext& ShadowView = ActiveShadowViews[ViewOffset];

			// 0은 공유 작업에 남겨두고 View 슬롯으로 안정적인 ID를 만든다.
			const uint32 ViewId = static_cast<uint32>(ViewIndex + 1);
			const bool bOrthographic = ShadowView.bOrthographic;
			const uint32 ExpectedCascadeCount = !bOrthographic && Light->IsCSMEnabled()
				? static_cast<uint32>(Light->GetCascadeCount()) : 1u;
			const auto Group = ShadowStats::GetGroup(true, ExpectedCascadeCount);
			FScopeCycleCounter SetupCounter(ShadowStats::SetupCPUStats[Group]);
			ReceiverCorners.Reset();

			if (bOrthographic)
			{
				// 직교 View는 기존 논리 원점 앞뒤의 단일 범위를 유지한다.
				if (MultipleViewportsAdapter.AppendShadowFrustumCorners(ViewIndex, ShadowDistance, ReceiverCorners))
				{
					World->GatherDirectionalShadow(ShadowInfos, ReceiverCorners, ShadowView,
						-ShadowDistance, ShadowDistance, MinScreenRadiusSquared, 0, ViewId);
				}
				continue;
			}

			// 패널 PIE와 별도 PIE 모두 실제 게임 카메라의 클립 범위를 사용한다.
			const FViewCamera& Camera = MultipleViewportsAdapter.GetEffectiveCamera(ViewIndex);
			if (!Directional->CalculateCascadeSplitDistances(Camera.Projection.NearClip, Camera.Projection.FarClip, SplitDistances))
				continue;

			// 빈 큐가 생겨도 논리 Cascade 개수와 번호는 압축하지 않는다.
			const uint32 LogicalCascadeCount = static_cast<uint32>(SplitDistances.Num() - 1);
			for (int32 CascadeIndex = 0; CascadeIndex + 1 < SplitDistances.Num(); ++CascadeIndex)
			{
				float RenderNear = SplitDistances[CascadeIndex];
				if (CascadeIndex > 0)
				{
					const float PreviousLength = SplitDistances[CascadeIndex] - SplitDistances[CascadeIndex - 1];
					RenderNear -= PreviousLength * Light->GetCascadeTransitionFraction();
				}

				ReceiverCorners.Reset();
				if (!MultipleViewportsAdapter.AppendShadowFrustumCorners(ViewIndex, RenderNear, SplitDistances[CascadeIndex + 1], ReceiverCorners))
					continue;

				// 각 Cascade는 자신이 속한 View의 크기 기준으로만 선별한다.
				World->GatherDirectionalShadow(ShadowInfos, ReceiverCorners, ShadowView,
					RenderNear, SplitDistances[CascadeIndex + 1], MinScreenRadiusSquared,
					static_cast<uint32>(CascadeIndex), ViewId, LogicalCascadeCount);
			}
		}
	}

	// 모든 World·View의 작업을 모은 뒤 Atlas 배정과 Depth 실행을 한 번 수행한다.
	// 빈 목록도 전달해 이전 프레임의 작업·바인딩·타일 할당을 정리한다.
	Renderer->RenderShadowMaps(std::move(ShadowInfos), *ShadowViewRenderer);

	for (int32 ViewIndex = 0; ViewIndex < 4; ++ViewIndex)
	{
		const bool bActive = MultipleViewportsAdapter.IsViewActive(ViewIndex);
		ViewportsPanel->SetView(ViewIndex, MultipleViewportsAdapter.GetViewRect(ViewIndex), bActive);
		if (!bActive)
			continue;

		UWorld* ViewWorld = GetWorldForView(ViewIndex);

		if (ViewWorld != PreparedWorld)
		{
			SCOPE_CYCLE_COUNTER(STAT_PrepareRenderViews);

			// FireBall 라이트 상수는 Renderer 공용 상태라 월드가 바뀌면 다시 올린다.
			// ViewWorld->GetScene().UpdateFireBallLight(Renderer);
			ViewWorld->GetScene().UpdateLights(Renderer);
			PreparedWorld = ViewWorld;
		}

		{
			SCOPE_CYCLE_COUNTER(STAT_BuildRenderQueue);
			MultipleViewportsAdapter.BuildRenderQueue(*ViewWorld, ViewIndex, RenderQueue, IconQueue, ArrowQueue);
		}

		RenderFrame(
			ViewIndex,
			*ViewWorld,
			ViewportsPanel->GetRenderingInfo(ViewIndex),
			MultipleViewportsAdapter.GetEngineViewProjection(ViewIndex),
			MultipleViewportsAdapter.GetEngineCameraLocation(ViewIndex),
			MultipleViewportsAdapter.GetEngineCameraForward(ViewIndex),
			RenderQueue, IconQueue, ArrowQueue);
	}

	// 별도 PIE 창은 패널 View 뒤에 PlayWorld의 데이터로 그린다.
	if (bRenderPIEWindow && MultipleViewportsAdapter.IsViewActive(PIEViewIndex))
	{
		UWorld* ViewWorld = GetWorldForView(PIEViewIndex);
		if (ViewWorld != PreparedWorld)
		{
			SCOPE_CYCLE_COUNTER(STAT_PrepareRenderViews);

			//ViewWorld->GetScene().UpdateFireBallLight(Renderer);
			ViewWorld->GetScene().UpdateLights(Renderer);
			PreparedWorld = ViewWorld;
		}

		{
			SCOPE_CYCLE_COUNTER(STAT_BuildRenderQueue);
			MultipleViewportsAdapter.BuildRenderQueue(*ViewWorld, PIEViewIndex, RenderQueue, IconQueue, ArrowQueue);
		}

		RenderFrame(
			PIEViewIndex,
			*ViewWorld,
			PIERenderingInfo,
			MultipleViewportsAdapter.GetEngineViewProjection(PIEViewIndex),
			MultipleViewportsAdapter.GetEngineCameraLocation(PIEViewIndex),
			MultipleViewportsAdapter.GetEngineCameraForward(PIEViewIndex),
			RenderQueue, IconQueue, ArrowQueue);
		CopyPIEWindowToBackbuffer();
	}

	EMultipleViewportsCameraPreset CameraPresets[4]{};
	for (int32 ViewIndex = 0; ViewIndex < 4; ++ViewIndex)
		CameraPresets[ViewIndex] = MultipleViewportsAdapter.GetCameraPreset(ViewIndex);
	ViewportsPanel->SetControlState(
		MultipleViewportsAdapter.GetLayoutMode(),
		MultipleViewportsAdapter.GetSingleViewIndex(),
		CameraPresets);
}

void UEditorEngine::UpdatePIEViewCamera()
{
	if (!PlayWorld || PIEViewIndex == InvalidViewIndex)
		return;

	ACameraActor* GameCameraActor = PlayWorld->GetMainCamera();
	UCameraComponent* GameCamera = GameCameraActor ? GameCameraActor->GetCameraComponent() : nullptr;
	if (!GameCamera)
	{
		MultipleViewportsAdapter.ClearViewCameraOverride(PIEViewIndex);
		return;
	}

	// 게임 카메라 자체 입력(WASD·우클릭 회전·휠)은 PIE View를 선택했을 때만 받는다.
	// 별도 창이면 그 창이 OS 포커스를 가졌을 때다.
	const bool bPIEViewFocused = IsPIEInWindow()
		? ::GetForegroundWindow() == PIEWindow->GetHandle()
		: MultipleViewportsAdapter.GetActiveViewIndex() == PIEViewIndex && !ImGui::GetIO().WantTextInput;
	GameCamera->SetExternalInputManaged(!bPIEViewFocused);

	// 엔진 View 행렬에서 깊이축(View X)을 꺼내면 회전 규약과 무관하게 실제 렌더 방향과 같다.
	const FMatrix View = GameCamera->GetViewMatrix();
	const FVector Forward(View.M[0][0], View.M[1][0], View.M[2][0]);

	const FCameraProjection Projection{
		GameCamera->GetIsOrthogonal() ? EProjectionMode::Orthographic : EProjectionMode::Perspective,
		GameCamera->GetFieldOfView(),
		GameCamera->GetOrthoWidth(),
		GameCamera->GetNearZ(),
		GameCamera->GetFarZ() };
	MultipleViewportsAdapter.SetViewCameraOverride(PIEViewIndex, GameCamera->GetWorldLocation(), Forward, Projection);
}

// 화면을 표시하고 UI 변경 후 View 설정을 보관한다.
void UEditorEngine::EndFrame()
{
	PresentFrame();
	// 메인 Swapchain은 FEngineLoop가 VSync 설정대로 Present하므로 PIE 창은 기다리지 않고 올린다.
	if (bPIEWindowRendered && PIESwapchain)
		PIESwapchain->SwapBuffers(0);
	// UI 변경 후 설정을 복사해 종료 시 카메라 수명에 의존하지 않는다.
	SettingsPanel->CaptureViewportSettings();
	// 프로파일러 반영
	FStatRegistry::EndFrame();
}

FWorldContext& UEditorEngine::GetEditorWorldContext()
{
	for (int32 i = 0; i < WorldList.Num(); ++i)
	{
		if (WorldList[i].WorldType == EWorldType::Editor)
		{
			return WorldList[i];
		}
	}

	return CreateNewWorldContext(EWorldType::Editor);
}

FWorldContext* UEditorEngine::GetPIEWorldContext(int32 WorldPIEInstance)
{
	for (FWorldContext& WorldContext : WorldList)
	{
		if (WorldContext.WorldType == EWorldType::PIE)
		{
			return &WorldContext;
		}
	}

	return nullptr;
}

// 입력 View의 Ray와 피킹으로 Gizmo·공유 선택을 갱신한다.
void UEditorEngine::RenderOpaquePass(int32 ViewIndex, const FRenderingInfo& ViewRenderingInfo, const FViewConstants& ViewConstants, FRenderQueue& RenderQueue)
{
	const FMatrix& ViewProjection = ViewConstants.ViewProjection;
	const FVector& ViewCameraLocation = ViewConstants.CameraWorldPos;
	FRenderingInfo HDRInfo = ViewRenderingInfo;
	HDRInfo.ColorRenderTargets[0].Texture = GetHDRTargetForView(ViewIndex);
	RenderCommand::BeginRenderPass(HDRInfo);

	const bool bDrawPrimitives = SettingsPanel->GetSettings().ShowFlags.IsEnabled(EEngineShowFlag::Primitives);
	// 삼각형 연결은 유지하고 View별 Fill Mode만 선택한다.
	const ERasterizerState SceneRasterizerState = MultipleViewportsAdapter.IsViewWireframe(ViewIndex)
		? ERasterizerState::Wireframe : ERasterizerState::SolidBack;
	// View별 조명 뷰모드. 불투명·반투명 모두 이 값으로 UberLit 변형을 고른다.
	Renderer->SetViewMode(MultipleViewportsAdapter.GetViewMode(ViewIndex));

	// 렌더 루프 — 반드시 RenderAll보다 먼저
	SkyboxRenderer->OnRender(ViewProjection, ViewCameraLocation);
	if (bDrawPrimitives)
	{
		RenderCommand::SetRasterizerState(SceneRasterizerState);
		RenderCommand::SetBlendState(EBlendState::Opaque);
		RenderCommand::SetDepthStencilState(EDepthStencilState::Default);

		RenderCommand::SetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		// 반투명은 Grid 뒤에 합성되어야 하므로 불투명만 먼저 그린다.
		Renderer->RenderQueueSorting(RenderQueue, ViewConstants);

		// 2.5D는 타일의 실제 깊이가 필요하므로 불투명 깊이를 먼저 채운 뒤 컬링한다.
		FTexture2D* DepthTexture = ViewRenderingInfo.DepthStencil.Texture;
		const bool bDepthPrepass = Renderer->GetLightCullingMode() == ELightCullingMode::Tiled25D && DepthTexture;
		if (bDepthPrepass)
		{
			Renderer->RenderDepth(ViewProjection);
			// DSV로 묶인 텍스처는 SRV로 읽을 수 없으므로 렌더 타깃을 푼다
			RenderCommand::EndRenderPass(HDRInfo);
		}

		Renderer->CullLights(
			MultipleViewportsAdapter.GetEngineView(ViewIndex),
			MultipleViewportsAdapter.GetEngineProjection(ViewIndex),
			ViewRenderingInfo.ViewportSetting.Width,
			ViewRenderingInfo.ViewportSetting.Height,
			bDepthPrepass ? DepthTexture : nullptr);

		if (bDepthPrepass)
		{
			// 스카이박스와 Prepass 깊이를 지우지 않도록 Load로 다시 묶는다
			FRenderingInfo ResumeInfo = HDRInfo;
			for (FRenderingDesc& Color : ResumeInfo.ColorRenderTargets)
				Color.LoadOp = ERenderTargetLoadOp::Load;
			ResumeInfo.DepthStencil.LoadOp = ERenderTargetLoadOp::Load;
			RenderCommand::BeginRenderPass(ResumeInfo);
		}

		Renderer->RenderOpaque(ViewProjection);
		// 장면 Wireframe이 Grid·Gizmo·UI로 전파되지 않도록 복원한다.
		RenderCommand::SetRasterizerState(ERasterizerState::SolidBack);
	}



	if (bDrawPrimitives)
	{
		// Grid 파이프라인이 바꾼 상태를 장면 기준으로 되돌린 뒤 반투명을 먼 것부터 그린다.
		RenderCommand::SetRasterizerState(SceneRasterizerState);
		RenderCommand::SetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		Renderer->RenderTranslucent(ViewProjection);
		RenderCommand::SetRasterizerState(ERasterizerState::SolidBack);
	}



	RenderCommand::EndRenderPass(HDRInfo);
}

void UEditorEngine::RenderFogPass(const int32 ViewIndex, UWorld& ViewWorld, const FRenderingInfo& ViewRenderingInfo, const FVector& ViewCameraLocation, const FMatrix& ViewProjection, FRenderQueue& RenderQueue)
{
	FRenderingInfo HDRInfo = ViewRenderingInfo;
	HDRInfo.ColorRenderTargets[0].Texture = GetHDRTargetForView(ViewIndex);
	const TArray<FExponentialHeightFogSceneInfo>& SceneFogInfos = ViewWorld.GetScene().FogInfos;

	if (SceneFogInfos.Num() == 0)
		return;

	FRenderingInfo PassInfo = HDRInfo;

	//Load는 기존 내용을 그대로 두고 그 위에 그림을 그린다는뜻
	for (FRenderingDesc& Color : PassInfo.ColorRenderTargets)
		Color.LoadOp = ERenderTargetLoadOp::Load;

	PassInfo.DepthStencil.Texture = nullptr;
	RenderCommand::BeginRenderPass(PassInfo);

	FogRenderer->OnRender(HDRInfo.DepthStencil.Texture, ViewProjection, ViewCameraLocation, SceneFogInfos[0].FogInfo);

	//Fog는 여러개있더라도 1개의 Fog만 인식해서 그리도록 해야한다.

	RenderCommand::EndRenderPass(PassInfo);
}

void UEditorEngine::RenderOverlayPass(const int32 ViewIndex, UWorld& ViewWorld, const FRenderingInfo& ViewRenderingInfo, const FMatrix& ViewProjection, const FVector& ViewCameraLocation, const FVector& ViewCameraForward, FRenderQueue& RenderQueue, FRenderQueue& IconQueue, FRenderQueue& ArrowQueue)
{
	// 실행 중인 PIE View에는 Grid·Gizmo·Outline 같은 에디터 표시를 그리지 않는다.
	// 별도 PIE 창은 편집 입력이 없으므로 일시정지 중에도 게임 화면만 보인다.
	const bool bShowEditorOverlay = !IsPlayingView(ViewIndex) && ViewIndex != FMultipleViewportsAdapter::ExternalViewIndex;
	const FEngineShowFlags& ShowFlags = SettingsPanel->GetSettings().ShowFlags;
	FRenderingInfo PassInfo = ViewRenderingInfo;

	for (FRenderingDesc& Color : PassInfo.ColorRenderTargets)
		Color.LoadOp = ERenderTargetLoadOp::Load;

	PassInfo.DepthStencil.LoadOp = ERenderTargetLoadOp::Load;


	RenderCommand::BeginRenderPass(PassInfo);

	if (bShowEditorOverlay && ShowFlags.IsEnabled(EEngineShowFlag::Grid) && !ShowFlags.IsEnabled(EEngineShowFlag::DepthView))
	{
		// 라인 배처는 매 프레임 한 번만 비우고 한 번만 그린다.
		// 바운딩박스는 그 안에 쌓이는 여러 항목 중 하나일 뿐이다.
		LineBatcher->BeginFrame();

		if (ShowFlags.IsEnabled(EEngineShowFlag::Bounds))
		{
			LineBatcher->BuildVertexBuffer(ViewWorld);
			ViewWorld.GetPathTracker().OnRender(LineBatcher.get());
		}

		// 선택된 액터가 라이트면 원뿔을 같이 쌓는다
		if (Gizmo->GetTarget())
		{
			if (ALightActorBase* LightActor = Cast<ALightActorBase>(Gizmo->GetTarget()->GetOwner()))
			{
				LightActor->GetLightComponent()->DrawDebug(LineBatcher.get());
			}
		}

		LineBatcher->OnRender(ViewProjection);
	}

	//그리드
	//TODO : 그리드도 안개가 적용되어야하나..?
	if (bShowEditorOverlay && ShowFlags.IsEnabled(EEngineShowFlag::Grid))
	{
		const EGridPlane GridPlane = MultipleViewportsAdapter.GetGridPlane(ViewIndex);

		if (ShowFlags.IsEnabled(EEngineShowFlag::PSGrid) && !MultipleViewportsAdapter.IsOrthographic(ViewIndex))
		{
			GridRenderer->OnRenderPSGrid(ViewProjection, ViewCameraLocation, SettingsPanel->GetSettings(), PassInfo.ViewportSetting);
		}
		else
		{
			GridRenderer->OnRenderBatchGrid(
				ViewProjection,
				ViewCameraLocation,
				ViewCameraForward,
				GridPlane,
				static_cast<float>(SettingsPanel->GetSettings().GridSpacing),
				!MultipleViewportsAdapter.IsOrthographic(ViewIndex) ||
				MultipleViewportsAdapter.GetCameraPreset(ViewIndex) == EMultipleViewportsCameraPreset::OrthographicView,
				PassInfo.ViewportSetting
			);
		}
	}

	// Editor Icon (안개, Light Actor) + 빛 방향 Arrow
	// TODO: 모든 프리미티브를 공통 큐로 통합하고 패스별로 렌더하도록 별도 IconQueue·ArrowQueue를 제거한다.
	if (bShowEditorOverlay)
	{
		RenderCommand::SetRasterizerState(ERasterizerState::SolidBack);
		RenderCommand::SetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// 아이콘 먼저 그리기 (반투명)
		Renderer->RenderQueueSorting(IconQueue, ViewProjection, ViewCameraLocation);
		Renderer->RenderTranslucent(ViewProjection);
		
		// 화살표 그리기 (불투명)
		Renderer->RenderQueueSorting(ArrowQueue, ViewProjection, ViewCameraLocation);
		Renderer->RenderOpaque(ViewProjection);
		Renderer->RenderTranslucent(ViewProjection);	// 패킷 뒷정리
	}

	// TextRenderComponent 렌더링
	for (TObjectIterator<UTextRenderComponent> TextComponent; TextComponent; ++TextComponent)
	{
		if (!TextComponent || !TextComponent->GetFont() || !TextComponent->IsVisible())
		{
			continue;
		}
		// 에디터·PIE 월드의 컴포넌트가 함께 순회되므로 이 View의 월드만 그린다.
		if (!TextComponent->GetOwner() || TextComponent->GetOwner()->GetWorld() != &ViewWorld)
		{
			continue;
		}

		TextRenderer->OnRender(
			TextComponent->GetText(),
			TextComponent->GetWorldMatrix(),
			TextComponent->GetTextSize(),
			*TextComponent->GetFont(),
			ViewProjection
		);
	}

	// 선택 대상은 한 월드에만 속하므로 그 월드를 그리는 View에만 표시한다.
	const auto IsInViewWorld = [&ViewWorld](const UActorComponent* Component)
	{
		return Component && Component->GetOwner() && Component->GetOwner()->GetWorld() == &ViewWorld;
	};

	// 스텐실 기반이라 선택 대상의 가시성이 꺼져 있어도 외곽선만 그린다.
	if (bShowEditorOverlay && IsInViewWorld(Outline->GetTarget()))
	{
		OutlineRenderer->OnRender(*Outline, ViewProjection, PassInfo.ViewportSetting);
	}

	if (bShowEditorOverlay && IsInViewWorld(Gizmo->GetTarget()))
	{
		//auto Target = Cast<UPrimitiveComponent>(Gizmo->GetTarget());
		//FBox box = Target->CalcBounds();

		RenderCommand::ClearDepthStencil(ViewRenderingInfo.DepthStencil.Texture);

		GizmoRenderer->OnRender(
			*Gizmo,
			ViewProjection,
			ViewCameraLocation,
			MultipleViewportsAdapter.IsOrthographic(ViewIndex));
	}

	RenderCommand::ClearDepthStencil(PassInfo.DepthStencil.Texture);

	if (ShowFlags.IsEnabled(EEngineShowFlag::UUID))
	{
		for (AActor* Actor : ViewWorld.GetPersistentLevel()->GetActors())
		{
			if (!Actor)
				continue;

			FBox ActorBounds;
			Actor->TryGetActorBounds(ActorBounds);
			UUIDLocation.X = (ActorBounds.Min.X + ActorBounds.Max.X) * 0.5f;
			UUIDLocation.Y = (ActorBounds.Min.Y + ActorBounds.Max.Y) * 0.5f;
			UUIDLocation.Z = ActorBounds.Max.Z + 0.5f;

			FString Text =
				"UUID : " + std::to_string(Actor->GetUUID());

			TextRenderer->BuildTextMesh(
				Text,
				0.5f,
				*SystemFont
			);

			const FMatrix BillboardWorld = MultipleViewportsAdapter.BuildEngineBillboardMatrix(ViewIndex, UUIDLocation, 1.0f, 1.0f);
			TextRenderer->OnRender(Text, BillboardWorld, 0.5f, *SystemFont, ViewProjection);
		}
	}

	RenderCommand::EndRenderPass(PassInfo);
}

void UEditorEngine::RenderFXAAPass(const int32 ViewIndex, const FRenderingInfo& ViewRenderingInfo)
{
	FTexture2D* SceneColor = ViewRenderingInfo.ColorRenderTargets[0].Texture;
	FTexture2D* FxaaTarget = GetFxaaTargetForView(ViewIndex);   // 뷰별 임시 텍스처
	if (!SceneColor || !FxaaTarget)
		return;

	// 출력은 임시 텍스처, 깊이 없음, 어차피 전부 덮어쓰니 Clear 불필요
	FRenderingInfo PassInfo;
	PassInfo.ViewportSetting = ViewRenderingInfo.ViewportSetting;

	FRenderingDesc ColorDesc{};
	ColorDesc.Texture = FxaaTarget;
	ColorDesc.LoadOp = ERenderTargetLoadOp::DontCare;
	PassInfo.ColorRenderTargets.Add(ColorDesc);
	PassInfo.DepthStencil.Texture = nullptr;

	//장면 텍스처를 읽어서 임시 텍스처에 FXAA 결과를 그린다
	RenderCommand::BeginRenderPass(PassInfo);
	FXAARenderer->OnRender(SceneColor);
	RenderCommand::EndRenderPass(PassInfo);

	// SceneColor가 SRV로 남아 있으면 Overlay 패스에서 RTV로 쓸 때 D3D가 강제로 해제하므로 먼저 비운다
	RenderCommand::UnbindShaderResource(0, EShaderBindFlagBits::Pixel);

	RenderCommand::CopyTexture(SceneColor, FxaaTarget);
}

void UEditorEngine::RenderToneMapPass(const int32 ViewIndex, const FRenderingInfo& ViewRenderingInfo)
{
	FTexture2D* LDRColor = ViewRenderingInfo.ColorRenderTargets[0].Texture;
	FTexture2D* HDRColor = GetHDRTargetForView(ViewIndex);   // 뷰별 임시 텍스처
	if (!LDRColor || !HDRColor)
		return;

	// 출력은 임시 텍스처, 깊이 없음, 어차피 전부 덮어쓰니 Clear 불필요
	FRenderingInfo PassInfo;
	PassInfo.ViewportSetting = ViewRenderingInfo.ViewportSetting;

	FRenderingDesc ColorDesc{};
	ColorDesc.Texture = LDRColor;
	ColorDesc.LoadOp = ERenderTargetLoadOp::DontCare;
	PassInfo.ColorRenderTargets.Add(ColorDesc);
	PassInfo.DepthStencil.Texture = nullptr;

	// 뷰모드에 따라 톤 매핑 방식을 고른다.
	// - Lit: 노출 → ACES → sRGB 인코딩
	// - Unlit: 텍스처 원본 색이 보이도록 sRGB 인코딩만
	// - WorldNormal·Light Heatmap: 셰이더가 쓴 디버그 값을 그대로
	EToneMapMode ToneMapMode = EToneMapMode::ACES;
	const EViewMode ViewMode = MultipleViewportsAdapter.GetViewMode(ViewIndex);
	if (ViewMode == EViewMode::WorldNormal || SettingsPanel->GetSettings().LightDebugMode == 1)
		ToneMapMode = EToneMapMode::Passthrough;
	else if (ViewMode == EViewMode::Unlit)
		ToneMapMode = EToneMapMode::EncodeOnly;

	//HDR 장면 텍스처를 읽어서 LDR 뷰 텍스처에 ToneMap 결과를 그린다
	RenderCommand::BeginRenderPass(PassInfo);
	ToneMapRenderer->SetExposure(SettingsPanel->GetSettings().Exposure);
	ToneMapRenderer->OnRender(HDRColor, ToneMapMode);
	RenderCommand::EndRenderPass(PassInfo);

	// SceneColor가 SRV로 남아 있으면 Overlay 패스에서 RTV로 쓸 때 D3D가 강제로 해제하므로 먼저 비운다
	RenderCommand::UnbindShaderResource(0, EShaderBindFlagBits::Pixel);
}

void UEditorEngine::RenderDepthPass(const int32 ViewIndex, const FRenderingInfo& ViewRenderingInfo, const FMatrix& ViewProjection, const FVector& ViewCameraLocation, const FVector& ViewCameraForward)
{
	FRenderingInfo PassInfo = ViewRenderingInfo;

	for (FRenderingDesc& Color : PassInfo.ColorRenderTargets)
		Color.LoadOp = ERenderTargetLoadOp::DontCare;
	PassInfo.DepthStencil.Texture = nullptr;

	RenderCommand::BeginRenderPass(PassInfo);
	DepthViewRenderer->OnRender(ViewRenderingInfo.DepthStencil.Texture, ViewProjection, ViewCameraLocation, ViewCameraForward);
	RenderCommand::EndRenderPass(PassInfo);
}

// 입력 View의 Ray와 피킹으로 Gizmo·공유 선택을 갱신한다.
void UEditorEngine::UpdateGizmoAndPicking()
{
	// Delete는 BeginFrame에서 한 번만 처리하고 여기서는 View 입력만 다룬다.
	const int32 ViewIndex = MultipleViewportsAdapter.GetActiveViewIndex();
	if (ViewIndex == InvalidViewIndex || !ViewportsPanel->IsHovered())
		return;
	// 실행 중인 PIE View는 편집하지 않는다. 일시정지 중이면 PlayWorld를 피킹한다.
	if (IsPlayingView(ViewIndex))
		return;

	const FVector2 LocalMousePosition = ViewportsPanel->GetLocalMousePosition();
	FRay Ray{};
	if (!MultipleViewportsAdapter.TryGetActiveViewRay(LocalMousePosition, Ray))
		return;

	const FRect& Rect = MultipleViewportsAdapter.GetViewRect(ViewIndex);
	const FVector2 ViewLocalMouse(
		LocalMousePosition.X - Rect.X,
		LocalMousePosition.Y - Rect.Y);
	const FMatrix ViewProjection = MultipleViewportsAdapter.GetEngineViewProjection(ViewIndex);
	bool bMouseDown = FInputSystem::IsMouseDown(EMouseButton::Left);

	// 다른 월드의 선택 대상은 이 View에 Gizmo가 보이지 않으므로 조작하지 않는다.
	UWorld* ViewWorld = GetWorldForView(ViewIndex);
	const USceneComponent* GizmoTarget = Gizmo->GetTarget();
	const bool bGizmoInView = !GizmoTarget ||
		(GizmoTarget->GetOwner() && GizmoTarget->GetOwner()->GetWorld() == ViewWorld);

	if (bGizmoInView)
	{
		Gizmo->Update(
			Ray,
			ViewLocalMouse,
			ViewProjection,
			static_cast<int>(Rect.Width),
			static_cast<int>(Rect.Height),
			bMouseDown,
			MultipleViewportsAdapter.GetEngineCameraLocation(ViewIndex),
			MultipleViewportsAdapter.IsOrthographic(ViewIndex));
	}

	const bool bGizmoBlocksPick = bGizmoInView && (Gizmo->IsUsing() || Gizmo->GetHoveredAxis() >= 0);
	if (FInputSystem::IsMousePressed(EMouseButton::Left) && !bGizmoBlocksPick)
	{
		// Billboard는 렌더링에 사용한 View와 동일한 행렬로 교차를 판정한다.
		const auto ResolveBillboardTransform = [](const UBillboardComponent& Billboard, const void* Context) -> FMatrix
		{
			const auto& Adapter = *static_cast<const FMultipleViewportsAdapter*>(Context);
			const FVector Scale = Billboard.GetWorldScale3D();
			return Adapter.BuildEngineBillboardMatrix(Adapter.GetActiveViewIndex(),
				Billboard.GetWorldLocation(), Scale.Y, Scale.Z);
		};

		// Gizmo에 사용한 Ray를 재사용하고, 월드의 피킹 결과를 선택에 직접 반영한다.
		FHitResult Hit;
		const bool bHit = ViewWorld->LineTraceSingle(Ray, Hit, ResolveBillboardTransform, &MultipleViewportsAdapter);
		OutlinerPanel->SelectActor(bHit ? Hit.HitComponent->GetOwner() : nullptr);
	}
}

// View 행렬로 Scene·Grid·Gizmo·텍스트·Outline을 렌더한다.
void UEditorEngine::RenderFrame(const int32 ViewIndex, UWorld& ViewWorld, const FRenderingInfo& ViewRenderingInfo, 
	const FMatrix& ViewProjection, const FVector& ViewCameraLocation, const FVector& ViewCameraForward, FRenderQueue& RenderQueue, 
	FRenderQueue& IconQueue, FRenderQueue& ArrowQueue)
{
	const FEngineShowFlags& ShowFlags = SettingsPanel->GetSettings().ShowFlags;

	// GPU의 int4·float4와 Component의 분할 상한이 일치해야 한다.
	static_assert(UDirectionalLightComponent::MaxShadowCascades == 4);

	FViewConstants ViewConstants{};
	ViewConstants.ViewProjection = ViewProjection;
	ViewConstants.CameraWorldPos = ViewCameraLocation;
	ViewConstants.CameraForward = ViewCameraForward;
	// 패널과 별도 PIE 모두 해당 Client의 표시 상태를 전달한다.
	ViewConstants.VisualizeShadowCascades = MultipleViewportsAdapter.GetViewShowFlags(ViewIndex).IsEnabled(EEngineShowFlag::ShadowCascades) ? 1u : 0u;


	// 작업 생성과 동일한 표시 조건으로 Directional 수신 데이터를 준비한다.
	const EViewMode ViewMode = MultipleViewportsAdapter.GetViewMode(ViewIndex);
	const bool bUseDirectionalShadows =
		ShowFlags.IsEnabled(EEngineShowFlag::Primitives)
		&& ShowFlags.IsEnabled(EEngineShowFlag::DynamicShadows)
		&& (ViewMode == EViewMode::Lit_Lambert || ViewMode == EViewMode::Lit_BlinnPhong);

	const FDirectionalLightSceneProxy* Directional = bUseDirectionalShadows ? ViewWorld.GetScene().GetPrimaryDirectionalLight() : nullptr;

	if (Directional && Directional->ShouldCastShadow())
	{
		TArray<float> SplitDistances;

		if (MultipleViewportsAdapter.IsOrthographic(ViewIndex))
		{
			// 직교 작업 생성과 같은 논리 원점 앞뒤의 단일 구간이다.
			const auto* Light = static_cast<const UDirectionalLightComponent*>(Directional->GetLightComponent());
			const float ShadowDistance = Light->GetShadowDistance();
			SplitDistances.Add(-ShadowDistance);
			SplitDistances.Add(ShadowDistance);
		}
		else
		{
			// 작업 생성에 사용한 것과 같은 EffectiveCamera·분할 규칙을 사용한다.
			const FViewCamera& Camera =	MultipleViewportsAdapter.GetEffectiveCamera(ViewIndex);
			Directional->CalculateCascadeSplitDistances(Camera.Projection.NearClip, Camera.Projection.FarClip, SplitDistances);
		}

		// 분할 실패 시 배열이 비어 있으므로 기본 CascadeCount 0을 유지한다.
		if (SplitDistances.Num() >= 2)
		{
			const uint32 ViewId = static_cast<uint32>(ViewIndex + 1);
			const uint32 CascadeCount = static_cast<uint32>(SplitDistances.Num() - 1);
			ViewConstants.DirectionalCascadeCount = CascadeCount;
			ViewConstants.DirectionalShadowNearDistance = SplitDistances[0];
			if (CascadeCount > 1)
			{
				const auto* Light = static_cast<const UDirectionalLightComponent*>(Directional->GetLightComponent());
				ViewConstants.DirectionalCascadeTransitionFraction = Light->GetCascadeTransitionFraction();
			}
			for (uint32 CascadeIndex = 0; CascadeIndex < CascadeCount; ++CascadeIndex)
			{
				// GPU 인덱스는 연속 번호라고 가정하지 않고 각각 조회한다.
				ViewConstants.DirectionalShadowIndices[CascadeIndex] = Renderer->GetShadowIndex(*Directional, CascadeIndex, ViewId);
				ViewConstants.DirectionalCascadeFarDistances[CascadeIndex] = SplitDistances[CascadeIndex + 1];
			}
		}
	}

	// 준비한 View 상수를 장면 패스에 전달한다.
	RenderOpaquePass(ViewIndex, ViewRenderingInfo, ViewConstants, RenderQueue);

	if (ShowFlags.IsEnabled(EEngineShowFlag::DepthView))
	{
		RenderDepthPass(ViewIndex, ViewRenderingInfo, ViewProjection, ViewCameraLocation, ViewCameraForward);

	}
	else
	{
		//직교인 경우 안개 Pass를 그리지 않는다.
		if (!MultipleViewportsAdapter.IsOrthographic(ViewIndex) && ShowFlags.IsEnabled(EEngineShowFlag::Fog))
			// 2. 안개 Pass
			RenderFogPass(ViewIndex, ViewWorld, ViewRenderingInfo, ViewCameraLocation, ViewProjection, RenderQueue);

		// 3. ToneMap(HDR) Pass
		RenderToneMapPass(ViewIndex, ViewRenderingInfo);

		// 3. FXAA Pass
		if (ShowFlags.IsEnabled(EEngineShowFlag::FXAA))
			RenderFXAAPass(ViewIndex, ViewRenderingInfo);
	}

	// 4. 오버레이 Pass
	RenderOverlayPass(ViewIndex, ViewWorld, ViewRenderingInfo, ViewProjection, ViewCameraLocation, ViewCameraForward, RenderQueue, IconQueue, ArrowQueue);

}


// View Texture가 포함된 UI를 Swapchain 백버퍼에 합성한다. Present는 FEngineLoop가 한다.
void UEditorEngine::PresentFrame()
{
	// Swapchain 렌더링
	RenderCommand::BeginRenderPass(MainWindowSC->GetRenderingInfo());

	{
		SCOPE_CYCLE_COUNTER(STAT_ImGui);

		ImGuiRenderer->Begin();

		EditorUI->OnRender();

		ImGuiRenderer->End();
	}

	RenderCommand::EndRenderPass(MainWindowSC->GetRenderingInfo());
}

// ImGui를 정리한다. UObject 일괄 삭제와 공용 자원·Device 정리는 FEngineLoop가 이어서 한다.
void UEditorEngine::PreExit()
{
	EndPlayMap();
	EditorWorld->ClearWorld();
	// PIE 창이 열린 채 종료하면 Device가 살아 있을 때 Swapchain과 타깃을 먼저 정리한다.
	DestroyPIEWindow();
	ImGuiRenderer->Shutdown();
}

// 선택과 Gizmo 참조를 정리한 뒤 Actor를 삭제한다.
void UEditorEngine::DeleteActor(AActor* Actor)
{
	if (!Actor)
		return;
	// 실행 중 삭제는 게임 로직이 들고 있는 참조를 깨뜨릴 수 있어 Pause 상태에서만 허용한다.
	if (IsPlaying() && !bIsPaused)
		return;

	OutlinerPanel->SelectActor(nullptr);
	DetailsPanel->SelectComponent(nullptr);

	Actor->Destroy();
}

void UEditorEngine::DeleteComponent(UActorComponent* Component)
{
	if (!Component)
		return;
	if (IsPlaying() && !bIsPaused)
		return;
	DetailsPanel->SelectComponent(nullptr);
	Outline->SetTarget(nullptr);
	Component->GetOwner()->DestroyComponent(Component);
}

void UEditorEngine::StartPlayInEditorSession()
{
	// 별도 창 모드면 패널 View는 모두 에디터 월드로 두고 창 하나에만 PIE 월드를 그린다.
	// 창을 만들지 못하면 선택한 View에서 실행한다.
	if (PlayMode == EPlayModeType::InEditorFloating && CreatePIEWindow())
		PIEViewIndex = FMultipleViewportsAdapter::ExternalViewIndex;
	else
		// 지금 편집 중인 View 하나만 PIE 월드를 보여준다.
		PIEViewIndex = MultipleViewportsAdapter.GetEditorViewIndex();

	FWorldContext& PIEContext = CreateNewWorldContext(EWorldType::PIE);
	PlayWorld = CreatePIEWorldByDuplication(PIEContext, EditorWorld);

	OnActiveWorldChanged();
}

UWorld* UEditorEngine::CreatePIEWorldByDuplication(FWorldContext& PIEContext, UWorld* InEditorWorld)
{
	UWorld* NewPIEWorld = UWorld::GetDuplicatedWorldForPIE(InEditorWorld);
	PIEContext.SetCurrentWorld(NewPIEWorld);
	return NewPIEWorld;
}

void UEditorEngine::OnActiveWorldChanged()
{
	ResetSceneSelection();
	OutlinerPanel->SetWorld(GetActiveWorld());
}

void UEditorEngine::EndPlayMap()
{
	if (!PlayWorld)
		return;

	ResetSceneSelection();
	if (PIEViewIndex != InvalidViewIndex)
		MultipleViewportsAdapter.ClearViewCameraOverride(PIEViewIndex);
	PlayWorld->EndPlay();
	PlayWorld->ClearWorld();
	DestroyWorldContext(PlayWorld);
	delete PlayWorld;
	PlayWorld = nullptr;
	PIEViewIndex = InvalidViewIndex;
	DestroyPIEWindow();
	OnActiveWorldChanged();
}

bool UEditorEngine::CreatePIEWindow()
{
	constexpr int32 DefaultWidth = 1280;
	constexpr int32 DefaultHeight = 720;

	TUniquePtr<FWindow> NewWindow = MakeUnique<FWindow>();
	if (!NewWindow->Create(::GetModuleHandleW(nullptr), DefaultWidth, DefaultHeight, L"Hitori Engine - PIE"))
	{
		HTR_LOG(Error, "Failed to create PIE window. Falling back to the selected viewport.");
		return false;
	}
	// 닫기 버튼은 PIE만 끝내고, ImGui는 메인 창 입력만 받도록 훅을 끊는다.
	NewWindow->SetQuitOnClose(false);
	NewWindow->SetReceivesWndProcHook(false);

	PIEWindow = std::move(NewWindow);
	PIESwapchain = MakeUnique<FSwapchain>(GetEngineLoop().GetRenderDevice(), PIEWindow.get());
	PIETargetWidth = 0;
	PIETargetHeight = 0;
	PIEWindow->Show();
	return true;
}

void UEditorEngine::DestroyPIEWindow()
{
	if (!PIEWindow)
		return;

	MultipleViewportsAdapter.SetExternalViewSize(0, 0);
	MultipleViewportsAdapter.ClearViewCameraOverride(FMultipleViewportsAdapter::ExternalViewIndex);

	PIERenderingInfo = {};
	PIEColorTarget.reset();
	PIEDepthTarget.reset();
	PIEFxaaTarget.reset();
	PIEHDRTarget.reset();
	PIETargetWidth = 0;
	PIETargetHeight = 0;
	bPIEWindowRendered = false;

	// Swapchain이 창 핸들을 출력 대상으로 쥐고 있으므로 창보다 먼저 놓는다.
	PIESwapchain.reset();
	PIEWindow->Destroy();
	PIEWindow.reset();

	// PIE 창이 포커스를 갖고 있었다면 에디터로 돌려준다.
	::SetForegroundWindow(MainWindow->GetHandle());
}

bool UEditorEngine::UpdatePIEWindowTargets()
{
	const uint32 Width = PIEWindow->GetWidth();
	const uint32 Height = PIEWindow->GetHeight();
	// 최소화 중에는 그릴 대상이 없다.
	if (Width == 0 || Height == 0 || ::IsIconic(PIEWindow->GetHandle()))
	{
		MultipleViewportsAdapter.SetExternalViewSize(0, 0);
		return false;
	}

	if (PIEWindow->CheckResized())
		PIESwapchain->Resize(Width, Height);

	if (Width != PIETargetWidth || Height != PIETargetHeight)
	{
		// 패널 View 슬롯과 같은 구성이다. Fog가 깊이를 읽으므로 깊이는 TYPELESS로 만든다.
		D3D11_TEXTURE2D_DESC Desc{};
		Desc.Width = Width;
		Desc.Height = Height;
		Desc.MipLevels = 1;
		Desc.ArraySize = 1;
		Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		Desc.SampleDesc.Count = 1;
		Desc.Usage = D3D11_USAGE_DEFAULT;
		Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		PIEColorTarget = RenderCommand::CreateTexture2D(Desc);
		PIEFxaaTarget = RenderCommand::CreateTexture2D(Desc);

		Desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
		Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
		PIEDepthTarget = RenderCommand::CreateTexture2D(Desc);

		Desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		PIEHDRTarget = RenderCommand::CreateTexture2D(Desc);

		PIERenderingInfo.ColorRenderTargets.Reset();
		PIERenderingInfo.ViewportSetting.Width = Width;
		PIERenderingInfo.ViewportSetting.Height = Height;
		FRenderingDesc ColorDesc{};
		ColorDesc.Texture = PIEColorTarget.get();
		PIERenderingInfo.ColorRenderTargets.Add(ColorDesc);
		PIERenderingInfo.DepthStencil.Texture = PIEDepthTarget.get();

		PIETargetWidth = Width;
		PIETargetHeight = Height;
	}

	MultipleViewportsAdapter.SetExternalViewSize(Width, Height);
	return true;
}

void UEditorEngine::CopyPIEWindowToBackbuffer()
{
	const FRenderingInfo& BackbufferInfo = PIESwapchain->GetRenderingInfo();
	if (BackbufferInfo.ColorRenderTargets.Num() == 0)
		return;

	// 백버퍼는 SRV가 없어 FXAA·Fog 입력으로 쓸 수 없으므로 오프스크린 결과를 통째로 복사한다.
	// 창 크기 변경 직후 한 프레임은 크기가 어긋날 수 있어 그때는 건너뛴다.
	FTexture2D* Backbuffer = BackbufferInfo.ColorRenderTargets[0].Texture;
	if (!Backbuffer || !PIEColorTarget ||
		BackbufferInfo.ViewportSetting.Width != PIETargetWidth ||
		BackbufferInfo.ViewportSetting.Height != PIETargetHeight)
		return;

	RenderCommand::CopyTexture(Backbuffer, PIEColorTarget.get());
	bPIEWindowRendered = true;
}

FTexture2D* UEditorEngine::GetFxaaTargetForView(const int32 ViewIndex) const
{
	if (ViewIndex == FMultipleViewportsAdapter::ExternalViewIndex)
		return PIEFxaaTarget.get();
	return ViewportsPanel->GetFxaaTarget(ViewIndex);
}

FTexture2D* UEditorEngine::GetHDRTargetForView(const int32 ViewIndex) const
{
	if (ViewIndex == FMultipleViewportsAdapter::ExternalViewIndex)
		return PIEHDRTarget.get();
	return ViewportsPanel->GetHDRTarget(ViewIndex);
}


// 씬 변경으로 무효화된 에디터의 선택 참조를 모두 해제한다.
void UEditorEngine::ResetSceneSelection()
{
	Gizmo->SetTarget(nullptr, false);
	Outline->SetTarget(nullptr);
	DetailsPanel->SetTarget(nullptr);
	OutlinerPanel->SelectActor(nullptr);
}

// 새 씬 생성이 성공하면 에디터 선택 상태를 초기화한다.
void UEditorEngine::CreateNewScene()
{
	if (IsPlaying())
	{
		HTR_LOG(Warning, "Cannot create a new scene during PIE. Stop the play session first.");
		return;
	}
	if (!FEditorFileUtils::NewScene(EditorWorld))
		return;

	ResetSceneSelection();
}

// 씬 불러오기가 성공하면 에디터 선택 상태를 초기화한다.
void UEditorEngine::OpenScene()
{
	if (IsPlaying())
	{
		HTR_LOG(Warning, "Cannot open a scene during PIE. Stop the play session first.");
		return;
	}
	if (!FEditorFileUtils::LoadScene(EditorWorld))
		return;

	ResetSceneSelection();
}

// 공통 파일 유틸리티로 현재 씬을 저장한다.
void UEditorEngine::SaveCurrentScene()
{
	if (IsPlaying())
	{
		HTR_LOG(Warning, "Cannot save during PIE. Stop the play session first.");
		return;
	}
	FEditorFileUtils::SaveScene(EditorWorld);
}

// 공통 파일 유틸리티로 새 경로에 씬을 저장한다.
void UEditorEngine::SaveSceneAs()
{
	if (IsPlaying())
	{
		HTR_LOG(Warning, "Cannot save during PIE. Stop the play session first.");
		return;
	}
	FEditorFileUtils::SaveSceneAs(EditorWorld);
}
