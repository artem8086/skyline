// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <android/log.h>
#include "common.h"
#include "nce.h"
#include "soc.h"
#include "gpu.h"
#include "audio.h"
#include "input.h"
#include "kernel/types/KThread.h"

namespace skyline {
    Logger::Logger(const std::string &path, LogLevel configLevel) : configLevel(configLevel), start(util::GetTimeNs() / constant::NsInMillisecond) {
        logFile.open(path, std::ios::trunc);
        UpdateTag();
        Write(LogLevel::Info, "Logging started");
    }

    Logger::~Logger() {
        Write(LogLevel::Info, "Logging ended");
        logFile.flush();
    }

    thread_local static std::string logTag, threadName;

    void Logger::UpdateTag() {
        std::array<char, 16> name;
        if (!pthread_getname_np(pthread_self(), name.data(), name.size()))
            threadName = name.data();
        else
            threadName = "unk";
        logTag = std::string("emu-cpp-") + threadName;
    }

    void Logger::Write(LogLevel level, const std::string &str) {
        constexpr std::array<char, 5> levelCharacter{'E', 'W', 'I', 'D', 'V'}; // The LogLevel as written out to a file
        constexpr std::array<int, 5> levelAlog{ANDROID_LOG_ERROR, ANDROID_LOG_WARN, ANDROID_LOG_INFO, ANDROID_LOG_DEBUG, ANDROID_LOG_VERBOSE}; // This corresponds to LogLevel and provides its equivalent for NDK Logging

        if (logTag.empty())
            UpdateTag();

        __android_log_write(levelAlog[static_cast<u8>(level)], logTag.c_str(), str.c_str());

        std::lock_guard guard(mutex);
        logFile << '\036' << levelCharacter[static_cast<u8>(level)] << '\035' << std::dec << (util::GetTimeNs() / constant::NsInMillisecond) - start << '\035' << threadName << '\035' << str << '\n'; // We use RS (\036) and GS (\035) as our delimiters
    }

    DeviceState::DeviceState(kernel::OS *os, std::shared_ptr<JvmManager> jvmManager, std::shared_ptr<Settings> settings, std::shared_ptr<Logger> logger)
        : os(os), jvm(std::move(jvmManager)), settings(std::move(settings)), logger(std::move(logger)) {
        // We assign these later as they use the state in their constructor and we don't want null pointers
        gpu = std::make_shared<gpu::GPU>(*this);
        soc = std::make_shared<soc::SOC>(*this);
        audio = std::make_shared<audio::Audio>(*this);
        nce = std::make_shared<nce::NCE>(*this);
        scheduler = std::make_shared<kernel::Scheduler>(*this);
        input = std::make_shared<input::Input>(*this);
    }
}
