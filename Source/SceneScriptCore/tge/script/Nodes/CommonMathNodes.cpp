#include <stdafx.h>
#include "CommonMathNodes.h"

#include <tge/script/ScriptNodeTypeRegistry.h>
#include <tge/stringRegistry/StringRegistry.h>
#include <tge/script/ScriptCommon.h>
#include <tge/script/ScriptNodeBase.h>
#include <tge/script/Contexts/ScriptUpdateContext.h>
#include <tge/script/BaseProperties.h>

using namespace Tga;


class FloatValueNode : public ScriptNodeBase
{
	ScriptPinId myValuePinId;

public:
	void Init(const ScriptCreationContext& context) override
	{
		ScriptPin outputPin = {};
		outputPin.type = ScriptLinkType::Property;
		outputPin.dataType = GetPropertyType<float>();
		outputPin.name = "Value"_tgaid;
		outputPin.node = context.GetNodeId();
		outputPin.role = ScriptPinRole::Output;
		context.FindOrCreatePin(outputPin);

		ScriptPin valuePin = {};
		valuePin.type = ScriptLinkType::Property;
		valuePin.dataType = GetPropertyType<float>();
		valuePin.role = ScriptPinRole::Input;
		valuePin.name = "Float"_tgaid;
		valuePin.node = context.GetNodeId();
		valuePin.defaultValue = Property::Create<float>(0.f);
		myValuePinId = context.FindOrCreatePin(valuePin);
	}

	Property ReadPin(Tga::ScriptExecutionContext& ctx, Tga::ScriptPinId) const override
	{
		return ctx.ReadInputPin(myValuePinId);
	}
};

class IntValueNode : public ScriptNodeBase
{
	ScriptPinId myValuePinId;

public:
	void Init(const ScriptCreationContext& context) override
	{
		ScriptPin outputPin = {};
		outputPin.type = ScriptLinkType::Property;
		outputPin.dataType = GetPropertyType<int>();
		outputPin.name = "Value"_tgaid;
		outputPin.node = context.GetNodeId();
		outputPin.role = ScriptPinRole::Output;
		context.FindOrCreatePin(outputPin);

		ScriptPin valuePin = {};
		valuePin.type = ScriptLinkType::Property;
		valuePin.dataType = GetPropertyType<int>();
		valuePin.role = ScriptPinRole::Input;
		valuePin.name = "Int"_tgaid;
		valuePin.node = context.GetNodeId();
		valuePin.defaultValue = Property::Create<int>(0);
		myValuePinId = context.FindOrCreatePin(valuePin);
	}

	Property ReadPin(Tga::ScriptExecutionContext& ctx, Tga::ScriptPinId) const override
	{
		return ctx.ReadInputPin(myValuePinId);
	}
};

template <typename T>
class Vec2ValueNode : public ScriptNodeBase
{
	ScriptPinId myXPinId;
	ScriptPinId myYPinId;

public:
	void Init(const ScriptCreationContext& context) override
	{
		ScriptPin outputPin = {};
		outputPin.type = ScriptLinkType::Property;
		outputPin.dataType = GetPropertyType<Vector2<T>>();
		outputPin.name = "Value"_tgaid;
		outputPin.node = context.GetNodeId();
		outputPin.role = ScriptPinRole::Output;
		context.FindOrCreatePin(outputPin);

		ScriptPin xPin = {};
		xPin.type = ScriptLinkType::Property;
		xPin.dataType = GetPropertyType<T>();
		xPin.role = ScriptPinRole::Input;
		xPin.name = "X"_tgaid;
		xPin.node = context.GetNodeId();
		xPin.defaultValue = Property::Create<T>(T{0});
		myXPinId = context.FindOrCreatePin(xPin);

		ScriptPin yPin = {};
		yPin.type = ScriptLinkType::Property;
		yPin.dataType = GetPropertyType<T>();
		yPin.role = ScriptPinRole::Input;
		yPin.name = "Y"_tgaid;
		yPin.node = context.GetNodeId();
		yPin.defaultValue = Property::Create<T>(T{0});
		myYPinId = context.FindOrCreatePin(yPin);
	}

	Property ReadPin(Tga::ScriptExecutionContext& ctx, Tga::ScriptPinId) const override
	{
		T x = *ctx.ReadInputPin(myXPinId).Get<T>();
		T y = *ctx.ReadInputPin(myYPinId).Get<T>();

		return Property::Create<Vector2<T>>(x, y);
	}
};

template <typename T>
class Vec3ValueNode : public ScriptNodeBase
{
	ScriptPinId myXPinId;
	ScriptPinId myYPinId;
	ScriptPinId myZPinId;

public:
	void Init(const ScriptCreationContext& context) override
	{
		ScriptPin outputPin = {};
		outputPin.type = ScriptLinkType::Property;
		outputPin.dataType = GetPropertyType<Vector3<T>>();
		outputPin.name = "Value"_tgaid;
		outputPin.node = context.GetNodeId();
		outputPin.role = ScriptPinRole::Output;
		context.FindOrCreatePin(outputPin);

		ScriptPin xPin = {};
		xPin.type = ScriptLinkType::Property;
		xPin.dataType = GetPropertyType<T>();
		xPin.role = ScriptPinRole::Input;
		xPin.name = "X"_tgaid;
		xPin.node = context.GetNodeId();
		xPin.defaultValue = Property::Create<T>(T{0});
		myXPinId = context.FindOrCreatePin(xPin);

		ScriptPin yPin = {};
		yPin.type = ScriptLinkType::Property;
		yPin.dataType = GetPropertyType<T>();
		yPin.role = ScriptPinRole::Input;
		yPin.name = "Y"_tgaid;
		yPin.node = context.GetNodeId();
		yPin.defaultValue = Property::Create<T>(T{0});
		myYPinId = context.FindOrCreatePin(yPin);

		ScriptPin zPin = {};
		zPin.type = ScriptLinkType::Property;
		zPin.dataType = GetPropertyType<T>();
		zPin.role = ScriptPinRole::Input;
		zPin.name = "Z"_tgaid;
		zPin.node = context.GetNodeId();
		zPin.defaultValue = Property::Create<T>(T{ 0 });
		myZPinId = context.FindOrCreatePin(zPin);
	}

