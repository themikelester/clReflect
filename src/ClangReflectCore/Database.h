
#pragma once


#include <string>
#include <vector>
#include <map>
#include <cassert>


namespace crdb
{
	//
	// Some handy types
	//
	typedef unsigned char		u8;
	typedef unsigned short		u16;
	typedef unsigned int		u32;


	//
	// Hashes the full string int a 32-bit value
	//
	u32 HashNameString(const char* name_string);


	u32 MixHashes(u32 a, u32 b);


	//
	// A convenient way to quickly share and map names within the database
	//
	typedef std::map<u32, std::string> NameMap;
	typedef NameMap::const_iterator Name;


	//
	// Base class for all types of C++ primitives that are reflected
	//
	struct Primitive
	{
		enum Kind
		{
			KIND_NAMESPACE,
			KIND_TYPE,
			KIND_CLASS,
			KIND_ENUM,
			KIND_ENUM_CONSTANT,
			KIND_FUNCTION,
			KIND_FIELD
		};

		Primitive(Kind k) : kind(k) { }
		Primitive(Kind k, Name n, Name p) : kind(k), name(n), parent(p) { }

		Kind kind;
		Name name;

		// Parent scope primitive
		Name parent;
	};

	
	struct Namespace : public Primitive
	{
		Namespace() : Primitive(Primitive::KIND_NAMESPACE) { }
		Namespace(Name n, Name p) : Primitive(Primitive::KIND_NAMESPACE, n, p) { }
	};


	//
	// A basic built-in type that classes/structs can also inherit from
	//
	struct Type : public Primitive
	{
		Type() : Primitive(Primitive::KIND_TYPE) { }
		Type(Name n, Name p) : Primitive(Primitive::KIND_TYPE, n, p) { }
		Type(Kind k) : Primitive(k) { }
		Type(Kind k, Name n, Name p) : Primitive(k, n, p) { }
	};


	//
	// Description of a C++ struct or class with containing fields, functions, classes, etc.
	// Only one base class is supported until it becomes necessary to do otherwise.
	//
	struct Class : public Type
	{
		Class() : Type(Primitive::KIND_CLASS) { }
		Class(Name n, Name p, Name b, u32 s) : Type(Primitive::KIND_CLASS, n, p), base_class(b), size(s) { }

		// Single base class
		Name base_class;

		// Total size of the class, including alignment
		u32 size;
	};


	//
	// An enumeration of name/value constant pairs
	//
	struct Enum : public Type
	{
		Enum() : Type(Primitive::KIND_ENUM) { }
		Enum(Name n, Name p) : Type(Primitive::KIND_ENUM, n, p) { }
	};


	//
	// A name/value pair for enumeration constants
	//
	struct EnumConstant : public Primitive
	{
		EnumConstant() : Primitive(Primitive::KIND_ENUM_CONSTANT) { }
		EnumConstant(Name n, Name p, __int64 v) : Primitive(Primitive::KIND_ENUM_CONSTANT, n, p), value(v) { }

		bool operator == (const EnumConstant& rhs) const
		{
			return
				name == rhs.name &&
				parent == rhs.parent &&
				value == rhs.value;
		}

		// Enumeration constants can have values that are signed/unsigned and of arbitrary width.
		// For now I'm just assuming they're 32-bit signed.
		__int64 value;
	};


	//
	// A function or class method with a list of parameters and a return value.
	//
	struct Function : public Primitive
	{
		Function() : Primitive(Primitive::KIND_FUNCTION), unique_id(0) { }
		Function(Name n, Name p, u32 uid) : Primitive(Primitive::KIND_FUNCTION, n, p), unique_id(uid) { }

		bool operator == (const Function& rhs) const
		{
			return
				name == rhs.name &&
				parent == rhs.parent &&
				unique_id == rhs.unique_id;
		}

		u32 unique_id;
	};


	//
	// Can be either a class/struct field or a function parameter
	//
	struct Field : public Primitive
	{
		enum Modifier
		{
			MODIFIER_VALUE,
			MODIFIER_POINTER,
			MODIFIER_REFERENCE
		};

		Field() : Primitive(Primitive::KIND_FIELD), modifier(MODIFIER_VALUE), is_const(false), offset(-1), parent_unique_id(0) { }
		Field(Name n, Name p, Name t, Modifier pass, bool c, int o, u32 uid = 0) : Primitive(Primitive::KIND_FIELD, n, p), type(t), modifier(pass), is_const(c), offset(o), parent_unique_id(uid) { }

