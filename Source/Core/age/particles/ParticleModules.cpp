#include "stdafx.h"
#include <age/particles/ParticleAsset.h>

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

namespace Ag::Particles
{
	namespace
	{
		using Keys = std::vector<CurveKey>;

		CurveKey Key(float aTime, float a, float b = 0.f, float c = 0.f, float d = 0.f)
		{
			CurveKey key;
			key.time = aTime;
			key.value[0] = a; key.value[1] = b; key.value[2] = c; key.value[3] = d;
			return key;
		}

		ParamDef MakeFloat(const char* aName, float aDefault, float aMin, float aMax, const char* aTip = "")
		{
			ParamDef def;
			def.name = aName; def.type = ParamType::Float; def.defaults[0] = aDefault;
			def.minValue = aMin; def.maxValue = aMax; def.tooltip = aTip;
			return def;
		}

		ParamDef MakeBool(const char* aName, bool aDefault, const char* aTip = "")
		{
			ParamDef def;
			def.name = aName; def.type = ParamType::Bool; def.defaults[0] = aDefault ? 1.f : 0.f; def.tooltip = aTip;
			return def;
		}

		ParamDef MakeVec3(const char* aName, float x, float y, float z, const char* aTip = "")
		{
			ParamDef def;
			def.name = aName; def.type = ParamType::Vec3;
			def.defaults[0] = x; def.defaults[1] = y; def.defaults[2] = z; def.tooltip = aTip;
			return def;
		}

		ParamDef MakeColor(const char* aName, float r, float g, float b, float a, const char* aTip = "")
		{
			ParamDef def;
			def.name = aName; def.type = ParamType::Color;
			def.defaults[0] = r; def.defaults[1] = g; def.defaults[2] = b; def.defaults[3] = a; def.tooltip = aTip;
			return def;
		}

		ParamDef MakeFloatRange(const char* aName, float aMin, float aMax, float aLimitMin, float aLimitMax, const char* aTip = "")
		{
			ParamDef def;
			def.name = aName; def.type = ParamType::FloatRange;
			def.defaults[0] = aMin; def.defaults[1] = aMax;
			def.minValue = aLimitMin; def.maxValue = aLimitMax; def.tooltip = aTip;
			return def;
		}

		ParamDef MakeVec3Range(const char* aName, float x0, float y0, float z0, float x1, float y1, float z1, const char* aTip = "")
		{
			ParamDef def;
			def.name = aName; def.type = ParamType::Vec3Range;
			def.defaults[0] = x0; def.defaults[1] = y0; def.defaults[2] = z0;
			def.defaults[3] = x1; def.defaults[4] = y1; def.defaults[5] = z1;
			def.tooltip = aTip;
			return def;
		}

		ParamDef MakeCurve(const char* aName, Keys someKeys, const char* aTip = "")
		{
			ParamDef def;
			def.name = aName; def.type = ParamType::Curve; def.defaultCurve = std::move(someKeys); def.tooltip = aTip;
			return def;
		}

		ParamDef MakeColorCurve(const char* aName, Keys someKeys, const char* aTip = "")
		{
			ParamDef def;
			def.name = aName; def.type = ParamType::ColorCurve; def.defaultCurve = std::move(someKeys); def.tooltip = aTip;
			return def;
		}

		ParamDef MakeEnum(const char* aName, const char* someOptions, int aDefault, const char* aTip = "")
		{
			ParamDef def;
			def.name = aName; def.type = ParamType::Enum; def.options = someOptions;
			def.defaults[0] = (float)aDefault; def.tooltip = aTip;
			return def;
		}

		ModuleDef MakeModule(ModuleType aType, const char* anId, const char* aName, const char* aCategory, ModuleStage aStage, const char* aTip, std::vector<ParamDef> someParams)
		{
			ModuleDef def;
			def.type = aType; def.id = anId; def.name = aName; def.category = aCategory; def.stage = aStage;
			def.tooltip = aTip; def.params = std::move(someParams);
			return def;
		}

