#include "EnginePCH.h"
#include "EngineTimer.h"

void EngineTimer::Init()
{
	PrevTime = Clock::now();
}

void EngineTimer::Tick()
{
	Clock::time_point Current = Clock::now();
	std::chrono::duration<float> Elapsed = Current - PrevTime;

	// 느린 프레임도 실제 경과 시간을 그대로 전달한다.
	DeltaTime = Elapsed.count();
	PrevTime = Current;
	TotalTime += DeltaTime;
}