	Tga::Property ReadPin(Tga::ScriptExecutionContext& ctx, Tga::ScriptPinId) const override
	{
		T x = *ctx.ReadInputPin(myXPinId).Get<T>();
		T y = *ctx.ReadInputPin(myYPinId).Get<T>();
		T z = *ctx.ReadInputPin(myZPinId).Get<T>();

		return Property::Create<Vector3<T>>(x, y, z);
	}
};

template <typename T>
class Vec4ValueNode : public ScriptNodeBase
{
	ScriptPinId myValuePinId;

public:
	void Init(const ScriptCreationContext& context) override
	{
		ScriptPin outputPin = {};
		outputPin.type = ScriptLinkType::Property;
		outputPin.dataType = GetPropertyType<Vector4<T>>();
		outputPin.name = "Value"_tgaid;
		outputPin.node = context.GetNodeId();
		outputPin.role = ScriptPinRole::Output;
		context.FindOrCreatePin(outputPin);

		ScriptPin valuePin = {};
		valuePin.type = ScriptLinkType::Property;
		valuePin.dataType = GetPropertyType<Vector4<T>>();
		valuePin.role = ScriptPinRole::Input;
		valuePin.name = "Float4"_tgaid;
		valuePin.node = context.GetNodeId();
		valuePin.defaultValue = Property::Create<Vector4<T>>(T{});
		myValuePinId = context.FindOrCreatePin(valuePin);
	}

	Tga::Property ReadPin(Tga::ScriptExecutionContext& ctx, Tga::ScriptPinId) const override
	{
		return ctx.ReadInputPin(myValuePinId);
	}
};

class ColorNode : public ScriptNodeBase
{
	ScriptPinId myValuePinId;

public:
	void Init(const ScriptCreationContext& context) override
	{
		ScriptPin outputPin = {};
		outputPin.type = ScriptLinkType::Property;
		outputPin.dataType = GetPropertyType<Color>();
		outputPin.name = "Value"_tgaid;
		outputPin.node = context.GetNodeId();
		outputPin.role = ScriptPinRole::Output;
		context.FindOrCreatePin(outputPin);

		ScriptPin valuePin = {};
		valuePin.type = ScriptLinkType::Property;
		valuePin.dataType = GetPropertyType<Color>();
		valuePin.role = ScriptPinRole::Input;
		valuePin.name = "Color"_tgaid;
		valuePin.node = context.GetNodeId();
		valuePin.defaultValue = Property::Create<Color>(0.f, 0.f, 0.f, 1.f);
		myValuePinId = context.FindOrCreatePin(valuePin);
	}

	Tga::Property ReadPin(Tga::ScriptExecutionContext& ctx, Tga::ScriptPinId) const override
	{
		return ctx.ReadInputPin(myValuePinId);
	}
};

template <typename T>
class AdditionNode : public ScriptNodeBase
{
	ScriptPinId myFirstValuePinId;
	ScriptPinId mySecondValuePinId;

public:
	void Init(const ScriptCreationContext& context) override
	{
		{
			ScriptPin outputPin = {};
			outputPin.type = ScriptLinkType::Property;
			outputPin.dataType = GetPropertyType<T>();
			outputPin.name = "Value"_tgaid;
			outputPin.node = context.GetNodeId();
			outputPin.role = ScriptPinRole::Output;
			context.FindOrCreatePin(outputPin);
		}

		{
			ScriptPin firstValuePin = {};
			firstValuePin.type = ScriptLinkType::Property;
			firstValuePin.dataType = GetPropertyType<T>();
			firstValuePin.role = ScriptPinRole::Input;
			firstValuePin.name = "First"_tgaid;
			firstValuePin.node = context.GetNodeId();
			firstValuePin.defaultValue = Property::Create<T>(T{ 0 });
			myFirstValuePinId = context.FindOrCreatePin(firstValuePin);
		}

		{
			ScriptPin secondValuePin = {};
			secondValuePin.type = ScriptLinkType::Property;
			secondValuePin.dataType = GetPropertyType<T>();
			secondValuePin.role = ScriptPinRole::Input;
			secondValuePin.name = "Second"_tgaid;
			secondValuePin.node = context.GetNodeId();
			secondValuePin.defaultValue = Property::Create<T>(T{ 0 });
			mySecondValuePinId = context.FindOrCreatePin(secondValuePin);
		}
	}

	Property ReadPin(Tga::ScriptExecutionContext& ctx, Tga::ScriptPinId) const override
	{
		T lhs = *ctx.ReadInputPin(myFirstValuePinId).Get<T>();
		T rhs = *ctx.ReadInputPin(mySecondValuePinId).Get<T>();

		return Property::Create<T>(lhs + rhs);
	}
};

