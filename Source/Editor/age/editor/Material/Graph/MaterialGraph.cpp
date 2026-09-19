#include "stdafx.h"
#include <age/editor/Material/Graph/MaterialGraph.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>

using namespace Ag::MaterialGraphNS;

namespace
{
	constexpr float kPi = 3.14159265358979f;
	constexpr size_t kConstantSlots = 5;

	float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
	float Fract(float v) { return v - std::floor(v); }
	float SmoothStep01(float t) { t = Clampf(t, 0.f, 1.f); return t * t * (3.f - 2.f * t); }

	// Integer hash -> [0,1). Deterministic and free of trig, so a bake is identical on every machine.
	float Hash(int x, int y, uint32_t seed)
	{
		uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
		h = (h ^ (h >> 13)) * 1274126177u;
		h ^= h >> 16;
		return (float)(h & 0xFFFFFFu) / 16777216.f;
	}

	float ValueNoise(float x, float y)
	{
		const int ix = (int)std::floor(x), iy = (int)std::floor(y);
		const float fx = SmoothStep01(Fract(x)), fy = SmoothStep01(Fract(y));
		const float a = Hash(ix, iy, 1), b = Hash(ix + 1, iy, 1), c = Hash(ix, iy + 1, 1), d = Hash(ix + 1, iy + 1, 1);
		const float top = a + (b - a) * fx;
		const float bottom = c + (d - c) * fx;
		return top + (bottom - top) * fy;
	}

	float FractalNoise(float x, float y, int octaves)
	{
		float sum = 0.f, amplitude = 0.5f, total = 0.f;
		for (int i = 0; i < octaves; ++i)
		{
			sum += ValueNoise(x, y) * amplitude;
			total += amplitude;
			amplitude *= 0.5f;
			x *= 2.f; y *= 2.f;
		}
		return total > 0.f ? sum / total : 0.f;
	}

	void RgbToHsv(const float r, const float g, const float b, float& h, float& s, float& v)
	{
		const float mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b));
		const float d = mx - mn;
		v = mx;
		s = mx > 0.f ? d / mx : 0.f;
		if (d <= 0.f) { h = 0.f; return; }
		if (mx == r) h = std::fmod((g - b) / d, 6.f);
		else if (mx == g) h = (b - r) / d + 2.f;
		else h = (r - g) / d + 4.f;
		h /= 6.f;
		if (h < 0.f) h += 1.f;
	}

	void HsvToRgb(float h, const float s, const float v, float& r, float& g, float& b)
	{
		h = Fract(h) * 6.f;
		const float c = v * s;
		const float x = c * (1.f - std::fabs(std::fmod(h, 2.f) - 1.f));
		const float m = v - c;
		float rr = 0.f, gg = 0.f, bb = 0.f;
		if (h < 1.f) { rr = c; gg = x; }
		else if (h < 2.f) { rr = x; gg = c; }
		else if (h < 3.f) { gg = c; bb = x; }
		else if (h < 4.f) { gg = x; bb = c; }
		else if (h < 5.f) { rr = x; bb = c; }
		else { rr = c; bb = x; }
		r = rr + m; g = gg + m; b = bb + m;
	}
}

