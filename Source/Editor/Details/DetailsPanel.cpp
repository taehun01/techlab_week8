#include "EnginePCH.h"
#include "Editor/Details/DetailsPanel.h"

#include "imgui_internal.h"
#include "Editor/HitoriEd/EditorDragDrop.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/ParticleSubUVComponent.h"
#include "Components/LightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SkyLightComponent.h"

#include "Components/RotatingMovementComponent.h"
#include "Components/ProjectileMovementComponent.h"
#include "Components/FireBallComponent.h"
#include "Asset/AssetManager.h"
#include "Render/Material.h"
#include "Render/Texture2D.h"
#include "Render/Renderer.h"
#include "Render/ShadowSettings.h"
#include "Text/Font.h"
#include "UObject/UObjectIterator.h"
#include "GameFramework/Actor.h"
#include "Core/EngineLog.h"
#include <algorithm>


namespace
{
	bool DrawAxisControl(const FString& _label, float& _value, float _speed, float _minValue, float _maxValue, float _resetValue, FVector4 _color)
	{
		bool isValueChanged = false;

		float lineHeight = GImGui->Font->LegacySize + GImGui->Style.FramePadding.y * 2.0f;
		ImVec4 color(_color.X, _color.Y, _color.Z, _color.W);
		ImVec2 buttonSize = { lineHeight + 2.0f , lineHeight };

		// 각 버튼의 색상 설정
		ImGui::PushStyleColor(ImGuiCol_Button, color);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ color.x + 0.1f, color.y + 0.1f, color.z + 0.1f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ color.x - 0.1f, color.y - 0.1f, color.z - 0.1f, 1.0f });

		// 굵은 폰트 적용
		if (ImGui::Button(_label.c_str(), buttonSize))
		{
			_value = _resetValue;
			isValueChanged = true;
		}
		ImGui::PopStyleColor(3);

		// 드래그 슬라이더
		ImGui::SameLine();
		std::string dragID = "##" + _label;
		isValueChanged |= ImGui::DragFloat(dragID.c_str(), &_value, _speed, _minValue, _maxValue, "%.2f");

		return isValueChanged;

	}

	bool DrawVector3Controller(const FString& _label, float* _values, float _resetValue, float _columnWidth)
	{
		ImGuiIO& io = ImGui::GetIO();
		auto boldFont = io.Fonts->Fonts[0];

		ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;

		bool isVectorChanged = false;

		ImGui::PushID(_label.c_str());
		if (ImGui::BeginTable(_label.c_str(), 2, flags)) // 고유 ID, 열 2개, 플래그
		{
			// ImGuiTableColumnFlags_WidthFixed: 초기 너비 고정
			// ImGuiTableColumnFlags_WidthStretch: 창 크기에 따라 너비 조절 (기본값)
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, _columnWidth);
			ImGui::TableSetupColumn("Content", ImGuiTableColumnFlags_WidthStretch);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text(_label.c_str()); // 왼쪽 열: 레이블

			ImGui::TableSetColumnIndex(1);
			// [컨트롤러 UI 코드] // 오른쪽 열: 컨트롤러
			ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 2,0 });
			ImGui::PushFont(boldFont);

			isVectorChanged |= DrawAxisControl("X", _values[0], 0.1f, 0.0f, 0.0f, _resetValue, { 0.8f, 0.1f, 0.1f, 1.0f });

			ImGui::PopItemWidth();

			ImGui::SameLine();
			isVectorChanged |= DrawAxisControl("Y", _values[1], 0.1f, 0.0f, 0.0f, _resetValue, { 0.1f, 0.8f, 0.1f, 1.0f });

			ImGui::PopItemWidth();

			ImGui::SameLine();
			isVectorChanged |= DrawAxisControl("Z", _values[2], 0.1f, 0.0f, 0.0f, _resetValue, { 0.1f, 0.1f, 0.8f, 1.0f });

			ImGui::PopItemWidth();

			ImGui::PopFont();
			ImGui::PopStyleVar();
			ImGui::EndTable();
		}

		ImGui::Columns(1);

		ImGui::PopID();

		return isVectorChanged;
	}

	// FRotator의 V는 (Pitch, Yaw, Roll) 순서지만 엔진은 Roll=X, Pitch=Y, Yaw=Z축이므로
	// X/Y/Z 칸이 실제 회전축과 맞도록 재배열해 편집한다.
	bool DrawRotatorAsXYZ(const FString& _label, FRotator& _rotator)
	{
		float Euler[3] = { _rotator.Roll, _rotator.Pitch, _rotator.Yaw };
		const bool isChanged = DrawVector3Controller(_label, Euler, 0.0f, 55.0f);
		if (isChanged)
		{
			_rotator.Roll = Euler[0];
			_rotator.Pitch = Euler[1];
			_rotator.Yaw = Euler[2];
		}
		return isChanged;
	}

	void DrawTextureSlot(UMaterial** MaterialPtr, int32 SlotIndex)
	{
		const float ThumbnailSize = 64.0f;

		UMaterial* Material = *MaterialPtr;
		UTexture2D* Texture = nullptr;
		if (Material && SlotIndex < static_cast<int32>(Material->Textures.Num()))
		{
			Texture = Material->Textures[SlotIndex];
		}

		ImGui::PushID(SlotIndex);

		if (Texture && Texture->GetResource())
		{
			ImGui::Image(
				(ImTextureID)Texture->GetResource()->GetSRV(),
				{ ThumbnailSize, ThumbnailSize });
		}
		else
		{
			ImGui::Button("Empty", { ThumbnailSize, ThumbnailSize });
		}

		// 바로 위 위젯이 드롭을 받는다
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Texture))
			{
				UTexture2D* DroppedTexture = *static_cast<UTexture2D**>(Payload->Data);

				// 머티리얼이 없으면 기본 머티리얼에서 시작한다
				if (!Material)
				{
					Material = UMaterial::CreateInstance(UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial"));
					*MaterialPtr = Material;
				}
				// 공유 에셋이면 지금 복제한다.
				// 이 시점까지 미루면 손대지 않은 오브젝트는 계속 공유 에셋을 쓴다.
				else if (Material->bIsInstance == false)
				{
					Material = UMaterial::CreateInstance(Material);
					*MaterialPtr = Material;
				}

				if (Material)
				{
					// 슬롯이 배열 범위 밖이면 그 자리까지 늘려 준다
					while (static_cast<int32>(Material->Textures.Num()) <= SlotIndex)
					{
						Material->Textures.Add(nullptr);
					}

					Material->Textures[SlotIndex] = DroppedTexture;
					Texture = DroppedTexture;
				}
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::SameLine();

		ImGui::BeginGroup();
		ImGui::Text("Slot %d", SlotIndex);

		if (Texture)
		{
			ImGui::TextWrapped("%s", Texture->GetPath().c_str());
		}
		else
		{
			ImGui::TextDisabled("(비어 있음)");
		}
		ImGui::EndGroup();

		ImGui::PopID();
	}

	FString GetAssetDisplayName(const FString& Path)
	{
		fs::path AssetPath(Path);
		if (AssetPath.stem() == "Atlas")
		{
			return AssetPath.parent_path().filename().string();
		}
		return AssetPath.stem().string();
	}

	void DrawFontSlot(UObject* Owner, UFont** FontPtr)
	{
		UFont* Current = *FontPtr;

		FString CurrentPath = Current ? Current->GetPath() : "";
		FString CurrentFileName = Current ? GetAssetDisplayName(CurrentPath) : "None";

		ImGui::SetNextItemWidth(-1.0f);

		bool bOpen = ImGui::BeginCombo("##Font", CurrentFileName.c_str());
		if (ImGui::IsItemHovered() && Current)
		{
			ImGui::SetTooltip("%s", CurrentPath.c_str());
		}

		if (bOpen)
		{
			for (TObjectIterator<UFont> Itr; Itr; ++Itr)
			{
				bool bSelected = (*Itr == Current);

				FString ItemPath = Itr->GetPath();
				FString ItemFileName = GetAssetDisplayName(ItemPath);

				ImGui::PushID(*Itr);

				if (ImGui::Selectable(ItemFileName.c_str(), bSelected))
				{
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("%s", ItemPath.c_str());
					}

					// 메모리를 직접 바꾸지 않고 SetFont를 거친다
					if (UTextRenderComponent* TextComponent = Cast<UTextRenderComponent>(Owner))
						TextComponent->SetFont(*Itr);
					else
						*FontPtr = *Itr;
				}
				ImGui::PopID();
				if (bSelected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
	}

	void DrawMeshSlot(UObject* Owner, UStaticMesh** StaticMeshPtr)
	{
		UStaticMesh* Current = *StaticMeshPtr;

		FString CurrentPath = Current ? Current->GetPath() : "";
		FString CurrentFileName = Current ? GetAssetDisplayName(CurrentPath) : "None";

		ImGui::SetNextItemWidth(-1.0f);

		bool bOpen = ImGui::BeginCombo("##StaticMesh", CurrentFileName.c_str());
		if (ImGui::IsItemHovered() && Current)
		{
			ImGui::SetTooltip("%s", CurrentPath.c_str());
		}

		if (bOpen)
		{
			for (TObjectIterator<UStaticMesh> Itr; Itr; ++Itr)
			{
				bool bSelected = (*Itr == Current);

				FString ItemPath = Itr->GetPath();
				FString ItemFileName = GetAssetDisplayName(ItemPath);

				ImGui::PushID(*Itr);

				if (ImGui::Selectable(ItemFileName.c_str(), bSelected))
				{
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("%s", ItemPath.c_str());
					}

					// 메모리를 직접 바꾸지 않고 SetFont를 거친다
					if (UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Owner))
						MeshComponent->SetStaticMesh(*Itr);
					else
						*StaticMeshPtr = *Itr;
				}
				ImGui::PopID();
				if (bSelected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
	}

	UMaterial* EnsureMaterialOverride(UMeshComponent* MeshComponent, int32 Slot, UMaterial* Effective, UMaterial* Override)
	{
		// 이 컴포넌트 전용 인스턴스가 아니면 지금 적용 중인 머티리얼을 복제해서 덮어쓰기로 만든다.
		// 메시의 머티리얼은 같은 메시를 쓰는 모든 액터가 공유하므로 직접 고치면 안 된다.
		if (Override && Override->bIsInstance)	return Override;

		UMaterial* Source = Effective ? Effective : UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");

		Override = UMaterial::CreateInstance(Source);
		MeshComponent->SetMaterial(Slot, Override);

		return Override;
	}

	bool SetMaterialTexture(UMeshComponent* MeshComponent, int32 MaterialSlot, int32 TextureSlot, UTexture2D* Texture, UMaterial*& Effective, UMaterial*& Override) {
		UTexture2D* Current{ Effective && Effective->Textures.IsValidIndex(TextureSlot) ? Effective->Textures[TextureSlot] : nullptr };
		if (Current == Texture) {
			return false;
		}

		Override = EnsureMaterialOverride(MeshComponent, MaterialSlot, Effective, Override);
		if (!Override) {
			return false;
		}

		while (!Override->Textures.IsValidIndex(TextureSlot)) {
			Override->Textures.Add(nullptr);
		}
		Override->Textures[TextureSlot] = Texture;
		Effective = Override;
		return true;
	}

	void DrawMaterialTextureSlot(UMeshComponent* MeshComponent, int32 MaterialSlot, int32 TextureSlot, const char* Label, UMaterial*& Effective, UMaterial*& Override) {
		const float ThumbnailSize{ 64.0f };
		const float ItemThumbnailSize{ 24.0f };
		UTexture2D* Texture{ Effective && Effective->Textures.IsValidIndex(TextureSlot) ? Effective->Textures[TextureSlot] : nullptr };

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextUnformatted(Label);
		ImGui::TableSetColumnIndex(1);
		ImGui::PushID(TextureSlot);

		if (Texture && Texture->GetResource()) {
			ImGui::Image((ImTextureID)Texture->GetResource()->GetSRV(), { ThumbnailSize, ThumbnailSize });
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("%s", Texture->GetPath().c_str());
			}
		} else {
			ImGui::Button("Empty", { ThumbnailSize, ThumbnailSize });
		}

		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* Payload{ ImGui::AcceptDragDropPayload(EditorDragDrop::Texture) }) {
				UTexture2D* DroppedTexture{ *static_cast<UTexture2D**>(Payload->Data) };
				if (SetMaterialTexture(MeshComponent, MaterialSlot, TextureSlot, DroppedTexture, Effective, Override)) {
					Texture = DroppedTexture;
				}
			}
			ImGui::EndDragDropTarget();
		}

		const FString CurrentPath{ Texture ? Texture->GetPath() : "" };
		const FString CurrentFileName{ Texture ? GetAssetDisplayName(CurrentPath) : "None" };
		ImGui::SetNextItemWidth(-1.0f);
		const bool Open{ ImGui::BeginCombo("##Texture", CurrentFileName.c_str()) };
		if (ImGui::IsItemHovered() && Texture) {
			ImGui::SetTooltip("%s", CurrentPath.c_str());
		}

		if (Open) {
			if (ImGui::Selectable("None", Texture == nullptr)) {
				SetMaterialTexture(MeshComponent, MaterialSlot, TextureSlot, nullptr, Effective, Override);
			}

			for (TObjectIterator<UTexture2D> Iterator{}; Iterator; ++Iterator) {
				UTexture2D* ItemTexture{ *Iterator };
				if (!ItemTexture->GetResource() || ItemTexture->GetPath().empty()) {
					continue;
				}

				const bool Selected{ ItemTexture == Texture };
				const FString ItemPath{ ItemTexture->GetPath() };
				const FString ItemFileName{ GetAssetDisplayName(ItemPath) };
				ImGui::PushID(ItemTexture);
				ImGui::Image((ImTextureID)ItemTexture->GetResource()->GetSRV(), { ItemThumbnailSize, ItemThumbnailSize });
				ImGui::SameLine();
				if (ImGui::Selectable(ItemFileName.c_str(), Selected, ImGuiSelectableFlags_None, { 0.0f, ItemThumbnailSize })) {
					SetMaterialTexture(MeshComponent, MaterialSlot, TextureSlot, ItemTexture, Effective, Override);
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("%s", ItemPath.c_str());
				}
				if (Selected) {
					ImGui::SetItemDefaultFocus();
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}

		ImGui::PopID();
	}

	// 메시 컴포넌트의 머티리얼 슬롯을 한 줄씩 그린다.
	// 텍스처를 드롭하면 그 슬롯의 덮어쓰기에 반영하고, 메시 에셋의 머티리얼은 건드리지 않는다.
	void DrawMaterialSlots(UMeshComponent* MeshComponent)
	{
		const int32 NumSlots = MeshComponent->GetNumMaterials();
		if (NumSlots <= 0)
		{
			return;
		}

		if (!ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen))
		{
			return;
		}

		// 메쉬 내의 매터리얼 별 Slot 순회
		for (int32 Slot = 0; Slot < NumSlots; ++Slot)
		{
			ImGui::PushID(Slot);

			FString SlotLabel = std::format("[{}] {}", Slot, MeshComponent->GetMaterialSlotName(Slot));
			bool bOpen = ImGui::TreeNode(SlotLabel.c_str());

			UMaterial* Override = MeshComponent->GetOverrideMaterial(Slot);
			UMaterial* Effective = MeshComponent->GetMaterial(Slot);

			ImGui::SameLine();

			if (Override)
			{
				ImGui::TextDisabled("Override");
				ImGui::SameLine();

				if (ImGui::SmallButton("Reset"))
				{
					MeshComponent->SetMaterial(Slot, nullptr);
					Override = nullptr;
					Effective = MeshComponent->GetMaterial(Slot);
				}
			}
			else
			{
				ImGui::TextDisabled("Default");
			}

			if (bOpen)
			{
				if (ImGui::BeginTable("MaterialProperties", 2))
				{
					ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 110.0f);
					ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

					DrawMaterialTextureSlot(MeshComponent, Slot, 0, "Texture", Effective, Override);
					DrawMaterialTextureSlot(MeshComponent, Slot, 1, "Normal Map", Effective, Override);

					ImGui::TableNextRow();

					// 3: Base Color
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("Base Color");

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);

					FVector4 TempColor = Effective ? Effective->BaseColor : FVector4(1.0f, 1.0f, 1.0f, 1.0f);
					if (ImGui::ColorEdit4("##BaseColor", &TempColor.X))
					{
						Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
						if (Override)
						{
							Override->BaseColor = TempColor;
							Effective = Override;
						}
					}

					ImGui::TableNextRow();
					
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("Shininess");
					
					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);
					
					float TempShininess =Effective ? Effective->Shininess : 32.0f;
					if (ImGui::DragFloat("##Shininess", &TempShininess, 0.5f, 1.0f, 256.0f))
					{
						Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
						if (Override)
						{
							Override->Shininess = TempShininess;
							Effective = Override;
						}
					}

					ImGui::TableNextRow();
					
					// 4: UV Scroll Speed
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("UV Scroll");

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);

					FVector2 TempUVScrollSpeed = Effective ? Effective->UVScrollSpeed : FVector2();
					if (ImGui::DragFloat2("##UVScrollSpeed", &TempUVScrollSpeed.X, 0.01f))
					{
						Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
						if (Override)
						{
							Override->UVScrollSpeed = TempUVScrollSpeed;
							Effective = Override;
						}
					}

					ImGui::TableNextRow();

					// 5: Sampler State
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("Sampler");

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);

					const char* SamplerItems[] = { "Linear Clamp", "Linear Wrap" };
					int SamplerIndex = Effective ? static_cast<int>(Effective->SamplerState) : static_cast<int>(ESamplerState::LinearClamp);
					if (ImGui::BeginCombo("##SamplerState", SamplerItems[SamplerIndex]))
					{
						for (int i = 0; i < static_cast<int>(ESamplerState::Count); ++i)
						{
							if (ImGui::Selectable(SamplerItems[i], SamplerIndex == i))
							{
								Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
								if (Override)
								{
									Override->SamplerState = static_cast<ESamplerState>(i);
									Effective = Override;
								}
							}
						}
						ImGui::EndCombo();
					}

					ImGui::TableNextRow();

					// 6: Blend State
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("Blend Mode");

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);

					const char* BlendItems[] = { "Opaque", "Alpha Blend" };
					int BlendIndex = Effective ? static_cast<int>(Effective->BlendState) : static_cast<int>(EBlendState::Opaque);
					if (ImGui::BeginCombo("##BlendState", BlendItems[BlendIndex]))
					{
						for (int i = 0; i < IM_ARRAYSIZE(BlendItems); ++i)
						{
							if (ImGui::Selectable(BlendItems[i], BlendIndex == i))
							{
								Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
								if (Override)
								{
									Override->BlendState = static_cast<EBlendState>(i);
									Effective = Override;
								}
							}
						}
						ImGui::EndCombo();
					}

					ImGui::EndTable();
				}
				ImGui::TreePop();
			}
			ImGui::PopID();

			ImGui::Separator();
		}
	}

	// UClass에 등록된 프로퍼티를 타입에 맞는 위젯으로 그린다
	void DrawProperty(UObject* Object, const FProperty& Property, ImFont* CustomFont)
	{
		void* ValuePtr = reinterpret_cast<char*>(Object) + Property.Offset;
		const FString Label = "##" + Property.Name;

		const char* DisplayName = Property.Name.c_str();

		// 패널이 넓으면 라벨 | 값을 한 줄에, 좁거나 라벨이 길면 라벨 아래 줄에 값을 놓는다.
		const float StartX = ImGui::GetCursorPosX();
		const float AvailableWidth = ImGui::GetContentRegionAvail().x;
		const float LabelWidth = AvailableWidth * 0.45f;

		const bool bInline = Property.Type != EPropertyType::Transform && AvailableWidth >= 260.0f && ImGui::CalcTextSize(DisplayName).x < LabelWidth;

		ImGui::AlignTextToFramePadding();
		ImGui::TextWrapped("%s", DisplayName);

		if (bInline)
			ImGui::SameLine(StartX + LabelWidth);

		ImGui::SetNextItemWidth(-1.0f);

		bool bChanged = false;

		switch (Property.Type)
		{
		case EPropertyType::Float:
			bChanged = ImGui::DragFloat(Label.c_str(), static_cast<float*>(ValuePtr), 0.1f);
			break;
		case EPropertyType::Int:
			bChanged = ImGui::DragInt(Label.c_str(), static_cast<int*>(ValuePtr), 1.0f);
			break;
		case EPropertyType::Bool:
			bChanged = ImGui::Checkbox(Label.c_str(), static_cast<bool*>(ValuePtr));
			break;
		case EPropertyType::Vector:
		{
			FVector* Value = static_cast<FVector*>(ValuePtr);
			bChanged = ImGui::DragFloat3(Label.c_str(), Value->V, 0.1f);
			break;
		}
		case EPropertyType::Rotator:
		{
			FRotator* Value = static_cast<FRotator*>(ValuePtr);
			// Transform의 Rotation과 같은 X/Y/Z 축 순서로 보여준다.
			float Euler[3] = { Value->Roll, Value->Pitch, Value->Yaw };
			if (ImGui::DragFloat3(Label.c_str(), Euler, 0.1f))
			{
				Value->Roll = Euler[0];
				Value->Pitch = Euler[1];
				Value->Yaw = Euler[2];
			}
			break;
		}


		case EPropertyType::Vector4:
		{
			FVector4* Value = static_cast<FVector4*>(ValuePtr);
			bChanged = ImGui::DragFloat4(Label.c_str(), &Value->X, 0.1f);
			break;
		}

		case EPropertyType::Color:
		{
			FColor* C = static_cast<FColor*>(ValuePtr);
			float V[4] = { C->R / 255.f, C->G / 255.f, C->B / 255.f, C->A / 255.f };
			if (ImGui::ColorEdit4(Label.c_str(), V))
			{
				*C = FColor(uint8(V[0] * 255.f + 0.5f), uint8(V[1] * 255.f + 0.5f),
					uint8(V[2] * 255.f + 0.5f), uint8(V[3] * 255.f + 0.5f));
				bChanged = true;
			}
			break;
		}

		case EPropertyType::LinearColor:
		{
			// 타입은 Vector4와 같고 위젯만 색상 선택기다
			FLinearColor* Value = static_cast<FLinearColor*>(ValuePtr);
			bChanged = ImGui::ColorEdit4(Label.c_str(), &Value->R);
			break;
		}
		case EPropertyType::String:
		{
			FString* Value = static_cast<FString*>(ValuePtr);

			char Buffer[256] = {};
			strncpy_s(Buffer, Value->c_str(), sizeof(Buffer) - 1);
			if (CustomFont) ImGui::PushFont(CustomFont);
			if (ImGui::InputText(Label.c_str(), Buffer, sizeof(Buffer)))
			{
				*Value = Buffer;
			}
			if (CustomFont) ImGui::PopFont();
			break;
		}
		case EPropertyType::Transform:
		{
			FTransform* Value = static_cast<FTransform*>(ValuePtr);

			ImGui::NewLine();

			FTransform Transform = *Value;
			bool bChange = false;

			bChanged |= DrawVector3Controller("Location", Value->Location.V, 0.0f, 55.0f);
			bChanged |= DrawRotatorAsXYZ("Rotation", Value->Rotation);
			bChanged |= DrawVector3Controller("Scale", Value->Scale.V, 1.0f, 55.0f);

			//if (bChange)
			//{
			//	// 씬 컴포넌트면 SetTransform을 거치고, 아니면 값을 직접 쓴다
			//	if (USceneComponent* SceneComponent = Cast<USceneComponent>(Object))
			//	{
			//		SceneComponent->SetTransform(Transform);
			//	}
			//	else
			//	{
			//		*Value = Transform;
			//	}
			//}
			break;
		}
		case EPropertyType::Object:
		{
			UObject** ObjPtr = static_cast<UObject**>(ValuePtr);

			if (Property.Class == UMaterial::StaticClass())
			{
				DrawTextureSlot(reinterpret_cast<UMaterial**>(ObjPtr), 0);
			}
			else if (Property.Class == UFont::StaticClass())
			{
				DrawFontSlot(Object, reinterpret_cast<UFont**>(ObjPtr));
			}
			else if (Property.Class == UStaticMesh::StaticClass())
			{
				DrawMeshSlot(Object, reinterpret_cast<UStaticMesh**>(ObjPtr));
			}
			break;
		}
		default:
			ImGui::TextDisabled("(Unsupported)");
			break;
		}

		if (bChanged)
		{
			Object->PostEditChangeProperty(Property);
		}
	}

	// 클래스 계층을 따라 올라가며 각 단계의 프로퍼티를 표시
	void DrawProperties(UObject* Object, ImFont* CustomFont)
	{
		if (!Object)
		{
			return;
		}

		TArray<UClass*> ClassChain;
		for (UClass* Class = Object->GetClass(); Class; Class = Class->Super)
		{
			ClassChain.Add(Class);
		}

		// 기반 클래스부터 표시
		for (auto It = ClassChain.rbegin(); It != ClassChain.rend(); ++It)
		{
			UClass* Class = *It;

			// 노출할 프로퍼티가 하나도 없으면 헤더도 그리지 않는다
			bool bHasVisibleProperty = false;
			for (const FProperty& Property : Class->GetProperties())
			{
				if (Property.HasAnyFlags(CPF_Edit))
				{
					bHasVisibleProperty = true;
					break;
				}
			}
			if (!bHasVisibleProperty)
			{
				continue;
			}

			ImGui::PushID(Class->Name.c_str());
			if (ImGui::CollapsingHeader(Class->Name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			{
				for (const FProperty& Property : Class->GetProperties())
				{
					if (!Property.HasAnyFlags(CPF_Edit))
					{
						continue;
					}

					const bool bReadOnly = Property.HasAnyFlags(CPF_EditConst);
					if (bReadOnly) ImGui::BeginDisabled();
					DrawProperty(Object, Property, CustomFont);
					if (bReadOnly) ImGui::EndDisabled();
				}
			}
			ImGui::PopID();
		}
	}

	void DrawActorHeader(AActor* Actor, UActorComponent*& OutSelectedComponent)
	{
		if (!Actor) { return; }

		static char ComponentNameInputBuf[256] = {};

		ImGui::Text("Actor: %s", Actor->GetName().c_str());
		ImGui::Text("UUID: %u", Actor->GetUUID());

		if (ImGui::Button("Add Component"))
		{
			ImGui::OpenPopup("AddComponentPopup");
		}

		if (ImGui::BeginPopup("AddComponentPopup"))
		{
			ImGui::Text("Component Name:");
			ImGui::SameLine();
			ImGui::InputText("##Component Name", ComponentNameInputBuf, sizeof(ComponentNameInputBuf));
			TArray<UClass*> Classes;
			GetDerivedClasses(UActorComponent::StaticClass(), Classes);
			// 이름순 정렬하면 메뉴가 안정적
			for (UClass* Class : Classes)
			{
				//if (!IsSpawnableComponent(Class)) continue;
				if (ImGui::MenuItem(Class->Name.c_str()))
				{
					OutSelectedComponent = Actor->AddComponent(Class, FString(ComponentNameInputBuf));
					ComponentNameInputBuf[0] = '\0';
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::EndPopup();
		}
	}
}

void FDetailsPanel::DrawShadowMapPreview(const ULightComponent& Light)
{
	if (!ImGui::CollapsingHeader("Shadow Depth Map", ImGuiTreeNodeFlags_DefaultOpen))
		return;

	// UI에서 Cast Shadows를 끄면 이전에 생성한 맵도 즉시 숨긴다.
	if (!Light.CastsShadows())
	{
		ImGui::TextDisabled("Cast Shadows is disabled.");
		return;
	}
	// 선택한 라이트가 속한 World를 표시해 Editor·PIE 결과를 구분한다.
	UWorld* LightWorld = Light.GetWorld();
	if (LightWorld)
	{
		ImGui::TextDisabled("World: %s", LightWorld->IsPlayInEditor() ? "PIE" : "Editor");
	}

	if (Light.GetLightType() == ELightType::Directional)
	{
		// 화면 조명에 선택되지 않은 Directional에는 그림자 작업이 생성되지 않는다.
		if (LightWorld && LightWorld->GetScene().GetPrimaryDirectionalLight() != Light.SceneProxy)
		{
			ImGui::TextDisabled("This directional light is not used by this world.");
			ImGui::TextWrapped("Only the first visible directional light is used.");
			return;
		}

		// 현재 편집값을 표시한다. 실제 투영 범위는 각 View의 클립 범위로 제한된다.
		const auto& Directional = static_cast<const UDirectionalLightComponent&>(Light);
		ImGui::Text("Requested ShadowDistance: %.1f", Directional.GetShadowDistance());
		// Directional은 View마다 단일 맵 또는 Cascade 맵을 만든다.
		ImGui::TextWrapped("Each view has its own directional shadow projection(s).");
	}

	// 설정 UI보다 그림자 패스가 먼저 실행되므로 변경 결과는 다음 프레임에 반영된다.
	ImGui::TextDisabled("Shadow settings apply next frame.");

	// 선택한 실제 라이트의 현재 프레임 작업만 사용한다.
	if (!Renderer || !Light.SceneProxy)
	{
		ImGui::TextDisabled("No shadow map generated this frame.");
		return;
	}

	uint32 ViewId = 0;
	uint32 ProjectionIndex = 0;

	if (Light.GetLightType() == ELightType::Directional)
	{
		TArray<uint32> ViewIds;
		Renderer->GetShadowViewIds(*Light.SceneProxy, ViewIds);
		if (ViewIds.IsEmpty())
		{
			ImGui::TextDisabled("No shadow map generated this frame.");
			return;
		}

		// 레이아웃·PIE·ViewMode 변경으로 사라진 선택을 복구한다.
		if (ViewIds.Find(ShadowPreviewViewId) == INDEX_NONE)
			ShadowPreviewViewId = ViewIds[0];

		const FString ViewLabel = std::format("View {}", ShadowPreviewViewId);
		if (ImGui::BeginCombo("Shadow View", ViewLabel.c_str()))
		{
			for (uint32 Candidate : ViewIds)
			{
				const FString Label = std::format("View {}", Candidate);
				if (ImGui::Selectable(Label.c_str(), Candidate == ShadowPreviewViewId))
					ShadowPreviewViewId = Candidate;
			}
			ImGui::EndCombo();
		}

		// 선택된 View의 Client 상태를 편집한다. 라이트 설정으로 저장하지 않는다.
		if (ShadowCascadeDebugGetter && ShadowCascadeDebugSetter)
		{
			bool bVisualize = ShadowCascadeDebugGetter(ShadowPreviewViewId);
			if (ImGui::Checkbox("Cascade Colors (Selected View)", &bVisualize))
				ShadowCascadeDebugSetter(ShadowPreviewViewId, bVisualize);
		}

		// 편집된 Cascade 개수보다 실제 생성된 타깃을 기준으로 목록을 만든다.
		TArray<uint32> Projections;
		for (uint32 Index = 0; Index < UDirectionalLightComponent::MaxShadowCascades; ++Index)
		{
			if (Renderer->GetShadowIndex(*Light.SceneProxy, Index, ShadowPreviewViewId) != INDEX_NONE)
				Projections.Add(Index);
		}
		if (Projections.IsEmpty())
			return;

		if (Projections.Find(ShadowPreviewProjectionIndex) == INDEX_NONE)
			ShadowPreviewProjectionIndex = Projections[0];

		const FString ProjectionLabel = std::format("Cascade {}", ShadowPreviewProjectionIndex + 1);
		if (ImGui::BeginCombo("Shadow Cascade", ProjectionLabel.c_str()))
		{
			for (uint32 Candidate : Projections)
			{
				const FString Label = std::format("Cascade {}", Candidate + 1);
				if (ImGui::Selectable(Label.c_str(), Candidate == ShadowPreviewProjectionIndex))
					ShadowPreviewProjectionIndex = Candidate;
			}
			ImGui::EndCombo();
		}

		ViewId = ShadowPreviewViewId;
		ProjectionIndex = ShadowPreviewProjectionIndex;
	}
	else if (Light.GetLightType() == ELightType::Point)
	{
		// Point 투영과 Shader의 면 선택 순서에 맞춘 표시 이름이다.
		constexpr uint32 FaceCount = 6;
		static constexpr const char* FaceNames[FaceCount] =
		{
			"+X", "-X", "+Y", "-Y", "+Z", "-Z"
		};

		// Point는 View 0을 공유하며, 실제 Depth 타깃이 있는 면만 표시한다.
		TArray<uint32> Faces;
		for (uint32 FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex)
		{
			if (Renderer->GetShadowIndex(*Light.SceneProxy, FaceIndex, 0) != INDEX_NONE)
				Faces.Add(FaceIndex);
		}

		if (Faces.IsEmpty())
		{
			ImGui::TextDisabled("No shadow map generated this frame.");
			return;
		}

		// 라이트 전환이나 타깃 변경으로 선택한 면이 사라지면 복구한다.
		if (Faces.Find(ShadowPreviewProjectionIndex) == INDEX_NONE)
			ShadowPreviewProjectionIndex = Faces[0];

		if (ImGui::BeginCombo("Shadow Face", FaceNames[ShadowPreviewProjectionIndex]))
		{
			for (uint32 Candidate : Faces)
			{
				const bool bSelected = Candidate == ShadowPreviewProjectionIndex;
				if (ImGui::Selectable(FaceNames[Candidate], bSelected))
					ShadowPreviewProjectionIndex = Candidate;

				// 콤보박스를 열었을 때 현재 선택한 면에 포커스를 둔다.
				if (bSelected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		// 아래 공통 조회·Viewport·UV 계산에 선택한 면을 전달한다.
		ProjectionIndex = ShadowPreviewProjectionIndex;
	}
	if (Renderer->IsShadowKnownEmpty(*Light.SceneProxy, ProjectionIndex, ViewId))
	{
		ImGui::TextDisabled("No casters: depth target allocation skipped.");
		return;
	}
	// Spot은 기존 공유 키 (0, 0)를 사용한다.
	const FTexture2D* DepthTexture = Renderer->GetShadowDepthTexture(*Light.SceneProxy, ProjectionIndex, ViewId);
	FViewportSettings ShadowViewport;
	if (!DepthTexture || !Renderer->GetShadowDepthViewport(*Light.SceneProxy, ShadowViewport, ProjectionIndex, ViewId))
	{
		ImGui::TextDisabled("No shadow map generated this frame.");
		return;
	}

	// 물리 Atlas 크기와 선택한 라이트의 실제 타일 해상도를 구분한다.
	ImGui::Text("Rendered: %u x %u", ShadowViewport.Width, ShadowViewport.Height);

	// 이번 프레임에 캐시를 재사용했는지, 다시 그렸다면 그 이유를 표시한다.
	FRenderer::FShadowCacheDebugInfo CacheInfo;
	if (Renderer->GetShadowCacheDebugInfo(*Light.SceneProxy, CacheInfo, ProjectionIndex, ViewId))
	{
		if (CacheInfo.Result == EShadowCacheResult::Hit)
		{
			ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Cache: Hit (updated %llu frames ago)",
				static_cast<unsigned long long>(CacheInfo.FramesSinceUpdate));
		}
		else
		{
			ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "Cache: Updated (%s)",
				GetShadowCacheResultName(CacheInfo.Result));
		}
		ImGui::TextDisabled("Casters: %u", CacheInfo.CasterCount);
	}
	ImGui::TextDisabled("Atlas: %u x %u", DepthTexture->GetWidth(), DepthTexture->GetHeight());
	// 요청값과 품질 상한을 구분하고, 실제 다음 프레임 변경만 대기 상태로 표시한다.
	const uint32 EffectiveResolution = static_cast<uint32>(FShadowSettings::GetEffectiveResolution(Light.GetShadowMapResolution()));
	ImGui::TextDisabled("Requested: %d  Global max: %d", Light.GetShadowMapResolution(), FShadowSettings::GetMaxResolution());
	if (ShadowViewport.Width != EffectiveResolution)
		ImGui::TextDisabled("Resolution applies next frame.");

	// 현재 Spot Shadow Map은 정사각형이며, 패널 폭 안에서 표시한다.
	const float PreviewSize = std::min(256.0f, ImGui::GetContentRegionAvail().x);
	if (PreviewSize <= 0.0f)
		return;

	// 동일한 SRV를 사용하되 선택한 라이트에 배정된 타일만 표시한다.
	const float InvWidth = 1.0f / DepthTexture->GetWidth();
	const float InvHeight = 1.0f / DepthTexture->GetHeight();
	const ImVec2 UVMin(ShadowViewport.StartX * InvWidth, ShadowViewport.StartY * InvHeight);
	const ImVec2 UVMax((ShadowViewport.StartX + ShadowViewport.Width) * InvWidth,
		(ShadowViewport.StartY + ShadowViewport.Height) * InvHeight);
	ImGui::Image((ImTextureID)DepthTexture->GetSRV(), ImVec2(PreviewSize, PreviewSize), UVMin, UVMax);
}

bool FDetailsPanel::Init()
{
	ImGuiIO& io = ImGui::GetIO();

	io.Fonts->AddFontDefault();

	const char* fontPath = "HMKMRHD.ttf";
	float fontSize = 15.0f;
	const ImWchar* koreanRanges = io.Fonts->GetGlyphRangesKorean();

	CustomFont = io.Fonts->AddFontFromFileTTF(fontPath, fontSize, nullptr, koreanRanges);
	assert(CustomFont != nullptr);

	return true;
}

void FDetailsPanel::Tick(float DeltaTime)
{
}


void FDetailsPanel::OnRender()
{
	ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_FirstUseEver);

	ImGui::Begin("Details");

	if (TargetActor)
	{
		// 빌보드처럼 디테일에 숨겨진 컴포넌트가 선택되면 노출되는 부모로 올린다.
		// (예: 라이트는 빌보드를 클릭해서 고르지만 수치는 SpotLight 쪽에 있다)
		while (TargetComponent && TargetComponent->IsHiddenInDetails())
		{
			USceneComponent* HiddenComponent = Cast<USceneComponent>(TargetComponent);
			TargetComponent = HiddenComponent ? HiddenComponent->GetAttachParent() : nullptr;
		}

		UActorComponent* SelectCandidateComponent = TargetComponent;

		DrawActorHeader(TargetActor, TargetComponent);
		DrawCompoenetList(TargetActor, TargetComponent);				
		ImGui::Separator();

		// 액터 -> 컴포넌트 순으로, 클래스별 프로퍼티 표시
		ImGui::PushID(TargetActor);
		DrawProperties(TargetActor, CustomFont);
		ImGui::PopID();

		if (TargetComponent)
		{
			ImGui::PushID(TargetComponent);
			ImGui::SeparatorText("Component Properties");
			DrawProperties(TargetComponent, CustomFont);

			// Light Component를 선택했을 때 기존 Property 아래에 Depth Map을 표시한다.
			if (const ULightComponent* Light = Cast<ULightComponent>(TargetComponent))
				DrawShadowMapPreview(*Light);

			if(SelectCandidateComponent != TargetComponent)
			{
				SelectComponent(TargetComponent);
			}
			if (UMeshComponent* MeshComponent = Cast<UMeshComponent>(TargetComponent))
			{
				DrawMaterialSlots(MeshComponent);
			}
			ImGui::PopID();

			if (PendingDeleteComponent)
			{
				if (DeleteCallback)
				{
					DeleteCallback(PendingDeleteComponent);
				}

				PendingDeleteComponent = nullptr;
			}			
		}		
	}	
	ImGui::End();
}


void FDetailsPanel::SelectComponent(UActorComponent* Component)
{
	if (!Component)
	{
		TargetComponent = nullptr;
		EditingNameComponent = nullptr;
		bFocusNameEdit = false;

		if (Callback)
			Callback(nullptr);

		return;
	}

	TargetComponent = Component;

	if (Callback)
		Callback(TargetComponent);
}

void FDetailsPanel::DrawCompoenetList(AActor* SelectedActor, UActorComponent*& OutSelectedComponent)
{
	if (!SelectedActor) { return; }

	if (PendingDragComponent && PendingDroppedComponent)
	{
		TryReparent(PendingDroppedComponent, PendingDragComponent);
		PendingDragComponent = nullptr;
		PendingDroppedComponent = nullptr;
	}

	USceneComponent* Root = SelectedActor->GetRootComponent();

	if (Root && ImGui::CollapsingHeader("Components", ImGuiTreeNodeFlags_DefaultOpen))
	{
		DrawSceneComponentNode(Root, Root, OutSelectedComponent);
	}

	ImGui::Separator();

	// Draw ActorComponent
	for (UActorComponent* ActorComp : SelectedActor->GetComponents())
	{
		if (!ActorComp || Cast<USceneComponent>(ActorComp))
		{
			continue;
		}

		ImGui::PushID(ActorComp);
		ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
		if (ActorComp == OutSelectedComponent)
		{
			Flags |= ImGuiTreeNodeFlags_Selected;
		}
		
		FString Label = ActorComp->GetName();
		const ImVec2 RowStart = ImGui::GetCursorScreenPos();
		const float RowWidth = ImGui::GetContentRegionAvail().x;

		ImGui::TreeNodeEx("ActorComponentNode", Flags, "%s", Label.c_str());

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
		{
			OutSelectedComponent = ActorComp;
		}

		if (ImGui::BeginPopupContextItem("Component Context"))
		{
			if (ImGui::MenuItem("Delete", nullptr, false, ActorComp != Root))
				PendingDeleteComponent = ActorComp;
			ImGui::EndPopup();
		}

		const FString ClassName = std::format("({})", ActorComp->GetClass()->Name);
		const float TextX = RowStart.x + RowWidth - ImGui::CalcTextSize(ClassName.c_str()).x;
		ImGui::GetWindowDrawList()->AddText(ImVec2(TextX, RowStart.y), IM_COL32(200, 200, 200, 255), ClassName.c_str());		

		ImGui::PopID();
	}
}

void FDetailsPanel::DrawSceneComponentNode(USceneComponent* Component, USceneComponent* Root, UActorComponent*& OutSelectedComponent)
{
	// 빌보드 같은 에디터 표시용 컴포넌트는 트리에 노출하지 않는다
	if (!Component || Component->IsHiddenInDetails())
		return;

	// 숨겨진 자식만 있는 노드가 펼침 화살표를 갖지 않도록 노출되는 자식만 센다
	TArray<USceneComponent*> Children;
	for (USceneComponent* Child : Component->GetAttachChildren())
	{
		if (Child && !Child->IsHiddenInDetails())
		{
			Children.Add(Child);
		}
	}

	const bool bIsLeaf = Children.IsEmpty();
	bIsEditingName = (EditingNameComponent == Component);

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
	if (Component == OutSelectedComponent)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}
	if (bIsLeaf)
	{
		Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}

	FString Label = Component->GetName();
	if (Component == Root)
		Label += " (Root)";

	ImGui::PushID(Component);
	const ImVec2 RowStart = ImGui::GetCursorScreenPos();
	const float RowWidth = ImGui::GetContentRegionAvail().x;
	const bool bOpen = ImGui::TreeNodeEx("ComponentNode", Flags, "%s", bIsEditingName ? "" : Label.c_str());

	USceneComponent* DragComponent = Component;

	// source: Root 제외, 현재 Component를 payload에 담음
	if (Component != Root && ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload(EditorDragDrop::Component, &Component, sizeof(USceneComponent*));
		ImGui::EndDragDropSource();
	}
	// target: 모든 행이 가능, payload를 받아 현재 Component를 부모로 사용
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Component))
		{
			USceneComponent* DroppedComponent = *static_cast<USceneComponent**>(Payload->Data);
			PendingDroppedComponent = DroppedComponent;
			PendingDragComponent = DragComponent;
		}
		ImGui::EndDragDropTarget();
	}

	if (!bIsEditingName)
	{
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			OutSelectedComponent = Component;

		if (Component != Root && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			EditingNameComponent = Component;
			strncpy_s(ComponentNameEditBuffer, sizeof(ComponentNameEditBuffer), Component->GetName().c_str(), _TRUNCATE);
			bFocusNameEdit = true;
		}

		if (ImGui::BeginPopupContextItem("Component Context"))
		{
			if (ImGui::MenuItem("Delete", nullptr, false, Component != Root))
				PendingDeleteComponent = Component;
			ImGui::EndPopup();
		}

		const FString ClassName = std::format("({})", Component->GetClass()->Name);
		const float TextX = RowStart.x + RowWidth - ImGui::CalcTextSize(ClassName.c_str()).x;
		ImGui::GetWindowDrawList()->AddText(ImVec2(TextX, RowStart.y), IM_COL32(200, 200, 200, 255), ClassName.c_str());
	}
	else
	{
		ImGui::SameLine();
		if (bFocusNameEdit)
		{
			ImGui::SetKeyboardFocusHere();
			bFocusNameEdit = false;
		}

		const bool bConfirmed = ImGui::InputText("Rename", ComponentNameEditBuffer,
			sizeof(ComponentNameEditBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
		const bool bCancelled = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
		const bool bFinished = bConfirmed || ImGui::IsItemDeactivatedAfterEdit();
		if (bCancelled || bFinished)
		{
			if (!bCancelled && ComponentNameEditBuffer[0] != '\0')
			{
				FName NewName(ComponentNameEditBuffer);
				AActor* Owner = Component->GetOwner();

				UActorComponent* Existing = Owner->FindComponentByName(NewName);
				if (!Existing)
				{
					Component->SetName(FName(ComponentNameEditBuffer));
				}
				else
				{
					HTR_LOG(Warning, "A component with this name already exists in this Actor.");
				}
			}
			EditingNameComponent = nullptr;
		}
	}

	if (bOpen && !bIsLeaf)
	{
		for (USceneComponent* Child : Children)
		{
			DrawSceneComponentNode(Child, Root, OutSelectedComponent);
		}
		ImGui::TreePop();
	}

	ImGui::PopID();
}

void FDetailsPanel::TryReparent(USceneComponent* DroppedComponent, USceneComponent* DragComponent)
{
	if (DroppedComponent != nullptr 
		&& DroppedComponent->GetAttachParent() != DragComponent
		&& DroppedComponent != DragComponent)
	{
		DroppedComponent->SetupAttachment(DragComponent, EAttachmentRule::KeepWorld);
	}
}


FDetailsPanel::~FDetailsPanel()
{
	//delete transform;
}
