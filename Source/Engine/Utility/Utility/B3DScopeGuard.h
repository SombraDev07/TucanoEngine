//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once
#include "B3DUtilityPrerequisites.h"

namespace b3d
{
	/** @addtogroup General
	 *  @{
	 */

	/** Executes a function when the object goes out of scope. */
	struct ScopeGuard : INonCopyable
	{
		ScopeGuard(const Function<void()>& callback) :
			Callback(callback)
		{ }

		ScopeGuard(Function<void()>&& callback) :
			Callback(std::move(callback))
		{ }

		~ScopeGuard() { Callback(); }

	private:
		Function<void()> Callback;
	};


	/** Ensures a piece of code executes when the current scope ends. */
#define B3D_SCOPE_CLEANUP(x) auto B3D_CONCAT(scopeGuard_, __LINE__) = ScopeGuard([&] { x; })

	/** @} */
} // namespace b3d