template <typename T>
class SubtractNode : public ScriptNodeBase
{
	ScriptPinId myFirstValuePinId;
	ScriptPinId mySecondValuePinId;

public:
	void Init(const ScriptCreationContext& context) override
	{
		{
			ScriptPin outputPin = {};
			outputPin.type = ScriptLinkType::Property;
			outputPin.dataType = GetPropertyType<T>();
			outputPin.name = "Value"_tgaid;
			outputPin.node = context.GetNodeId();
			outputPin.role = ScriptPinRole::Output;
			context.FindOrCreatePin(outputPin);
		}

		{
			ScriptPin firstValuePin = {};
			firstValuePin.type = ScriptLinkType::Property;
			firstValuePin.dataType = GetPropertyType<T>();
			firstValuePin.role = ScriptPinRole::Input;
			firstValuePin.name = "First"_tgaid;
			firstValuePin.node = context.GetNodeId();
			firstValuePin.defaultValue = Property::Create<T>(T{ 0 });
			myFirstValuePinId = context.FindOrCreatePin(firstValuePin);
		}

		{
			ScriptPin secondValuePin = {};
			secondValuePin.type = ScriptLinkType::Property;
			secondValuePin.dataType = GetPropertyType<T>();
			secondValuePin.role = ScriptPinRole::Input;
			secondValuePin.name = "Second"_tgaid;
			secondValuePin.node = context.GetNodeId();
			secondValuePin.defaultValue = Property::Create<T>(T{ 0 });
			mySecondValuePinId = context.FindOrCreatePin(secondValuePin);
		}
	}

	Property ReadPin(Tga::ScriptExecutionContext& ctx, Tga::ScriptPinId) const override
	{
		T lhs = *ctx.ReadInputPin(myFirstValuePinId).Get<T>();
		T rhs = *ctx.ReadInputPin(mySecondValuePinId).Get<T>();

		return Property::Create<T>(lhs - rhs);
	}
};

template <typename T>
class MultiplyNode : public ScriptNodeBase
{
	ScriptPinId myFirstValuePinId;
	ScriptPinId mySecondValuePinId;

public:
	void Init(const ScriptCreationContext& context) override
	{
		ScriptPin outputPin = {};
		outputPin.type = ScriptLinkType::Property;
		outputPin.dataType = GetPropertyType<T>();
		outputPin.name = "Value"_tgaid;
		outputPin.node = context.GetNodeId();
		outputPin.role = ScriptPinRole::Output;
		context.FindOrCreatePin(outputPin);

		ScriptPin firstValuePin = {};
		firstValuePin.type = ScriptLinkType::Property;
		firstValuePin.dataType = GetPropertyType<T>();
		firstValuePin.role = ScriptPinRole::Input;
		firstValuePin.name = "First"_tgaid;
		firstValuePin.node = context.GetNodeId();
		firstValuePin.defaultValue = Property::Create<T>(T{ 0 });
		myFirstValuePinId = context.FindOrCreatePin(firstValuePin);

		ScriptPin secondValuePin = {};
		secondValuePin.type = ScriptLinkType::Property;
		secondValuePin.dataType = GetPropertyType<T>();
		secondValuePin.role = ScriptPinRole::Input;
		secondValuePin.name = "Second"_tgaid;
		secondValuePin.node = context.GetNodeId();
		secondValuePin.defaultValue = Property::Create<T>(T{0});
		mySecondValuePinId = context.FindOrCreatePin(secondValuePin);
	}

	Property ReadPin(Tga::ScriptExecutionContext& ctx, Tga::ScriptPinId) const override
	{
		T lhs = *ctx.ReadInputPin(myFirstValuePinId).Get<T>();
		T rhs = *ctx.ReadInputPin(mySecondValuePinId).Get<T>();

		return Property::Create<T>(lhs * rhs);
	}
};

template <typename T>
class DivideNode : public ScriptNodeBase
{
	ScriptPinId myFirstValuePinId;
	ScriptPinId mySecondValuePinId;

public:
	void Init(const ScriptCreationContext& context) override
	{
		ScriptPin outputPin = {};
		outputPin.type = ScriptLinkType::Property;
		outputPin.dataType = GetPropertyType<T>();
		outputPin.name = "Value"_tgaid;
		outputPin.node = context.GetNodeId();
		outputPin.role = ScriptPinRole::Output;
		context.FindOrCreatePin(outputPin);

		ScriptPin firstValuePin = {};
		firstValuePin.type = ScriptLinkType::Property;
		firstValuePin.dataType = GetPropertyType<T>();
		firstValuePin.role = ScriptPinRole::Input;
		firstValuePin.name = "First"_tgaid;
		firstValuePin.node = context.GetNodeId();
		firstValuePin.defaultValue = Property::Create<T>(T{});
		myFirstValuePinId = context.FindOrCreatePin(firstValuePin);

		ScriptPin secondValuePin = {};
		secondValuePin.type = ScriptLinkType::Property;
		secondValuePin.dataType = GetPropertyType<T>();
		secondValuePin.role = ScriptPinRole::Input;
		secondValuePin.name = "Second"_tgaid;
		secondValuePin.node = context.GetNodeId();
		secondValuePin.defaultValue = Property::Create<T>(T{});
		mySecondValuePinId = context.FindOrCreatePin(secondValuePin);
	}

	Property ReadPin(Tga::ScriptExecutionContext& ctx, Tga::ScriptPinId) const override
	{
		T lhs = *ctx.ReadInputPin(myFirstValuePinId).Get<T>();
		T rhs = *ctx.ReadInputPin(mySecondValuePinId).Get<T>();

		return Property::Create<T>((rhs == T{ 0 }) ? 0 : lhs / rhs);
	}
};

