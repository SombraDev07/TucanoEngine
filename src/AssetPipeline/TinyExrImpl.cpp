// Single translation unit for tinyexr, mirroring src/Runtime/StbImpl.cpp.
//
// The ZIP backend is stb's zlib (see cmake/Dependencies.cmake), so the two symbols tinyexr declares
// extern — stbi_zlib_decode_buffer and stbi_zlib_compress — resolve against StbImpl.cpp.

#define TINYEXR_IMPLEMENTATION
#include <tinyexr.h>