// One entry per NodeKind, in enum order (the enum is serialised, see MaterialGraph.h).
static const std::vector<NodeDef>& NodeDefs()
{
	static const std::vector<NodeDef> defs =
	{
		// TextureSample: the UV input was added later, older graphs simply have no such pin.
		{ "Texture Sample", "Texture", { { "UV", 0.f, false, true } }, { "RGBA" } },
		{ "Constant (Scalar)", "Constants", {}, { "Value" } },
		{ "Constant (Vector3)", "Constants", {}, { "Value" } },
		{ "Multiply", "Math", { { "A", 0.f, true }, { "B", 0.f, true } }, { "Result" } },
		{ "Add", "Math", { { "A", 0.f, true }, { "B", 0.f, true } }, { "Result" } },
		{ "Lerp", "Math", { { "A", 0.f, true }, { "B", 0.f, true }, { "T", 0.f, true } }, { "Result" } },
		{ "Clamp", "Math", { { "Value" } }, { "Result" } },
		{ "One Minus", "Math", { { "Value" } }, { "Result" } },
		{ "Split Channels", "Vector", { { "RGBA" } }, { "R", "G", "B", "A" } },
		{ "Combine Channels", "Vector", { { "R" }, { "G" }, { "B" }, { "A", 1.f } }, { "RGBA" } },
		{ "Material Output", "", { { "Value" } }, {} },

		{ "Constant (Vector4)", "Constants", {}, { "Value" } },
		{ "Texture Coordinate", "Coordinates", {}, { "UV", "U", "V" }, true },
		{ "Rotate UV", "Coordinates", { { "UV", 0.f, false, true }, { "Angle", 0.f, true } }, { "UV" } },

		{ "Subtract", "Math", { { "A", 0.f, true }, { "B", 0.f, true } }, { "Result" } },
		{ "Divide", "Math", { { "A", 0.f, true }, { "B", 1.f, true } }, { "Result" } },
		{ "Power", "Math", { { "Base", 1.f, true }, { "Exponent", 2.f, true } }, { "Result" } },
		{ "Min", "Math", { { "A", 0.f, true }, { "B", 0.f, true } }, { "Result" } },
		{ "Max", "Math", { { "A", 0.f, true }, { "B", 0.f, true } }, { "Result" } },
		{ "Modulo", "Math", { { "A", 0.f, true }, { "B", 1.f, true } }, { "Result" } },
		{ "Abs", "Math", { { "Value" } }, { "Result" } },
		{ "Negate", "Math", { { "Value" } }, { "Result" } },
		{ "Saturate", "Math", { { "Value" } }, { "Result" } },
		{ "Floor", "Math", { { "Value" } }, { "Result" } },
		{ "Ceil", "Math", { { "Value" } }, { "Result" } },
		{ "Frac", "Math", { { "Value" } }, { "Result" } },
		{ "Round", "Math", { { "Value" } }, { "Result" } },
		{ "Sign", "Math", { { "Value" } }, { "Result" } },
		{ "Sqrt", "Math", { { "Value" } }, { "Result" } },
		{ "Reciprocal", "Math", { { "Value" } }, { "Result" } },
		{ "Sine", "Math", { { "Value" } }, { "Result" } },
		{ "Cosine", "Math", { { "Value" } }, { "Result" } },
		{ "Step", "Math", { { "Edge", 0.5f, true }, { "Value", 0.f, true } }, { "Result" } },
		{ "Smooth Step", "Math", { { "Min", 0.f, true }, { "Max", 1.f, true }, { "Value", 0.f, true } }, { "Result" } },
		{ "Remap Range", "Math", { { "Value", 0.f, true }, { "In Min", 0.f, true }, { "In Max", 1.f, true }, { "Out Min", 0.f, true }, { "Out Max", 1.f, true } }, { "Result" } },
		{ "If", "Math", { { "A", 0.f, true }, { "B", 0.f, true }, { "A > B", 0.f, true }, { "A = B", 0.f, true }, { "A < B", 0.f, true } }, { "Result" } },

		{ "Dot Product", "Vector", { { "A", 0.f, true }, { "B", 0.f, true } }, { "Result" } },
		{ "Cross Product", "Vector", { { "A", 0.f, true }, { "B", 0.f, true } }, { "Result" } },
		{ "Distance", "Vector", { { "A", 0.f, true }, { "B", 0.f, true } }, { "Result" } },
		{ "Normalize", "Vector", { { "Value" } }, { "Result" } },
		{ "Length", "Vector", { { "Value" } }, { "Result" } },
		{ "Component Mask", "Vector", { { "Value" } }, { "Result" } },

		{ "Contrast", "Color", { { "Value", 0.f, true }, { "Contrast", 1.f, true } }, { "Result" } },
		{ "Desaturation", "Color", { { "Color", 0.f, true }, { "Fraction", 1.f, true } }, { "Result" } },
		{ "Hue Shift", "Color", { { "Color", 0.f, true }, { "Shift", 0.f, true } }, { "Result" } },
		{ "Overlay", "Color", { { "Base", 0.5f, true }, { "Blend", 0.5f, true } }, { "Result" } },
		{ "Screen", "Color", { { "Base", 0.f, true }, { "Blend", 0.f, true } }, { "Result" } },

		{ "Checker", "Procedural", { { "UV", 0.f, false, true } }, { "Mask" } },
		{ "Noise", "Procedural", { { "UV", 0.f, false, true } }, { "Value" } },
		{ "Voronoi", "Procedural", { { "UV", 0.f, false, true } }, { "Distance", "Cell" } },
		{ "Circle", "Procedural", { { "UV", 0.f, false, true } }, { "Mask" } },
	};
	return defs;
}

