// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#pragma once

#include <gpu/memory_manager.h>

namespace skyline::gpu {
    namespace texture {
        struct Dimensions {
            u32 width;
            u32 height;
            u32 depth;

            constexpr Dimensions() : width(0), height(0), depth(0) {}

            constexpr Dimensions(u32 width) : width(width), height(1), depth(1) {}

            constexpr Dimensions(u32 width, u32 height) : width(width), height(height), depth(1) {}

            constexpr Dimensions(u32 width, u32 height, u32 depth) : width(width), height(height), depth(depth) {}

            constexpr Dimensions(vk::Extent2D extent) : Dimensions(extent.width, extent.height) {}

            constexpr Dimensions(vk::Extent3D extent) : Dimensions(extent.width, extent.height, extent.depth) {}

            auto operator<=>(const Dimensions &) const = default;

            constexpr vk::ImageType GetType() const {
                if (depth > 1)
                    return vk::ImageType::e3D;
                else if (height > 1)
                    return vk::ImageType::e2D;
                else
                    return vk::ImageType::e1D;
            }

            constexpr operator vk::Extent2D() const {
                return vk::Extent2D{
                    .width = width,
                    .height = height,
                };
            }

            constexpr operator vk::Extent3D() const {
                return vk::Extent3D{
                    .width = width,
                    .height = height,
                    .depth = depth,
                };
            }

            /**
             * @return If the dimensions are valid and don't equate to zero
             */
            constexpr operator bool() const {
                return width && height && depth;
            }
        };

        /**
         * @note Blocks refers to the atomic unit of a compressed format (IE: The minimum amount of data that can be decompressed)
         */
        struct FormatBase {
            u8 bpb{}; //!< Bytes Per Block, this is used instead of bytes per pixel as that might not be a whole number for compressed formats
            u16 blockHeight{}; //!< The height of a block in pixels
            u16 blockWidth{}; //!< The width of a block in pixels
            vk::Format vkFormat{vk::Format::eUndefined};
            vk::ImageAspectFlags vkAspect{vk::ImageAspectFlagBits::eColor};

            constexpr bool IsCompressed() const {
                return (blockHeight != 1) || (blockWidth != 1);
            }

            /**
             * @param width The width of the texture in pixels
             * @param height The height of the texture in pixels
             * @param depth The depth of the texture in layers
             * @return The size of the texture in bytes
             */
            constexpr size_t GetSize(u32 width, u32 height, u32 depth = 1) const {
                return (((width / blockWidth) * (height / blockHeight)) * bpb) * depth;
            }

            constexpr size_t GetSize(Dimensions dimensions) const {
                return GetSize(dimensions.width, dimensions.height, dimensions.depth);
            }

            constexpr bool operator==(const FormatBase &format) const {
                return vkFormat == format.vkFormat;
            }

            constexpr bool operator!=(const FormatBase &format) const {
                return vkFormat != format.vkFormat;
            }

            constexpr operator vk::Format() const {
                return vkFormat;
            }

            /**
             * @return If this format is actually valid or not
             */
            constexpr operator bool() const {
                return bpb;
            }

            /**
             * @return If the supplied format is texel-layout compatible with the current format
             */
            constexpr bool IsCompatible(const FormatBase &other) const {
                return bpb == other.bpb && blockHeight == other.blockHeight && blockWidth == other.blockWidth;
            }
        };

        /**
         * @brief A wrapper around a pointer to underlying format metadata to prevent redundant copies
         */
        class Format {
          private:
            const FormatBase *base;

          public:
            constexpr Format(const FormatBase &base) : base(&base) {}

            constexpr Format() : base(nullptr) {}

            constexpr const FormatBase *operator->() const {
                return base;
            }

            constexpr const FormatBase &operator*() const {
                return *base;
            }

            constexpr operator bool() const {
                return base;
            }
        };

        /**
         * @brief The layout of a texture in GPU memory
         * @note Refer to Chapter 20.1 of the Tegra X1 TRM for information
         */
        enum class TileMode {
            Linear, //!< All pixels are arranged linearly
            Pitch,  //!< All pixels are arranged linearly but rows aligned to the pitch
            Block,  //!< All pixels are arranged into blocks and swizzled in a Z-order curve to optimize for spacial locality
        };

        /**
         * @brief The parameters of the tiling mode, covered in Table 76 in the Tegra X1 TRM
         */
        struct TileConfig {
            TileMode mode;
            union {
                struct {
                    u8 blockHeight; //!< The height of the blocks in GOBs
                    u8 blockDepth;  //!< The depth of the blocks in GOBs
                };
                u32 pitch; //!< The pitch of the texture if it's pitch linear
            };

            constexpr bool operator==(const TileConfig &other) const {
                if (mode == other.mode)
                    if (mode == TileMode::Linear)
                        return true;
                    else if (mode == TileMode::Pitch)
                        return pitch == other.pitch;
                    else if (mode == TileMode::Block)
                        return blockHeight == other.blockHeight && blockDepth == other.blockDepth;
                return false;
            }
        };

