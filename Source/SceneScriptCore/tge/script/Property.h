#pragma once
#include <tge/stringRegistry/StringRegistry.h>

#include <span>

namespace Tga
{
	struct JsonData;

	template <typename T>
	void LoadFromJson(T& value, const JsonData& json);

	template <typename T>
	void WriteToJson(const T& value, JsonData& json);

	template <typename T>
	bool ShowImGuiEditor(T& value, const char* name, const char* description);

	struct PropertyTypeId
	{
		constexpr static unsigned int InvalidId = 0xFFFFFFFF;
		static const PropertyTypeId Invalid;

		unsigned int id = InvalidId;

		bool operator==(const PropertyTypeId& other) const { return this->id == other.id; }
		bool operator!=(const PropertyTypeId& other) const { return this->id != other.id; }
	};

	inline constexpr PropertyTypeId PropertyTypeId::Invalid = {};

	constexpr size_t PropertyBufferSize = 16;
	constexpr size_t PropertyBufferAlignment = 16;

	struct PropertyBuffer
	{
		alignas(PropertyBufferSize) unsigned char data[PropertyBufferAlignment];
	};

	void FinalizeTypeRegistry();

	class PropertyTypeBase
	{
		friend void FinalizeTypeRegistry();
		// todo: add flags for default move, copy, etc to avoid virtual function call
		PropertyTypeId myId = {};
		const char* myName;
		StringId myStringId; // not available during static intialization
	public:
		PropertyTypeBase(const char* name);
		PropertyTypeId GetTypeId() const { return myId; }
		StringId GetName() const;

		virtual void CreateDefault(PropertyBuffer& target) const = 0;
		virtual void Copy(const PropertyBuffer& source, PropertyBuffer& target) const = 0;
		virtual void Move(PropertyBuffer& source, PropertyBuffer& target) const = 0;
		virtual void Destruct(PropertyBuffer& buffer) const = 0;

		virtual void LoadFromJson(PropertyBuffer& buffer, const JsonData& data) const = 0;
		virtual void WriteToJson(const PropertyBuffer& buffer, JsonData& data) const = 0;

		virtual bool ShowImGuiEditor(PropertyBuffer& buffer, const char* name, const char* description) const = 0;
	};

	template <typename T>
	struct PropertyType : PropertyTypeBase
	{
	public:
		PropertyType(const char* name) : PropertyTypeBase(name) 
		{
			static_assert(sizeof(T) <= PropertyBufferSize);
		};
		void CreateDefault(PropertyBuffer& buffer) const override;
		void Copy(const PropertyBuffer& source, PropertyBuffer& target) const override;
		void Move(PropertyBuffer& source, PropertyBuffer& target) const override;
		void Destruct(PropertyBuffer& buffer) const override;

		void LoadFromJson(PropertyBuffer& buffer, const JsonData& data) const override;
		void WriteToJson(const PropertyBuffer& buffer, JsonData& data) const override;
		bool ShowImGuiEditor(PropertyBuffer& buffer, const char* name, const char* description) const override;
	};

	template <typename T>
	void PropertyType<T>::CreateDefault(PropertyBuffer& buffer) const
	{
		new (buffer.data)T{};
	}

	template <typename T>
	void PropertyType<T>::Copy(const PropertyBuffer& source, PropertyBuffer& target) const
	{
		new (target.data)T{ *reinterpret_cast<const T*>(source.data) };
	}

	template <typename T>
	void PropertyType<T>::Move(PropertyBuffer& source, PropertyBuffer& target) const
	{
		new (target.data)T{ std::move(*reinterpret_cast<const T*>(source.data)) };
	}

	template <typename T>
	void PropertyType<T>::Destruct(PropertyBuffer& buffer) const
	{
		reinterpret_cast<T*>(buffer.data)->~T();
	}
	
	template <typename T>
	void PropertyType<T>::LoadFromJson(PropertyBuffer& buffer, const JsonData& data) const
	{
		Tga::LoadFromJson(*reinterpret_cast<T*>(buffer.data), data);
	}

	template <typename T>
	void PropertyType<T>::WriteToJson(const PropertyBuffer& buffer, JsonData& data) const 
	{
		Tga::WriteToJson(*reinterpret_cast<const T*>(buffer.data), data);
	}