		const std::vector<ModuleDef>& Defs()
		{
			static const std::vector<ModuleDef> defs = []
			{
				using S = ModuleStage;
				std::vector<ModuleDef> d;
				// Same order as ModuleType.
				d.push_back(MakeModule(ModuleType::SpawnRate, "SpawnRate", "Spawn Rate", "Spawn", S::EmitterUpdate,
					"Spawns particles continuously.",
					{ MakeFloat("Rate", 30.f, 0.f, 100000.f, "Particles per second.") }));
				d.push_back(MakeModule(ModuleType::SpawnBurst, "SpawnBurst", "Spawn Burst Instantaneous", "Spawn", S::EmitterUpdate,
					"Spawns a group of particles at one moment of the emitter's loop.",
					{ MakeFloat("Time", 0.f, 0.f, 3600.f, "Seconds after the loop starts."),
					  MakeFloatRange("Count", 10.f, 10.f, 0.f, 100000.f, "How many particles.") }));
				d.push_back(MakeModule(ModuleType::SpawnPerUnit, "SpawnPerUnit", "Spawn Per Unit", "Spawn", S::EmitterUpdate,
					"Spawns particles as the emitter moves: a trail behind a moving object.",
					{ MakeFloat("Per Unit", 10.f, 0.f, 10000.f, "Particles per world unit travelled.") }));

				d.push_back(MakeModule(ModuleType::InitializeParticle, "InitializeParticle", "Initialize Particle", "Initialize", S::ParticleSpawn,
					"Sets the starting lifetime, size, colour and rotation.",
					{ MakeFloatRange("Lifetime", 1.f, 2.f, 0.01f, 3600.f, "Seconds."),
					  MakeFloatRange("Sprite Size", 0.5f, 1.f, 0.f, 0.f, "World units."),
					  MakeColor("Color", 1.f, 1.f, 1.f, 1.f),
					  MakeFloatRange("Rotation", 0.f, 0.f, 0.f, 0.f, "Degrees."),
					  MakeFloatRange("Rotation Rate", 0.f, 0.f, 0.f, 0.f, "Degrees per second.") }));
				d.push_back(MakeModule(ModuleType::ShapeLocation, "ShapeLocation", "Shape Location", "Location", S::ParticleSpawn,
					"Places new particles inside a shape.",
					{ MakeEnum("Shape", "Point\0Sphere\0Box\0Disc\0Cylinder\0", 1),
					  MakeVec3("Extents", 0.5f, 0.5f, 0.5f, "Sphere/Disc/Cylinder use X as the radius; Box and Cylinder use the extents as half sizes."),
					  MakeBool("Surface Only", false),
					  MakeVec3("Offset", 0.f, 0.f, 0.f) }));
				d.push_back(MakeModule(ModuleType::AddVelocity, "AddVelocity", "Add Velocity", "Velocity", S::ParticleSpawn,
					"Adds a random velocity between two extremes.",
					{ MakeVec3Range("Velocity", 0.f, 1.f, 0.f, 0.f, 2.f, 0.f, "Units per second.") }));
				d.push_back(MakeModule(ModuleType::AddVelocityInCone, "AddVelocityInCone", "Add Velocity in Cone", "Velocity", S::ParticleSpawn,
					"Shoots particles in a cone around a direction.",
					{ MakeVec3("Direction", 0.f, 1.f, 0.f),
					  MakeFloat("Cone Angle", 30.f, 0.f, 180.f, "Half angle in degrees."),
					  MakeFloatRange("Speed", 1.f, 2.f, 0.f, 0.f) }));
				d.push_back(MakeModule(ModuleType::AddVelocityFromPoint, "AddVelocityFromPoint", "Add Velocity from Point", "Velocity", S::ParticleSpawn,
					"Pushes particles away from a point, like an explosion.",
					{ MakeVec3("Origin", 0.f, 0.f, 0.f),
					  MakeFloatRange("Speed", 1.f, 2.f, 0.f, 0.f) }));

				d.push_back(MakeModule(ModuleType::GravityForce, "GravityForce", "Gravity Force", "Forces", S::ParticleUpdate,
					"Constant acceleration, gravity by default.",
					{ MakeVec3("Gravity", 0.f, -9.81f, 0.f, "Units per second squared.") }));
				d.push_back(MakeModule(ModuleType::DragForce, "DragForce", "Drag", "Forces", S::ParticleUpdate,
					"Slows particles down.",
					{ MakeFloat("Drag", 1.f, 0.f, 100.f) }));
				d.push_back(MakeModule(ModuleType::AccelerationForce, "AccelerationForce", "Acceleration Force", "Forces", S::ParticleUpdate,
					"Accelerates particles in one direction: wind or buoyancy.",
					{ MakeVec3("Acceleration", 0.f, 1.f, 0.f) }));
				d.push_back(MakeModule(ModuleType::CurlNoiseForce, "CurlNoiseForce", "Curl Noise Force", "Forces", S::ParticleUpdate,
					"Swirling, divergence-free turbulence.",
					{ MakeFloat("Strength", 2.f, 0.f, 1000.f),
					  MakeFloat("Frequency", 1.f, 0.001f, 100.f, "Higher means smaller swirls.") }));
				d.push_back(MakeModule(ModuleType::VortexForce, "VortexForce", "Vortex Force", "Forces", S::ParticleUpdate,
					"Spins particles around an axis.",
					{ MakeVec3("Axis", 0.f, 1.f, 0.f),
					  MakeVec3("Center", 0.f, 0.f, 0.f),
					  MakeFloat("Strength", 2.f, -1000.f, 1000.f),
					  MakeFloat("Pull", 0.f, -1000.f, 1000.f, "Acceleration towards the axis.") }));
				d.push_back(MakeModule(ModuleType::ScaleColor, "ScaleColor", "Scale Color", "Over Life", S::ParticleUpdate,
					"Multiplies the particle colour by a curve over its life.",
					{ MakeColorCurve("Color Over Life", { Key(0.f, 1.f, 1.f, 1.f, 0.f), Key(0.1f, 1.f, 1.f, 1.f, 1.f), Key(1.f, 1.f, 1.f, 1.f, 0.f) }) }));
				d.push_back(MakeModule(ModuleType::ScaleSize, "ScaleSize", "Scale Sprite Size", "Over Life", S::ParticleUpdate,
					"Multiplies the particle size by a curve over its life.",
					{ MakeCurve("Size Over Life", { Key(0.f, 1.f), Key(1.f, 1.f) }) }));
				d.push_back(MakeModule(ModuleType::SubUVAnimation, "SubUVAnimation", "Sub UV Animation", "Over Life", S::ParticleUpdate,
					"Steps through the frames of a flipbook texture (set the columns and rows on the renderer).",
					{ MakeEnum("Mode", "Over Life\0Random\0Frames Per Second\0", 0),
					  MakeFloat("Frames Per Second", 15.f, 0.f, 240.f) }));
				d.push_back(MakeModule(ModuleType::PlaneCollision, "PlaneCollision", "Collision (Plane)", "Collision", S::ParticleUpdate,
					"Bounces particles off a horizontal plane.",
					{ MakeFloat("Height", 0.f, 0.f, 0.f, "World height of the plane."),
					  MakeFloat("Restitution", 0.4f, 0.f, 1.f),
					  MakeFloat("Friction", 0.2f, 0.f, 1.f),
					  MakeBool("Kill On Hit", false) }));
				d.push_back(MakeModule(ModuleType::KillBelowHeight, "KillBelowHeight", "Kill Below Height", "Kill", S::ParticleUpdate,
					"Removes particles that fall below a height.",
					{ MakeFloat("Height", -100.f, 0.f, 0.f) }));
				return d;
			}();
			return defs;
		}

