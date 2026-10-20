#include "EnginePCH.h"
#include "ShadowSettings.h"
#include <cctype>
#include <format>

// 지원하는 128~4096의 거듭제곱 단계만 허용하며, 실패하면 기존 설정을 유지한다.
bool FShadowSettings::SetMaxResolution(int32 Resolution)
{
	if (Resolution < 128 || Resolution > 4096 || (Resolution & (Resolution - 1)) != 0)
		return false;
	MaxResolution = Resolution;
	return true;
}

void FShadowSettings::SetCullMode(EShadowCullMode Mode)
{
	if (Mode == CullMode)
		return;
	CullMode = Mode;
	InvalidateAllCaches();
}

namespace ShadowConsole
{
	// 콘솔 입력은 대소문자를 구분하지 않는다. UE도 "STAT FPS"와 "stat fps"를 같게 다룬다.
	bool EqualsIgnoreCase(const FString& A, const char* B)
	{
		uint64 Index = 0;
		for (; Index < A.size() && B[Index] != '\0'; ++Index)
		{
			const int Left = std::tolower(static_cast<unsigned char>(A[Index]));
			const int Right = std::tolower(static_cast<unsigned char>(B[Index]));
			if (Left != Right)
				return false;
		}
		return Index == A.size() && B[Index] == '\0';
	}

	// 공백 구분 토큰으로 나눈다. 연속 공백과 앞뒤 공백은 무시한다.
	void Tokenize(const FString& Line, TArray<FString>& OutTokens)
	{
		OutTokens.Reset();
		uint64 Cursor = 0;
		while (Cursor < Line.size())
		{
			while (Cursor < Line.size() && std::isspace(static_cast<unsigned char>(Line[Cursor])))
				++Cursor;
			const uint64 Start = Cursor;
			while (Cursor < Line.size() && !std::isspace(static_cast<unsigned char>(Line[Cursor])))
				++Cursor;
			if (Cursor > Start)
				OutTokens.Add(Line.substr(Start, Cursor - Start));
		}
	}

	// "0/1/true/false/on/off"를 받는다. 해석 실패 시 false 반환.
	bool ParseBool(const FString& Text, bool& OutValue)
	{
		if (Text == "1" || EqualsIgnoreCase(Text, "true") || EqualsIgnoreCase(Text, "on")) { OutValue = true;  return true; }
		if (Text == "0" || EqualsIgnoreCase(Text, "false") || EqualsIgnoreCase(Text, "off")) { OutValue = false; return true; }
		return false;
	}
}

bool FShadowSettings::ExecCommand(const FString& CommandLine, FString& OutMessage)
{
	TArray<FString> Tokens;
	ShadowConsole::Tokenize(CommandLine, Tokens);

	// shadow_cull [front|back] : Shadow Depth 패스의 컬링 면을 바꾼다. 인자가 없으면 토글한다.
	if (Tokens.Num() > 0 && (ShadowConsole::EqualsIgnoreCase(Tokens[0], "shadow_cull")
		|| ShadowConsole::EqualsIgnoreCase(Tokens[0], "r.shadow.CullMode")))
	{
		EShadowCullMode NewMode = CullMode == EShadowCullMode::Back ? EShadowCullMode::Front : EShadowCullMode::Back;
		bool bValid = Tokens.Num() <= 2;
		if (bValid && Tokens.Num() == 2)
		{
			if (ShadowConsole::EqualsIgnoreCase(Tokens[1], "front"))     NewMode = EShadowCullMode::Front;
			else if (ShadowConsole::EqualsIgnoreCase(Tokens[1], "back")) NewMode = EShadowCullMode::Back;
			else bValid = false;
		}
		if (!bValid)
		{
			OutMessage = std::format("Usage: {} [front|back]", Tokens[0]);
			return true;
		}

		SetCullMode(NewMode);
		OutMessage = std::format("{}: {}", Tokens[0], CullMode == EShadowCullMode::Front ? "front" : "back");
		return true;
	}
	// 기존 명령과 과제의 콘솔 이름이 같은 런타임 설정을 제어한다.
	if (Tokens.Num() == 0 || (!ShadowConsole::EqualsIgnoreCase(Tokens[0], "shadow_cache")
		&& !ShadowConsole::EqualsIgnoreCase(Tokens[0], "r.shadow.CacheWholeSceneShadows")))
		return false;

	bool bNewValue = !bCacheWholeSceneShadows;          // 인자 없으면 토글
	if (Tokens.Num() > 2 || (Tokens.Num() == 2 && !ShadowConsole::ParseBool(Tokens[1], bNewValue)))
	{
		OutMessage = std::format("Usage: {} [0|1]", Tokens[0]);
		return true;
	}

	if (bNewValue != bCacheWholeSceneShadows)
	{
		bCacheWholeSceneShadows = bNewValue;
		InvalidateAllCaches();
	}

	OutMessage = std::format("{}: {}", Tokens[0], bCacheWholeSceneShadows ? "on" : "off");
	return true;
}
