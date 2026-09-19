#include <stdafx.h>
#include "MathMoreNodes.h"

#include "NodeHelpers.h"

#include <algorithm>
#include <cmath>
#include <random>

using namespace Ag;
using namespace Ag::NodeHelpers;

namespace
{
	constexpr float kDegToRad = 0.01745329252f;
	constexpr float kRadToDeg = 57.2957795131f;

	std::mt19937& Rng()
	{
		static std::mt19937 generator{ std::random_device{}() };
		return generator;
	}

	// ---------------------------------------------------------------- generic node shapes

	// T -> R
	template <typename T, typename R, R (*Fn)(T)>
	class UnaryNode : public ScriptNodeBase
	{
		ScriptPinId myIn;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myIn = In<T>(context, "Value");
			Out<R>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			return Make<R>(Fn(Read<T>(context, myIn)));
		}
	};

	// (T, T) -> R
	template <typename T, typename R, R (*Fn)(T, T)>
	class BinaryNode : public ScriptNodeBase
	{
		ScriptPinId myA, myB;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myA = In<T>(context, "A");
			myB = In<T>(context, "B");
			Out<R>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			return Make<R>(Fn(Read<T>(context, myA), Read<T>(context, myB)));
		}
	};

	// (T, T, T) -> R
	template <typename T, typename R, R (*Fn)(T, T, T)>
	class TernaryNode : public ScriptNodeBase
	{
		ScriptPinId myA, myB, myC;
		const char* nameA() const { return "Value"; }
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myA = In<T>(context, "A");
			myB = In<T>(context, "B");
			myC = In<T>(context, "C");
			Out<R>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			return Make<R>(Fn(Read<T>(context, myA), Read<T>(context, myB), Read<T>(context, myC)));
		}
	};

	// ---------------------------------------------------------------- integers

	int IntAdd(int a, int b) { return a + b; }
	int IntSubtract(int a, int b) { return a - b; }
	int IntMultiply(int a, int b) { return a * b; }
	int IntDivide(int a, int b) { return b != 0 ? a / b : 0; }
	int IntModulo(int a, int b) { return b != 0 ? a % b : 0; }
	int IntMin(int a, int b) { return std::min(a, b); }
	int IntMax(int a, int b) { return std::max(a, b); }
	int IntAbs(int a) { return a < 0 ? -a : a; }
	int IntNegate(int a) { return -a; }
	int IntSign(int a) { return a > 0 ? 1 : (a < 0 ? -1 : 0); }
	int IntClamp(int v, int lo, int hi) { return std::clamp(v, std::min(lo, hi), std::max(lo, hi)); }
	bool IntEqual(int a, int b) { return a == b; }
	bool IntNotEqual(int a, int b) { return a != b; }
	bool IntLess(int a, int b) { return a < b; }
	bool IntLessEqual(int a, int b) { return a <= b; }
	bool IntGreater(int a, int b) { return a > b; }
	bool IntGreaterEqual(int a, int b) { return a >= b; }
	bool IntIsEven(int a) { return (a & 1) == 0; }

	class RandomIntNode : public ScriptNodeBase
	{
		ScriptPinId myMin, myMax;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myMin = In<int>(context, "Min", 0);
			myMax = In<int>(context, "Max", 10);
			Out<int>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const int a = Read<int>(context, myMin), b = Read<int>(context, myMax);
			return Make<int>(std::uniform_int_distribution<int>(std::min(a, b), std::max(a, b))(Rng()));
		}
	};

	// ---------------------------------------------------------------- floats

	bool FloatEqual(float a, float b) { return a == b; }
	bool FloatNotEqual(float a, float b) { return a != b; }
	bool FloatLess(float a, float b) { return a < b; }
	bool FloatLessEqual(float a, float b) { return a <= b; }
	bool FloatGreater(float a, float b) { return a > b; }
	bool FloatGreaterEqual(float a, float b) { return a >= b; }

	float FloatSubtract(float a, float b) { return a - b; }
	float FloatAdd(float a, float b) { return a + b; }
	float FloatMultiply(float a, float b) { return a * b; }
	float FloatDivide(float a, float b) { return b != 0.f ? a / b : 0.f; }
	float FloatPower(float base, float exponent) { return base > 0.f ? std::pow(base, exponent) : (exponent == 0.f ? 1.f : 0.f); }
	float FloatFloor(float v) { return std::floor(v); }
	float FloatCeil(float v) { return std::ceil(v); }
	float FloatRound(float v) { return std::round(v); }
	float FloatFrac(float v) { return v - std::floor(v); }
	float FloatTruncate(float v) { return std::trunc(v); }
	float FloatSquare(float v) { return v * v; }
	float FloatExp(float v) { return std::exp(v); }
	float FloatLog(float v) { return v > 0.f ? std::log(v) : 0.f; }
	float FloatTan(float degrees) { return std::tan(degrees * kDegToRad); }
	float FloatAsin(float v) { return std::asin(std::clamp(v, -1.f, 1.f)) * kRadToDeg; }
	float FloatAcos(float v) { return std::acos(std::clamp(v, -1.f, 1.f)) * kRadToDeg; }
	float FloatAtan(float v) { return std::atan(v) * kRadToDeg; }
	float DegreesToRadians(float v) { return v * kDegToRad; }
	float RadiansToDegrees(float v) { return v * kRadToDeg; }
	float Saturate(float v) { return std::clamp(v, 0.f, 1.f); }
	float OneMinus(float v) { return 1.f - v; }
	bool IsNearlyZero(float v) { return std::fabs(v) < 1e-4f; }

	float SmoothStep(float edgeLow, float edgeHigh, float x)
	{
		const float range = edgeHigh - edgeLow;
		const float t = range != 0.f ? std::clamp((x - edgeLow) / range, 0.f, 1.f) : (x >= edgeHigh ? 1.f : 0.f);
		return t * t * (3.f - 2.f * t);
	}

	// Where Value sits between A and B, as 0..1 (0 when A equals B).
	float InverseLerp(float a, float b, float value)
	{
		return b != a ? (value - a) / (b - a) : 0.f;
	}

	float MoveTowards(float current, float target, float maxDelta)
	{
		const float difference = target - current;
		return std::fabs(difference) <= maxDelta ? target : current + (difference > 0.f ? maxDelta : -maxDelta);
	}

	class MapRangeNode : public ScriptNodeBase
	{
		ScriptPinId myValue, myInA, myInB, myOutA, myOutB, myClamp;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myValue = In<float>(context, "Value", 0.f);
			myInA = In<float>(context, "In Min", 0.f);
			myInB = In<float>(context, "In Max", 1.f);
			myOutA = In<float>(context, "Out Min", 0.f);
			myOutB = In<float>(context, "Out Max", 100.f);
			myClamp = In<bool>(context, "Clamp", true);
			Out<float>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const float inA = Read<float>(context, myInA), inB = Read<float>(context, myInB);
			float t = inB != inA ? (Read<float>(context, myValue) - inA) / (inB - inA) : 0.f;
			if (Read<bool>(context, myClamp)) t = std::clamp(t, 0.f, 1.f);
			const float outA = Read<float>(context, myOutA), outB = Read<float>(context, myOutB);
			return Make<float>(outA + (outB - outA) * t);
		}
	};

	class NearlyEqualNode : public ScriptNodeBase
	{
		ScriptPinId myA, myB, myTolerance;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myA = In<float>(context, "A", 0.f);
			myB = In<float>(context, "B", 0.f);
			myTolerance = In<float>(context, "Tolerance", 0.0001f);
			Out<bool>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			return Make<bool>(std::fabs(Read<float>(context, myA) - Read<float>(context, myB)) <= Read<float>(context, myTolerance));
		}
	};

	// Eases Current towards Target: fast when far, slower as it arrives.
	class InterpToNode : public ScriptNodeBase
	{
		ScriptPinId myCurrent, myTarget, myDelta, mySpeed;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myCurrent = In<float>(context, "Current", 0.f);
			myTarget = In<float>(context, "Target", 1.f);
			myDelta = In<float>(context, "Delta Time", 0.016f);
			mySpeed = In<float>(context, "Speed", 5.f);
			Out<float>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const float current = Read<float>(context, myCurrent), target = Read<float>(context, myTarget);
			const float speed = Read<float>(context, mySpeed);
			if (speed <= 0.f) return Make<float>(target);
			const float distance = target - current;
			if (std::fabs(distance) < 1e-4f) return Make<float>(target);
			return Make<float>(current + distance * std::clamp(Read<float>(context, myDelta) * speed, 0.f, 1.f));
		}
	};

	class LerpAngleNode : public ScriptNodeBase
	{
		ScriptPinId myA, myB, myT;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myA = In<float>(context, "A", 0.f);
			myB = In<float>(context, "B", 90.f);
			myT = In<float>(context, "T", 0.5f);
			Out<float>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const float a = Read<float>(context, myA);
			float difference = std::fmod(Read<float>(context, myB) - a, 360.f);
			if (difference > 180.f) difference -= 360.f;
			if (difference < -180.f) difference += 360.f;
			return Make<float>(a + difference * Read<float>(context, myT));
		}
	};

	class RandomFloatNode : public ScriptNodeBase
	{
		ScriptPinId myMin, myMax;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myMin = In<float>(context, "Min", 0.f);
			myMax = In<float>(context, "Max", 1.f);
			Out<float>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const float a = Read<float>(context, myMin), b = Read<float>(context, myMax);
			return Make<float>(a + (b - a) * std::uniform_real_distribution<float>(0.f, 1.f)(Rng()));
		}
	};

	class RandomBoolNode : public ScriptNodeBase
	{
		ScriptPinId myChance;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myChance = In<float>(context, "Chance", 0.5f);
			Out<bool>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			return Make<bool>(std::uniform_real_distribution<float>(0.f, 1.f)(Rng()) < Read<float>(context, myChance));
		}
	};

	// ---------------------------------------------------------------- vectors

	Vector3f VecAdd(Vector3f a, Vector3f b) { return a + b; }
	Vector3f VecSubtract(Vector3f a, Vector3f b) { return a - b; }
	Vector3f VecMultiply(Vector3f a, Vector3f b) { return a * b; }
	Vector3f VecNegate(Vector3f a) { return a * -1.f; }
	float VecDistance(Vector3f a, Vector3f b) { return (a - b).Length(); }
	float VecDistanceSquared(Vector3f a, Vector3f b) { return (a - b).LengthSqr(); }
	Vector3f VecAbs(Vector3f a) { return Vector3f(std::fabs(a.x), std::fabs(a.y), std::fabs(a.z)); }
	Vector3f VecMin(Vector3f a, Vector3f b) { return Vector3f(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)); }
	Vector3f VecMax(Vector3f a, Vector3f b) { return Vector3f(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)); }
	Vector3f VecLerp(Vector3f a, Vector3f b, Vector3f t) { return Vector3f(a.x + (b.x - a.x) * t.x, a.y + (b.y - a.y) * t.x, a.z + (b.z - a.z) * t.x); }
	bool VecIsZero(Vector3f a) { return a.LengthSqr() < 1e-8f; }

	class MakeVectorNode : public ScriptNodeBase
	{
		ScriptPinId myX, myY, myZ;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myX = In<float>(context, "X", 0.f);
			myY = In<float>(context, "Y", 0.f);
			myZ = In<float>(context, "Z", 0.f);
			Out<Vector3f>(context, "Vector");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			return Make<Vector3f>(Vector3f(Read<float>(context, myX), Read<float>(context, myY), Read<float>(context, myZ)));
		}
	};

	class BreakVectorNode : public ScriptNodeBase
	{
		ScriptPinId myVector, myX, myY, myZ;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myVector = In<Vector3f>(context, "Vector", Vector3f(0.f, 0.f, 0.f));
			myX = Out<float>(context, "X");
			myY = Out<float>(context, "Y");
			myZ = Out<float>(context, "Z");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId pin) const override
		{
			const Vector3f v = Read<Vector3f>(context, myVector);
			return Make<float>(pin == myX ? v.x : (pin == myY ? v.y : v.z));
		}
	};

	class VectorDivideNode : public ScriptNodeBase
	{
		ScriptPinId myVector, myScalar;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myVector = In<Vector3f>(context, "Vector", Vector3f(0.f, 0.f, 0.f));
			myScalar = In<float>(context, "Divide By", 1.f);
			Out<Vector3f>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const float d = Read<float>(context, myScalar);
			return Make<Vector3f>(d != 0.f ? Read<Vector3f>(context, myVector) * (1.f / d) : Vector3f(0.f, 0.f, 0.f));
		}
	};

	class VectorInterpToNode : public ScriptNodeBase
	{
		ScriptPinId myCurrent, myTarget, myDelta, mySpeed;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myCurrent = In<Vector3f>(context, "Current", Vector3f(0.f, 0.f, 0.f));
			myTarget = In<Vector3f>(context, "Target", Vector3f(0.f, 0.f, 0.f));
			myDelta = In<float>(context, "Delta Time", 0.016f);
			mySpeed = In<float>(context, "Speed", 5.f);
			Out<Vector3f>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const Vector3f current = Read<Vector3f>(context, myCurrent), target = Read<Vector3f>(context, myTarget);
			const float speed = Read<float>(context, mySpeed);
			if (speed <= 0.f) return Make<Vector3f>(target);
			const float t = std::clamp(Read<float>(context, myDelta) * speed, 0.f, 1.f);
			return Make<Vector3f>(current + (target - current) * t);
		}
	};

	class RandomUnitVectorNode : public ScriptNodeBase
	{
	public:
		void Init(const ScriptCreationContext& context) override { Out<Vector3f>(context, "Direction"); }
		Property ReadPin(ScriptExecutionContext&, ScriptPinId) const override
		{
			std::uniform_real_distribution<float> unit(-1.f, 1.f);
			const float z = unit(Rng());
			const float phi = unit(Rng()) * 3.14159265f;
			const float ring = std::sqrt(std::max(0.f, 1.f - z * z));
			return Make<Vector3f>(Vector3f(ring * std::cos(phi), z, ring * std::sin(phi)));
		}
	};

	class MakeColorNode : public ScriptNodeBase
	{
		ScriptPinId myR, myG, myB, myA;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myR = In<float>(context, "R", 1.f);
			myG = In<float>(context, "G", 1.f);
			myB = In<float>(context, "B", 1.f);
			myA = In<float>(context, "A", 1.f);
			Out<Color>(context, "Color");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			return Make<Color>(Color{ Read<float>(context, myR), Read<float>(context, myG), Read<float>(context, myB), Read<float>(context, myA) });
		}
	};

	// ---------------------------------------------------------------- conversions and logic

	bool BoolXor(bool a, bool b) { return a != b; }
	bool BoolNand(bool a, bool b) { return !(a && b); }
	bool BoolNor(bool a, bool b) { return !(a || b); }
	int BoolToInt(bool v) { return v ? 1 : 0; }
	float BoolToFloat(bool v) { return v ? 1.f : 0.f; }
	bool IntToBool(int v) { return v != 0; }
	bool FloatToBool(float v) { return v != 0.f; }
}

