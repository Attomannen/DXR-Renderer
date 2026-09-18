#include "stdafx.h"
#include <tge/editor/Material/Graph/MaterialGraph.h>

#include <algorithm>
#include <fstream>

using namespace Tga::MaterialGraphNS;

namespace
{
	float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
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

		const Id id = AddNode(NodeKind::Output, 600.f, 40.f + 120.f * (float)i);
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

	// Pin layout per kind. Unconnected inputs fall back to `constant[idx]`
	// (broadcast to all channels) at evaluation time -- see Evaluate() --
	// rather than every input requiring a wire, matching how most shader
	// graph editors (including UE's) let a pin carry its own default.
	switch (kind)
	{
	case NodeKind::TextureSample:
		node.outputPins.push_back(AddPin(node.id, false, "RGBA").id);
		break;
	case NodeKind::ConstantScalar:
	case NodeKind::ConstantVector:
		node.outputPins.push_back(AddPin(node.id, false, "Value").id);
		break;
	case NodeKind::Multiply:
	case NodeKind::Add:
		node.inputPins.push_back(AddPin(node.id, true, "A").id);
		node.inputPins.push_back(AddPin(node.id, true, "B").id);
		node.outputPins.push_back(AddPin(node.id, false, "Result").id);
		break;
	case NodeKind::Lerp:
		node.inputPins.push_back(AddPin(node.id, true, "A").id);
		node.inputPins.push_back(AddPin(node.id, true, "B").id);
		node.inputPins.push_back(AddPin(node.id, true, "T").id);
		node.outputPins.push_back(AddPin(node.id, false, "Result").id);
		break;
	case NodeKind::Clamp:
	case NodeKind::OneMinus:
		node.inputPins.push_back(AddPin(node.id, true, "Value").id);
		node.outputPins.push_back(AddPin(node.id, false, "Result").id);
		break;
	case NodeKind::SplitChannels:
		node.inputPins.push_back(AddPin(node.id, true, "RGBA").id);
		node.outputPins.push_back(AddPin(node.id, false, "R").id);
		node.outputPins.push_back(AddPin(node.id, false, "G").id);
		node.outputPins.push_back(AddPin(node.id, false, "B").id);
		node.outputPins.push_back(AddPin(node.id, false, "A").id);
		break;
	case NodeKind::CombineChannels:
		node.inputPins.push_back(AddPin(node.id, true, "R").id);
		node.inputPins.push_back(AddPin(node.id, true, "G").id);
		node.inputPins.push_back(AddPin(node.id, true, "B").id);
		node.inputPins.push_back(AddPin(node.id, true, "A").id);
		node.outputPins.push_back(AddPin(node.id, false, "RGBA").id);
		break;
	case NodeKind::Output:
		node.inputPins.push_back(AddPin(node.id, true, "Value").id);
		break;
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
		return node->texturePath.empty();   // no path assigned -> always the same default value
	case NodeKind::ConstantScalar:
	case NodeKind::ConstantVector:
		return true;
	case NodeKind::Output:
		return true;   // never actually an entry point -- Evaluate()/IsConstant() are called on what feeds it
	case NodeKind::Multiply:
	case NodeKind::Add:
	case NodeKind::Lerp:
	case NodeKind::Clamp:
	case NodeKind::OneMinus:
	case NodeKind::CombineChannels:
	case NodeKind::SplitChannels:
		for (Id inputPin : node->inputPins)
		{
			const Link* link = IncomingLink(inputPin);
			// An unconnected input falls back to a literal float on the node
			// itself (see Evaluate()) -- always constant regardless of kind.
			if (link && !IsConstant(link->fromPin)) return false;
		}
		return true;
	}
	return true;
}

