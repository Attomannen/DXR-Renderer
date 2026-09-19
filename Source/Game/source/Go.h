#pragma once

// aStartupScene: a scene to load instead of the first one found, as the editor's
// Play button passes it (argv[1], e.g. "Scenes\TEST.tgs"). BENCH_SCENE still
// wins when set, so scripted runs are unaffected. Null or empty keeps the old
// behaviour.
void Go(const char* aStartupScene = nullptr);

#include <string>
// The normalised scene Go was asked for ("Scenes/TEST"), empty if none.
const std::string& StartupScene();

// Set the scene a following GameWorld::Init should load, in the editor's
// spelling ("Scenes\TEST.tgs"); normalised the same way Go does. Used by the
// editor's in-viewport play session, which constructs a GameWorld directly
// instead of going through Go.
void SetStartupScene(const char* aScene);
