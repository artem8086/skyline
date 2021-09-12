// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#pragma once

#include "texture.h"

namespace skyline::gpu::format {
    using Format = gpu::texture::FormatBase;
    using vkf = vk::Format;
    using vka = vk::ImageAspectFlagBits;

    constexpr Format RGBA8888Unorm{sizeof(u8) * 4, 1, 1, vkf::eR8G8B8A8Unorm, vka::eColor}; //!< 8-bits per channel 4-channel pixels
    constexpr Format RGB565Unorm{sizeof(u8) * 2, 1, 1, vkf::eR5G6B5UnormPack16, vka::eColor}; //!< Red channel: 5-bit, Green channel: 6-bit, Blue channel: 5-bit

    /**
     * @brief Converts a Vulkan format to a Skyline format
     */
    constexpr gpu::texture::Format GetFormat(vk::Format format) {
        switch (format) {
            case vk::Format::eR8G8B8A8Unorm:
                return RGBA8888Unorm;
            case vk::Format::eR5G6B5UnormPack16:
                return RGB565Unorm;
            default:
                throw exception("Vulkan format not supported: '{}'", vk::to_string(format));
        }
    }
}
