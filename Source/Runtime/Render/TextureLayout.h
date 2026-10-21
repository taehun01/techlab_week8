#pragma once

#include "Containers/Array.h"

// GPU 자원과 무관하게 공간을 배정한다. 직사각형도 긴 변 기준의 POT 정사각형을 예약한다.
class FTextureLayout
{
public:
	explicit FTextureLayout(uint32 InSize);

	bool AddElement(uint32& OutBaseX, uint32& OutBaseY, uint32 ElementSizeX, uint32 ElementSizeY);
	bool RemoveElement(uint32 BaseX, uint32 BaseY, uint32 ElementSizeX, uint32 ElementSizeY);

	uint32 GetSizeX() const { return Size; }
	uint32 GetSizeY() const { return Size; }
	// 콘텐츠 면적이 아니라 실제 예약한 POT 블록들의 면적이다.
	uint64 GetUsedArea() const { return UsedArea; }
	static uint32 GetBlockSize(uint32 ElementSizeX, uint32 ElementSizeY);

private:
	static constexpr int32 NoChild = -1;

	// 좌표와 크기는 재귀 호출로 전달한다. 자식 없는 노드의 MaxFreeSize=0은 사용 중이다.
	struct FNode
	{
		int32 FirstChild = NoChild;
		uint32 MaxFreeSize = 0;
	};

	bool AddElementToNode(int32 NodeIndex, uint32 NodeX, uint32 NodeY, uint32 NodeSize,
		uint32 BlockSize, uint32& OutBaseX, uint32& OutBaseY);
	bool RemoveElementFromNode(int32 NodeIndex, uint32 NodeX, uint32 NodeY, uint32 NodeSize,
		uint32 BlockSize, uint32 BaseX, uint32 BaseY);
	void SplitNode(int32 NodeIndex, uint32 ChildSize);
	void UpdateMaxFreeSize(int32 NodeIndex);

	uint32 Size;
	uint64 UsedArea = 0;
	TArray<FNode> Nodes;
	// 병합된 연속 자식 4개의 시작 인덱스를 재사용한다.
	TArray<int32> FreeChildGroups;
};
