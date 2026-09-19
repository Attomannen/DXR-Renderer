#include <stdafx.h>
#include "StringNodes.h"

#include "NodeHelpers.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace Ag;
using namespace Ag::NodeHelpers;

namespace
{
	std::string Lower(std::string_view text)
	{
		std::string result(text);
		for (char& c : result) c = (char)std::tolower((unsigned char)c);
		return result;
	}

	StringId Append(StringId a, StringId b) { return MakeString(std::string(a.GetStringView()) + std::string(b.GetStringView())); }
	bool Equals(StringId a, StringId b) { return a == b; }
	bool EqualsIgnoreCase(StringId a, StringId b) { return Lower(a.GetStringView()) == Lower(b.GetStringView()); }
	bool Contains(StringId text, StringId part) { return text.GetStringView().find(part.GetStringView()) != std::string_view::npos; }
	bool StartsWith(StringId text, StringId part) { return text.GetStringView().substr(0, part.GetStringView().size()) == part.GetStringView(); }
	bool EndsWith(StringId text, StringId part)
	{
		const std::string_view t = text.GetStringView(), p = part.GetStringView();
		return t.size() >= p.size() && t.substr(t.size() - p.size()) == p;
	}
	int Length(StringId text) { return (int)text.GetStringView().size(); }
	bool IsEmpty(StringId text) { return text.GetStringView().empty(); }
	StringId ToUpper(StringId text)
	{
		std::string result(text.GetStringView());
		for (char& c : result) c = (char)std::toupper((unsigned char)c);
		return MakeString(result);
	}
	StringId ToLower(StringId text) { return MakeString(Lower(text.GetStringView())); }

	StringId IntToString(int value) { return MakeString(std::to_string(value)); }
	StringId BoolToString(bool value) { return MakeString(value ? "true" : "false"); }
	StringId FloatToString(float value)
	{
		char buffer[32];
		snprintf(buffer, sizeof(buffer), "%g", value);
		return MakeString(buffer);
	}
	StringId VectorToString(Vector3f value)
	{
		char buffer[96];
		snprintf(buffer, sizeof(buffer), "(%g, %g, %g)", value.x, value.y, value.z);
		return MakeString(buffer);
	}
	int StringToInt(StringId text) { return std::atoi(text.GetString()); }
	float StringToFloat(StringId text) { return (float)std::atof(text.GetString()); }

	template <typename T, typename R, R (*Fn)(T)>
	class UnaryNode : public ScriptNodeBase
	{
		ScriptPinId myIn;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myIn = In<T>(context, "Value");
			Out<R>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			return Make<R>(Fn(Read<T>(context, myIn)));
		}
	};

	template <typename T, typename R, R (*Fn)(T, T)>
	class BinaryNode : public ScriptNodeBase
	{
		ScriptPinId myA, myB;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myA = In<T>(context, "A");
			myB = In<T>(context, "B");
			Out<R>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			return Make<R>(Fn(Read<T>(context, myA), Read<T>(context, myB)));
		}
	};

	class SubstringNode : public ScriptNodeBase
	{
		ScriptPinId myText, myStart, myLength;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myText = In<StringId>(context, "Text");
			myStart = In<int>(context, "Start", 0);
			myLength = In<int>(context, "Length", 1);
			Out<StringId>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const std::string_view text = Read<StringId>(context, myText).GetStringView();
			const int start = std::clamp(Read<int>(context, myStart), 0, (int)text.size());
			const int length = std::clamp(Read<int>(context, myLength), 0, (int)text.size() - start);
			return Make<StringId>(MakeString(text.substr((size_t)start, (size_t)length)));
		}
	};

	class ReplaceNode : public ScriptNodeBase
	{
		ScriptPinId myText, myFrom, myTo;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myText = In<StringId>(context, "Text");
			myFrom = In<StringId>(context, "Find");
			myTo = In<StringId>(context, "Replace With");
			Out<StringId>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			std::string text(Read<StringId>(context, myText).GetStringView());
			const std::string from(Read<StringId>(context, myFrom).GetStringView());
			const std::string to(Read<StringId>(context, myTo).GetStringView());
			if (!from.empty())
				for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
					text.replace(at, from.size(), to);
			return Make<StringId>(MakeString(text));
		}
	};

	// Joins up to four pieces: "Score: " + 12 + ...
	class ConcatenateNode : public ScriptNodeBase
	{
		ScriptPinId myParts[4];
	public:
		void Init(const ScriptCreationContext& context) override
		{
			static const char* names[4] = { "A", "B", "C", "D" };
			for (int i = 0; i < 4; ++i) myParts[i] = In<StringId>(context, names[i]);
			Out<StringId>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			std::string result;
			for (const ScriptPinId part : myParts) result += Read<StringId>(context, part).GetStringView();
			return Make<StringId>(MakeString(result));
		}
	};
}

void Ag::RegisterStringNodes()
{
	using R = ScriptNodeTypeRegistry;
	R::RegisterType<BinaryNode<StringId, StringId, Append>>("Common/String/append", "A followed by B");
	R::RegisterType<ConcatenateNode>("Common/String/concatenate", "Joins up to four texts");
	R::RegisterType<BinaryNode<StringId, bool, Equals>>("Common/String/equal (string)", "True when A and B are the same text");
	R::RegisterType<BinaryNode<StringId, bool, EqualsIgnoreCase>>("Common/String/equal ignoring case", "True when A and B match, ignoring upper and lower case");
	R::RegisterType<BinaryNode<StringId, bool, Contains>>("Common/String/contains", "True when A contains B");
	R::RegisterType<BinaryNode<StringId, bool, StartsWith>>("Common/String/starts with", "True when A begins with B");
	R::RegisterType<BinaryNode<StringId, bool, EndsWith>>("Common/String/ends with", "True when A ends with B");
	R::RegisterType<UnaryNode<StringId, int, Length>>("Common/String/length", "The number of characters");
	R::RegisterType<UnaryNode<StringId, bool, IsEmpty>>("Common/String/is empty", "True when there are no characters");
	R::RegisterType<UnaryNode<StringId, StringId, ToUpper>>("Common/String/to upper", "UPPER CASE");
	R::RegisterType<UnaryNode<StringId, StringId, ToLower>>("Common/String/to lower", "lower case");
	R::RegisterType<SubstringNode>("Common/String/substring", "Length characters of Text, starting at Start");
	R::RegisterType<ReplaceNode>("Common/String/replace", "Replaces every Find in Text with Replace With");

	R::RegisterType<UnaryNode<int, StringId, IntToString>>("Common/String/int to string", "The number as text");
	R::RegisterType<UnaryNode<float, StringId, FloatToString>>("Common/String/float to string", "The number as text");
	R::RegisterType<UnaryNode<bool, StringId, BoolToString>>("Common/String/bool to string", "true or false");
	R::RegisterType<UnaryNode<Vector3f, StringId, VectorToString>>("Common/String/float3 to string", "(x, y, z)");
	R::RegisterType<UnaryNode<StringId, int, StringToInt>>("Common/String/string to int", "The number at the start of the text, 0 if there is none");
	R::RegisterType<UnaryNode<StringId, float, StringToFloat>>("Common/String/string to float", "The number at the start of the text, 0 if there is none");
}