GraphValue MaterialGraph::Evaluate(Id outputPinId, float u, float v, SampleFn sample, void* userData) const
{
	const Pin* pin = FindPin(outputPinId);
	if (!pin || pin->isInput) return {};
	const Node* node = FindNode(pin->nodeId);
	if (!node) return {};

	auto inputValue = [&](size_t inputIndex, GraphValue fallback) -> GraphValue
	{
		if (inputIndex >= node->inputPins.size()) return fallback;
		const Link* link = IncomingLink(node->inputPins[inputIndex]);
		if (!link) return fallback;
		return Evaluate(link->fromPin, u, v, sample, userData);
	};
	auto broadcast = [](float f) { GraphValue g; g.v[0] = g.v[1] = g.v[2] = f; g.v[3] = 1.f; return g; };

	GraphValue out{};
	switch (node->kind)
	{
	case NodeKind::TextureSample:
		if (sample && !node->texturePath.empty()) out = sample(userData, node->texturePath, u, v);
		break;
	case NodeKind::ConstantScalar:
		out = broadcast(node->constant[0]);
		break;
	case NodeKind::ConstantVector:
		out.v[0] = node->constant[0]; out.v[1] = node->constant[1]; out.v[2] = node->constant[2]; out.v[3] = 1.f;
		break;
	case NodeKind::Multiply:
	case NodeKind::Add:
	{
		const GraphValue a = inputValue(0, broadcast(node->constant[0]));
		const GraphValue b = inputValue(1, broadcast(node->constant[1]));
		for (int c = 0; c < 4; ++c) out.v[c] = node->kind == NodeKind::Multiply ? a.v[c] * b.v[c] : a.v[c] + b.v[c];
		break;
	}
	case NodeKind::Lerp:
	{
		const GraphValue a = inputValue(0, broadcast(node->constant[0]));
		const GraphValue b = inputValue(1, broadcast(node->constant[1]));
		const GraphValue t = inputValue(2, broadcast(node->constant[2]));
		for (int c = 0; c < 4; ++c) out.v[c] = a.v[c] + (b.v[c] - a.v[c]) * t.v[0];
		break;
	}
	case NodeKind::Clamp:
	{
		const GraphValue value = inputValue(0, broadcast(node->constant[0]));
		for (int c = 0; c < 4; ++c) out.v[c] = Clampf(value.v[c], node->paramA, node->paramB);
		break;
	}
	case NodeKind::OneMinus:
	{
		const GraphValue value = inputValue(0, broadcast(node->constant[0]));
		out.v[0] = 1.f - value.v[0]; out.v[1] = 1.f - value.v[1]; out.v[2] = 1.f - value.v[2]; out.v[3] = value.v[3];
		break;
	}
	case NodeKind::SplitChannels:
	{
		const GraphValue value = inputValue(0, GraphValue{});
		size_t outIdx = 0;
		for (; outIdx < node->outputPins.size(); ++outIdx) if (node->outputPins[outIdx] == outputPinId) break;
		out = broadcast(value.v[std::min<size_t>(outIdx, 3)]);
		break;
	}
	case NodeKind::CombineChannels:
	{
		out.v[0] = inputValue(0, GraphValue{}).v[0];
		out.v[1] = inputValue(1, GraphValue{}).v[0];
		out.v[2] = inputValue(2, GraphValue{}).v[0];
		const GraphValue aDefault = { { 1.f,1.f,1.f,1.f } };
		out.v[3] = inputValue(3, aDefault).v[0];
		break;
	}
	case NodeKind::Output:
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
		nj["constant"] = { n.constant[0], n.constant[1], n.constant[2] };
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
		n.kind = (NodeKind)nj.value("kind", 0);
		n.posX = nj.value("x", 0.f); n.posY = nj.value("y", 0.f);
		n.texturePath = nj.value("texturePath", std::string());
		if (nj.contains("constant") && nj["constant"].is_array())
			for (size_t i = 0; i < 3 && i < nj["constant"].size(); ++i) n.constant[i] = nj["constant"][i].get<float>();
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
	switch (k)
	{
	case NodeKind::TextureSample: return "Texture Sample";
	case NodeKind::ConstantScalar: return "Constant (Scalar)";
	case NodeKind::ConstantVector: return "Constant (Vector3)";
	case NodeKind::Multiply: return "Multiply";
	case NodeKind::Add: return "Add";
	case NodeKind::Lerp: return "Lerp";
	case NodeKind::Clamp: return "Clamp";
	case NodeKind::OneMinus: return "One Minus";
	case NodeKind::SplitChannels: return "Split Channels";
	case NodeKind::CombineChannels: return "Combine Channels";
	case NodeKind::Output: return "Material Output";
	}
	return "?";
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