		int ValueCount(ParamType aType)
		{
			switch (aType)
			{
			case ParamType::Vec3: return 3;
			case ParamType::Color: return 4;
			case ParamType::FloatRange: return 2;
			case ParamType::Vec3Range: return 6;
			case ParamType::Curve:
			case ParamType::ColorCurve: return 0;
			default: return 1;
			}
		}
	}

	void Curve::Evaluate(float aTime, int aChannels, float* aOut) const
	{
		const int channels = std::clamp(aChannels, 1, 4);
		if (keys.empty())
		{
			for (int c = 0; c < channels; ++c) aOut[c] = c == 3 ? 1.f : (c == 0 ? 1.f : 0.f);
			return;
		}
		if (aTime <= keys.front().time || keys.size() == 1)
		{
			for (int c = 0; c < channels; ++c) aOut[c] = keys.front().value[c];
			return;
		}
		if (aTime >= keys.back().time)
		{
			for (int c = 0; c < channels; ++c) aOut[c] = keys.back().value[c];
			return;
		}
		for (size_t i = 1; i < keys.size(); ++i)
		{
			if (aTime > keys[i].time)
				continue;
			const CurveKey& a = keys[i - 1];
			const CurveKey& b = keys[i];
			const float span = b.time - a.time;
			const float t = span > 0.f ? (aTime - a.time) / span : 0.f;
			for (int c = 0; c < channels; ++c) aOut[c] = a.value[c] + (b.value[c] - a.value[c]) * t;
			return;
		}
	}

