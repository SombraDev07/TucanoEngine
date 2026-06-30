---
title: Flags
---

@b3d::Flags provides a type-safe wrapper around enum values that allows you to easily perform bitwise operations without explicit casting to integers. This makes working with bit flags more convenient and less error-prone.

# Basic usage

When using raw C++ enums, combining flags requires cumbersome casting:

~~~~~~~~~~~~~{.cpp}
enum class MyFlag
{
	Flag1 = 1 << 0,
	Flag2 = 1 << 1,
	Flag3 = 1 << 2
};

// Cumbersome casting required
MyFlag combined = (MyFlag)((u32)MyFlag::Flag1 | (u32)MyFlag::Flag2);
~~~~~~~~~~~~~

With **Flags**, this becomes much simpler. To create a **Flags** type for an enum, define a `typedef` with your enum type as the template parameter, then use the @b3d::B3D_FLAGS_OPERATORS macro to define all necessary operators:

~~~~~~~~~~~~~{.cpp}
enum class MyFlag
{
	Flag1 = 1 << 0,
	Flag2 = 1 << 1,
	Flag3 = 1 << 2
};

typedef Flags<MyFlag> MyFlags;
B3D_FLAGS_OPERATORS(MyFlag)
~~~~~~~~~~~~~

Now you can use the flags naturally:

~~~~~~~~~~~~~{.cpp}
MyFlags combined = MyFlag::Flag1 | MyFlag::Flag2;
~~~~~~~~~~~~~

# Operations

**Flags** supports all standard bitwise operations:

~~~~~~~~~~~~~{.cpp}
// Combining flags
MyFlags flags = MyFlag::Flag1 | MyFlag::Flag2;

// Adding more flags
flags |= MyFlag::Flag3;

// Removing flags
flags &= ~MyFlag::Flag1;

// Checking if specific flags are set
if(flags & MyFlag::Flag2)
{
	// Flag2 is set
}

// XOR operation
MyFlags toggled = flags ^ MyFlag::Flag3;
~~~~~~~~~~~~~

# Checking flag status

The **Flags** class provides several methods for checking flag status:

~~~~~~~~~~~~~{.cpp}
MyFlags flags = MyFlag::Flag1 | MyFlag::Flag2;

// Check if all specified flags are set
bool hasAllFlags = flags.IsSet(MyFlag::Flag1 | MyFlag::Flag2); // true
bool hasFlag3 = flags.IsSet(MyFlag::Flag3); // false

// Check if any of the specified flags are set
bool hasAnyFlag = flags.IsSetAny(MyFlag::Flag1 | MyFlag::Flag3); // true (Flag1 is set)

// Check using another Flags object
MyFlags testFlags = MyFlag::Flag2 | MyFlag::Flag3;
bool hasAnyTestFlag = flags.IsSetAny(testFlags); // true (Flag2 is set)
bool hasAllTestFlags = flags.IsSetAll(testFlags); // false (Flag3 is not set)
~~~~~~~~~~~~~

# Setting and unsetting flags

~~~~~~~~~~~~~{.cpp}
MyFlags flags = MyFlag::Flag1;

// Set additional flags
flags.Set(MyFlag::Flag2);
flags.Set(MyFlag::Flag3);

// Unset specific flags
flags.Unset(MyFlag::Flag1);

// Now flags contains only Flag2 and Flag3
~~~~~~~~~~~~~

# Comparison

**Flags** supports equality comparison:

~~~~~~~~~~~~~{.cpp}
MyFlags flags1 = MyFlag::Flag1 | MyFlag::Flag2;
MyFlags flags2 = MyFlag::Flag1 | MyFlag::Flag2;
MyFlags flags3 = MyFlag::Flag1;

bool areEqual = (flags1 == flags2); // true
bool areDifferent = (flags1 != flags3); // true

// Can also compare with individual enum values
bool isSingleFlag = (flags3 == MyFlag::Flag1); // true
~~~~~~~~~~~~~

# Custom storage type

By default, **Flags** uses `u32` for storage. You can specify a different storage type if needed:

~~~~~~~~~~~~~{.cpp}
enum class LargeFlag : u64
{
	Flag1 = 1ULL << 0,
	Flag2 = 1ULL << 32,
	Flag3 = 1ULL << 63
};

typedef Flags<LargeFlag, u64> LargeFlags;
B3D_FLAGS_OPERATORS(LargeFlag)

LargeFlags flags = LargeFlag::Flag1 | LargeFlag::Flag2;
~~~~~~~~~~~~~