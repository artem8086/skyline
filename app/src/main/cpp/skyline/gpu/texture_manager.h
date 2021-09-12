// SPDX-License-Identifier: MPL-2.0
// Copyright © 2021 Skyline Team and Contributors (https://github.com/skyline-emu/)

#pragma once

#include "texture/texture.h"
#include <random>

namespace skyline::gpu {
    /**
     * @brief The Texture Manager is responsible for maintaining a global view of textures being mapped from the guest to the host, any lookups and creation of host texture from equivalent guest textures alongside reconciliation of any overlaps with existing textures
     */
    class TextureManager {
      private:
        /**
         * @brief A single contiguous mapping of a texture in the CPU address space
         */
        struct TextureMapping : span<u8> {
            std::shared_ptr<Texture> texture;
            GuestTexture::Mappings::iterator iterator; //!< An iterator to the mapping in the texture's GuestTexture corresponding to this mapping

            template<typename... Args>
            TextureMapping(std::shared_ptr<Texture> texture, GuestTexture::Mappings::iterator iterator, Args &&... args) : span<u8>(std::forward<Args>(args)...), texture(std::move(texture)), iterator(iterator) {}
        };

        GPU &gpu;
        std::mutex mutex; //!< Synchronizes access to the texture mappings
        std::vector<TextureMapping> textures; //!< A sorted vector of all texture mappings

        bool IsSizeCompatible(texture::Dimensions lhsDimension, texture::TileConfig lhsConfig, texture::Dimensions rhsDimension, texture::TileConfig rhsConfig) {
            return lhsDimension == rhsDimension && lhsConfig == rhsConfig;
        }

      public:
        TextureManager(GPU &gpu);

        /**
         * @return A pre-existing or newly created Texture object which matches the specified criteria
         */
        TextureView FindOrCreate(const GuestTexture &guestTexture);
    };
}