const NodeDef& Ag::MaterialGraphNS::GetNodeDef(NodeKind kind)
{
	const std::vector<NodeDef>& defs = NodeDefs();
	static_assert((int)NodeKind::Count == 51, "NodeDefs() must have one entry per NodeKind");
	const size_t index = (size_t)kind;
	return defs[index < defs.size() ? index : (size_t)NodeKind::Output];
}

MaterialGraph::MaterialGraph()
{
	EnsureOutputNodes();
}

void MaterialGraph::EnsureOutputNodes()
{
	for (size_t i = 0; i < (size_t)RootChannel::Count; ++i)
	{
		const RootChannel channel = (RootChannel)i;
		bool exists = false;
		for (const Node& n : myNodes) if (n.kind == NodeKind::Output && n.outputChannel == channel) { exists = true; break; }
		if (exists) continue;

		const Id id = AddNode(NodeKind::Output, 900.f, 40.f + 120.f * (float)i);
		FindNode(id)->outputChannel = channel;
	}
}

Pin& MaterialGraph::AddPin(Id nodeId, bool isInput, const std::string& name)
{
	Pin p;
	p.id = NextId();
	p.nodeId = nodeId;
	p.isInput = isInput;
	p.name = name;
	myPins.push_back(p);
	return myPins.back();
}

Id MaterialGraph::AddNode(NodeKind kind, float x, float y)
{
	Node n;
	n.id = NextId();
	n.kind = kind;
	n.posX = x;
	n.posY = y;
	myNodes.push_back(n);
	Node& node = myNodes.back();

	// Pin layout comes from the node's definition. An unconnected input falls back to
	// constant[index] (broadcast to all channels) at evaluation time -- see Evaluate() --
	// so most inputs never need a wire, like in Unreal's material editor.
	const NodeDef& def = GetNodeDef(kind);
	for (size_t i = 0; i < def.inputs.size(); ++i)
	{
		node.inputPins.push_back(AddPin(node.id, true, def.inputs[i].name).id);
		if (i < kConstantSlots) node.constant[i] = def.inputs[i].defaultValue;
	}
	for (const char* output : def.outputs)
		node.outputPins.push_back(AddPin(node.id, false, output).id);

	switch (kind)
	{
	case NodeKind::ConstantVector4: node.constant[3] = 1.f; break;
	case NodeKind::TexCoord: node.constant[0] = 1.f; node.constant[1] = 1.f; break;   // tiling 1x1, offset 0
	case NodeKind::RotateUV: node.paramA = 0.5f; node.paramB = 0.5f; break;           // centre
	case NodeKind::ComponentMask: node.constant[0] = 1.f; node.constant[1] = 1.f; break;
	case NodeKind::Checker: node.paramA = 8.f; node.paramB = 8.f; break;
	case NodeKind::Noise: node.paramA = 8.f; node.paramB = 1.f; break;                 // scale, octaves
	case NodeKind::Voronoi: node.paramA = 8.f; break;
	case NodeKind::Circle: node.paramA = 0.4f; node.paramB = 0.02f; break;             // radius, softness
	default: break;
	}
	return node.id;
}

void MaterialGraph::RemoveNode(Id nodeId)
{
	Node* node = FindNode(nodeId);
	if (!node || node->kind == NodeKind::Output) return;   // fixed sockets are never deletable

	std::vector<Id> pinIds = node->inputPins;
	pinIds.insert(pinIds.end(), node->outputPins.begin(), node->outputPins.end());
	for (Id pinId : pinIds)
	{
		RemoveLinksToPin(pinId);
		myLinks.erase(std::remove_if(myLinks.begin(), myLinks.end(),
			[&](const Link& l) { return l.fromPin == pinId; }), myLinks.end());
		myPins.erase(std::remove_if(myPins.begin(), myPins.end(),
			[&](const Pin& p) { return p.id == pinId; }), myPins.end());
	}
	myNodes.erase(std::remove_if(myNodes.begin(), myNodes.end(),
		[&](const Node& n) { return n.id == nodeId; }), myNodes.end());
}

