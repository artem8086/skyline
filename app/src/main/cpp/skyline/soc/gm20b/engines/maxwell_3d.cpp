// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)
// Copyright © 2018-2020 fincs (https://github.com/devkitPro/deko3d)

#include <soc.h>

namespace skyline::soc::gm20b::engine::maxwell3d {
    Maxwell3D::Maxwell3D(const DeviceState &state, gmmu::GraphicsMemoryManager &gmmu) : Engine(state), macroInterpreter(*this), context(*state.gpu, gmmu) {
        ResetRegs();
    }

    void Maxwell3D::ResetRegs() {
        registers = {};

        registers.rasterizerEnable = true;

        for (auto &transform : registers.viewportTransforms) {
            transform.swizzles.x = type::ViewportTransform::Swizzle::PositiveX;
            transform.swizzles.y = type::ViewportTransform::Swizzle::PositiveY;
            transform.swizzles.z = type::ViewportTransform::Swizzle::PositiveZ;
            transform.swizzles.w = type::ViewportTransform::Swizzle::PositiveW;
        }

        for (auto &viewport : registers.viewports) {
            viewport.depthRangeFar = 1.0f;
            viewport.depthRangeNear = 0.0f;
        }

        registers.polygonMode.front = type::PolygonMode::Fill;
        registers.polygonMode.back = type::PolygonMode::Fill;

        registers.stencilFront.failOp = registers.stencilFront.zFailOp = registers.stencilFront.zPassOp = type::StencilOp::Keep;
        registers.stencilFront.compare.op = type::CompareOp::Always;
        registers.stencilFront.compare.mask = 0xFFFFFFFF;
        registers.stencilFront.writeMask = 0xFFFFFFFF;

        registers.stencilTwoSideEnable = true;
        registers.stencilBack.failOp = registers.stencilBack.zFailOp = registers.stencilBack.zPassOp = type::StencilOp::Keep;
        registers.stencilBack.compareOp = type::CompareOp::Always;
        registers.stencilBackExtra.compareMask = 0xFFFFFFFF;
        registers.stencilBackExtra.writeMask = 0xFFFFFFFF;

        registers.rtSeparateFragData = true;

        for (auto &attribute : registers.vertexAttributeState)
            attribute.fixed = true;

        registers.depthTestFunc = type::CompareOp::Always;

        registers.blend.colorOp = registers.blend.alphaOp = type::Blend::Op::Add;
        registers.blend.colorSrcFactor = registers.blend.alphaSrcFactor = type::Blend::Factor::One;
        registers.blend.colorDestFactor = registers.blend.alphaDestFactor = type::Blend::Factor::Zero;

        registers.lineWidthSmooth = 1.0f;
        registers.lineWidthAliased = 1.0f;

        registers.pointSpriteEnable = true;
        registers.pointSpriteSize = 1.0f;
        registers.pointCoordReplace.enable = true;

        registers.frontFace = type::FrontFace::CounterClockwise;
        registers.cullFace = type::CullFace::Back;

        for (auto &mask : registers.colorMask)
            mask.r = mask.g = mask.b = mask.a = 1;

        for (auto &blend : registers.independentBlend) {
            blend.colorOp = blend.alphaOp = type::Blend::Op::Add;
            blend.colorSrcFactor = blend.alphaSrcFactor = type::Blend::Factor::One;
            blend.colorDestFactor = blend.alphaDestFactor = type::Blend::Factor::Zero;
        }

        registers.viewportTransformEnable = true;
    }

