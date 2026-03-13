#ifndef SPINE_TEXTURE_LOADER_H
#define SPINE_TEXTURE_LOADER_H

#include <spine/SpineString.h>

namespace spine {

/// Override to load & unload textures for atlas pages.
class TextureLoader {
public:
    virtual ~TextureLoader() {}

    /// Called to load a texture for an atlas page.
    /// @param page  Opaque pointer you store on the AtlasPage.
    /// @param path  File path of the texture image.
    virtual void load(void *&textureHandle, const String &path) = 0;

    /// Called to unload a previously loaded texture.
    virtual void unload(void *textureHandle) = 0;
};

} // namespace spine

#endif // SPINE_TEXTURE_LOADER_H
