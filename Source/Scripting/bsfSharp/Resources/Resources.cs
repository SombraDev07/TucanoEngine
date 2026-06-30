//********************************* B3D Framework - Copyright 2018-2019 Marko Pintera ************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;

#if IS_B3D
namespace b3d
{
    /** @addtogroup Resources
     *  @{
     */

    public partial class Resources
    {
        /// <summary>
        /// Loads a resource at the specified path. If resource is already loaded an existing instance is returned.
        /// </summary>
        /// <remarks>
        /// If running outside of the editor you must make sure to mark that the resource gets included in the build. If
        /// running inside the editor this has similar functionality as if loading using the project library.
        /// </remarks>
        /// <typeparam name="T">Type of the resource.</typeparam>
        /// <param name="resourcePath">
        /// Path to the resource. This may be a virtual or physical path. e.g.:
        ///  Virtual path: '/game/textures/path/to/resource'
        ///  Physical path: 'D:/path/to/package.b3d/path/to/resource'
        /// </param>
        /// <param name="loadOptions">Options to control the loading process.</param>
        /// <returns>
        /// Handle to the resource. Note if performing async loading this method will return immediately, but the resource may not yet be loaded.
        /// Returns null if resource cannot be loaded, and logs why it failed.
        /// </returns>
        public static RRef<T> LoadAsReference<T>(string resourcePath, ResourceLoadOptions loadOptions = new ResourceLoadOptions()) where T : Resource
        {
            RRefBase resourceReference = Internal_Load(resourcePath, ref loadOptions);
            if (resourceReference != null)
                return resourceReference.As<T>();

            return null;
        }

        /// <summary>
        /// Loads a resource at the specified path. If resource is already loaded an existing instance is returned.
        /// </summary>
        /// <remarks>
        /// If running outside of the editor you must make sure to mark that the resource gets included in the build. If
        /// running inside the editor this has similar functionality as if loading using the project library.
        /// </remarks>
        /// <typeparam name="T">Type of the resource.</typeparam>
        /// <param name="resourcePath">
        /// Path to the resource. This may be a virtual or physical path. e.g.:
        ///  Virtual path: '/game/textures/path/to/resource'
        ///  Physical path: 'D:/path/to/package.b3d/path/to/resource'
        /// </param>
        /// <param name="loadOptions">Options to control the loading process.</param>
        /// <returns>
        /// Handle to the resource. Note if performing async loading this method will return immediately, but the resource may not yet be loaded.
        /// Returns null if resource cannot be loaded, and logs why it failed.
        /// </returns>
        public static T Load<T>(string resourcePath, ResourceLoadOptions loadOptions = new ResourceLoadOptions()) where T : Resource
        {
            RRef<T> resourceReference = LoadAsReference<T>(resourcePath, loadOptions);
            if (resourceReference != null)
                return resourceReference.Value;

            return null;
        }

        /// <summary>
        /// Loads a resource with the specified ID. If resource is already loaded an existing instance is returned.
        /// </summary>
        /// <remarks>
        /// If running outside of the editor you must make sure to mark that the resource gets included in the build. If
        /// running inside the editor this has similar functionality as if loading using the project library.
        /// </remarks>
        /// <typeparam name="T">Type of the resource.</typeparam>
        /// <param name="resourceId">ID of the resource</param>
        /// <param name="loadOptions">Options to control the loading process.</param>
        /// <returns>
        /// Handle to the resource. Note if performing async loading this method will return immediately, but the resource may not yet be loaded.
        /// Returns null if resource cannot be loaded, and logs why it failed.
        /// </returns>
        public static RRef<T> LoadAsReference<T>(UUID resourceId, ResourceLoadOptions loadOptions = new ResourceLoadOptions()) where T : Resource
        {
            RRefBase resourceReference = Internal_Load0(ref resourceId, ref loadOptions);
            if (resourceReference != null)
                return resourceReference.As<T>();

            return null;
        }

        /// <summary>
        /// Loads a resource with the specified ID. If resource is already loaded an existing instance is returned.
        /// </summary>
        /// <remarks>
        /// If running outside of the editor you must make sure to mark that the resource gets included in the build. If
        /// running inside the editor this has similar functionality as if loading using the project library.
        /// </remarks>
        /// <typeparam name="T">Type of the resource.</typeparam>
        /// <param name="resourceId">ID of the resource</param>
        /// <param name="loadOptions">Options to control the loading process.</param>
        /// <returns>
        /// Handle to the resource. Note if performing async loading this method will return immediately, but the resource may not yet be loaded.
        /// Returns null if resource cannot be loaded, and logs why it failed.
        /// </returns>
        public static T Load<T>(UUID resourceId, ResourceLoadOptions loadOptions = new ResourceLoadOptions()) where T : Resource
        {
            RRef<T> resourceReference = LoadAsReference<T>(resourceId, loadOptions);
            if (resourceReference != null)
                return resourceReference.Value;

            return null;
        }
    }

    /** @} */
}
#endif
