#include "EnginePCH.h"
#include "TextureLayout.h"

#include <algorithm>
#include <bit>
#include <cassert>

// 루트 크기는 POT여야 모든 단계에서 정사각형 4분할이 가능하다.
FTextureLayout::FTextureLayout(uint32 InSize)
	: Size(InSize)
{
	assert(std::has_single_bit(InSize));
	Nodes.Add(FNode{ NoChild, InSize });
}

// uint32로 표현 가능한 POT까지만 반환한다. 잘못된 요청은 0으로 표시한다.
uint32 FTextureLayout::GetBlockSize(uint32 ElementSizeX, uint32 ElementSizeY)
{
	if (ElementSizeX == 0 || ElementSizeY == 0)
		return 0;

	const uint32 Extent = std::max(ElementSizeX, ElementSizeY);
	return Extent <= 0x80000000u ? std::bit_ceil(Extent) : 0u;
}

// 요청 크기 검증은 공개 진입점에서 수행한다.
bool FTextureLayout::AddElement(uint32& OutBaseX, uint32& OutBaseY,
	uint32 ElementSizeX, uint32 ElementSizeY)
{
	const uint32 BlockSize = GetBlockSize(ElementSizeX, ElementSizeY);
	if (BlockSize == 0 || BlockSize > Size)
		return false;

	if (!AddElementToNode(0, 0, 0, Size, BlockSize, OutBaseX, OutBaseY))
		return false;

	UsedArea += static_cast<uint64>(BlockSize) * BlockSize;
	return true;
}

// 반환은 예약 블록의 시작 좌표와 크기가 정확히 일치해야 한다.
bool FTextureLayout::RemoveElement(uint32 BaseX, uint32 BaseY,
	uint32 ElementSizeX, uint32 ElementSizeY)
{
	const uint32 BlockSize = GetBlockSize(ElementSizeX, ElementSizeY);
	if (BlockSize == 0 || BlockSize > Size
		|| BaseX > Size - BlockSize || BaseY > Size - BlockSize
		|| BaseX % BlockSize != 0 || BaseY % BlockSize != 0)
	{
		return false;
	}

	if (!RemoveElementFromNode(0, 0, 0, Size, BlockSize, BaseX, BaseY))
		return false;

	UsedArea -= static_cast<uint64>(BlockSize) * BlockSize;
	return true;
}

// 배열 재할당에 대비해 노드 참조를 보관하지 않고 인덱스로 다시 접근한다.
void FTextureLayout::SplitNode(int32 NodeIndex, uint32 ChildSize)
{
	int32 FirstChild;
	if (FreeChildGroups.Num() > 0)
	{
		FirstChild = FreeChildGroups.Last();
		FreeChildGroups.Pop();
	}
	else
	{
		FirstChild = Nodes.Num();
		for (int32 Child = 0; Child < 4; ++Child)
			Nodes.Add(FNode{});
	}

	for (int32 Child = 0; Child < 4; ++Child)
		Nodes[FirstChild + Child] = FNode{ NoChild, ChildSize };

	Nodes[NodeIndex].FirstChild = FirstChild;
	Nodes[NodeIndex].MaxFreeSize = ChildSize;
}

// 하위 영역에 들어갈 수 있는 가장 큰 정사각형 크기를 저장한다.
void FTextureLayout::UpdateMaxFreeSize(int32 NodeIndex)
{
	const int32 FirstChild = Nodes[NodeIndex].FirstChild;
	uint32 MaxFreeSize = 0;
	for (int32 Child = 0; Child < 4; ++Child)
		MaxFreeSize = std::max(MaxFreeSize, Nodes[FirstChild + Child].MaxFreeSize);

	Nodes[NodeIndex].MaxFreeSize = MaxFreeSize;
}

// 좌상단, 우상단, 좌하단, 우하단 순서로 첫 번째 적합 공간을 선택한다.
bool FTextureLayout::AddElementToNode(int32 NodeIndex, uint32 NodeX, uint32 NodeY,
	uint32 NodeSize, uint32 BlockSize, uint32& OutBaseX, uint32& OutBaseY)
{
	if (Nodes[NodeIndex].MaxFreeSize < BlockSize)
		return false;

	if (NodeSize == BlockSize)
	{
		Nodes[NodeIndex].MaxFreeSize = 0;
		OutBaseX = NodeX;
		OutBaseY = NodeY;
		return true;
	}

	const uint32 ChildSize = NodeSize / 2;
	if (Nodes[NodeIndex].FirstChild == NoChild)
		SplitNode(NodeIndex, ChildSize);

	const int32 FirstChild = Nodes[NodeIndex].FirstChild;
	for (int32 Child = 0; Child < 4; ++Child)
	{
		const uint32 ChildX = NodeX + (Child & 1) * ChildSize;
		const uint32 ChildY = NodeY + ((Child >> 1) & 1) * ChildSize;
		if (AddElementToNode(FirstChild + Child, ChildX, ChildY, ChildSize,
			BlockSize, OutBaseX, OutBaseY))
		{
			UpdateMaxFreeSize(NodeIndex);
			return true;
		}
	}

	return false;
}

// 반환 좌표가 속한 자식만 방문하고, 비어 있는 형제 4개를 부모로 병합한다.
bool FTextureLayout::RemoveElementFromNode(int32 NodeIndex, uint32 NodeX, uint32 NodeY,
	uint32 NodeSize, uint32 BlockSize, uint32 BaseX, uint32 BaseY)
{
	if (NodeSize == BlockSize)
	{
		if (Nodes[NodeIndex].FirstChild != NoChild || Nodes[NodeIndex].MaxFreeSize != 0)
			return false;

		Nodes[NodeIndex].MaxFreeSize = NodeSize;
		return true;
	}

	const int32 FirstChild = Nodes[NodeIndex].FirstChild;
	if (FirstChild == NoChild)
		return false;

	const uint32 ChildSize = NodeSize / 2;
	const int32 Child = (BaseX >= NodeX + ChildSize ? 1 : 0)
		+ (BaseY >= NodeY + ChildSize ? 2 : 0);
	const uint32 ChildX = NodeX + (Child & 1) * ChildSize;
	const uint32 ChildY = NodeY + ((Child >> 1) & 1) * ChildSize;
	if (!RemoveElementFromNode(FirstChild + Child, ChildX, ChildY, ChildSize,
		BlockSize, BaseX, BaseY))
	{
		return false;
	}

	bool bCanMerge = true;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FNode& ChildNode = Nodes[FirstChild + Index];
		if (ChildNode.FirstChild != NoChild || ChildNode.MaxFreeSize != ChildSize)
		{
			bCanMerge = false;
			break;
		}
	}

	if (bCanMerge)
	{
		Nodes[NodeIndex].FirstChild = NoChild;
		Nodes[NodeIndex].MaxFreeSize = NodeSize;
		FreeChildGroups.Add(FirstChild);
	}
	else
	{
		UpdateMaxFreeSize(NodeIndex);
	}

	return true;
}
