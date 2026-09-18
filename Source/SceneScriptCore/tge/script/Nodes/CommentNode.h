#pragma once

#include <string>

#include <tge/script/ScriptNodeBase.h>
#include <tge/script/JsonData.h>

namespace Tga
{
	// A titled box drawn behind other nodes to explain a group of them. It has no pins and
	// does nothing when the script runs; the editor draws it specially and keeps its size.
	class CommentNode : public ScriptNodeBase
	{
	public:
		std::string text = "Comment";
		float width = 320.f;
		float height = 200.f;

		void Init(const ScriptCreationContext&) override {}

		void LoadFromJson(const JsonData& data) override
		{
			if (!data.json.is_object())
				return;
			text = data.json.value("text", text);
			width = data.json.value("width", width);
			height = data.json.value("height", height);
		}

		void WriteToJson(JsonData& data) const override
		{
			data.json["text"] = text;
			data.json["width"] = width;
			data.json["height"] = height;
		}
	};
}
