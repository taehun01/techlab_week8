#pragma once

#include "Components/LightComponent.h"

#include "SceneManagement.h"

struct FShadowProjection
{
	FVector ViewOrigin = FVector::ZeroVector;
	TArray<FMatrix> ViewProjections;

	// Projection 비율 저장
	float TanHalfFOV = 0.0f;
};

//MSVC 전용 최적화
//vftable을 만들게 되면 8byte를 먼저 쓰게 되는데
//alignas(16)과 같은 구조체를 멤버로 가지는 경우
//vftable 8 byte + padding 8 byte로 8byte라는 크기가 패딩으로 낭비되는 것을 막기 위함
//상속된 클래스는 16byte 정렬이 있어도 padding없이 채운다
class FLightSceneProxyFixLayout
{
public:
	virtual ~FLightSceneProxyFixLayout() = default;
};

class FLightSceneProxy : public FLightSceneProxyFixLayout
{
public:
	FLightSceneProxy(const ULightComponent* InLightComponent)
		:LightComponent(InLightComponent) {};
	virtual ~FLightSceneProxy() = default;
    
	virtual void GetLightShaderParameters(FLightRenderParameters& OutParameters) const = 0;
	
	// 지원하는 라이트가 그림자 View 원점과 VP를 계산한다.
	// false이면 출력값을 사용하지 않고 해당 작업을 생략한다.
	virtual bool GetShadowProjection(FShadowProjection& OutShadowProjection) const{ return false; }
	// RGB가 모두 0이면 직접광 기여가 없으므로 그림자 작업도 생략한다. Alpha는 조명 기여와 무관하다.
	bool ShouldCastShadow() const
	{
		const FColor Color = LightComponent->GetLightColor();
		return IsVisible() && LightComponent->CastsShadows() && LightComponent->GetIntensity() > 0.0f
			&& (Color.R != 0 || Color.G != 0 || Color.B != 0);
	}
	const ULightComponent* GetLightComponent() const { return LightComponent; }
	bool IsVisible() const { return LightComponent->IsVisible(); }

protected:
	const ULightComponent* LightComponent = nullptr;

private:
	friend class FScene;
	FScene* Scene = nullptr;
    int32 PackedIndex = INDEX_NONE;
};