	const ModuleDef& GetModuleDef(ModuleType aType)
	{
		const std::vector<ModuleDef>& defs = Defs();
		for (const ModuleDef& def : defs)
			if (def.type == aType)
				return def;
		return defs.front();
	}

	const ModuleDef* FindModuleDef(const std::string& anId)
	{
		for (const ModuleDef& def : Defs())
			if (anId == def.id)
				return &def;
		return nullptr;
	}

	ModuleInstance CreateModule(ModuleType aType)
	{
		const ModuleDef& def = GetModuleDef(aType);
		ModuleInstance instance;
		instance.type = def.type;
		for (const ParamDef& paramDef : def.params)
		{
			ParamValue value;
			for (int i = 0; i < 8; ++i) value.v[i] = paramDef.defaults[i];
			value.curve.keys = paramDef.defaultCurve;
			instance.params.push_back(std::move(value));
		}
		return instance;
	}

	std::vector<ModuleInstance>& EmitterAsset::Stack(ModuleStage aStage)
	{
		switch (aStage)
		{
		case ModuleStage::EmitterUpdate: return emitterUpdate;
		case ModuleStage::ParticleSpawn: return particleSpawn;
		default: return particleUpdate;
		}
	}

	const std::vector<ModuleInstance>& EmitterAsset::Stack(ModuleStage aStage) const
	{
		return const_cast<EmitterAsset*>(this)->Stack(aStage);
	}

	EmitterAsset MakeDefaultEmitter()
	{
		EmitterAsset emitter;
		emitter.name = "Smoke";
		emitter.renderer.texture = "Textures/T_ParticleSoft.dds";
		emitter.emitterUpdate.push_back(CreateModule(ModuleType::SpawnRate));

		ModuleInstance initialize = CreateModule(ModuleType::InitializeParticle);
		initialize.params[0].v[0] = 2.f; initialize.params[0].v[1] = 3.f;   // lifetime
		initialize.params[1].v[0] = 0.6f; initialize.params[1].v[1] = 1.2f; // size
		initialize.params[3].v[0] = 0.f; initialize.params[3].v[1] = 360.f; // rotation
		initialize.params[4].v[0] = -20.f; initialize.params[4].v[1] = 20.f;
		emitter.particleSpawn.push_back(initialize);
		emitter.particleSpawn.push_back(CreateModule(ModuleType::ShapeLocation));
		emitter.particleSpawn.push_back(CreateModule(ModuleType::AddVelocityInCone));

		emitter.particleUpdate.push_back(CreateModule(ModuleType::AccelerationForce));
		emitter.particleUpdate.push_back(CreateModule(ModuleType::DragForce));
		ModuleInstance scaleSize = CreateModule(ModuleType::ScaleSize);
		scaleSize.params[0].curve.keys = { Key(0.f, 0.6f), Key(1.f, 2.f) };
		emitter.particleUpdate.push_back(scaleSize);
		emitter.particleUpdate.push_back(CreateModule(ModuleType::ScaleColor));
		return emitter;
	}