bool MaterialGraph::AddLink(Id fromPin, Id toPin)
{
	const Pin* from = FindPin(fromPin);
	const Pin* to = FindPin(toPin);
	if (!from || !to) return false;
	if (from->isInput || !to->isInput) return false;   // must be output -> input
	if (from->nodeId == to->nodeId) return false;       // no self-loops

	RemoveLinksToPin(toPin);   // an input can only ever have one incoming link
	Link link;
	link.id = NextId();
	link.fromPin = fromPin;
	link.toPin = toPin;
	myLinks.push_back(link);
	return true;
}

void MaterialGraph::RemoveLink(Id linkId)
{
	myLinks.erase(std::remove_if(myLinks.begin(), myLinks.end(),
		[&](const Link& l) { return l.id == linkId; }), myLinks.end());
}

void MaterialGraph::RemoveLinksToPin(Id pinId)
{
	myLinks.erase(std::remove_if(myLinks.begin(), myLinks.end(),
		[&](const Link& l) { return l.toPin == pinId; }), myLinks.end());
}

Node* MaterialGraph::FindNode(Id id)
{
	for (Node& n : myNodes) if (n.id == id) return &n;
	return nullptr;
}
const Node* MaterialGraph::FindNode(Id id) const
{
	for (const Node& n : myNodes) if (n.id == id) return &n;
	return nullptr;
}
const Pin* MaterialGraph::FindPin(Id id) const
{
	for (const Pin& p : myPins) if (p.id == id) return &p;
	return nullptr;
}
const Link* MaterialGraph::IncomingLink(Id inputPinId) const
{
	for (const Link& l : myLinks) if (l.toPin == inputPinId) return &l;
	return nullptr;
}

const Node* MaterialGraph::OutputNode(RootChannel channel) const
{
	for (const Node& n : myNodes) if (n.kind == NodeKind::Output && n.outputChannel == channel) return &n;
	return nullptr;
}

Id MaterialGraph::RootValuePin(RootChannel channel) const
{
	const Node* out = OutputNode(channel);
	if (!out || out->inputPins.empty()) return kInvalidId;
	const Link* link = IncomingLink(out->inputPins[0]);
	return link ? link->fromPin : kInvalidId;
}

bool MaterialGraph::IsConstant(Id outputPinId) const
{
	const Pin* pin = FindPin(outputPinId);
	if (!pin || pin->isInput) return true;   // nothing feeding it -- trivially constant
	const Node* node = FindNode(pin->nodeId);
	if (!node) return true;

	switch (node->kind)
	{
	case NodeKind::TextureSample:
		if (node->texturePath.empty()) return true;   // no path assigned -> always the same default value
		return false;
	case NodeKind::ConstantScalar:
	case NodeKind::ConstantVector:
	case NodeKind::ConstantVector4:
		return true;
	case NodeKind::Output:
		return true;   // never actually an entry point -- Evaluate()/IsConstant() are called on what feeds it
	default:
		break;
	}

	const NodeDef& def = GetNodeDef(node->kind);
	if (def.generatesUV) return false;
	for (size_t i = 0; i < def.inputs.size(); ++i)
	{
		const Link* link = i < node->inputPins.size() ? IncomingLink(node->inputPins[i]) : nullptr;
		// An unconnected UV input means "this texel", so the value varies across the image.
		if (!link && def.inputs[i].isUV) return false;
		// Any other unconnected input falls back to a literal float on the node itself.
		if (link && !IsConstant(link->fromPin)) return false;
	}
	return true;
}

