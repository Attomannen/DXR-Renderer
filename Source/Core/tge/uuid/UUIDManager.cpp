#include <stdafx.h>

#include "UUIDManager.h"

#pragma comment(lib, "rpcrt4.lib")  // UuidCreate - Minimum supported OS Win 2000
#include <rpc.h>
#include <string>
#include <unordered_map>
#include <cassert>
#include <xxh64_en.hpp>
using namespace Tga;

struct UUIDHash
{
	uint32_t operator()(const UUID& uuid) const
	{
		return (uint32_t)xxh64::hash((char*)(void*) & uuid, sizeof(UUID), 0);
	}
};

static std::unordered_map<UUID, uint32_t, UUIDHash> myUUIDToIndex = { {UUID(), 0} };
static std::vector<UUID> myIndexToUUID(1, UUID());

uint32_t UUIDManager::CreateUUID()
{
	UUID uuid;
	if (UuidCreate(&uuid) != RPC_S_OK) {
		// handle error? See RPC_STATUS 
	}

	uint32_t index = (uint32_t)myIndexToUUID.size();
	myUUIDToIndex[uuid] = index;
	myIndexToUUID.push_back(uuid);

	return index;
}

const char* UUIDManager::GetUUIDStringFromID(const uint32_t id) {
	RPC_CSTR rpcstr;
	if (UuidToStringA(&myIndexToUUID[id], &rpcstr) != RPC_S_OK) {

	}
 
	return reinterpret_cast<const char*>(rpcstr);
}

void UUIDManager::DeallocateUUIDString(const char* uuid_str)
{
	RpcStringFreeA(reinterpret_cast<RPC_CSTR*>(const_cast<char**>(&uuid_str)));
}

const uint32_t UUIDManager::GetIDFromUUID(const char *uuid_str) 
{
	if (uuid_str == nullptr) {
		assert("nothing to do here, you have the incorrect UUID set!");
		return 0;
	}

	UUID uuid{ 0 };
	if (UuidFromStringA((RPC_CSTR)uuid_str, &uuid) != RPC_S_OK) {
		assert("UUID string is invalid, expected format: [xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx]!");
		return 0;
	}

	if (myUUIDToIndex.find(uuid) == myUUIDToIndex.end()) {
		uint32_t id = (uint32_t)myIndexToUUID.size();
		myUUIDToIndex[uuid] = id;
		myIndexToUUID.push_back(uuid);
	}
	return myUUIDToIndex[uuid];
}
