#pragma once

#include <vector>
#include <unordered_map>
#include <string>
#include <string_view>
#include <memory>

namespace Tga
{
	class StringId
	{
	public:
		StringId() = default;

		static const StringId Invalid;
		bool operator==(const StringId& other) const { return this->myString == other.myString; }
		bool operator!=(const StringId& other) const { return this->myString != other.myString; }
		bool operator<(const StringId& other) const { return strcmp(GetString(), other.GetString()) < 0; }

		bool IsEmpty() const { return myString == nullptr; }
		const char* GetString() const { return myString ? myString : ""; };
		std::string_view GetStringView() const { return GetString(); }
		operator std::string_view() const { return GetString(); }
	private:
		friend class StringRegistry;
		friend StringId operator""_tgaid(const char*, std::size_t);
		explicit StringId(const char* aString) : myString(aString) {}

		const char* myString = nullptr;
	};

	inline constexpr StringId StringId::Invalid = {};

	// Operator to create StringIds automatically
	// If you write "textGoesHere"_tgaid, that will automatically create a StringId for you
	Tga::StringId operator""_tgaid(const char*, std::size_t);

	// The StringRegistry help manage string memory, by deduplicating strings
	// Use by calling RegisterOrGetString
	// This will return a StringId that is unique per string
	// Instead of using string comparison, it is safe to simple compare IDs
	//
	// Do not use for string with dynamic values (debug string containing numbers for example), since all registered string will be kept in memory indefinitely 
	class StringRegistry
	{
	public:
		static StringId RegisterOrGetString(std::string_view string);
	};
} // namespace Tga

using Tga::operator""_tgaid;

template<>
struct std::hash<Tga::StringId>
{
	std::size_t operator()(Tga::StringId const& id) const noexcept
	{
		return std::hash<std::string_view>()(id.GetString());
	}
};