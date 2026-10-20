#include "EnginePCH.h"
#include "ShadowSettings.h"
#include <cctype>
#include <format>

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
