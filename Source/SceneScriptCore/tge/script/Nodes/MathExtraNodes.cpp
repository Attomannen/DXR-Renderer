#include <stdafx.h>
#include "MathExtraNodes.h"

#include <algorithm>
#include <cmath>

#include <tge/script/ScriptNodeTypeRegistry.h>
#include <tge/stringRegistry/StringRegistry.h>
#include <tge/script/ScriptCommon.h>
#include <tge/script/ScriptNodeBase.h>
#include <tge/script/BaseProperties.h>
#include <tge/math/Matrix4x4.h>

using namespace Tga;

namespace
{
	constexpr float kDegToRad = 0.01745329252f;
	constexpr float kRadToDeg = 57.2957795131f;

	template <typename T>
	ScriptPinId AddInput(const ScriptCreationContext& context, const char* name, const T& defaultValue)
	{
		ScriptPin pin = {};
		pin.type = ScriptLinkType::Property;
		pin.role = ScriptPinRole::Input;
		pin.dataType = GetPropertyType<T>();
		pin.name = StringRegistry::RegisterOrGetString(name);
		pin.node = context.GetNodeId();
		pin.defaultValue = Property::Create<T>(defaultValue);
		return context.FindOrCreatePin(pin);
	}

	template <typename T>
	ScriptPinId AddOutput(const ScriptCreationContext& context, const char* name)
	{
		ScriptPin pin = {};
		pin.type = ScriptLinkType::Property;
		pin.role = ScriptPinRole::Output;
		pin.dataType = GetPropertyType<T>();
		pin.name = StringRegistry::RegisterOrGetString(name);
		pin.node = context.GetNodeId();
		return context.FindOrCreatePin(pin);
	}

	template <typename T>
	T Read(ScriptExecutionContext& context, ScriptPinId pin)
	{
		const T* value = context.ReadInputPin(pin).Get<T>();
		return value ? *value : T{};
	}