    void Maxwell3D::CallMethod(MethodParams params) {
        state.logger->Debug("Called method in Maxwell 3D: 0x{:X} args: 0x{:X}", params.method, params.argument);

        // Methods that are greater than the register size are for macro control
        if (params.method >= RegisterCount) {
            if (!(params.method & 1))
                macroInvocation.index = ((params.method - RegisterCount) >> 1) % macroPositions.size();

            macroInvocation.arguments.push_back(params.argument);

            // Macros are always executed on the last method call in a pushbuffer entry
            if (params.lastCall) {
                macroInterpreter.Execute(macroPositions[macroInvocation.index], macroInvocation.arguments);

                macroInvocation.arguments.clear();
                macroInvocation.index = 0;
            }
            return;
        }

        #define MAXWELL3D_OFFSET(field) U32_OFFSET(Registers, field)
        #define MAXWELL3D_STRUCT_OFFSET(field, member) U32_OFFSET(Registers, field) + U32_OFFSET(typeof(Registers::field), member)
        #define MAXWELL3D_ARRAY_OFFSET(field, index) U32_OFFSET(Registers, field) + ((sizeof(typeof(Registers::field[0])) / sizeof(u32)) * index)
        #define MAXWELL3D_ARRAY_STRUCT_OFFSET(field, index, member) MAXWELL3D_ARRAY_OFFSET(field, index) + U32_OFFSET(typeof(Registers::field[0]), member)
        #define MAXWELL3D_ARRAY_STRUCT_STRUCT_OFFSET(field, index, member, submember) MAXWELL3D_ARRAY_STRUCT_OFFSET(field, index, member) + U32_OFFSET(typeof(Registers::field[0].member), submember)

        #define MAXWELL3D_CASE_BASE(fieldName, fieldAccessor, offset, content) case offset: { \
            auto fieldName{util::BitCast<typeof(registers.fieldAccessor)>(params.argument)};  \
            content                                                                           \
            return;                                                                           \
        }
        #define MAXWELL3D_CASE(field, content) MAXWELL3D_CASE_BASE(field, field, MAXWELL3D_OFFSET(field), content)
        #define MAXWELL3D_STRUCT_CASE(field, member, content) MAXWELL3D_CASE_BASE(member, field.member, MAXWELL3D_STRUCT_OFFSET(field, member), content)
        #define MAXWELL3D_ARRAY_CASE(field, index, content) MAXWELL3D_CASE_BASE(field, field[index], MAXWELL3D_ARRAY_OFFSET(field, index), content)
        #define MAXWELL3D_ARRAY_STRUCT_CASE(field, index, member, content) MAXWELL3D_CASE_BASE(member, field[index].member, MAXWELL3D_ARRAY_STRUCT_OFFSET(field, index, member), content)
        #define MAXWELL3D_ARRAY_STRUCT_STRUCT_CASE(field, index, member, submember, content) MAXWELL3D_CASE_BASE(submember, field[index].member.submember, MAXWELL3D_ARRAY_STRUCT_STRUCT_OFFSET(field, index, member, submember), content)

        if (params.method != MAXWELL3D_OFFSET(mme.shadowRamControl)) {
            if (shadowRegisters.mme.shadowRamControl == type::MmeShadowRamControl::MethodTrack || shadowRegisters.mme.shadowRamControl == type::MmeShadowRamControl::MethodTrackWithFilter)
                shadowRegisters.raw[params.method] = params.argument;
            else if (shadowRegisters.mme.shadowRamControl == type::MmeShadowRamControl::MethodReplay)
                params.argument = shadowRegisters.raw[params.method];
        }

        bool redundant{registers.raw[params.method] == params.argument};
        registers.raw[params.method] = params.argument;

