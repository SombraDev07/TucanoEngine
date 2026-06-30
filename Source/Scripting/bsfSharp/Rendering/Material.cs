//********************************* B3D Framework - Copyright 2018-2019 Marko Pintera ************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
namespace b3d
{
    /** @addtogroup Rendering
     *  @{
     */

    public partial class Material
    {
        /// <summary>
        /// Returns a texture assigned to the material. If the material has a sprite texture assigned, this will return
        /// the parent texture of the sprite.
        /// </summary>
        /// <param name="name">Name of the texture parameter.</param>
        /// <returns>Texture assigned to the specified material</returns>
        public RRef<Texture> GetTexture(string name)
        {
            return Internal_GetTexture(mCachedPtr, name);
        }

        /// <summary>
        /// Assigns a texture to the specified material parameter. Allows you to specify a surface parameter that allows you
        /// to bind only a sub-set of the texture.
        /// </summary>
        /// <param name="name">Name of the texture parameter.</param>
        /// <param name="texture">Texture resource to assign.</param>
        /// <param name="surface">Subset of the texture to assign.</param>
        public void SetTexture(string name, RRef<Texture> texture, TextureSurface surface)
        {
            Internal_SetTexture(mCachedPtr, name, texture, surface.MipLevel, surface.MipLevelCount, surface.Face, surface.FaceCount);
        }

        /// <summary>
        /// Assigns a texture to the specified material parameter.
        /// </summary>
        /// <param name="name">Name of the texture parameter.</param>
        /// <param name="texture">Texture resource to assign.</param>
        public void SetTexture(string name, RRef<Texture> texture)
        {
            Internal_SetTexture(mCachedPtr, name, texture, 0, 0, 0, 0);
        }

        /// <summary>
        /// Returns a sprite image assigned to the material.
        /// </summary>
        /// <param name="name">Name of the image parameter.</param>
        /// <returns>Image assigned to the specified material</returns>
        public RRef<SpriteImage> GetSpriteImage(string name)
        {
            return Internal_GetSpriteImage(mCachedPtr, name);
        }

        /// <summary>
        /// Assigns a sprite image to the specified material parameter. Sprite image is allowed to be animated, or just
        /// used for referencing a subset of a texture atlas.
        /// </summary>
        /// <param name="name">Name of the image parameter.</param>
        /// <param name="texture">Image resource to assign.</param>
        public void SetSpriteImage(string name, RRef<SpriteImage> image)
        {
            Internal_SetSpriteImage(mCachedPtr, name, image);
        }
    }

    /** @} */
}