	SystemAsset MakeDefaultSystem()
	{
		SystemAsset system;
		system.emitters.push_back(MakeDefaultEmitter());
		return system;
	}

	// ---------------------------------------------------------------- serialisation

	namespace
	{
		using json = nlohmann::json;

		json ModuleToJson(const ModuleInstance& aModule)
		{
			const ModuleDef& def = GetModuleDef(aModule.type);
			json j;
			j["type"] = def.id;
			j["enabled"] = aModule.enabled;
			json params = json::object();
			for (size_t i = 0; i < def.params.size() && i < aModule.params.size(); ++i)
			{
				const ParamDef& paramDef = def.params[i];
				const ParamValue& value = aModule.params[i];
				json p;
				if (paramDef.type == ParamType::Curve || paramDef.type == ParamType::ColorCurve)
				{
					const int channels = paramDef.type == ParamType::Curve ? 1 : 4;
					json keys = json::array();
					for (const CurveKey& key : value.curve.keys)
					{
						json k;
						k["t"] = key.time;
						k["v"] = std::vector<float>(key.value, key.value + channels);
						keys.push_back(k);
					}
					p["curve"] = keys;
				}
				else
				{
					p["v"] = std::vector<float>(value.v, value.v + ValueCount(paramDef.type));
				}
				if (!value.binding.empty())
					p["binding"] = value.binding;
				params[paramDef.name] = p;
			}
			j["params"] = params;
			return j;
		}

		bool ModuleFromJson(const json& j, ModuleInstance& aOut)
		{
			const ModuleDef* def = FindModuleDef(j.value("type", std::string()));
			if (!def)
				return false;   // a module from a newer version: skipped rather than failing the whole asset
			aOut = CreateModule(def->type);
			aOut.enabled = j.value("enabled", true);
			if (!j.contains("params") || !j["params"].is_object())
				return true;
			for (size_t i = 0; i < def->params.size(); ++i)
			{
				const ParamDef& paramDef = def->params[i];
				if (!j["params"].contains(paramDef.name))
					continue;
				const json& p = j["params"][paramDef.name];
				ParamValue& value = aOut.params[i];
				if (p.contains("curve") && p["curve"].is_array())
				{
					value.curve.keys.clear();
					for (const json& k : p["curve"])
					{
						CurveKey key;
						key.time = k.value("t", 0.f);
						if (k.contains("v") && k["v"].is_array())
							for (size_t c = 0; c < 4 && c < k["v"].size(); ++c)
								key.value[c] = k["v"][c].get<float>();
						value.curve.keys.push_back(key);
					}
					std::stable_sort(value.curve.keys.begin(), value.curve.keys.end(),
						[](const CurveKey& a, const CurveKey& b) { return a.time < b.time; });
				}
				if (p.contains("v") && p["v"].is_array())
					for (size_t c = 0; c < 8 && c < p["v"].size(); ++c)
						value.v[c] = p["v"][c].get<float>();
				value.binding = p.value("binding", std::string());
			}
			return true;
		}

		json StackToJson(const std::vector<ModuleInstance>& aStack)
		{
			json a = json::array();
			for (const ModuleInstance& module : aStack)
				a.push_back(ModuleToJson(module));
			return a;
		}

		void StackFromJson(const json& j, const char* aKey, std::vector<ModuleInstance>& aOut)
		{
			aOut.clear();
			if (!j.contains(aKey) || !j[aKey].is_array())
				return;
			for (const json& m : j[aKey])
			{
				ModuleInstance module;
				if (ModuleFromJson(m, module))
					aOut.push_back(std::move(module));
			}
		}
	}