		bool operator == (const Field& rhs) const
		{
			return
				name == rhs.name &&
				parent == rhs.parent &&
				type == rhs.type &&
				modifier == rhs.modifier &&
				is_const == rhs.is_const &&
				offset == rhs.offset &&
				parent_unique_id == rhs.parent_unique_id;
		}

		Name type;
		Modifier modifier;
		bool is_const;

		// Index of the field parameter within its parent function or byte offset within its parent class
		int offset;

		// If this is set then the field is a function parameter
		u32 parent_unique_id;

		// TODO: arrays
		// TODO: bit fields
	};


	//
	// Primitive stores allow multiple primitives of the same type to be stored and
	// quickly looked up, allowing symbol overloading.
	//
	template <typename TYPE>
	struct PrimitiveStore : public std::multimap<u32, TYPE>
	{
		typedef std::pair<iterator, iterator> range;
		typedef std::pair<const_iterator, const_iterator> const_range;
	};


	class Database
	{
	public:
		Database();

		void AddBaseTypePrimitives();

		Name GetNoName() const;
		Name GetName(const char* text);
		Name GetName(u32 hash) const;

		void Merge(const Database& db);

		template <typename TYPE> void AddPrimitive(const TYPE& prim)
		{
			if (prim.name == GetNoName())
			{
				// Unnamed primitives are mapped by parent
				PrimitiveStore<TYPE>& store = GetUnnamedPrimitiveStore<TYPE>();
				store.insert(PrimitiveStore<TYPE>::value_type(prim.parent->first, prim));
			}
			else
			{
				PrimitiveStore<TYPE>& store = GetPrimitiveStore<TYPE>();
				store.insert(PrimitiveStore<TYPE>::value_type(prim.name->first, prim));
			}
		}

		template <typename TYPE> const TYPE* GetFirstPrimitive(const char* name_string) const
		{
			// Get the store associated with this type
			const PrimitiveStore<TYPE>& store = GetPrimitiveStore<TYPE>();

			// Return the first instance of an object with this name
			u32 name = HashNameString(name_string);
			PrimitiveStore<TYPE>::const_iterator i = store.find(name);
			if (i != store.end())
			{
				return &i->second;
			}

			return 0;
		}

		// A compile-time map to runtime data stores for each primitive type
		template <typename TYPE> PrimitiveStore<TYPE>& GetPrimitiveStore() { }
		template <> PrimitiveStore<Namespace>& GetPrimitiveStore() { return m_Namespaces; }
		template <> PrimitiveStore<Type>& GetPrimitiveStore() { return m_Types; }
		template <> PrimitiveStore<Class>& GetPrimitiveStore() { return m_Classes; }
		template <> PrimitiveStore<Enum>& GetPrimitiveStore() { return m_Enums; }
		template <> PrimitiveStore<EnumConstant>& GetPrimitiveStore() { return m_EnumConstants; }
		template <> PrimitiveStore<Function>& GetPrimitiveStore() { return m_Functions; }
		template <> PrimitiveStore<Field>& GetPrimitiveStore() { return m_Fields; }

		// The same for unnamed primitives
		template <typename TYPE> PrimitiveStore<TYPE>& GetUnnamedPrimitiveStore()
		{
			assert(false && "No unnamed primitive store for primitives of this type");
			return *(PrimitiveStore<TYPE>*)0;
		}
		template <> PrimitiveStore<Field>& GetUnnamedPrimitiveStore() { return m_UnnamedFields; }

		// Single pass-through const retrieval of the primitive stores. This strips the const-ness
		// of the 'this' pointer to remove the need to copy-paste the GetPrimitiveStore implementations
		// with const added.
		template <typename TYPE> const PrimitiveStore<TYPE>& GetPrimitiveStore() const
		{
			return const_cast<Database*>(this)->GetPrimitiveStore<TYPE>();
		}
		template <typename TYPE> const PrimitiveStore<TYPE>& GetUnnamedPrimitiveStore() const
		{
			return const_cast<Database*>(this)->GetUnnamedPrimitiveStore<TYPE>();
		}

		// All unique, scope-qualified names
		NameMap m_Names;

		// Primitives are owned by the following maps depending upon their type
		PrimitiveStore<Namespace> m_Namespaces;
		PrimitiveStore<Type> m_Types;
		PrimitiveStore<Class> m_Classes;
		PrimitiveStore<Enum> m_Enums;
		PrimitiveStore<EnumConstant> m_EnumConstants;
		PrimitiveStore<Function> m_Functions;
		PrimitiveStore<Field> m_Fields;

		PrimitiveStore<Field> m_UnnamedFields;
	};
}