        enum class SwizzleChannel : u8 {
            Zero, //!< Write 0 to the channel
            One, //!< Write 1 to the channel
            Red, //!< Red color channel
            Green, //!< Green color channel
            Blue, //!< Blue color channel
            Alpha, //!< Alpha channel
        };

        struct Swizzle {
            SwizzleChannel red{SwizzleChannel::Red}; //!< Swizzle for the red channel
            SwizzleChannel green{SwizzleChannel::Green}; //!< Swizzle for the green channel
            SwizzleChannel blue{SwizzleChannel::Blue}; //!< Swizzle for the blue channel
            SwizzleChannel alpha{SwizzleChannel::Alpha}; //!< Swizzle for the alpha channel

            constexpr operator vk::ComponentMapping() {
                auto swizzleConvert{[](SwizzleChannel channel) {
                    switch (channel) {
                        case SwizzleChannel::Zero:
                            return vk::ComponentSwizzle::eZero;
                        case SwizzleChannel::One:
                            return vk::ComponentSwizzle::eOne;
                        case SwizzleChannel::Red:
                            return vk::ComponentSwizzle::eR;
                        case SwizzleChannel::Green:
                            return vk::ComponentSwizzle::eG;
                        case SwizzleChannel::Blue:
                            return vk::ComponentSwizzle::eB;
                        case SwizzleChannel::Alpha:
                            return vk::ComponentSwizzle::eA;
                    }
                }};

                return vk::ComponentMapping{
                    .r = swizzleConvert(red),
                    .g = swizzleConvert(green),
                    .b = swizzleConvert(blue),
                    .a = swizzleConvert(alpha),
                };
            }
        };

        /**
         * @brief The type of a texture to determine the access patterns for it
         * @note This is effectively the Tegra X1 texture types with the 1DBuffer + 2DNoMipmap removed as those are handled elsewhere
         * @note We explicitly utilize Vulkan types here as it provides the most efficient conversion while not exposing Vulkan to the outer API
         */
        enum class TextureType {
            e1D = VK_IMAGE_VIEW_TYPE_1D,
            e2D = VK_IMAGE_VIEW_TYPE_2D,
            e3D = VK_IMAGE_VIEW_TYPE_3D,
            eCube = VK_IMAGE_VIEW_TYPE_CUBE,
            e1DArray = VK_IMAGE_VIEW_TYPE_1D_ARRAY,
            e2DArray = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
            eCubeArray = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY,
        };
    }

    class Texture;
    class PresentationEngine; //!< A forward declaration of PresentationEngine as we require it to be able to create a Texture object

    /**
     * @brief A descriptor for a texture present in guest memory, it can be used to create a corresponding Texture object for usage on the host
     */
    struct GuestTexture {
        using Mappings = std::vector<span<u8>>;

        Mappings mappings; //!< Spans to CPU memory for the underlying data backing this texture
        texture::Dimensions dimensions;
        texture::Format format;
        texture::TileConfig tileConfig;
        texture::TextureType type;
        u16 baseArrayLayer;
        u16 layerCount;
        u32 layerStride; //!< An optional hint regarding the size of a single layer, it will be set to 0 when not available

        GuestTexture() {}

        GuestTexture(Mappings mappings, texture::Dimensions dimensions, texture::Format format, texture::TileConfig tileConfig, texture::TextureType type, u16 baseArrayLayer = 0, u16 layerCount = 1, u32 layerStride = 0) : mappings(mappings), dimensions(dimensions), format(format), tileConfig(tileConfig), type(type), baseArrayLayer(baseArrayLayer), layerCount(layerCount), layerStride(layerStride) {}

        GuestTexture(span <u8> mapping, texture::Dimensions dimensions, texture::Format format, texture::TileConfig tileConfig, texture::TextureType type, u16 baseArrayLayer = 0, u16 layerCount = 1, u32 layerStride = 0) : mappings(1, mapping), dimensions(dimensions), format(format), tileConfig(tileConfig), type(type), baseArrayLayer(baseArrayLayer), layerCount(layerCount), layerStride(layerStride) {}
    };

    class TextureManager;

    /**
     * @brief A view into a specific subresource of a Texture
     */
    class TextureView {
      private:
        vk::raii::ImageView *view{};

      public:
        std::shared_ptr<Texture> backing;
        vk::ImageViewType type;
        texture::Format format;
        vk::ComponentMapping mapping;
        vk::ImageSubresourceRange range;

        /**
         * @param format A compatible format for the texture view (Defaults to the format of the backing texture)
         */
        TextureView(std::shared_ptr<Texture> backing, vk::ImageViewType type, vk::ImageSubresourceRange range, texture::Format format = {}, vk::ComponentMapping mapping = {});

        /**
         * @return A Vulkan Image View that corresponds to the properties of this view
         */
        vk::ImageView GetView();
    };