	// float -> float, e.g. Sin
	template <float (*Fn)(float)>
	class Float1Node : public ScriptNodeBase
	{
		ScriptPinId myIn;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myIn = AddInput<float>(context, "Value", 0.f);
			AddOutput<float>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			return Property::Create<float>(Fn(Read<float>(context, myIn)));
		}
	};

	// (float, float) -> float, e.g. Min
	template <float (*Fn)(float, float)>
	class Float2Node : public ScriptNodeBase
	{
		ScriptPinId myA, myB;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myA = AddInput<float>(context, "A", 0.f);
			myB = AddInput<float>(context, "B", 0.f);
			AddOutput<float>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			return Property::Create<float>(Fn(Read<float>(context, myA), Read<float>(context, myB)));
		}
	};

	float Sin(float degrees) { return std::sin(degrees * kDegToRad); }
	float Cos(float degrees) { return std::cos(degrees * kDegToRad); }
	float Abs(float v) { return std::fabs(v); }
	float Sqrt(float v) { return v > 0.f ? std::sqrt(v) : 0.f; }
	float Negate(float v) { return -v; }
	float Sign(float v) { return v > 0.f ? 1.f : (v < 0.f ? -1.f : 0.f); }
	float Min(float a, float b) { return std::min(a, b); }
	float Max(float a, float b) { return std::max(a, b); }
	float Atan2Degrees(float y, float x) { return std::atan2(y, x) * kRadToDeg; }
	float Modulo(float a, float b) { return b != 0.f ? std::fmod(a, b) : 0.f; }

	// Brings an angle into (-180, 180].
	float WrapAngle(float degrees)
	{
		degrees = std::fmod(degrees, 360.f);
		if (degrees > 180.f) degrees -= 360.f;
		if (degrees <= -180.f) degrees += 360.f;
		return degrees;
	}

	class ClampNode : public ScriptNodeBase
	{
		ScriptPinId myValue, myMin, myMax;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myValue = AddInput<float>(context, "Value", 0.f);
			myMin = AddInput<float>(context, "Min", 0.f);
			myMax = AddInput<float>(context, "Max", 1.f);
			AddOutput<float>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const float lo = Read<float>(context, myMin), hi = Read<float>(context, myMax);
			return Property::Create<float>(std::clamp(Read<float>(context, myValue), std::min(lo, hi), std::max(lo, hi)));
		}
	};

	class LerpNode : public ScriptNodeBase
	{
		ScriptPinId myA, myB, myT;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myA = AddInput<float>(context, "A", 0.f);
			myB = AddInput<float>(context, "B", 1.f);
			myT = AddInput<float>(context, "T", 0.5f);
			AddOutput<float>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const float a = Read<float>(context, myA), b = Read<float>(context, myB), t = Read<float>(context, myT);
			return Property::Create<float>(a + (b - a) * t);
		}
	};

	// --- Vector3 ---

	class Vector3ScaleNode : public ScriptNodeBase
	{
		ScriptPinId myVector, myScale;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myVector = AddInput<Vector3f>(context, "Vector", Vector3f{ 0.f, 0.f, 0.f });
			myScale = AddInput<float>(context, "Scale", 1.f);
			AddOutput<Vector3f>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const Vector3f v = Read<Vector3f>(context, myVector);
			const float s = Read<float>(context, myScale);
			return Property::Create<Vector3f>(Vector3f{ v.x * s, v.y * s, v.z * s });
		}
	};

	class Vector3LengthNode : public ScriptNodeBase
	{
		ScriptPinId myVector;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myVector = AddInput<Vector3f>(context, "Vector", Vector3f{ 0.f, 0.f, 0.f });
			AddOutput<float>(context, "Length");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const Vector3f v = Read<Vector3f>(context, myVector);
			return Property::Create<float>(std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z));
		}
	};

	// A zero vector stays zero, so releasing all keys does not produce NaNs.
	class Vector3NormalizeNode : public ScriptNodeBase
	{
		ScriptPinId myVector;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myVector = AddInput<Vector3f>(context, "Vector", Vector3f{ 0.f, 0.f, 0.f });
			AddOutput<Vector3f>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const Vector3f v = Read<Vector3f>(context, myVector);
			const float length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
			if (length < 1e-6f)
				return Property::Create<Vector3f>(Vector3f{ 0.f, 0.f, 0.f });
			return Property::Create<Vector3f>(Vector3f{ v.x / length, v.y / length, v.z / length });
		}
	};

	class Vector3DotNode : public ScriptNodeBase
	{
		ScriptPinId myA, myB;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myA = AddInput<Vector3f>(context, "A", Vector3f{ 0.f, 0.f, 0.f });
			myB = AddInput<Vector3f>(context, "B", Vector3f{ 0.f, 0.f, 0.f });
			AddOutput<float>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const Vector3f a = Read<Vector3f>(context, myA), b = Read<Vector3f>(context, myB);
			return Property::Create<float>(a.x * b.x + a.y * b.y + a.z * b.z);
		}
	};

	class Vector3CrossNode : public ScriptNodeBase
	{
		ScriptPinId myA, myB;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myA = AddInput<Vector3f>(context, "A", Vector3f{ 0.f, 0.f, 0.f });
			myB = AddInput<Vector3f>(context, "B", Vector3f{ 0.f, 0.f, 0.f });
			AddOutput<Vector3f>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const Vector3f a = Read<Vector3f>(context, myA), b = Read<Vector3f>(context, myB);
			return Property::Create<Vector3f>(Vector3f{ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x });
		}
	};

	// --- Boolean and selection ---

	template <int Op> // 0 = and, 1 = or
	class BoolBinaryNode : public ScriptNodeBase
	{
		ScriptPinId myA, myB;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myA = AddInput<bool>(context, "A", false);
			myB = AddInput<bool>(context, "B", false);
			AddOutput<bool>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const bool a = Read<bool>(context, myA), b = Read<bool>(context, myB);
			return Property::Create<bool>(Op == 0 ? (a && b) : (a || b));
		}
	};

	class BoolNotNode : public ScriptNodeBase
	{
		ScriptPinId myValue;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myValue = AddInput<bool>(context, "Value", false);
			AddOutput<bool>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			return Property::Create<bool>(!Read<bool>(context, myValue));
		}
	};

	template <typename T>
	class SelectNode : public ScriptNodeBase
	{
		ScriptPinId myCondition, myIfTrue, myIfFalse;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myCondition = AddInput<bool>(context, "Condition", false);
			myIfTrue = AddInput<T>(context, "If True", T{});
			myIfFalse = AddInput<T>(context, "If False", T{});
			AddOutput<T>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			return Property::Create<T>(Read<bool>(context, myCondition) ? Read<T>(context, myIfTrue) : Read<T>(context, myIfFalse));
		}
	};

	// Yaw (degrees about Y, the same rotation Set Rotation applies) to the horizontal
	// directions an object with that yaw faces and has to its right. Built from the engine's
	// own rotation matrix, so it always agrees with what the object actually does.
	class YawDirectionNode : public ScriptNodeBase
	{
		ScriptPinId myYaw, myForward, myRight;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myYaw = AddInput<float>(context, "Yaw", 0.f);
			myForward = AddOutput<Vector3f>(context, "Forward");
			myRight = AddOutput<Vector3f>(context, "Right");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId pin) const override
		{
			const Matrix4x4f rotation = Matrix4x4f::CreateFromRollPitchYaw(Vector3f{ 0.f, Read<float>(context, myYaw), 0.f });
			return Property::Create<Vector3f>(pin == myForward ? rotation.GetForward() : rotation.GetRight());
		}
	};
}

