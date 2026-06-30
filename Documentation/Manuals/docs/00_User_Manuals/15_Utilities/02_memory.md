---
title: Memory allocation
---

When allocating memory in the framework it is prefered (but not required) to use framework allocator functions instead of the standard *new* / *delete* operators or *malloc* / *free*.

- Use @b3d::B3DNew instead of *new* and @b3d::B3DDelete instead of *delete*.
- Use @b3d::B3DNewMultiple instead of *new[]* and @b3d::B3DDeleteMultiple instead of *delete[]*.
- Use @b3d::B3DAllocate instead of *malloc* and @b3d::B3DFree instead of *free*.

This ensures the framework can keep track of all allocated memory, which ensures better debugging and profiling, as well as ensuring that internal memory allocation method can be changed in the future.

~~~~~~~~~~~~~{.cpp}
// Helper structure
struct MyStruct
{
	MyStruct() {}
	MyStruct(i32 integerValue, bool booleanValue)
		:IntegerValue(integerValue), BooleanValue(booleanValue)
	{ }

	i32 IntegerValue;
	bool BooleanValue;
};

// Allocating memory the normal way
MyStruct* structPointer = new MyStruct(123, false);
MyStruct** structPointerArray = new MyStruct[5];
void* rawMemory = malloc(12);

delete structPointer;
delete[] structPointerArray;
free(rawMemory);

// Allocating memory the framework way
MyStruct* bansheeStructPointer = B3DNew<MyStruct>(123, false);
MyStruct** bansheeStructPointerArray = B3DNewMultiple<MyStruct>(5);
void* bansheeRawMemory = B3DAllocate(12);

B3DDelete(bansheeStructPointer);
B3DDeleteMultiple(bansheeStructPointerArray, 5);
B3DFree(bansheeRawMemory);
~~~~~~~~~~~~~
