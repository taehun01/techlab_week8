#pragma once

#include "Components/LightComponent.h"

#include "SceneManagement.h"

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

	const ULightComponent* GetLightComponent() const { return LightComponent; }
	bool IsVisible() const { return LightComponent->IsVisible(); }

protected:
	const ULightComponent* LightComponent = nullptr;

private:
	friend class FScene;
	FScene* Scene = nullptr;
    int32 PackedIndex = INDEX_NONE;
};






