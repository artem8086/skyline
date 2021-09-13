// SPDX-License-Identifier: MIT OR MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <kernel/types/KProcess.h>
#include "INvDrvServices.h"
#include "driver.h"
#include "devices/nvdevice.h"

#define NVRESULT(x) [&response, this](NvResult err) {     \
        if (err != NvResult::Success)                     \
            state.logger->Debug("IOCTL Failed: {}", err); \
                                                          \
        response.Push<NvResult>(err);                     \
        return Result{};                                  \
    } (x)

namespace skyline::service::nvdrv {
    INvDrvServices::INvDrvServices(const DeviceState &state, ServiceManager &manager, Driver &driver, const SessionPermissions &perms) : BaseService(state, manager), driver(driver), ctx(SessionContext{.perms = perms}) {}

    Result INvDrvServices::Open(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        constexpr FileDescriptor SessionFdLimit{sizeof(u64) * 2 * 8}; //!< Nvdrv uses two 64 bit variables to store a bitset

        auto path{request.inputBuf.at(0).as_string(true)};
        if (path.empty() || nextFdIndex == SessionFdLimit) {
            response.Push<FileDescriptor>(InvalidFileDescriptor);
            return NVRESULT(NvResult::FileOperationFailed);
        }

        if (auto err{driver.OpenDevice(path, nextFdIndex, ctx)}; err != NvResult::Success) {
            response.Push<FileDescriptor>(InvalidFileDescriptor);
            return NVRESULT(err);
        }

        response.Push(nextFdIndex++);
        return NVRESULT(NvResult::Success);
    }

    static NvResultValue<span<u8>> GetMainIoctlBuffer(IoctlDescriptor ioctl, std::vector<span<u8>> &inBuf, std::vector<span<u8>> &outBuf) {
        if (ioctl.in && (inBuf.empty() || inBuf[0].size() < ioctl.size))
            return NvResult::InvalidSize;

        if (ioctl.out && (outBuf.empty() || outBuf[0].size() < ioctl.size))
            return NvResult::InvalidSize;

        if (ioctl.in && ioctl.out) {
            auto in{inBuf[0]};
            auto out{outBuf[0]};

            if (out.size() < in.size())
                return NvResult::InvalidSize;

            // Copy in buf to out buf for inout ioctls to avoid needing to pass around two buffers everywhere
            if (out.data() != in.data())
                out.copy_from(in, ioctl.size);

            return out;
        }

        return ioctl.out ? outBuf[0] : inBuf[0];
    }

    Result INvDrvServices::Ioctl(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        auto fd{request.Pop<FileDescriptor>()};
        auto ioctl{request.Pop<IoctlDescriptor>()};

        auto buf{GetMainIoctlBuffer(ioctl, request.inputBuf, request.outputBuf)};
        if (!buf)
            return NVRESULT(buf);
        else
            return NVRESULT(driver.Ioctl(fd, ioctl, *buf));
    }

    Result INvDrvServices::Close(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        auto fd{request.Pop<u32>()};
        state.logger->Debug("Closing NVDRV device ({})", fd);

        driver.CloseDevice(fd);

        return NVRESULT(NvResult::Success);
    }

    Result INvDrvServices::Initialize(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        return NVRESULT(NvResult::Success);
    }

    Result INvDrvServices::QueryEvent(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        auto fd{request.Pop<u32>()};
        auto eventId{request.Pop<u32>()};

        auto event{driver.QueryEvent(fd, eventId)};

        if (event != nullptr) {
            auto handle{state.process->InsertItem<type::KEvent>(event)};

            state.logger->Debug("FD: {}, Event ID: {}, Handle: 0x{:X}", fd, eventId, handle);
            response.copyHandles.push_back(handle);

            return NVRESULT(NvResult::Success);
        } else {
            return NVRESULT(NvResult::BadValue);
        }
    }

    Result INvDrvServices::Ioctl2(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        auto fd{request.Pop<FileDescriptor>()};
        auto ioctl{request.Pop<IoctlDescriptor>()};

        // The inline buffer is technically not required
        auto inlineBuf{request.inputBuf.size() > 1 ? request.inputBuf.at(1) : span<u8>{}};

        auto buf{GetMainIoctlBuffer(ioctl, request.inputBuf, request.outputBuf)};
        if (!buf)
            return NVRESULT(buf);
        else
            return NVRESULT(driver.Ioctl2(fd, ioctl, *buf, inlineBuf));
    }

    Result INvDrvServices::Ioctl3(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        auto fd{request.Pop<FileDescriptor>()};
        auto ioctl{request.Pop<IoctlDescriptor>()};

        // The inline buffer is technically not required
        auto inlineBuf{request.outputBuf.size() > 1 ? request.outputBuf.at(1) : span<u8>{}};

        auto buf{GetMainIoctlBuffer(ioctl, request.inputBuf, request.outputBuf)};
        if (!buf)
            return NVRESULT(buf);
        else
            return NVRESULT(driver.Ioctl3(fd, ioctl, *buf, inlineBuf));
    }

    Result INvDrvServices::SetAruid(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        return NVRESULT(NvResult::Success);
    }


    Result INvDrvServices::SetGraphicsFirmwareMemoryMarginEnabled(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        return {};
    }
}