template <typename T>
class EqualityNode : public ScriptNodeBase
{
	ScriptPinId myFirstValuePinId;
	ScriptPinId mySecondValuePinId;

public:
	void Init(const ScriptCreationContext& context) override
	{
		ScriptPin outputPin = {};
		outputPin.type = ScriptLinkType::Property;
		outputPin.dataType = GetPropertyType<bool>();
		outputPin.name = "Value"_tgaid;
		outputPin.node = context.GetNodeId();
		outputPin.role = ScriptPinRole::Output;
		context.FindOrCreatePin(outputPin);

		ScriptPin firstValuePin = {};
		firstValuePin.type = ScriptLinkType::Property;
		firstValuePin.dataType = GetPropertyType<T>();
		firstValuePin.role = ScriptPinRole::Input;
		firstValuePin.name = "First"_tgaid;
		firstValuePin.node = context.GetNodeId();
		firstValuePin.defaultValue = Property::Create<T>(T{0});
		myFirstValuePinId = context.FindOrCreatePin(firstValuePin);

		ScriptPin secondValuePin = {};
		secondValuePin.type = ScriptLinkType::Property;
		secondValuePin.dataType = GetPropertyType<T>();
		secondValuePin.role = ScriptPinRole::Input;
		secondValuePin.name = "Second"_tgaid;
		secondValuePin.node = context.GetNodeId();
		secondValuePin.defaultValue = Property::Create<T>(T{0});
		mySecondValuePinId = context.FindOrCreatePin(secondValuePin);
	}

	Property ReadPin(Tga::ScriptExecutionContext& ctx, Tga::ScriptPinId) const override
	{
		T lhs = *ctx.ReadInputPin(myFirstValuePinId).Get<T>();
		T rhs = *ctx.ReadInputPin(mySecondValuePinId).Get<T>();

		return Property::Create<bool>(lhs == rhs);
	}
};

template <typename T>
class GreaterNode : public ScriptNodeBase
{
	ScriptPinId myFirstValuePinId;
	ScriptPinId mySecondValuePinId;

public:
	void Init(const ScriptCreationContext& context) override
	{
		ScriptPin outputPin = {};
		outputPin.type = ScriptLinkType::Property;
		outputPin.dataType = GetPropertyType<bool>();
		outputPin.name = "Value"_tgaid;
		outputPin.node = context.GetNodeId();
		outputPin.role = ScriptPinRole::Output;
		context.FindOrCreatePin(outputPin);

		ScriptPin firstValuePin = {};
		firstValuePin.type = ScriptLinkType::Property;
		firstValuePin.dataType = GetPropertyType<T>();
		firstValuePin.role = ScriptPinRole::Input;
		firstValuePin.name = "First"_tgaid;
		firstValuePin.node = context.GetNodeId();
		firstValuePin.defaultValue = Property::Create<T>(T{ 0 });
		myFirstValuePinId = context.FindOrCreatePin(firstValuePin);

		ScriptPin secondValuePin = {};
		secondValuePin.type = ScriptLinkType::Property;
		secondValuePin.dataType = GetPropertyType<T>();
		secondValuePin.role = ScriptPinRole::Input;
		secondValuePin.name = "Second"_tgaid;
		secondValuePin.node = context.GetNodeId();
		secondValuePin.defaultValue = Property::Create<T>(T{0});
		mySecondValuePinId = context.FindOrCreatePin(secondValuePin);
	}

	Property ReadPin(Tga::ScriptExecutionContext& ctx, Tga::ScriptPinId) const override
	{
		T lhs = *ctx.ReadInputPin(myFirstValuePinId).Get<T>();
		T rhs = *ctx.ReadInputPin(mySecondValuePinId).Get<T>();

		return Property::Create<bool>(lhs > rhs);
	}
};

template <typename T>
class GreaterEqualNode : public ScriptNodeBase
{
	ScriptPinId myFirstValuePinId;
	ScriptPinId mySecondValuePinId;

public:
	void Init(const ScriptCreationContext& context) override
	{
		ScriptPin outputPin = {};
		outputPin.type = ScriptLinkType::Property;
		outputPin.dataType = GetPropertyType<bool>();
		outputPin.name = "Value"_tgaid;
		outputPin.node = context.GetNodeId();
		outputPin.role = ScriptPinRole::Output;
		context.FindOrCreatePin(outputPin);

		ScriptPin firstValuePin = {};
		firstValuePin.type = ScriptLinkType::Property;
		firstValuePin.dataType = GetPropertyType<T>();
		firstValuePin.role = ScriptPinRole::Input;
		firstValuePin.name = "First"_tgaid;
		firstValuePin.node = context.GetNodeId();
		firstValuePin.defaultValue = Property::Create<T>(T{ 0 });
		myFirstValuePinId = context.FindOrCreatePin(firstValuePin);

		ScriptPin secondValuePin = {};
		secondValuePin.type = ScriptLinkType::Property;
		secondValuePin.dataType = GetPropertyType<T>();
		secondValuePin.role = ScriptPinRole::Input;
		secondValuePin.name = "Second"_tgaid;
		secondValuePin.node = context.GetNodeId();
		secondValuePin.defaultValue = Property::Create<T>(T{0});
		mySecondValuePinId = context.FindOrCreatePin(secondValuePin);
	}

	Property ReadPin(Tga::ScriptExecutionContext& ctx, Tga::ScriptPinId) const override
	{
		T lhs = *ctx.ReadInputPin(myFirstValuePinId).Get<T>();
		T rhs = *ctx.ReadInputPin(mySecondValuePinId).Get<T>();

		return Property::Create<bool>(lhs >= rhs);
	}
};