        if (!redundant) {
            switch (params.method) {
                MAXWELL3D_STRUCT_CASE(mme, shadowRamControl, {
                    shadowRegisters.mme.shadowRamControl = shadowRamControl;
                })

                #define RENDER_TARGET_ARRAY(index)                               \
                MAXWELL3D_ARRAY_STRUCT_STRUCT_CASE(renderTargets, index, address, high, { \
                    context.SetRenderTargetAddressHigh(index, high);                      \
                })                                                                        \
                MAXWELL3D_ARRAY_STRUCT_STRUCT_CASE(renderTargets, index, address, low, {  \
                    context.SetRenderTargetAddressLow(index, low);                        \
                })                                                                        \
                MAXWELL3D_ARRAY_STRUCT_CASE(renderTargets, index, width, {                \
                    context.SetRenderTargetWidth(index, width);                           \
                })                                                                        \
                MAXWELL3D_ARRAY_STRUCT_CASE(renderTargets, index, height, {               \
                    context.SetRenderTargetHeight(index, height);                         \
                })                                                                        \
                MAXWELL3D_ARRAY_STRUCT_CASE(renderTargets, index, format, {               \
                    context.SetRenderTargetFormat(index, format);                         \
                })                                                                        \
                MAXWELL3D_ARRAY_STRUCT_CASE(renderTargets, index, tileMode, {             \
                    context.SetRenderTargetTileMode(index, tileMode);                     \
                })                                                                        \
                MAXWELL3D_ARRAY_STRUCT_CASE(renderTargets, index, arrayMode, {            \
                    context.SetRenderTargetArrayMode(index, arrayMode);                   \
                })                                                                        \
                MAXWELL3D_ARRAY_STRUCT_CASE(renderTargets, index, layerStrideLsr2, {      \
                    context.SetRenderTargetLayerStride(index, layerStrideLsr2);           \
                })                                                                        \
                MAXWELL3D_ARRAY_STRUCT_CASE(renderTargets, index, baseLayer, {            \
                    context.SetRenderTargetBaseLayer(index, baseLayer);                   \
                })

                RENDER_TARGET_ARRAY(0)
                RENDER_TARGET_ARRAY(1)
                RENDER_TARGET_ARRAY(2)
                RENDER_TARGET_ARRAY(3)
                RENDER_TARGET_ARRAY(4)
                RENDER_TARGET_ARRAY(5)
                RENDER_TARGET_ARRAY(6)
                RENDER_TARGET_ARRAY(7)
                #undef RENDER_TARGET_ARRAY

                #define VIEWPORT_TRANSFORM_CALLBACKS(index)                                      \
                MAXWELL3D_ARRAY_STRUCT_CASE(viewportTransforms, index, scaleX, {                          \
                    context.SetViewportX(index, scaleX, registers.viewportTransforms[index].translateX);  \
                })                                                                                        \
                MAXWELL3D_ARRAY_STRUCT_CASE(viewportTransforms, index, translateX, {                      \
                    context.SetViewportX(index, registers.viewportTransforms[index].scaleX, translateX);  \
                })                                                                                        \
                MAXWELL3D_ARRAY_STRUCT_CASE(viewportTransforms, index, scaleY, {                          \
                    context.SetViewportY(index, scaleY, registers.viewportTransforms[index].translateY);  \
                })                                                                                        \
                MAXWELL3D_ARRAY_STRUCT_CASE(viewportTransforms, index, translateY, {                      \
                    context.SetViewportY(index, registers.viewportTransforms[index].scaleY, translateY);  \
                })                                                                                        \
                MAXWELL3D_ARRAY_STRUCT_CASE(viewportTransforms, index, scaleZ, {                          \
                    context.SetViewportZ(index, scaleZ, registers.viewportTransforms[index].translateZ);  \
                })                                                                                        \
                MAXWELL3D_ARRAY_STRUCT_CASE(viewportTransforms, index, translateZ, {                      \
                    context.SetViewportZ(index, registers.viewportTransforms[index].scaleZ, translateZ);  \
                })

                VIEWPORT_TRANSFORM_CALLBACKS(0)
                VIEWPORT_TRANSFORM_CALLBACKS(1)
                VIEWPORT_TRANSFORM_CALLBACKS(2)
                VIEWPORT_TRANSFORM_CALLBACKS(3)
                VIEWPORT_TRANSFORM_CALLBACKS(4)
                VIEWPORT_TRANSFORM_CALLBACKS(5)
                VIEWPORT_TRANSFORM_CALLBACKS(6)
                VIEWPORT_TRANSFORM_CALLBACKS(7)
                VIEWPORT_TRANSFORM_CALLBACKS(8)
                VIEWPORT_TRANSFORM_CALLBACKS(9)
                VIEWPORT_TRANSFORM_CALLBACKS(10)
                VIEWPORT_TRANSFORM_CALLBACKS(11)
                VIEWPORT_TRANSFORM_CALLBACKS(12)
                VIEWPORT_TRANSFORM_CALLBACKS(13)
                VIEWPORT_TRANSFORM_CALLBACKS(14)
                VIEWPORT_TRANSFORM_CALLBACKS(15)
                #undef VIEWPORT_TRANSFORM_CALLBACKS

                #define COLOR_CLEAR_CALLBACKS(index)              \
                MAXWELL3D_ARRAY_CASE(clearColorValue, index, {             \
                    context.UpdateClearColorValue(index, clearColorValue); \
                })

                COLOR_CLEAR_CALLBACKS(0)
                COLOR_CLEAR_CALLBACKS(1)
                COLOR_CLEAR_CALLBACKS(2)
                COLOR_CLEAR_CALLBACKS(3)
                #undef COLOR_CLEAR_CALLBACKS

                #define SCISSOR_CALLBACKS(index)                                                           \
                MAXWELL3D_ARRAY_STRUCT_CASE(scissors, index, enable, {                                              \
                    context.SetScissor(index, enable ? registers.scissors[index] : std::optional<type::Scissor>{}); \
                })                                                                                                  \
                MAXWELL3D_ARRAY_STRUCT_CASE(scissors, index, horizontal, {                                          \
                    context.SetScissorHorizontal(index, horizontal);                                                \
                })                                                                                                  \
                MAXWELL3D_ARRAY_STRUCT_CASE(scissors, index, vertical, {                                            \
                    context.SetScissorVertical(index, vertical);                                                    \
                })

                SCISSOR_CALLBACKS(0)
                SCISSOR_CALLBACKS(1)
                SCISSOR_CALLBACKS(2)
                SCISSOR_CALLBACKS(3)
                SCISSOR_CALLBACKS(4)
                SCISSOR_CALLBACKS(5)
                SCISSOR_CALLBACKS(6)
                SCISSOR_CALLBACKS(7)
                SCISSOR_CALLBACKS(8)
                SCISSOR_CALLBACKS(9)
                SCISSOR_CALLBACKS(10)
                SCISSOR_CALLBACKS(11)
                SCISSOR_CALLBACKS(12)
                SCISSOR_CALLBACKS(13)
                SCISSOR_CALLBACKS(14)
                SCISSOR_CALLBACKS(15)
                #undef SCISSOR_CALLBACKS

                MAXWELL3D_CASE(renderTargetControl, {
                    context.UpdateRenderTargetControl(registers.renderTargetControl);
                })
            }
        }