void Ag::RegisterMathMoreNodes()
{
	using R = ScriptNodeTypeRegistry;

	// integers
	R::RegisterType<BinaryNode<int, int, IntAdd>>("Common/Math/int/add (int)", "A + B");
	R::RegisterType<BinaryNode<int, int, IntSubtract>>("Common/Math/int/subtract (int)", "A - B");
	R::RegisterType<BinaryNode<int, int, IntMultiply>>("Common/Math/int/multiply (int)", "A * B");
	R::RegisterType<BinaryNode<int, int, IntDivide>>("Common/Math/int/divide (int)", "A / B, rounded towards zero. 0 when B is 0");
	R::RegisterType<BinaryNode<int, int, IntModulo>>("Common/Math/int/modulo (int)", "The remainder of A / B");
	R::RegisterType<BinaryNode<int, int, IntMin>>("Common/Math/int/min (int)", "The smaller of A and B");
	R::RegisterType<BinaryNode<int, int, IntMax>>("Common/Math/int/max (int)", "The larger of A and B");
	R::RegisterType<UnaryNode<int, int, IntAbs>>("Common/Math/int/abs (int)", "The value without its sign");
	R::RegisterType<UnaryNode<int, int, IntNegate>>("Common/Math/int/negate (int)", "-Value");
	R::RegisterType<UnaryNode<int, int, IntSign>>("Common/Math/int/sign (int)", "-1, 0 or 1");
	R::RegisterType<UnaryNode<int, bool, IntIsEven>>("Common/Math/int/is even", "True when the number is even");
	R::RegisterType<TernaryNode<int, int, IntClamp>>("Common/Math/int/clamp (int)", "A limited to the range B..C");
	R::RegisterType<RandomIntNode>("Common/Math/int/random int", "A random whole number from Min to Max, both included");

	// comparisons
	R::RegisterType<BinaryNode<int, bool, IntEqual>>("Common/Compare/equal (int)", "A == B");
	R::RegisterType<BinaryNode<int, bool, IntNotEqual>>("Common/Compare/not equal (int)", "A != B");
	R::RegisterType<BinaryNode<int, bool, IntLess>>("Common/Compare/less (int)", "A < B");
	R::RegisterType<BinaryNode<int, bool, IntLessEqual>>("Common/Compare/less or equal (int)", "A <= B");
	R::RegisterType<BinaryNode<int, bool, IntGreater>>("Common/Compare/greater (int)", "A > B");
	R::RegisterType<BinaryNode<int, bool, IntGreaterEqual>>("Common/Compare/greater or equal (int)", "A >= B");
	R::RegisterType<BinaryNode<float, bool, FloatEqual>>("Common/Compare/equal (float)", "A == B exactly. Use Nearly Equal for calculated values");
	R::RegisterType<BinaryNode<float, bool, FloatNotEqual>>("Common/Compare/not equal (float)", "A != B");
	R::RegisterType<BinaryNode<float, bool, FloatLess>>("Common/Compare/less (float)", "A < B");
	R::RegisterType<BinaryNode<float, bool, FloatLessEqual>>("Common/Compare/less or equal (float)", "A <= B");
	R::RegisterType<BinaryNode<float, bool, FloatGreater>>("Common/Compare/greater (float)", "A > B");
	R::RegisterType<BinaryNode<float, bool, FloatGreaterEqual>>("Common/Compare/greater or equal (float)", "A >= B");
	R::RegisterType<NearlyEqualNode>("Common/Compare/nearly equal (float)", "True when A and B differ by no more than Tolerance");
	R::RegisterType<UnaryNode<float, bool, IsNearlyZero>>("Common/Compare/is nearly zero", "True when the value is within 0.0001 of zero");

	// floats
	R::RegisterType<BinaryNode<float, float, FloatAdd>>("Common/Math/float/add (float)", "A + B");
	R::RegisterType<BinaryNode<float, float, FloatSubtract>>("Common/Math/float/subtract (float)", "A - B");
	R::RegisterType<BinaryNode<float, float, FloatMultiply>>("Common/Math/float/multiply (float)", "A * B");
	R::RegisterType<BinaryNode<float, float, FloatDivide>>("Common/Math/float/divide (float)", "A / B. 0 when B is 0");
	R::RegisterType<BinaryNode<float, float, FloatPower>>("Common/Math/float/power", "A raised to the power B");
	R::RegisterType<UnaryNode<float, float, FloatSquare>>("Common/Math/float/square", "Value * Value");
	R::RegisterType<UnaryNode<float, float, FloatExp>>("Common/Math/float/exp", "e raised to the value");
	R::RegisterType<UnaryNode<float, float, FloatLog>>("Common/Math/float/log", "The natural logarithm");
	R::RegisterType<UnaryNode<float, float, FloatFloor>>("Common/Math/float/floor", "Rounds down");
	R::RegisterType<UnaryNode<float, float, FloatCeil>>("Common/Math/float/ceil", "Rounds up");
	R::RegisterType<UnaryNode<float, float, FloatRound>>("Common/Math/float/round", "Rounds to the nearest whole number");
	R::RegisterType<UnaryNode<float, float, FloatTruncate>>("Common/Math/float/truncate", "Drops the fraction, towards zero");
	R::RegisterType<UnaryNode<float, float, FloatFrac>>("Common/Math/float/frac", "The fraction part, 0 up to 1");
	R::RegisterType<UnaryNode<float, float, Saturate>>("Common/Math/float/saturate", "Limits the value to 0..1");
	R::RegisterType<UnaryNode<float, float, OneMinus>>("Common/Math/float/one minus", "1 - Value");
	R::RegisterType<UnaryNode<float, float, FloatTan>>("Common/Math/float/tan (degrees)", "The tangent of an angle in degrees");
	R::RegisterType<UnaryNode<float, float, FloatAsin>>("Common/Math/float/asin (degrees)", "The angle in degrees whose sine is the value");
	R::RegisterType<UnaryNode<float, float, FloatAcos>>("Common/Math/float/acos (degrees)", "The angle in degrees whose cosine is the value");
	R::RegisterType<UnaryNode<float, float, FloatAtan>>("Common/Math/float/atan (degrees)", "The angle in degrees whose tangent is the value");
	R::RegisterType<UnaryNode<float, float, DegreesToRadians>>("Common/Math/float/degrees to radians", "Converts an angle");
	R::RegisterType<UnaryNode<float, float, RadiansToDegrees>>("Common/Math/float/radians to degrees", "Converts an angle");
	R::RegisterType<TernaryNode<float, float, SmoothStep>>("Common/Math/float/smooth step", "0 below A, 1 above B, a smooth curve between. C is the value");
	R::RegisterType<TernaryNode<float, float, InverseLerp>>("Common/Math/float/inverse lerp", "Where C sits between A and B, as 0..1");
	R::RegisterType<TernaryNode<float, float, MoveTowards>>("Common/Math/float/move towards", "Moves A towards B by at most C, without overshooting");
	R::RegisterType<MapRangeNode>("Common/Math/float/map range", "Converts Value from one range to another");
	R::RegisterType<InterpToNode>("Common/Math/float/interp to", "Eases Current towards Target. Call every frame with Delta Time");
	R::RegisterType<LerpAngleNode>("Common/Math/float/lerp angle", "Blends two angles in degrees along the shortest way round");
	R::RegisterType<RandomFloatNode>("Common/Math/float/random float", "A random number from Min to Max");
	R::RegisterType<RandomBoolNode>("Common/Math/bool/random bool", "True with the given Chance, 0 to 1");

	// vectors
	R::RegisterType<MakeVectorNode>("Common/Math/vector3/make vector", "Builds a vector from X, Y and Z");
	R::RegisterType<BreakVectorNode>("Common/Math/vector3/break vector", "Splits a vector into X, Y and Z");
	R::RegisterType<BinaryNode<Vector3f, Vector3f, VecAdd>>("Common/Math/vector3/float3 + float3", "A + B");
	R::RegisterType<BinaryNode<Vector3f, Vector3f, VecSubtract>>("Common/Math/vector3/float3 - float3", "A - B");
	R::RegisterType<BinaryNode<Vector3f, Vector3f, VecMultiply>>("Common/Math/vector3/float3 * float3", "A * B, component by component");
	R::RegisterType<VectorDivideNode>("Common/Math/vector3/float3 / float", "Divides every component");
	R::RegisterType<UnaryNode<Vector3f, Vector3f, VecNegate>>("Common/Math/vector3/float3 negate", "-Vector");
	R::RegisterType<UnaryNode<Vector3f, Vector3f, VecAbs>>("Common/Math/vector3/float3 abs", "Absolute value of every component");
	R::RegisterType<UnaryNode<Vector3f, bool, VecIsZero>>("Common/Math/vector3/float3 is zero", "True when the vector has no length");
	R::RegisterType<BinaryNode<Vector3f, Vector3f, VecMin>>("Common/Math/vector3/float3 min", "The smaller of every component");
	R::RegisterType<BinaryNode<Vector3f, Vector3f, VecMax>>("Common/Math/vector3/float3 max", "The larger of every component");
	R::RegisterType<BinaryNode<Vector3f, float, VecDistance>>("Common/Math/vector3/float3 distance", "The distance between two points");
	R::RegisterType<BinaryNode<Vector3f, float, VecDistanceSquared>>("Common/Math/vector3/float3 distance squared", "The distance squared: cheaper to compare");
	R::RegisterType<TernaryNode<Vector3f, Vector3f, VecLerp>>("Common/Math/vector3/float3 lerp", "Blends A to B. The blend is the X of C");
	R::RegisterType<VectorInterpToNode>("Common/Math/vector3/float3 interp to", "Eases a point towards a target. Call every frame with Delta Time");
	R::RegisterType<RandomUnitVectorNode>("Common/Math/vector3/random direction", "A random direction of length 1");
	R::RegisterType<MakeColorNode>("Common/Math/color/make color", "Builds a colour from R, G, B and A, each 0 to 1");

	// logic and conversions
	R::RegisterType<BinaryNode<bool, bool, BoolXor>>("Common/Math/bool/xor", "True when exactly one of A and B is true");
	R::RegisterType<BinaryNode<bool, bool, BoolNand>>("Common/Math/bool/nand", "Not (A and B)");
	R::RegisterType<BinaryNode<bool, bool, BoolNor>>("Common/Math/bool/nor", "Not (A or B)");
	R::RegisterType<UnaryNode<bool, int, BoolToInt>>("Common/Math/cast/bool to int", "1 for true, 0 for false");
	R::RegisterType<UnaryNode<bool, float, BoolToFloat>>("Common/Math/cast/bool to float", "1 for true, 0 for false");
	R::RegisterType<UnaryNode<int, bool, IntToBool>>("Common/Math/cast/int to bool", "False for 0, true for anything else");
	R::RegisterType<UnaryNode<float, bool, FloatToBool>>("Common/Math/cast/float to bool", "False for 0, true for anything else");
}
