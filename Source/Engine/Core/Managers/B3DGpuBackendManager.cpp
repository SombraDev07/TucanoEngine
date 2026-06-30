//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#include "Managers/B3DGpuBackendManager.h"
#include "CoreObject/B3DRenderThread.h"
#include "GpuBackend/B3DGpuBackend.h"
#include "Plugin/B3DPluginLoader.h"

using namespace b3d;

GpuBackendManager::~GpuBackendManager()
{
	if(mGpuBackendInitialized)
		GpuBackend::ShutDown();

	// Flush any pending render thread work before unloading the plugin DLL,
	// since GPU backend code referenced by the render thread lives in it.
	if(mPlugin.Library != nullptr || mPlugin.ReturnValue != nullptr)
		GetRenderThread().PostCommand([] {}, "GpuBackendManager plugin unload flush", true);

	PluginLoader::Unload(mPlugin);
	mFactory = nullptr;
}

void GpuBackendManager::Initialize(const String& pluginFilename)
{
	if(!B3D_ENSURE(!mGpuBackendInitialized))
		return;

	mPlugin = PluginLoader::Load(pluginFilename);
	mFactory = static_cast<GpuBackendFactory*>(mPlugin.ReturnValue);

	if(mFactory != nullptr)
	{
		mFactory->Create();
		mGpuBackendInitialized = true;
	}
}
