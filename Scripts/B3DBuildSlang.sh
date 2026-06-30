#!/bin/sh

. ./B3DBuildCommon.sh

echo "Builds Slang shader compiler from source"
echo ""

# Check prerequisites
if ! command -v cmake &> /dev/null; then
    echo "[Error] CMake is not installed. Please install CMake 3.26 or later."
    exit 1
fi

if ! command -v python3 &> /dev/null && ! command -v python &> /dev/null; then
    echo "[Error] Python 3 is not installed. Please install Python 3."
    exit 1
fi

# Platform-specific information
if [[ "$Platform" == "win32" || "$Platform" == "msys" ]]; then
    echo "Building for Windows."
    CMakePreset="vs2022"
elif [[ "$Platform" == "darwin"* ]]; then
    echo "Building for macOS."
    CMakePreset="default"
elif [[ "$Platform" == "linux-gnu"* ]]; then
    echo "Building for Linux."
    CMakePreset="default"
else
    echo "[Error] This build script is not currently supported on the current platform: $Platform."
    exit 1
fi

# Create intermediate folders
cd ..

mkdir -p Intermediate
cd Intermediate

mkdir -p DependencySources
cd DependencySources

# Clone or update Slang repository
if [ -d "slang" ]; then
    echo "Slang repository exists, updating..."
    cd slang
    git stash
    git fetch --tags
    git pull origin v2025.21.0
    git submodule update --init --recursive
    git stash pop
else
    echo "Cloning Slang repository..."
    git clone https://github.com/shader-slang/slang.git --recursive slang
    cd slang
    git checkout v2025.21.0 || git checkout master
fi

# Setup Slang output folders
SlangOutputFolder="$PlatformDependencyFolder/Slang"

echo "Output folder: $SlangOutputFolder"

rm -rf "$SlangOutputFolder"
mkdir -p "$SlangOutputFolder/include/"
mkdir -p "$SlangOutputFolder/lib/"
mkdir -p "$SlangOutputFolder/bin/"

if [[ "$Platform" == "win32" || "$Platform" == "msys" ]]; then
    mkdir -p "$SlangOutputFolder/lib/Release/"
    mkdir -p "$SlangOutputFolder/lib/Debug/"
    mkdir -p "$SlangOutputFolder/bin/Release/"
    mkdir -p "$SlangOutputFolder/bin/Debug/"
fi

# Configure CMake
echo "Configuring CMake with preset: $CMakePreset"
cmake --preset "$CMakePreset" || exit 1

# Build based on platform
if [[ "$Platform" == "win32" || "$Platform" == "msys" ]]; then
    echo "Building Release configuration..."
    cmake --build --preset release || exit 1

    echo "Building Debug configuration..."
    cmake --build --preset debug || exit 1

    # Copy Release binaries
    echo "Copying Release binaries..."
    cp -p build/Release/bin/slang-compiler${SharedLibraryExtension} "$SlangOutputFolder/bin/Release/" 2>/dev/null || \
        cp -p build/Release/bin/slang${SharedLibraryExtension} "$SlangOutputFolder/bin/Release/"
    cp -p build/Release/bin/slang.exe "$SlangOutputFolder/bin/Release/" 2>/dev/null || true
    cp -p build/Release/bin/slangc.exe "$SlangOutputFolder/bin/Release/" 2>/dev/null || true
    cp -p build/Release/lib/slang-compiler${ImportLibraryExtension} "$SlangOutputFolder/lib/Release/" 2>/dev/null || \
        cp -p build/Release/lib/slang${ImportLibraryExtension} "$SlangOutputFolder/lib/Release/" 2>/dev/null || true

    # Copy Debug binaries
    echo "Copying Debug binaries..."
    cp -p build/Debug/bin/slang-compiler${SharedLibraryExtension} "$SlangOutputFolder/bin/Debug/" 2>/dev/null || \
        cp -p build/Debug/bin/slang${SharedLibraryExtension} "$SlangOutputFolder/bin/Debug/"
    cp -p build/Debug/bin/slang.exe "$SlangOutputFolder/bin/Debug/" 2>/dev/null || true
    cp -p build/Debug/bin/slangc.exe "$SlangOutputFolder/bin/Debug/" 2>/dev/null || true
    cp -p build/Debug/lib/slang-compiler${ImportLibraryExtension} "$SlangOutputFolder/lib/Debug/" 2>/dev/null || \
        cp -p build/Debug/lib/slang${ImportLibraryExtension} "$SlangOutputFolder/lib/Debug/" 2>/dev/null || true
    cp -p build/Debug/bin/slang-compiler.pdb "$SlangOutputFolder/bin/Debug/" 2>/dev/null || \
        cp -p build/Debug/bin/slang.pdb "$SlangOutputFolder/bin/Debug/" 2>/dev/null || true

    # Copy includes
    echo "Copying headers..."
    cp -a include/*.h "$SlangOutputFolder/include/" 2>/dev/null || true
    cp -a build/Release/include/slang-tag-version.h "$SlangOutputFolder/include/" 2>/dev/null || true

else
    echo "Building Release configuration..."
    cmake --build --preset release || exit 1

    # Copy Release binaries
    echo "Copying Release binaries..."
    cp -p build/Release/bin/${StaticLibraryPrefix}slang-compiler${SharedLibraryExtension}* "$SlangOutputFolder/bin/" 2>/dev/null || \
        cp -p build/Release/bin/${StaticLibraryPrefix}slang${SharedLibraryExtension}* "$SlangOutputFolder/bin/" || true
    cp -p build/Release/bin/slang "$SlangOutputFolder/bin/" 2>/dev/null || true
    cp -p build/Release/bin/slangc "$SlangOutputFolder/bin/" 2>/dev/null || true
    cp -p build/Release/lib/${StaticLibraryPrefix}slang-compiler${StaticLibraryExtension} "$SlangOutputFolder/lib/" 2>/dev/null || \
        cp -p build/Release/lib/${StaticLibraryPrefix}slang${StaticLibraryExtension} "$SlangOutputFolder/lib/" 2>/dev/null || true

    # Copy includes
    echo "Copying headers..."
    cp -a include/*.h "$SlangOutputFolder/include/" 2>/dev/null || true
    cp -a build/Release/include/slang-tag-version.h "$SlangOutputFolder/include/" 2>/dev/null || true
fi

echo ""
echo "======================================================================"
echo "Build complete!"
echo "======================================================================"
echo ""
echo "Slang has been built and installed to:"
echo "  $SlangOutputFolder"
