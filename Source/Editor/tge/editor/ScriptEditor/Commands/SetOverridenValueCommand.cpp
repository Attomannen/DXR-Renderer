#include <tge/editor/ScriptEditor/Commands/SetOverridenValueCommand.h>

#include <tge/script/Script.h>

using namespace Tga;

void SetOverridenValueCommand::ExecuteImpl()
{
	ScriptPin pin = myScript.GetPin(myPinId);

	assert("New overriden value has different type than current value" && pin.defaultValue.GetType() == myNewData.GetType());

	myOldData = pin.overridenValue;
	pin.overridenValue = myNewData;

	myScript.SetPin(myPinId, pin);
}

void SetOverridenValueCommand::UndoImpl()
{
	ScriptPin pin = myScript.GetPin(myPinId);

	pin.overridenValue = myOldData;

	myScript.SetPin(myPinId, pin);
}