	bool SystemAsset::Save(const std::string& aPath) const
	{
		json j;
		j["version"] = 1;
		j["name"] = name;
		j["seed"] = seed;

		json users = json::array();
		for (const UserParameter& user : userParameters)
			users.push_back({ { "name", user.name }, { "type", (int)user.type }, { "value", std::vector<float>(user.value, user.value + 4) } });
		j["user"] = users;

		json emitterArray = json::array();
		for (const EmitterAsset& emitter : emitters)
		{
			json e;
			e["name"] = emitter.name;
			e["enabled"] = emitter.enabled;
			e["space"] = (int)emitter.space;
			e["maxParticles"] = emitter.maxParticles;
			e["looping"] = emitter.looping;
			e["loopDuration"] = emitter.loopDuration;
			e["startDelay"] = emitter.startDelay;
			e["emitterUpdate"] = StackToJson(emitter.emitterUpdate);
			e["particleSpawn"] = StackToJson(emitter.particleSpawn);
			e["particleUpdate"] = StackToJson(emitter.particleUpdate);

			const RendererSettings& r = emitter.renderer;
			e["renderer"] = {
				{ "enabled", r.enabled }, { "texture", r.texture }, { "blend", (int)r.blend }, { "facing", (int)r.facing },
				{ "sort", (int)r.sort }, { "columns", r.flipbookColumns }, { "rows", r.flipbookRows }, { "stretch", r.velocityStretch }, { "brightness", r.brightness } };
			emitterArray.push_back(e);
		}
		j["emitters"] = emitterArray;

		std::ofstream out(aPath);
		if (!out.is_open())
			return false;
		out << j.dump(2) << "\n";
		return true;
	}

	bool SystemAsset::Load(const std::string& aPath)
	{
		std::ifstream in(aPath);
		if (!in.is_open())
			return false;
		json j;
		try { in >> j; }
		catch (const std::exception&) { return false; }

		name = j.value("name", std::string("Particle System"));
		seed = j.value("seed", 0u);

		userParameters.clear();
		if (j.contains("user") && j["user"].is_array())
		{
			for (const json& u : j["user"])
			{
				UserParameter user;
				user.name = u.value("name", std::string());
				user.type = (ParamType)u.value("type", (int)ParamType::Float);
				if (u.contains("value") && u["value"].is_array())
					for (size_t c = 0; c < 4 && c < u["value"].size(); ++c)
						user.value[c] = u["value"][c].get<float>();
				if (!user.name.empty())
					userParameters.push_back(std::move(user));
			}
		}

		emitters.clear();
		if (j.contains("emitters") && j["emitters"].is_array())
		{
			for (const json& e : j["emitters"])
			{
				EmitterAsset emitter;
				emitter.name = e.value("name", std::string("Emitter"));
				emitter.enabled = e.value("enabled", true);
				emitter.space = (SimulationSpace)e.value("space", 0);
				emitter.maxParticles = std::clamp(e.value("maxParticles", 1000), 1, 100000);
				emitter.looping = e.value("looping", true);
				emitter.loopDuration = e.value("loopDuration", 5.f);
				emitter.startDelay = e.value("startDelay", 0.f);
				StackFromJson(e, "emitterUpdate", emitter.emitterUpdate);
				StackFromJson(e, "particleSpawn", emitter.particleSpawn);
				StackFromJson(e, "particleUpdate", emitter.particleUpdate);
				if (e.contains("renderer") && e["renderer"].is_object())
				{
					const json& r = e["renderer"];
					emitter.renderer.enabled = r.value("enabled", true);
					emitter.renderer.texture = r.value("texture", std::string());
					emitter.renderer.blend = (BlendMode)r.value("blend", 0);
					emitter.renderer.facing = (FacingMode)r.value("facing", 0);
					emitter.renderer.sort = (SortMode)r.value("sort", 1);
					emitter.renderer.flipbookColumns = std::max(1, r.value("columns", 1));
					emitter.renderer.flipbookRows = std::max(1, r.value("rows", 1));
					emitter.renderer.velocityStretch = r.value("stretch", 0.1f);
					emitter.renderer.brightness = r.value("brightness", 2000.f);
				}
				emitters.push_back(std::move(emitter));
			}
		}
		return true;
	}
}
