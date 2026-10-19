#pragma once

#include <format>
#include "Editor/EditorUI/EditorPanel.h"
#include "Core/EngineLog.h"
#include <functional>
#include <utility>

struct FLogData
{
	FString message;
	ImVec4 Color;
};

class FOutputLogPanel : public IEditorPanel, public ILogSink
{
public:
	bool Init() override;
	void Tick(float DeltaTime)override;
	void OnRender() override;
	const char* GetPanelName() const override { return "Output Log"; }

	void ClearLog();

	//template<typename... Args>
	//void AddLog(ELogVerbosity Verbosity, std::format_string<Args...> fmt, Args&&... args)
	//{

	//}

	void ExecCommand(const FString& command_line);
	void SetCommandHandler(std::function<bool(const FString&, FString&)> InHandler)
	{
		CommandHandler = std::move(InHandler);
	}
	int TextEditCallback(ImGuiInputTextCallbackData* data);

private:
	char                  InputBuf[256];
	TArray<FLogData>       Items;
	TArray<FString>		  Commands;
	TArray<FString>       History;
	int                   HistoryPos;
	ImGuiTextFilter       Filter;
	bool                  AutoScroll;
	bool                  ScrollToBottom;
	std::function<bool(const FString&, FString&)> CommandHandler;
	// Inherited via ILogSink
	void OnLog(ELogVerbosity, const FString& Message) override;
};