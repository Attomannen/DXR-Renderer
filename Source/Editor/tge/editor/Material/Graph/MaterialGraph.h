#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// A small node-graph material authoring layer that sits *on top of* the
// existing flat .tgmat format -- it does not replace it. The graph is
// evaluated per-texel and baked into ordinary _C/_N/_M/_FX.dds files plus
// MaterialAsset fields (see MaterialGraphBake, implemented in the
// EditorDefaultGraphics project since only that project links DirectXTex).
// A .tgmat with no graph behaves exactly as it always has -- the graph is
// strictly opt-in, stored in a sibling ".tgmatgraph" JSON file.
//
// This header is intentionally graphics-backend-agnostic (no DirectXTex, no
// Graphics-project includes) so MaterialDocument (Editor project) can own a
// MaterialGraph by value. Only the bake implementation, which needs to
// sample real texture pixels, lives on the EditorDefaultGraphics side.
namespace Tga::MaterialGraphNS
{
	using Id = int;
	constexpr Id kInvalidId = 0;

	enum class RootChannel { BaseColor, Normal, Roughness, Metalness, AO, Emissive, Count };

	enum class NodeKind
	{
		TextureSample,
		ConstantScalar,
		ConstantVector,
		Multiply,
		Add,
		Lerp,
		Clamp,
		OneMinus,
		SplitChannels,
		CombineChannels,
		Output,   // fixed material-output socket, like a shader graph's Material Output node
	};

	// Every evaluated value is a float4; scalar-producing nodes only fill [0]
	// (and replicate it into [1],[2] where a node broadcasts a scalar against
	// a vector input) rather than needing a separate scalar/vector value type.
	struct GraphValue
	{
		float v[4] = { 0.f, 0.f, 0.f, 1.f };
	};

	struct Pin
	{
		Id id = kInvalidId;
		Id nodeId = kInvalidId;
		bool isInput = true;
		std::string name;
	};

	// Fixed-shape per-kind parameters. Only the fields relevant to `kind` are
	// meaningful; kept as one struct (rather than a variant) to match this
	// codebase's existing preference for plain data over polymorphism for
	// small per-node-type config (see ScriptNodeBase's simpler cousins).
	struct Node
	{
		Id id = kInvalidId;
		NodeKind kind = NodeKind::ConstantScalar;
		float posX = 0.f, posY = 0.f; // ImNodes editor canvas position

		// TextureSample
		std::string texturePath;
		// ConstantScalar / ConstantVector (vector uses all 3 of [0..2]); also
		// doubles as the default value for Multiply/Add/Lerp's unconnected
		// inputs (constant[0]=A's default, [1]=B's, [2]=Lerp's T's).
		float constant[3] = { 0.f, 0.f, 0.f };
		// Clamp: min/max.
		float paramA = 0.f, paramB = 1.f;
		// Output only: which fixed material socket this node represents.
		RootChannel outputChannel = RootChannel::BaseColor;

		std::vector<Id> inputPins;   // meaning depends on `kind`, see MaterialGraph.cpp
		std::vector<Id> outputPins;
	};

	struct Link
	{
		Id id = kInvalidId;
		Id fromPin = kInvalidId; // output pin
		Id toPin = kInvalidId;   // input pin
	};

	class MaterialGraph
	{
	public:
		// A fresh/loaded graph always has exactly one Output node per
		// RootChannel (created on construction, never deletable) -- see
		// EnsureOutputNodes(), called from the constructor and after Load().
		MaterialGraph();

		Id AddNode(NodeKind kind, float x, float y);
		void RemoveNode(Id nodeId);   // no-op for an Output node
		bool AddLink(Id fromPin, Id toPin);   // fails if toPin already has an incoming link, or types mismatch direction
		void RemoveLink(Id linkId);
		void RemoveLinksToPin(Id pinId);

		const std::vector<Node>& Nodes() const { return myNodes; }
		const std::vector<Pin>& Pins() const { return myPins; }
		const std::vector<Link>& Links() const { return myLinks; }
		Node* FindNode(Id id);
		const Node* FindNode(Id id) const;
		const Pin* FindPin(Id id) const;
		// The single link feeding an input pin, or nullptr if unconnected.
		const Link* IncomingLink(Id inputPinId) const;

		// The always-present Output node for `channel`, and what feeds it
		// (kInvalidId if unconnected).
		const Node* OutputNode(RootChannel channel) const;
		Id RootValuePin(RootChannel channel) const;

		// True if the subgraph feeding `outputPinId` never reaches a
		// TextureSample node with a path assigned -- i.e. its value is the
		// same at every texel. Lets a bake skip generating (and BC7/BC5
		// compressing) an entire flat texture for something like a bare
		// Constant node, writing straight into the matching MaterialAsset
		// scalar/vector field instead -- both much faster and matches how a
		// texture-less .tgmat already represents a flat value with no map.
		bool IsConstant(Id outputPinId) const;

		// Evaluates the node feeding `outputPinId` at a normalized (u,v) in
		// [0,1). `SampleFn` is called for TextureSample nodes: given a texture
		// path and (u,v), it must return the sampled RGBA in [0,1]. Kept as a
		// callback (rather than this class doing the sampling) so this header
		// stays free of any texture-loading dependency.
		using SampleFn = GraphValue(*)(void* userData, const std::string& path, float u, float v);
		GraphValue Evaluate(Id outputPinId, float u, float v, SampleFn sample, void* userData) const;

		bool LoadFromJson(const nlohmann::json& j);
		nlohmann::json ToJson() const;
		bool Load(const std::string& path);   // path is a ".tgmatgraph" sidecar
		bool Save(const std::string& path) const;

		static const char* NodeKindName(NodeKind k);
		static const char* RootChannelName(RootChannel c);

	private:
		void EnsureOutputNodes();
		Id NextId() { return ++myNextId; }
		Pin& AddPin(Id nodeId, bool isInput, const std::string& name);

		Id myNextId = 0;
		std::vector<Node> myNodes;
		std::vector<Pin> myPins;
		std::vector<Link> myLinks;
	};
}
