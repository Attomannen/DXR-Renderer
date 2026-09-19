#pragma once

// Data model of the particle system, modelled on Unreal's Niagara:
//
//   System  -> a list of Emitters plus User Parameters that gameplay can set.
//   Emitter -> three ordered module stacks (Emitter Update, Particle Spawn, Particle Update)
//              and one sprite renderer.
//   Module  -> a named unit of behaviour with typed parameters (see ParticleModules.cpp).
//
// Everything here is plain data. Modules are described by a table (ModuleDef), so saving, loading and the
// editor's stack UI work for every module without per-module code; only the simulation
// (ParticleSystem.cpp) knows what each module does.

#include <cstdint>
#include <string>
#include <vector>

namespace Ag::Particles
{
	struct CurveKey
	{
		float time = 0.f;
		float value[4] = {};
	};

	// Piecewise linear, clamped outside its keys. Up to four channels (one for a scalar curve, four for RGBA).
	struct Curve
	{
		std::vector<CurveKey> keys;
		void Evaluate(float aTime, int aChannels, float* aOut) const;
	};

	enum class ParamType
	{
		Float,
		Int,
		Bool,
		Vec3,
		Color,
		FloatRange,   // random value between v[0] and v[1]
		Vec3Range,    // random point in the box between v[0..2] and v[3..5]
		Curve,        // scalar curve over the particle's normalised age
		ColorCurve,   // RGBA curve over the particle's normalised age
		Enum,         // index into ParamDef::options
	};

	struct ParamValue
	{
		float v[8] = {};
		Curve curve;
		// Name of a User Parameter that overrides the value at runtime (Float, Vec3 and Color parameters only).
		std::string binding;
	};

	struct ParamDef
	{
		const char* name = "";
		ParamType type = ParamType::Float;
		float defaults[8] = {};
		float minValue = 0.f;
		float maxValue = 0.f;          // min == max means "no limit"
		const char* options = "";      // Enum: entries separated by '\0', ended by an extra '\0'
		const char* tooltip = "";
		std::vector<CurveKey> defaultCurve;
	};

	enum class ModuleStage
	{
		EmitterUpdate,    // decides how many particles to spawn
		ParticleSpawn,    // initialises each new particle
		ParticleUpdate,   // runs on every live particle each step
		Count
	};

	// Serialised by name (ModuleDef::id), so this enum can be reordered freely.
	enum class ModuleType
	{
		SpawnRate,
		SpawnBurst,
		SpawnPerUnit,

		InitializeParticle,
		ShapeLocation,
		AddVelocity,
		AddVelocityInCone,
		AddVelocityFromPoint,

		GravityForce,
		DragForce,
		AccelerationForce,
		CurlNoiseForce,
		VortexForce,
		ScaleColor,
		ScaleSize,
		SubUVAnimation,
		PlaneCollision,
		KillBelowHeight,

		Count
	};

	struct ModuleDef
	{
		ModuleType type = ModuleType::Count;
		const char* id = "";
		const char* name = "";
		const char* category = "";
		ModuleStage stage = ModuleStage::ParticleUpdate;
		const char* tooltip = "";
		std::vector<ParamDef> params;
	};

	const ModuleDef& GetModuleDef(ModuleType aType);
	const ModuleDef* FindModuleDef(const std::string& anId);

	struct ModuleInstance
	{
		ModuleType type = ModuleType::Count;
		bool enabled = true;
		std::vector<ParamValue> params;   // parallel to ModuleDef::params
	};

	ModuleInstance CreateModule(ModuleType aType);

	enum class SimulationSpace { World, Local };
	enum class BlendMode { Alpha, Additive };
	enum class FacingMode { Camera, VelocityAligned, VerticalBillboard };
	enum class SortMode { None, ViewDepth };

	struct RendererSettings
	{
		bool enabled = true;
		std::string texture;   // path relative to the asset root, e.g. "Textures/smoke.dds"
		BlendMode blend = BlendMode::Alpha;
		FacingMode facing = FacingMode::Camera;
		SortMode sort = SortMode::ViewDepth;
		int flipbookColumns = 1;
		int flipbookRows = 1;
		float velocityStretch = 0.1f;   // VelocityAligned: extra length per unit of speed
		// Luminance in cd/m2 that a colour of (1,1,1) is drawn at. Scene units are photometric (a sunlit sky is a few
		// thousand cd/m2), so 1.0 in scene units would be blinding; sprites are unlit and need a sensible brightness.
		float brightness = 2000.f;
	};

	struct EmitterAsset
	{
		std::string name = "Emitter";
		bool enabled = true;
		SimulationSpace space = SimulationSpace::World;
		int maxParticles = 1000;
		bool looping = true;
		float loopDuration = 5.f;
		float startDelay = 0.f;

		std::vector<ModuleInstance> emitterUpdate;
		std::vector<ModuleInstance> particleSpawn;
		std::vector<ModuleInstance> particleUpdate;
		RendererSettings renderer;

		std::vector<ModuleInstance>& Stack(ModuleStage aStage);
		const std::vector<ModuleInstance>& Stack(ModuleStage aStage) const;
	};

	struct UserParameter
	{
		std::string name;
		ParamType type = ParamType::Float;   // Float, Vec3 or Color
		float value[4] = {};
	};

	struct SystemAsset
	{
		std::string name = "Particle System";
		uint32_t seed = 0;   // 0 picks a new random seed for every instance
		std::vector<UserParameter> userParameters;
		std::vector<EmitterAsset> emitters;

		bool Load(const std::string& aPath);
		bool Save(const std::string& aPath) const;
	};

	// A ready-to-edit system: one smoke-like emitter with the usual stack.
	SystemAsset MakeDefaultSystem();
	EmitterAsset MakeDefaultEmitter();
}