template <typename T>
class LessNode : public ScriptNodeBase
{
	ScriptPinId myFirstValuePinId;
	ScriptPinId mySecondValuePinId;

public:
	void Init(const ScriptCreationContext& context) override
	{
		ScriptPin outputPin = {};
		outputPin.type = ScriptLinkType::Property;
		outputPin.dataType = GetPropertyType<bool>();
		outputPin.name = "Value"_tgaid;
		outputPin.node = context.GetNodeId();
		outputPin.role = ScriptPinRole::Output;
		context.FindOrCreatePin(outputPin);

		ScriptPin firstValuePin = {};
		firstValuePin.type = ScriptLinkType::Property;
		firstValuePin.dataType = GetPropertyType<T>();
		firstValuePin.role = ScriptPinRole::Input;
		firstValuePin.name = "First"_tgaid;
		firstValuePin.node = context.GetNodeId();
		firstValuePin.defaultValue = Property::Create<T>(T{0});
		myFirstValuePinId = context.FindOrCreatePin(firstValuePin);

		ScriptPin secondValuePin = {};
		secondValuePin.type = ScriptLinkType::Property;
		secondValuePin.dataType = GetPropertyType<T>();
		secondValuePin.role = ScriptPinRole::Input;
		secondValuePin.name = "Second"_tgaid;
		secondValuePin.node = context.GetNodeId();
		secondValuePin.defaultValue = Property::Create<T>(T{0});
		mySecondValuePinId = context.FindOrCreatePin(secondValuePin);
	}

	Property ReadPin(Tga::ScriptExecutionContext& ctx, Tga::ScriptPinId) const override
	{
		T lhs = *ctx.ReadInputPin(myFirstValuePinId).Get<T>();
		T rhs = *ctx.ReadInputPin(mySecondValuePinId).Get<T>();

		return Property::Create<bool>(lhs < rhs);
	}
};

template <typename T>
class LessEqualNode : public ScriptNodeBase
{
	ScriptPinId myFirstValuePinId;
	ScriptPinId mySecondValuePinId;

public:
	void Init(const ScriptCreationContext& context) override
	{
		ScriptPin outputPin = {};
		outputPin.type = ScriptLinkType::Property;
		outputPin.dataType = GetPropertyType<bool>();
		outputPin.name = "Value"_tgaid;
		outputPin.node = context.GetNodeId();
		outputPin.role = ScriptPinRole::Output;
		context.FindOrCreatePin(outputPin);

		ScriptPin firstValuePin = {};
		firstValuePin.type = ScriptLinkType::Property;
		firstValuePin.dataType = GetPropertyType<T>();
		firstValuePin.role = ScriptPinRole::Input;
		firstValuePin.name = "First"_tgaid;
		firstValuePin.node = context.GetNodeId();
		firstValuePin.defaultValue = Property::Create<T>(T{0});
		myFirstValuePinId = context.FindOrCreatePin(firstValuePin);

		ScriptPin secondValuePin = {};
		secondValuePin.type = ScriptLinkType::Property;
		secondValuePin.dataType = GetPropertyType<T>();
		secondValuePin.role = ScriptPinRole::Input;
		secondValuePin.name = "Second"_tgaid;
		secondValuePin.node = context.GetNodeId();
		secondValuePin.defaultValue = Property::Create<T>(T{0});
		mySecondValuePinId = context.FindOrCreatePin(secondValuePin);
	}

	Property ReadPin(Tga::ScriptExecutionContext& ctx, Tga::ScriptPinId) const override
	{
		T lhs = *ctx.ReadInputPin(myFirstValuePinId).Get<T>();
		T rhs = *ctx.ReadInputPin(mySecondValuePinId).Get<T>();

		return Property::Create<bool>(lhs <= rhs);
	}
};

template <typename T>
class NotEqualNode : public ScriptNodeBase
{
	ScriptPinId myFirstValuePinId;
	ScriptPinId mySecondValuePinId;

public:
	void Init(const ScriptCreationContext& context) override
	{
		ScriptPin outputPin = {};
		outputPin.type = ScriptLinkType::Property;
		outputPin.dataType = GetPropertyType<bool>();
		outputPin.name = "Value"_tgaid;
		outputPin.node = context.GetNodeId();
		outputPin.role = ScriptPinRole::Output;
		context.FindOrCreatePin(outputPin);

		ScriptPin firstValuePin = {};
		firstValuePin.type = ScriptLinkType::Property;
		firstValuePin.dataType = GetPropertyType<T>();
		firstValuePin.role = ScriptPinRole::Input;
		firstValuePin.name = "First"_tgaid;
		firstValuePin.node = context.GetNodeId();
		firstValuePin.defaultValue = Property::Create<T>(T{0});
		myFirstValuePinId = context.FindOrCreatePin(firstValuePin);

		ScriptPin secondValuePin = {};
		secondValuePin.type = ScriptLinkType::Property;
		secondValuePin.dataType = GetPropertyType<T>();
		secondValuePin.role = ScriptPinRole::Input;
		secondValuePin.name = "Second"_tgaid;
		secondValuePin.node = context.GetNodeId();
		secondValuePin.defaultValue = Property::Create<T>(T{0});
		mySecondValuePinId = context.FindOrCreatePin(secondValuePin);
	}