GraphValue MaterialGraph::Evaluate(Id outputPinId, float u, float v, SampleFn sample, void* userData) const
{
	const Pin* pin = FindPin(outputPinId);
	if (!pin || pin->isInput) return {};
	const Node* node = FindNode(pin->nodeId);
	if (!node) return {};
	const NodeDef& def = GetNodeDef(node->kind);

	auto broadcast = [](float f) { GraphValue g; g.v[0] = g.v[1] = g.v[2] = f; g.v[3] = 1.f; return g; };
	auto isLinked = [&](size_t i) { return i < node->inputPins.size() && IncomingLink(node->inputPins[i]) != nullptr; };
	// The value of input i: what is wired to it, the texel coordinate for an unconnected UV input, or its own literal.
	auto in = [&](size_t i) -> GraphValue
	{
		if (i < node->inputPins.size())
			if (const Link* link = IncomingLink(node->inputPins[i]))
				return Evaluate(link->fromPin, u, v, sample, userData);
		if (i < def.inputs.size() && def.inputs[i].isUV)
		{
			GraphValue uv; uv.v[0] = u; uv.v[1] = v; uv.v[2] = 0.f; uv.v[3] = 1.f;
			return uv;
		}
		return broadcast(i < kConstantSlots ? node->constant[i] : 0.f);
	};
	auto map1 = [&](auto fn) { const GraphValue a = in(0); GraphValue o; for (int c = 0; c < 4; ++c) o.v[c] = fn(a.v[c]); return o; };
	auto map2 = [&](auto fn) { const GraphValue a = in(0), b = in(1); GraphValue o; for (int c = 0; c < 4; ++c) o.v[c] = fn(a.v[c], b.v[c]); return o; };
	auto vec3 = [](const GraphValue& g, float& x, float& y, float& z) { x = g.v[0]; y = g.v[1]; z = g.v[2]; };

	size_t outIdx = 0;
	for (; outIdx < node->outputPins.size(); ++outIdx) if (node->outputPins[outIdx] == outputPinId) break;

	GraphValue out{};
	switch (node->kind)
	{
	case NodeKind::TextureSample:
	{
		if (!sample || node->texturePath.empty()) break;
		float su = u, sv = v;
		if (isLinked(0))
		{
			const GraphValue uv = in(0);
			su = Fract(uv.v[0]); sv = Fract(uv.v[1]);   // repeat
		}
		out = sample(userData, node->texturePath, su, sv);
		break;
	}
	case NodeKind::ConstantScalar:
		out = broadcast(node->constant[0]);
		break;
	case NodeKind::ConstantVector:
		out.v[0] = node->constant[0]; out.v[1] = node->constant[1]; out.v[2] = node->constant[2]; out.v[3] = 1.f;
		break;
	case NodeKind::ConstantVector4:
		for (int c = 0; c < 4; ++c) out.v[c] = node->constant[c];
		break;
	case NodeKind::Multiply: out = map2([](float a, float b) { return a * b; }); break;
	case NodeKind::Add: out = map2([](float a, float b) { return a + b; }); break;
	case NodeKind::Subtract: out = map2([](float a, float b) { return a - b; }); break;
	case NodeKind::Divide: out = map2([](float a, float b) { return b != 0.f ? a / b : 0.f; }); break;
	case NodeKind::Power: out = map2([](float a, float b) { return a > 0.f ? std::pow(a, b) : 0.f; }); break;
	case NodeKind::Min: out = map2([](float a, float b) { return std::min(a, b); }); break;
	case NodeKind::Max: out = map2([](float a, float b) { return std::max(a, b); }); break;
	case NodeKind::Modulo: out = map2([](float a, float b) { return b != 0.f ? std::fmod(a, b) : 0.f; }); break;
	case NodeKind::Lerp:
	{
		const GraphValue a = in(0), b = in(1), t = in(2);
		for (int c = 0; c < 4; ++c) out.v[c] = a.v[c] + (b.v[c] - a.v[c]) * t.v[0];
		break;
	}
	case NodeKind::Clamp:
	{
		const float lo = node->paramA, hi = node->paramB;
		out = map1([&](float a) { return Clampf(a, lo, hi); });
		break;
	}
	case NodeKind::OneMinus:
	{
		const GraphValue value = in(0);
		out.v[0] = 1.f - value.v[0]; out.v[1] = 1.f - value.v[1]; out.v[2] = 1.f - value.v[2]; out.v[3] = value.v[3];
		break;
	}
	case NodeKind::Abs: out = map1([](float a) { return std::fabs(a); }); break;
	case NodeKind::Negate: out = map1([](float a) { return -a; }); break;
	case NodeKind::Saturate: out = map1([](float a) { return Clampf(a, 0.f, 1.f); }); break;
	case NodeKind::Floor: out = map1([](float a) { return std::floor(a); }); break;
	case NodeKind::Ceil: out = map1([](float a) { return std::ceil(a); }); break;
	case NodeKind::Frac: out = map1([](float a) { return Fract(a); }); break;
	case NodeKind::Round: out = map1([](float a) { return std::round(a); }); break;
	case NodeKind::Sign: out = map1([](float a) { return a > 0.f ? 1.f : (a < 0.f ? -1.f : 0.f); }); break;
	case NodeKind::Sqrt: out = map1([](float a) { return a > 0.f ? std::sqrt(a) : 0.f; }); break;
	case NodeKind::Reciprocal: out = map1([](float a) { return a != 0.f ? 1.f / a : 0.f; }); break;
	case NodeKind::Sine: out = map1([](float a) { return std::sin(a); }); break;
	case NodeKind::Cosine: out = map1([](float a) { return std::cos(a); }); break;
	case NodeKind::Step: out = map2([](float edge, float x) { return x >= edge ? 1.f : 0.f; }); break;
	case NodeKind::SmoothStep:
	{
		const GraphValue lo = in(0), hi = in(1), x = in(2);
		for (int c = 0; c < 4; ++c)
		{
			const float range = hi.v[c] - lo.v[c];
			out.v[c] = range != 0.f ? SmoothStep01((x.v[c] - lo.v[c]) / range) : (x.v[c] >= hi.v[c] ? 1.f : 0.f);
		}
		break;
	}
	case NodeKind::RemapRange:
	{
		const GraphValue x = in(0), inMin = in(1), inMax = in(2), outMin = in(3), outMax = in(4);
		for (int c = 0; c < 4; ++c)
		{
			const float range = inMax.v[c] - inMin.v[c];
			const float t = range != 0.f ? (x.v[c] - inMin.v[c]) / range : 0.f;
			out.v[c] = outMin.v[c] + (outMax.v[c] - outMin.v[c]) * t;
		}
		break;
	}
	case NodeKind::If:
	{
		const float a = in(0).v[0], b = in(1).v[0];
		out = std::fabs(a - b) < 1e-4f ? in(3) : (a > b ? in(2) : in(4));
		break;
	}
	case NodeKind::Dot:
	{
		float ax, ay, az, bx, by, bz;
		vec3(in(0), ax, ay, az); vec3(in(1), bx, by, bz);
		out = broadcast(ax * bx + ay * by + az * bz);
		break;
	}
	case NodeKind::Cross:
	{
		float ax, ay, az, bx, by, bz;
		vec3(in(0), ax, ay, az); vec3(in(1), bx, by, bz);
		out.v[0] = ay * bz - az * by; out.v[1] = az * bx - ax * bz; out.v[2] = ax * by - ay * bx; out.v[3] = 1.f;
		break;
	}
	case NodeKind::Distance:
	{
		float ax, ay, az, bx, by, bz;
		vec3(in(0), ax, ay, az); vec3(in(1), bx, by, bz);
		const float dx = ax - bx, dy = ay - by, dz = az - bz;
		out = broadcast(std::sqrt(dx * dx + dy * dy + dz * dz));
		break;
	}
	case NodeKind::Length:
	{
		float x, y, z;
		vec3(in(0), x, y, z);
		out = broadcast(std::sqrt(x * x + y * y + z * z));
		break;
	}
	case NodeKind::Normalize:
	{
		float x, y, z;
		vec3(in(0), x, y, z);
		const float length = std::sqrt(x * x + y * y + z * z);
		if (length > 0.f) { x /= length; y /= length; z /= length; }
		out.v[0] = x; out.v[1] = y; out.v[2] = z; out.v[3] = 1.f;
		break;
	}
	case NodeKind::ComponentMask:
	{
		// Selected channels are packed to the front, so a single channel comes out as a scalar in [0].
		const GraphValue value = in(0);
		int written = 0;
		for (int c = 0; c < 4; ++c)
			if (node->constant[c] > 0.5f) out.v[written++] = value.v[c];
		for (int c = written; c < 4; ++c) out.v[c] = c == 3 ? 1.f : 0.f;
		break;
	}
	case NodeKind::SplitChannels:
	{
		const GraphValue value = in(0);
		out = broadcast(value.v[std::min<size_t>(outIdx, 3)]);
		break;
	}
	case NodeKind::CombineChannels:
	{
		out.v[0] = in(0).v[0];
		out.v[1] = in(1).v[0];
		out.v[2] = in(2).v[0];
		// Alpha defaults to opaque even for graphs saved before the pin's default was stored.
		out.v[3] = isLinked(3) ? in(3).v[0] : 1.f;
		break;
	}
	case NodeKind::Contrast:
	{
		const GraphValue x = in(0);
		const float amount = in(1).v[0];
		for (int c = 0; c < 3; ++c) out.v[c] = (x.v[c] - 0.5f) * amount + 0.5f;
		out.v[3] = x.v[3];
		break;
	}
	case NodeKind::Desaturation:
	{
		const GraphValue color = in(0);
		const float fraction = in(1).v[0];
		const float luma = color.v[0] * 0.2126f + color.v[1] * 0.7152f + color.v[2] * 0.0722f;
		for (int c = 0; c < 3; ++c) out.v[c] = color.v[c] + (luma - color.v[c]) * fraction;
		out.v[3] = color.v[3];
		break;
	}
	case NodeKind::HueShift:
	{
		const GraphValue color = in(0);
		float h, s, val;
		RgbToHsv(color.v[0], color.v[1], color.v[2], h, s, val);
		HsvToRgb(h + in(1).v[0], s, val, out.v[0], out.v[1], out.v[2]);
		out.v[3] = color.v[3];
		break;
	}
	case NodeKind::Overlay:
		out = map2([](float base, float blend) { return base < 0.5f ? 2.f * base * blend : 1.f - 2.f * (1.f - base) * (1.f - blend); });
		break;
	case NodeKind::Screen:
		out = map2([](float base, float blend) { return 1.f - (1.f - base) * (1.f - blend); });
		break;
	case NodeKind::TexCoord:
	{
		const float tu = u * node->constant[0] + node->constant[2];
		const float tv = v * node->constant[1] + node->constant[3];
		out = outIdx == 1 ? broadcast(tu) : (outIdx == 2 ? broadcast(tv) : GraphValue{ { tu, tv, 0.f, 1.f } });
		break;
	}
	case NodeKind::RotateUV:
	{
		const GraphValue uv = in(0);
		const float angle = in(1).v[0] * kPi / 180.f;
		const float s = std::sin(angle), c = std::cos(angle);
		const float x = uv.v[0] - node->paramA, y = uv.v[1] - node->paramB;
		out = GraphValue{ { x * c - y * s + node->paramA, x * s + y * c + node->paramB, 0.f, 1.f } };
		break;
	}
	case NodeKind::Checker:
	{
		const GraphValue uv = in(0);
		const int cell = (int)std::floor(uv.v[0] * node->paramA) + (int)std::floor(uv.v[1] * node->paramB);
		out = broadcast((cell & 1) ? 1.f : 0.f);
		break;
	}
	case NodeKind::Noise:
	{
		const GraphValue uv = in(0);
		out = broadcast(FractalNoise(uv.v[0] * node->paramA, uv.v[1] * node->paramA, std::max(1, std::min(8, (int)std::lround(node->paramB)))));
		break;
	}
	case NodeKind::Voronoi:
	{
		const GraphValue uv = in(0);
		const float x = uv.v[0] * node->paramA, y = uv.v[1] * node->paramA;
		const int ix = (int)std::floor(x), iy = (int)std::floor(y);
		float best = 1e9f, cellValue = 0.f;
		for (int oy = -1; oy <= 1; ++oy)
			for (int ox = -1; ox <= 1; ++ox)
			{
				const int cx = ix + ox, cy = iy + oy;
				const float px = (float)cx + Hash(cx, cy, 11), py = (float)cy + Hash(cx, cy, 23);
				const float dx = px - x, dy = py - y;
				const float distance = dx * dx + dy * dy;
				if (distance < best) { best = distance; cellValue = Hash(cx, cy, 37); }
			}
		out = broadcast(outIdx == 1 ? cellValue : std::min(1.f, std::sqrt(best)));
		break;
	}
	case NodeKind::Circle:
	{
		const GraphValue uv = in(0);
		const float dx = uv.v[0] - 0.5f, dy = uv.v[1] - 0.5f;
		const float distance = std::sqrt(dx * dx + dy * dy);
		const float radius = node->paramA, softness = node->paramB;
		out = broadcast(softness > 0.f ? 1.f - SmoothStep01((distance - (radius - softness)) / softness) : (distance < radius ? 1.f : 0.f));
		break;
	}
	case NodeKind::Output:
	case NodeKind::Count:
		break;   // has no output pin; Evaluate() is never entered on one
	}
	return out;
}