	template <typename T>
	bool PropertyType<T>::ShowImGuiEditor(PropertyBuffer& buffer, const char* name, const char* description) const
	{
		return Tga::ShowImGuiEditor(*reinterpret_cast<T*>(buffer.data), name, description);
	}

	template <typename T>
	struct PropertyTypeDefinition {};


#define DECLARE_PROPERTY_TYPE(T) template<> \
struct PropertyTypeDefinition<T> \
{ \
static PropertyType<T> Type;\
};

#define IMPLEMENT_PROPERTY_TYPE(T, NAME) \
PropertyType<T> PropertyTypeDefinition<T>::Type(NAME);

template<typename T>
const PropertyTypeBase* GetPropertyType()
{
	return &PropertyTypeDefinition<T>::Type;
}

class Property
{
public:
	Property() : myType(nullptr) {}
	Property(const Property&);
	Property(Property&&) noexcept;
	Property(const PropertyTypeBase* type);
	~Property();
	Property& operator=(const Property& other);
	Property& operator=(Property&& other);

	template<typename T, typename... Parameters>
	static Property Create(Parameters ...);

	static Property CreateFromJson(const PropertyTypeBase* type, const JsonData& value);
	static Property CreateFromJson(const JsonData& data);
	void WriteToJson(JsonData& data) const;
	void WriteToJsonWithoutType(JsonData& data) const;

	bool ShowImGuiEditor(const char* text = nullptr, const char* description = nullptr);

	template<typename T>
	bool IsOfType() const;

	bool IsOfType(PropertyTypeId type) const;

	template<typename T>
	T* Get();

	template<typename T>
	const T* Get() const;

	const PropertyTypeBase* GetType() const { return myType; }

	bool HasValue() const { return myType != nullptr; }

protected:
	PropertyBuffer myBuffer;
	const PropertyTypeBase* myType;
};

template<typename T>
bool Property::IsOfType() const
{
	return &PropertyTypeDefinition<T>::Type == myType;
}

template<typename T>
T* Property::Get()
{
	if (&PropertyTypeDefinition<T>::Type != myType)
		return nullptr;

	return reinterpret_cast<T*>(myBuffer.data);
}

template<typename T>
const T* Property::Get() const
{
	if (&PropertyTypeDefinition<T>::Type != myType)
		return nullptr;

	return reinterpret_cast<const T*>(myBuffer.data);
}

template<typename T, typename... Parameters>
static Property Property::Create(Parameters ... params)
{
	Property p;
	p.myType = &PropertyTypeDefinition<T>::Type;
	new (p.myBuffer.data)T(params...);

	return p;
}

template<typename T>
class TypedProperty : public Property
{
	TypedProperty();
	TypedProperty(const TypedProperty&);
	TypedProperty(TypedProperty&&) noexcept;
	~TypedProperty();

	T* Get();
	const T* Get() const;
};

template<typename T>
TypedProperty<T>::TypedProperty() : myType(nullptr)
{}

template<typename T>
TypedProperty<T>::TypedProperty(const TypedProperty& typedProperty) : myType(typedProperty.myType)
{
	if (myType)
	{
		*reinterpret_cast<T*>(myBuffer.data) = *reinterpret_cast<const T*>(typedProperty.myBuffer.data);
	}
}
template<typename T>
TypedProperty<T>::TypedProperty(TypedProperty&& typedProperty) noexcept : myType(typedProperty.myType)
{
	if (myType)
	{
		*reinterpret_cast<T*>(myBuffer.data) = std::move(*reinterpret_cast<const T*>(typedProperty.myBuffer.data));
	}
}
template<typename T>
TypedProperty<T>::~TypedProperty() 
{
	if (myType)
	{
		reinterpret_cast<T*>(myBuffer.data)->~T();
	}
}

template<typename T>
T* TypedProperty<T>::Get() 
{
	if (!myType)
		return nullptr;

	return reinterpret_cast<T*>(myBuffer.data);
}

template<typename T>
const T* TypedProperty<T>::Get() const 
{
	if (!myType)
		return nullptr;

	return reinterpret_cast<const T*>(myBuffer.data);
}

class PropertyTypeRegistry
{
public:
	static const PropertyTypeBase* GetPropertyType(StringId string);
	static std::span<StringId> GetAllPropertyNames();
};

}