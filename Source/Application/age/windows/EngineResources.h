#pragma once
// Win32 resource IDs shared by every AttoEngine executable.
//
// The .rc files that bind these to actual files live next to each executable's
// source (Source/GameMain/source, Source/GameEditor/source) rather than in the
// Application static library: resources linked through a static library are
// only pulled in if something already references them, which is exactly what an
// icon and a splash bitmap do not do.
#define IDI_APP_ICON      101
#define IDR_SPLASH_PNG    102