	Property ReadPin(Tga::ScriptExecutionContext& ctx, Tga::ScriptPinId) const override
	{
		T lhs = *ctx.ReadInputPin(myFirstValuePinId).Get<T>();
		T rhs = *ctx.ReadInputPin(mySecondValuePinId).Get<T>();

		return Property::Create<bool>(lhs != rhs);
	}
};

template <typename T>
class Vec2Split : public ScriptNodeBase
{
	ScriptPinId myVec2InPinId;
	ScriptPinId myXOutPinId;
	ScriptPinId myYOutPinId;

public:
	void Init(const ScriptCreationContext& context) override
	{
		ScriptPin vec2InPin = {};
		vec2InPin.type = ScriptLinkType::Property;
		vec2InPin.dataType = GetPropertyType<Vector2<T>>();
		vec2InPin.role = ScriptPinRole::Input;
		vec2InPin.name = "Vec2"_tgaid;
		vec2InPin.node = context.GetNodeId();
		vec2InPin.defaultValue = Property::Create<Vector2<T>>(T{0});
		myVec2InPinId = context.FindOrCreatePin(vec2InPin);

		ScriptPin outputXPin = {};
		outputXPin.type = ScriptLinkType::Property;
		outputXPin.dataType = GetPropertyType<T>();
		outputXPin.name = "X"_tgaid;
		outputXPin.node = context.GetNodeId();
		outputXPin.role = ScriptPinRole::Output;
		myXOutPinId = context.FindOrCreatePin(outputXPin);

		ScriptPin outputYPin = {};
		outputYPin.type = ScriptLinkType::Property;
		outputYPin.dataType = GetPropertyType<T>();
		outputYPin.name = "Y"_tgaid;
		outputYPin.node = context.GetNodeId();
		outputYPin.role = ScriptPinRole::Output;
		myYOutPinId = context.FindOrCreatePin(outputYPin);
	}

	Property ReadPin(Tga::ScriptExecutionContext& ctx, Tga::ScriptPinId pin) const override
	{
		Vector2<T> output = *ctx.ReadInputPin(myVec2InPinId).Get<Vector2<T>>();

		if (pin.id == myXOutPinId.id)
		{
			return Property::Create<T>(output.x);
		}
		else
		{
			return Property::Create<T>(output.y);
		}
	}
};

template <typename T>
class Vec3Split : public ScriptNodeBase
{
	ScriptPinId myVec3InPinId;
	ScriptPinId myXOutPinId;
	ScriptPinId myYOutPinId;
	ScriptPinId myZOutPinId;

public:
	void Init(const ScriptCreationContext& context) override
	{
		ScriptPin vec3InPin = {};
		vec3InPin.type = ScriptLinkType::Property;
		vec3InPin.dataType = GetPropertyType<Vector3<T>>();
		vec3InPin.role = ScriptPinRole::Input;
		vec3InPin.name = "Vec3"_tgaid;
		vec3InPin.node = context.GetNodeId();
		vec3InPin.defaultValue = Property::Create<Vector3<T>>(T{0});
		myVec3InPinId = context.FindOrCreatePin(vec3InPin);

		ScriptPin outputXPin = {};
		outputXPin.type = ScriptLinkType::Property;
		outputXPin.dataType = GetPropertyType<T>();
		outputXPin.name = "X"_tgaid;
		outputXPin.node = context.GetNodeId();
		outputXPin.role = ScriptPinRole::Output;
		myXOutPinId = context.FindOrCreatePin(outputXPin);

		ScriptPin outputYPin = {};
		outputYPin.type = ScriptLinkType::Property;
		outputYPin.dataType = GetPropertyType<T>();
		outputYPin.name = "Y"_tgaid;
		outputYPin.node = context.GetNodeId();
		outputYPin.role = ScriptPinRole::Output;
		myYOutPinId = context.FindOrCreatePin(outputYPin);

		ScriptPin outputZPin = {};
		outputZPin.type = ScriptLinkType::Property;
		outputZPin.dataType = GetPropertyType<T>();
		outputZPin.name = "Z"_tgaid;
		outputZPin.node = context.GetNodeId();
		outputZPin.role = ScriptPinRole::Output;
		myZOutPinId = context.FindOrCreatePin(outputZPin);
	}

	Property ReadPin(Tga::ScriptExecutionContext& ctx, Tga::ScriptPinId pin) const override
	{
		Vector3<T> output = *ctx.ReadInputPin(myVec3InPinId).Get<Vector3<T>>();

		if (pin.id == myXOutPinId.id)
		{
			return Property::Create<T>(output.x);
		}
		else if(pin.id == myYOutPinId.id)
		{
			return Property::Create<T>(output.y);
		}
		else
		{
			return Property::Create<T>(output.z);
		}
	}
};