void Tga::RegisterMathExtraNodes()
{
	using R = ScriptNodeTypeRegistry;

	R::RegisterType<Float1Node<Sin>>("Common/Math/float/sin (degrees)", "Sine of an angle in degrees");
	R::RegisterType<Float1Node<Cos>>("Common/Math/float/cos (degrees)", "Cosine of an angle in degrees");
	R::RegisterType<Float1Node<Abs>>("Common/Math/float/abs", "Absolute value");
	R::RegisterType<Float1Node<Sqrt>>("Common/Math/float/sqrt", "Square root (0 for negative values)");
	R::RegisterType<Float1Node<Negate>>("Common/Math/float/negate", "Changes the sign");
	R::RegisterType<Float1Node<Sign>>("Common/Math/float/sign", "-1, 0 or 1");
	R::RegisterType<Float1Node<WrapAngle>>("Common/Math/float/wrap angle", "Brings an angle in degrees into -180..180");
	R::RegisterType<Float2Node<Min>>("Common/Math/float/min", "The smaller of two values");
	R::RegisterType<Float2Node<Max>>("Common/Math/float/max", "The larger of two values");
	R::RegisterType<Float2Node<Modulo>>("Common/Math/float/modulo", "Remainder of A divided by B");
	R::RegisterType<Float2Node<Atan2Degrees>>("Common/Math/float/atan2 (degrees)", "Angle in degrees of the point (X = B, Y = A)");
	R::RegisterType<ClampNode>("Common/Math/float/clamp", "Limits a value to a range");
	R::RegisterType<LerpNode>("Common/Math/float/lerp", "A + (B - A) * T");

	R::RegisterType<Vector3ScaleNode>("Common/Math/vector3/float3 * float", "Multiplies a vector by a number");
	R::RegisterType<Vector3LengthNode>("Common/Math/vector3/float3 length", "Length of a vector");
	R::RegisterType<Vector3NormalizeNode>("Common/Math/vector3/float3 normalize", "Vector with length 1 (zero stays zero)");
	R::RegisterType<Vector3DotNode>("Common/Math/vector3/float3 dot", "Dot product");
	R::RegisterType<Vector3CrossNode>("Common/Math/vector3/float3 cross", "Cross product");

	R::RegisterType<BoolBinaryNode<0>>("Common/Math/bool/and", "True when both are true");
	R::RegisterType<BoolBinaryNode<1>>("Common/Math/bool/or", "True when either is true");
	R::RegisterType<BoolNotNode>("Common/Math/bool/not", "Inverts a bool");
	R::RegisterType<SelectNode<float>>("Common/Math/bool/select float", "If True or If False depending on the condition");
	R::RegisterType<SelectNode<Vector3f>>("Common/Math/bool/select float3", "If True or If False depending on the condition");

	R::RegisterType<YawDirectionNode>("Common/Math/vector3/yaw direction", "Forward and right directions of an object turned by a yaw in degrees");
}