        switch (params.method) {
            MAXWELL3D_STRUCT_CASE(mme, instructionRamLoad, {
                if (registers.mme.instructionRamPointer >= macroCode.size())
                    throw exception("Macro memory is full!");

                macroCode[registers.mme.instructionRamPointer++] = params.argument;
            })

            MAXWELL3D_STRUCT_CASE(mme, startAddressRamLoad, {
                if (registers.mme.startAddressRamPointer >= macroPositions.size())
                    throw exception("Maximum amount of macros reached!");

                macroPositions[registers.mme.startAddressRamPointer++] = params.argument;
            })

            MAXWELL3D_CASE(syncpointAction, {
                state.logger->Debug("Increment syncpoint: {}", static_cast<u16>(syncpointAction.id));
                state.soc->host1x.syncpoints.at(syncpointAction.id).Increment();
            })

            MAXWELL3D_CASE(clearBuffers, {
                context.ClearBuffers(registers.clearBuffers);
            })

            MAXWELL3D_STRUCT_CASE(semaphore, info, {
                switch (info.op) {
                    case type::SemaphoreInfo::Op::Release:
                        WriteSemaphoreResult(registers.semaphore.payload);
                        break;

                    case type::SemaphoreInfo::Op::Counter: {
                        switch (info.counterType) {
                            case type::SemaphoreInfo::CounterType::Zero:
                                WriteSemaphoreResult(0);
                                break;

                            default:
                                state.logger->Warn("Unsupported semaphore counter type: 0x{:X}", static_cast<u8>(info.counterType));
                                break;
                        }
                        break;
                    }

                    default:
                        state.logger->Warn("Unsupported semaphore operation: 0x{:X}", static_cast<u8>(info.op));
                        break;
                }
            })

            MAXWELL3D_ARRAY_CASE(firmwareCall, 4, {
                registers.raw[0xD00] = 1;
            })
        }

        #undef MAXWELL3D_OFFSET
        #undef MAXWELL3D_STRUCT_OFFSET
        #undef MAXWELL3D_ARRAY_OFFSET
        #undef MAXWELL3D_ARRAY_STRUCT_OFFSET
        #undef MAXWELL3D_ARRAY_STRUCT_STRUCT_OFFSET

        #undef MAXWELL3D_CASE_BASE
        #undef MAXWELL3D_CASE
        #undef MAXWELL3D_STRUCT_CASE
        #undef MAXWELL3D_ARRAY_CASE
        #undef MAXWELL3D_ARRAY_STRUCT_CASE
        #undef MAXWELL3D_ARRAY_STRUCT_STRUCT_CASE
    }

    void Maxwell3D::WriteSemaphoreResult(u64 result) {
        struct FourWordResult {
            u64 value;
            u64 timestamp;
        };

        switch (registers.semaphore.info.structureSize) {
            case type::SemaphoreInfo::StructureSize::OneWord:
                state.soc->gmmu.Write<u32>(static_cast<u32>(result), registers.semaphore.address.Pack());
                break;

            case type::SemaphoreInfo::StructureSize::FourWords: {
                // Convert the current nanosecond time to GPU ticks
                constexpr u64 NsToTickNumerator{384};
                constexpr u64 NsToTickDenominator{625};

                u64 nsTime{util::GetTimeNs()};
                u64 timestamp{(nsTime / NsToTickDenominator) * NsToTickNumerator + ((nsTime % NsToTickDenominator) * NsToTickNumerator) / NsToTickDenominator};

                state.soc->gmmu.Write<FourWordResult>(FourWordResult{result, timestamp}, registers.semaphore.address.Pack());
                break;
            }
        }
    }
}