template <typename T>
class Vec4Split : public ScriptNodeBase
{
	ScriptPinId myVec4InPinId;
	ScriptPinId myXOutPinId;
	ScriptPinId myYOutPinId;
	ScriptPinId myZOutPinId;
	ScriptPinId myWOutPinId;

public:
	void Init(const ScriptCreationContext& context) override
	{
		ScriptPin vec4InPin = {};
		vec4InPin.type = ScriptLinkType::Property;
		vec4InPin.dataType = GetPropertyType<Vector4<T>>();
		vec4InPin.role = ScriptPinRole::Input;
		vec4InPin.name = "Vec4"_tgaid;
		vec4InPin.node = context.GetNodeId();
		vec4InPin.defaultValue = Property::Create<Vector4<T>>(T{0});
		myVec4InPinId = context.FindOrCreatePin(vec4InPin);

		ScriptPin outputXPin = {};
		outputXPin.type = ScriptLinkType::Property;
		outputXPin.dataType = GetPropertyType<T>();
		outputXPin.name = "X"_tgaid;
		outputXPin.node = context.GetNodeId();
		outputXPin.role = ScriptPinRole::Output;
		myXOutPinId = context.FindOrCreatePin(outputXPin);

		ScriptPin outputYPin = {};
		outputYPin.type = ScriptLinkType::Property;
		outputYPin.dataType = GetPropertyType<T>();
		outputYPin.name = "Y"_tgaid;
		outputYPin.node = context.GetNodeId();
		outputYPin.role = ScriptPinRole::Output;
		myYOutPinId = context.FindOrCreatePin(outputYPin);

		ScriptPin outputZPin = {};
		outputZPin.type = ScriptLinkType::Property;
		outputZPin.dataType = GetPropertyType<T>();
		outputZPin.name = "Z"_tgaid;
		outputZPin.node = context.GetNodeId();
		outputZPin.role = ScriptPinRole::Output;
		myZOutPinId = context.FindOrCreatePin(outputZPin);

		ScriptPin outputWPin = {};
		outputWPin.type = ScriptLinkType::Property;
		outputWPin.dataType = GetPropertyType<T>();
		outputWPin.name = "W"_tgaid;
		outputWPin.node = context.GetNodeId();
		outputWPin.role = ScriptPinRole::Output;
		myWOutPinId = context.FindOrCreatePin(outputWPin);
	}

	Property ReadPin(Tga::ScriptExecutionContext& ctx, Tga::ScriptPinId pin) const override
	{
		Vector4<T> output = *ctx.ReadInputPin(myVec4InPinId).Get<Vector4<T>>();

		if (pin.id == myXOutPinId.id)
		{
			return Property::Create<T>(output.x);
		}
		else if(pin.id == myYOutPinId.id)
		{
			return Property::Create<T>(output.y);
		}
		else if(pin.id == myZOutPinId.id)
		{
			return Property::Create<T>(output.z);
		}
		else
		{
			return Property::Create<T>(output.w);
		}
	}
};

template <typename T_IN, typename T_OUT>
class StaticCastNode : public ScriptNodeBase
{
	ScriptPinId myInputPinId;

public:
	void Init(const ScriptCreationContext& context) override
	{
		ScriptPin outputPin = {};
		outputPin.type = ScriptLinkType::Property;
		outputPin.dataType = GetPropertyType<T_OUT>();
		outputPin.name = "Out"_tgaid;
		outputPin.node = context.GetNodeId();
		outputPin.role = ScriptPinRole::Output;
		context.FindOrCreatePin(outputPin);

		ScriptPin inputPin = {};
		inputPin.type = ScriptLinkType::Property;
		inputPin.dataType = GetPropertyType<T_IN>();
		inputPin.role = ScriptPinRole::Input;
		inputPin.name = "Input"_tgaid;
		inputPin.node = context.GetNodeId();
		inputPin.defaultValue = Property::Create<T_IN>(T_IN{});
		myInputPinId = context.FindOrCreatePin(inputPin);
	}

	Property ReadPin(Tga::ScriptExecutionContext& ctx, Tga::ScriptPinId) const override
	{
		T_IN in = *ctx.ReadInputPin(myInputPinId).Get<T_IN>();

		return Property::Create<T_OUT>(static_cast<T_OUT>(in));
	}
};