    /**
     * @brief A texture which is backed by host constructs while being synchronized with the underlying guest texture
     * @note This class conforms to the Lockable and BasicLockable C++ named requirements
     */
    class Texture : public std::enable_shared_from_this<Texture>, public FenceCycleDependency {
      private:
        GPU &gpu;
        std::mutex mutex; //!< Synchronizes any mutations to the texture or its backing
        std::condition_variable backingCondition; //!< Signalled when a valid backing has been swapped in
        using BackingType = std::variant<vk::Image, vk::raii::Image, memory::Image>;
        BackingType backing; //!< The Vulkan image that backs this texture, it is nullable
        std::shared_ptr<FenceCycle> cycle; //!< A fence cycle for when any host operation mutating the texture has completed, it must be waited on prior to any mutations to the backing

        friend TextureManager;

      public:
        std::optional<GuestTexture> guest;
        texture::Dimensions dimensions;
        texture::Format format;
        vk::ImageLayout layout;
        vk::ImageTiling tiling;
        u32 mipLevels;
        u32 layerCount; //!< The amount of array layers in the image, utilized for efficient binding (Not to be confused with the depth or faces in a cubemap)
        vk::SampleCountFlagBits sampleCount;

        Texture(GPU &gpu, BackingType &&backing, GuestTexture guest, texture::Dimensions dimensions, texture::Format format, vk::ImageLayout layout, vk::ImageTiling tiling, u32 mipLevels = 1, u32 layerCount = 1, vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e1);

        Texture(GPU &gpu, BackingType &&backing, texture::Dimensions dimensions, texture::Format format, vk::ImageLayout layout, vk::ImageTiling tiling, u32 mipLevels = 1, u32 layerCount = 1, vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e1);

        Texture(GPU &gpu, GuestTexture guest);

        /**
         * @brief Creates and allocates memory for the backing to creates a texture object wrapping it
         * @param usage Usage flags that will applied aside from VK_IMAGE_USAGE_TRANSFER_SRC_BIT/VK_IMAGE_USAGE_TRANSFER_DST_BIT which are mandatory
         */
        Texture(GPU &gpu, texture::Dimensions dimensions, texture::Format format, vk::ImageLayout initialLayout = vk::ImageLayout::eGeneral, vk::ImageUsageFlags usage = {}, vk::ImageTiling tiling = vk::ImageTiling::eOptimal, u32 mipLevels = 1, u32 layerCount = 1, vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e1);

        /**
         * @note The handle returned is nullable and the appropriate precautions should be taken
         */
        constexpr vk::Image GetBacking() {
            return std::visit(VariantVisitor{
                [](vk::Image image) { return image; },
                [](const vk::raii::Image &image) { return *image; },
                [](const memory::Image &image) { return image.vkImage; },
            }, backing);
        }

        /**
         * @brief Acquires an exclusive lock on the texture for the calling thread
         * @note Naming is in accordance to the BasicLockable named requirement
         */
        void lock() {
            mutex.lock();
        }

        /**
         * @brief Relinquishes an existing lock on the texture by the calling thread
         * @note Naming is in accordance to the BasicLockable named requirement
         */
        void unlock() {
            mutex.unlock();
        }

        /**
         * @brief Attempts to acquire an exclusive lock but returns immediately if it's captured by another thread
         * @note Naming is in accordance to the Lockable named requirement
         */
        bool try_lock() {
            return mutex.try_lock();
        }

        /**
         * @brief Waits on the texture backing to be a valid non-null Vulkan image
         * @return If the mutex could be unlocked during the function
         * @note The texture **must** be locked prior to calling this
         */
        bool WaitOnBacking();

        /**
         * @brief Waits on a fence cycle if it exists till it's signalled and resets it after
         * @note The texture **must** be locked prior to calling this
         */
        void WaitOnFence();

        /**
         * @note All memory residing in the current backing is not copied to the new backing, it must be handled externally
         * @note The texture **must** be locked prior to calling this
         */
        void SwapBacking(BackingType &&backing, vk::ImageLayout layout = vk::ImageLayout::eUndefined);

        /**
         * @brief Transitions the backing to the supplied layout, if the backing already is in this layout then this does nothing
         * @note The texture **must** be locked prior to calling this
         */
        void TransitionLayout(vk::ImageLayout layout);

        /**
         * @brief Converts the texture to have the specified format
         */
        void SetFormat(texture::Format format);

        /**
         * @brief Synchronizes the host texture with the guest after it has been modified
         * @note The texture **must** be locked prior to calling this
         * @note The guest texture should not be null prior to calling this
         */
        void SynchronizeHost();

        /**
         * @brief Synchronizes the guest texture with the host texture after it has been modified
         * @note The texture **must** be locked prior to calling this
         * @note The guest texture should not be null prior to calling this
         */
        void SynchronizeGuest();

        /**
         * @brief Copies the contents of the supplied source texture into the current texture
         */
        void CopyFrom(std::shared_ptr<Texture> source, const vk::ImageSubresourceRange &subresource = vk::ImageSubresourceRange{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .levelCount = VK_REMAINING_MIP_LEVELS,
            .layerCount = VK_REMAINING_ARRAY_LAYERS,
        });
    };
}
