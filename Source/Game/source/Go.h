#pragma once

// aStartupScene: a scene to load instead of the first one found, as the editor's
// Play button passes it (argv[1], e.g. "Scenes\TEST.tgs"). BENCH_SCENE still
// wins when set, so scripted runs are unaffected. Null or empty keeps the old
// behaviour.
void Go(const char* aStartupScene = nullptr);

#include <string>
// The normalised scene Go was asked for ("Scenes/TEST"), empty if none.
const std::string& StartupScene();