void Tga::RegisterCommonMathNodes()
{
	// Float value nodes and operations
	{
		ScriptNodeTypeRegistry::RegisterType<FloatValueNode>(			"Common/Math/float/float value",	"A float value");
		ScriptNodeTypeRegistry::RegisterType<AdditionNode<float>>(		"Common/Math/float/float +",		"Add two float values");
		ScriptNodeTypeRegistry::RegisterType<SubtractNode<float>>(		"Common/Math/float/float -",		"Subtract a float value from another");
		ScriptNodeTypeRegistry::RegisterType<MultiplyNode<float>>(		"Common/Math/float/float *",		"Multiply two float values");
		ScriptNodeTypeRegistry::RegisterType<DivideNode<float>>(		"Common/Math/float/float /",		"Divide a float by another");
		ScriptNodeTypeRegistry::RegisterType<EqualityNode<float>>(		"Common/Math/float/float ==",		"Check if a float is equal to another");
		ScriptNodeTypeRegistry::RegisterType<GreaterNode<float>>(		"Common/Math/float/float >",		"Check if a float is greater than another");
		ScriptNodeTypeRegistry::RegisterType<GreaterEqualNode<float>>(	"Common/Math/float/float >=",		"Check if a float is greater or equal to another");
		ScriptNodeTypeRegistry::RegisterType<LessNode<float>>(			"Common/Math/float/float <",		"Check if a float is less than another");
		ScriptNodeTypeRegistry::RegisterType<LessEqualNode<float>>(		"Common/Math/float/float <=",		"Check if a float is less than or equal to another");
		ScriptNodeTypeRegistry::RegisterType<NotEqualNode<float>>(		"Common/Math/float/float !=",		"Check if a float is not equal to another");
	}
	// Int value nodes and operations
	{
		ScriptNodeTypeRegistry::RegisterType<IntValueNode>(				"Common/Math/int/int value",		"An int value");
		ScriptNodeTypeRegistry::RegisterType<AdditionNode<int>>(		"Common/Math/int/int +",			"Add two int values");
		ScriptNodeTypeRegistry::RegisterType<SubtractNode<int>>(		"Common/Math/int/int -",			"Subtract an int value from another");
		ScriptNodeTypeRegistry::RegisterType<MultiplyNode<int>>(		"Common/Math/int/int *",			"Multiply two int values");
		ScriptNodeTypeRegistry::RegisterType<DivideNode<int>>(			"Common/Math/int/int /",			"Divide an int by another");
		ScriptNodeTypeRegistry::RegisterType<EqualityNode<int>>(		"Common/Math/int/int ==",			"Check if an int is equal to another");
		ScriptNodeTypeRegistry::RegisterType<GreaterNode<int>>(			"Common/Math/int/int >",			"Check if an int is greater than another");
		ScriptNodeTypeRegistry::RegisterType<GreaterEqualNode<int>>(	"Common/Math/int/int >=",			"Check if an int is greater or equal to another");
		ScriptNodeTypeRegistry::RegisterType<LessNode<int>>(			"Common/Math/int/int <",			"Check if an int is less than another");
		ScriptNodeTypeRegistry::RegisterType<LessEqualNode<int>>(		"Common/Math/int/int <=",			"Check if an int is less than or equal to another");
		ScriptNodeTypeRegistry::RegisterType<NotEqualNode<int>>(		"Common/Math/int/int !=",			"Check if an int is not equal to another");
	}
	// Vector2 float nodes and operations
	{
		ScriptNodeTypeRegistry::RegisterType<Vec2ValueNode<float>>(		"Common/Math/vector2/float2",		"A two component float vector");
		ScriptNodeTypeRegistry::RegisterType<AdditionNode<Vector2f>>(	"Common/Math/vector2/float2 +",		"Add two vector2f producing a vector2f");
		ScriptNodeTypeRegistry::RegisterType<SubtractNode<Vector2f>>(	"Common/Math/vector2/float2 -",		"Subtract a vector2f from another, producing a vector2f");
		ScriptNodeTypeRegistry::RegisterType<MultiplyNode<Vector2f>>(	"Common/Math/vector2/float2 *",		"Multiply a vector2f component-wise by another, producing a scaled vector2f");
		ScriptNodeTypeRegistry::RegisterType<EqualityNode<Vector2f>>(	"Common/Math/vector2/float2 ==",	"Check if a vector2f is equal to another");
		ScriptNodeTypeRegistry::RegisterType<NotEqualNode<Vector2f>>(	"Common/Math/vector2/float2 !=",	"Check if a vector2f is not equal to another");
		ScriptNodeTypeRegistry::RegisterType<Vec2Split<float>>(			"Common/Math/vector2/float2 split", "Splits a vector2 into it's components");
	}
	// Vector3 float nodes and operations
	{
		ScriptNodeTypeRegistry::RegisterType<Vec3ValueNode<float>>(		"Common/Math/vector3/float3",		"A three component float vector");
		ScriptNodeTypeRegistry::RegisterType<AdditionNode<Vector3f>>(	"Common/Math/vector3/float3 +",		"Add three vector3f producing a vector3f");
		ScriptNodeTypeRegistry::RegisterType<SubtractNode<Vector3f>>(	"Common/Math/vector3/float3 -",		"Subtract a vector3f from another, producing a vector3f");
		ScriptNodeTypeRegistry::RegisterType<MultiplyNode<Vector3f>>(	"Common/Math/vector3/float3 *",		"Multiply a vector3f component-wise by another, producing a scaled vector3f");
		ScriptNodeTypeRegistry::RegisterType<EqualityNode<Vector3f>>(	"Common/Math/vector3/float3 ==",	"Check if a vector3f is equal to another");
		ScriptNodeTypeRegistry::RegisterType<NotEqualNode<Vector3f>>(	"Common/Math/vector3/float3 !=",	"Check if a vector3f is not equal to another");
		ScriptNodeTypeRegistry::RegisterType<Vec3Split<float>>(			"Common/Math/vector3/float3 split", "Splits a vector3 into it's components");
	}
	// Vector4 float nodes and operations
	{
		ScriptNodeTypeRegistry::RegisterType<Vec4ValueNode<float>>(		"Common/Math/vector4/float4",		"A four component float vector");
		ScriptNodeTypeRegistry::RegisterType<AdditionNode<Vector4f>>(	"Common/Math/vector4/float4 +",		"Add four vector4f producing a vector4f");
		ScriptNodeTypeRegistry::RegisterType<SubtractNode<Vector4f>>(	"Common/Math/vector4/float4 -",		"Subtract a vector4f from another, producing a vector4f");
		//ScriptNodeTypeRegistry::RegisterType<MultiplyNode<Vector4f>>(	"Common/Math/vector4/float4 *",		"Multiply a vector4f component-wise by another, producing a scaled vector4f");
		ScriptNodeTypeRegistry::RegisterType<EqualityNode<Vector4f>>(	"Common/Math/vector4/float4 ==",	"Check if a vector4f is equal to another");
		ScriptNodeTypeRegistry::RegisterType<NotEqualNode<Vector4f>>(	"Common/Math/vector4/float4 !=",	"Check if a vector4f is not equal to another");
		ScriptNodeTypeRegistry::RegisterType<Vec4Split<float>>(			"Common/Math/vector4/float4 split", "Splits a vector4 into it's components");
	}
	// Type casts
	{
		ScriptNodeTypeRegistry::RegisterType<StaticCastNode<float, int>>("Common/Math/cast/float to int", "Convert float to int");
		ScriptNodeTypeRegistry::RegisterType<StaticCastNode<int, float>>("Common/Math/cast/int to float", "Convert int to float");
	}

	ScriptNodeTypeRegistry::RegisterType<ColorNode>("Common/Color", "A color node");
}
