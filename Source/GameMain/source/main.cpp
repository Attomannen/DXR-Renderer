#pragma once

#include <cstdio>
#include <cstdint>
#include "Go.h"

// NRD's NRI is built against Agility SDK 619 and uses D3D12 features the system
// runtime does not have: NRI logs "ID3D12Device version is lower than expected",
// CheckFeatureSupport(OPTIONS22) fails with E_INVALIDARG, and the first denoiser
// dispatch then faults inside NRI with no error of its own. These two exports
// are how D3D12 is told to load the redistributable in Bin/AgilitySDK/ instead.
// The values come from NRI's own generated NRIAgilitySDK.h -- keep them in step
// with the NRD/NRI version.
extern "C" {
	__declspec(dllexport) extern const uint32_t D3D12SDKVersion;
	const uint32_t D3D12SDKVersion = 619;

	__declspec(dllexport) extern const char* D3D12SDKPath;
	const char* D3D12SDKPath = "AgilitySDK/";
}

int main(const int argc, const char* argv[])
{
	if (argc > 1)
		printf("argv[1] => %s", argv[1]);
	else
		printf("argc => %d", argc);
	Go();
	return 0;
}