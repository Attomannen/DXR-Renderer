#pragma once

namespace Tga
{
	namespace UUIDManager {
		extern uint32_t CreateUUID();
		extern const uint32_t GetIDFromUUID(const char *uuid_str);
		extern const char* GetUUIDStringFromID(const uint32_t id);
		extern void DeallocateUUIDString(const char* uuid_str);

		//extern const UUID& GetUUIDFromID(const uint32_t id);
	}
	/*
	class UUIDManager
	{
	public:
		static uint32_t CreateUUID();
		static const uint32_t GetIDFromUUID(const UUID& uuid);
		static const UUID& GetUUIDFromID(const uint32_t id);

	};
	*/
}