nlohmann::json MaterialGraph::ToJson() const
{
	nlohmann::json j;
	j["nextId"] = myNextId;
	for (const Node& n : myNodes)
	{
		nlohmann::json nj;
		nj["id"] = n.id;
		nj["kind"] = (int)n.kind;
		nj["x"] = n.posX; nj["y"] = n.posY;
		nj["texturePath"] = n.texturePath;
		nj["constant"] = std::vector<float>(n.constant, n.constant + kConstantSlots);
		nj["paramA"] = n.paramA; nj["paramB"] = n.paramB;
		nj["outputChannel"] = (int)n.outputChannel;
		nj["inputPins"] = n.inputPins;
		nj["outputPins"] = n.outputPins;
		j["nodes"].push_back(nj);
	}
	for (const Pin& p : myPins)
		j["pins"].push_back({ {"id",p.id}, {"nodeId",p.nodeId}, {"isInput",p.isInput}, {"name",p.name} });
	for (const Link& l : myLinks)
		j["links"].push_back({ {"id",l.id}, {"fromPin",l.fromPin}, {"toPin",l.toPin} });
	return j;
}

bool MaterialGraph::LoadFromJson(const nlohmann::json& j)
{
	myNodes.clear(); myPins.clear(); myLinks.clear();
	myNextId = j.value("nextId", 0);

	for (const auto& nj : j.value("nodes", nlohmann::json::array()))
	{
		Node n;
		n.id = nj.value("id", 0);
		const int kind = nj.value("kind", 0);
		n.kind = (kind >= 0 && kind < (int)NodeKind::Count) ? (NodeKind)kind : NodeKind::ConstantScalar;
		n.posX = nj.value("x", 0.f); n.posY = nj.value("y", 0.f);
		n.texturePath = nj.value("texturePath", std::string());
		if (nj.contains("constant") && nj["constant"].is_array())
			for (size_t i = 0; i < kConstantSlots && i < nj["constant"].size(); ++i) n.constant[i] = nj["constant"][i].get<float>();
		n.paramA = nj.value("paramA", 0.f); n.paramB = nj.value("paramB", 1.f);
		n.outputChannel = (RootChannel)nj.value("outputChannel", 0);
		n.inputPins = nj.value("inputPins", std::vector<Id>());
		n.outputPins = nj.value("outputPins", std::vector<Id>());
		myNodes.push_back(n);
	}
	for (const auto& pj : j.value("pins", nlohmann::json::array()))
	{
		Pin p;
		p.id = pj.value("id", 0); p.nodeId = pj.value("nodeId", 0);
		p.isInput = pj.value("isInput", true); p.name = pj.value("name", std::string());
		myPins.push_back(p);
	}
	for (const auto& lj : j.value("links", nlohmann::json::array()))
	{
		Link l;
		l.id = lj.value("id", 0); l.fromPin = lj.value("fromPin", 0); l.toPin = lj.value("toPin", 0);
		myLinks.push_back(l);
	}
	EnsureOutputNodes();   // covers loading a graph saved before this field existed
	return true;
}

bool MaterialGraph::Load(const std::string& path)
{
	std::ifstream in(path);
	if (!in.is_open()) return false;
	nlohmann::json j;
	try { in >> j; } catch (const std::exception&) { return false; }
	return LoadFromJson(j);
}

bool MaterialGraph::Save(const std::string& path) const
{
	std::ofstream out(path);
	if (!out.is_open()) return false;
	out << ToJson().dump(2) << "\n";
	return true;
}

const char* MaterialGraph::NodeKindName(NodeKind k)
{
	return GetNodeDef(k).name;
}

const char* MaterialGraph::RootChannelName(RootChannel c)
{
	switch (c)
	{
	case RootChannel::BaseColor: return "Base Color";
	case RootChannel::Normal: return "Normal";
	case RootChannel::Roughness: return "Roughness";
	case RootChannel::Metalness: return "Metalness";
	case RootChannel::AO: return "Ambient Occlusion";
	case RootChannel::Emissive: return "Emissive";
	default: return "?";
	}
}
