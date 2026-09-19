#include "stdafx.h"
#include "age/imgui/ConsolePanel.h"

#include <age/log/Log.h>

#include "imgui/imgui.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace Ag
{
	namespace
	{
		bool  locOpen = false;
		bool  locAutoScroll = true;
		bool  locShowInfo = true;
		bool  locShowErrors = true;
		bool  locShowTips = true;
		char  locFilter[128] = {};

		// The log dedups identical messages, so the retained set is far smaller
		// than the number of calls. This ceiling is about drawing cost, not about
		// storage, and it is generous enough that a whole startup fits.
		constexpr size_t kMaxEntries = 2048;

		ImVec4 ColourFor(LogType aType)
		{
			switch (aType)
			{
			case LogType::Error: return ImVec4(1.00f, 0.42f, 0.38f, 1.0f);
			case LogType::Tip:   return ImVec4(0.55f, 0.82f, 1.00f, 1.0f);
			default:             return ImVec4(0.85f, 0.85f, 0.88f, 1.0f);
			}
		}

		bool Passes(const LogEntry& aEntry)
		{
			switch (aEntry.type)
			{
			case LogType::Error: if (!locShowErrors) return false; break;
			case LogType::Tip:   if (!locShowTips)   return false; break;
			default:             if (!locShowInfo)   return false; break;
			}
			if (locFilter[0] == 0) return true;
			// Case-insensitive substring; a log filter that cares about case is a
			// filter nobody uses twice.
			const std::string& text = aEntry.message;
			const size_t needle = std::strlen(locFilter);
			if (needle > text.size()) return false;
			for (size_t i = 0; i + needle <= text.size(); ++i)
			{
				size_t j = 0;
				while (j < needle && std::tolower(static_cast<unsigned char>(text[i + j])) ==
				                     std::tolower(static_cast<unsigned char>(locFilter[j]))) ++j;
				if (j == needle) return true;
			}
			return false;
		}
	}

	bool ConsolePanel::IsOpen() { return locOpen; }
	void ConsolePanel::SetOpen(bool aOpen) { locOpen = aOpen; }

	void ConsolePanel::Draw()
	{
		// Grave/tilde, the conventional console key. Guarded on WantTextInput so
		// typing a backtick into any other field does not toggle the window.
		if (!ImGui::GetIO().WantTextInput && !ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false))
			locOpen = !locOpen;

		if (!locOpen) return;

		ImGui::SetNextWindowSize(ImVec2(900.0f, 340.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Console", &locOpen))
		{
			ImGui::End();
			return;
		}

		ImGui::Checkbox("Info", &locShowInfo);   ImGui::SameLine();
		ImGui::Checkbox("Errors", &locShowErrors); ImGui::SameLine();
		ImGui::Checkbox("Tips", &locShowTips);   ImGui::SameLine();
		ImGui::Checkbox("Auto-scroll", &locAutoScroll); ImGui::SameLine();
		ImGui::SetNextItemWidth(240.0f);
		ImGui::InputTextWithHint("##filter", "Filter", locFilter, sizeof(locFilter));
		ImGui::SameLine();
		if (ImGui::SmallButton("Copy all")) ImGui::LogToClipboard();

		ImGui::Separator();

		static std::vector<const LogEntry*> entries;
		entries.resize(kMaxEntries);
		const size_t count = Log::GetLatestLogEntries(entries.data(), kMaxEntries);

		if (ImGui::BeginChild("##log", ImVec2(0, 0), ImGuiChildFlags_None,
			ImGuiWindowFlags_HorizontalScrollbar))
		{
			// The log hands them back newest first; read order is oldest first.
			for (size_t i = count; i-- > 0; )
			{
				const LogEntry* entry = entries[i];
				if (!entry || !Passes(*entry)) continue;

				ImGui::PushStyleColor(ImGuiCol_Text, ColourFor(entry->type));
				if (entry->count > 1) ImGui::Text("%s  (x%u)", entry->message.c_str(), entry->count);
				else                  ImGui::TextUnformatted(entry->message.c_str());
				ImGui::PopStyleColor();
			}

			if (locAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
				ImGui::SetScrollHereY(1.0f);
		}
		ImGui::EndChild();
		ImGui::LogFinish();   // no-op unless Copy all started a capture

		ImGui::End();
	}
}
