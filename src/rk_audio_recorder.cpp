#include "rk_audio_recorder.h"

#include "audio_frame.h"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

extern "C" {
#include "rk_mpi_ai.h"
#include "rk_mpi_amix.h"
#include "rk_mpi_mb.h"
#include "rk_mpi_sys.h"
}

namespace hdra {
namespace {

constexpr AUDIO_DEV kAiDevice = 0;
constexpr AI_CHN kAiChannel = 0;
constexpr int kSampleRate = 16000;
constexpr int kFrameSamples = 256;

std::string trim_control_value(const char *value) {
    std::string result = value ? value : "";
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' ')) {
        result.pop_back();
    }
    const auto quote = result.find('\'');
    if (quote != std::string::npos) {
        const auto end = result.find('\'', quote + 1);
        if (end != std::string::npos) result = result.substr(quote + 1, end - quote - 1);
    }
    return result;
}

}  // namespace

RkAudioRecorder::RkAudioRecorder(bool denoise, std::string vqe_config_path)
    : denoise_(denoise), vqe_config_path_(std::move(vqe_config_path)) {}

RkAudioRecorder::~RkAudioRecorder() {
    cleanup();
}

void RkAudioRecorder::check(int result, const char *operation) {
    if (result == RK_SUCCESS) return;
    std::ostringstream message;
    message << operation << " failed: 0x" << std::hex << static_cast<uint32_t>(result);
    throw std::runtime_error(message.str());
}

void RkAudioRecorder::initialize() {
    if (sys_initialized_) throw std::runtime_error("audio recorder is already initialized");
    if (denoise_ && ::access(vqe_config_path_.c_str(), R_OK) != 0) {
        throw std::runtime_error("VQE configuration is not readable: " + vqe_config_path_);
    }

    try {
        check(RK_MPI_SYS_Init(), "RK_MPI_SYS_Init");
        sys_initialized_ = true;

        AIO_ATTR_S attr {};
        std::snprintf(reinterpret_cast<char *>(attr.u8CardName), sizeof(attr.u8CardName), "%s", "hw:0,0");
        attr.soundCard.channels = 2;
        attr.soundCard.sampleRate = kSampleRate;
        attr.soundCard.bitWidth = AUDIO_BIT_WIDTH_16;
        attr.enSamplerate = AUDIO_SAMPLE_RATE_16000;
        attr.enBitwidth = AUDIO_BIT_WIDTH_16;
        attr.enSoundmode = AUDIO_SOUND_MODE_MONO;
        attr.u32FrmNum = 4;
        attr.u32PtNumPerFrm = kFrameSamples;
        attr.u32EXFlag = 0;
        attr.u32ChnCnt = 2;
        check(RK_MPI_AI_SetPubAttr(kAiDevice, &attr), "RK_MPI_AI_SetPubAttr");
        attr_set_ = true;

        check(RK_MPI_AI_Enable(kAiDevice), "RK_MPI_AI_Enable");
        device_enabled_ = true;

        AI_CHN_PARAM_S channel_params {};
        channel_params.s32UsrFrmDepth = 4;
        channel_params.enLoopbackMode = AUDIO_LOOPBACK_NONE;
        check(RK_MPI_AI_SetChnParam(kAiDevice, kAiChannel, &channel_params), "RK_MPI_AI_SetChnParam");

        if (denoise_) {
            char current_loopback[128] = {};
            check(RK_MPI_AMIX_GetControl(kAiDevice, "I2STDM Digital Loopback Mode", current_loopback),
                  "RK_MPI_AMIX_GetControl(loopback)");
            previous_loopback_ = trim_control_value(current_loopback);
            if (previous_loopback_.empty()) previous_loopback_ = "Disabled";

            char mode2[] = "Mode2";
            check(RK_MPI_AMIX_SetControl(kAiDevice, "I2STDM Digital Loopback Mode", mode2),
                  "RK_MPI_AMIX_SetControl(loopback)");
            loopback_changed_ = true;

            AI_VQE_MOD_ENABLE_S modules {};
            modules.bAnr = RK_TRUE;
            check(RK_MPI_AI_SetVqeModuleEnable(kAiDevice, kAiChannel, &modules),
                  "RK_MPI_AI_SetVqeModuleEnable(ANR)");

            AI_VQE_CONFIG_S vqe {};
            vqe.enCfgMode = AIO_VQE_CONFIG_LOAD_FILE;
            if (vqe_config_path_.size() >= sizeof(vqe.aCfgFile)) {
                throw std::runtime_error("VQE configuration path is too long");
            }
            std::memcpy(vqe.aCfgFile, vqe_config_path_.c_str(), vqe_config_path_.size() + 1);
            vqe.s32WorkSampleRate = kSampleRate;
            vqe.s32FrameSample = kFrameSamples;
            vqe.s64RefChannelType = 0x2;
            vqe.s64RecChannelType = 0x1;
            vqe.s64ChannelLayoutType = 0x3;
            check(RK_MPI_AI_SetVqeAttr(kAiDevice, kAiChannel, 0, 0, &vqe), "RK_MPI_AI_SetVqeAttr");
            check(RK_MPI_AI_EnableVqe(kAiDevice, kAiChannel), "RK_MPI_AI_EnableVqe");
            vqe_enabled_ = true;
        }

        check(RK_MPI_AI_EnableChn(kAiDevice, kAiChannel), "RK_MPI_AI_EnableChn");
        channel_enabled_ = true;
        check(RK_MPI_AI_SetTrackMode(kAiDevice, AUDIO_TRACK_FRONT_LEFT), "RK_MPI_AI_SetTrackMode");
    } catch (...) {
        cleanup();
        throw;
    }
}

bool RkAudioRecorder::read_frame(std::vector<uint8_t> &data, int timeout_ms) {
    AUDIO_FRAME_S frame {};
    const RK_S32 result = RK_MPI_AI_GetFrame(kAiDevice, kAiChannel, &frame, RK_NULL, timeout_ms);
    if (result != RK_SUCCESS) return false;

    try {
        void *address = RK_MPI_MB_Handle2VirAddr(frame.pMbBlk);
        if (address == nullptr && frame.u32Len != 0) throw std::runtime_error("audio frame has no mapped data");
        if (!format_reported_) {
            std::cout << "Frame format: frame-bytes=" << frame.u32Len
                      << ", buffer-bytes=" << RK_MPI_MB_GetSize(frame.pMbBlk)
                      << ", sound-mode=" << static_cast<int>(frame.enSoundMode)
                      << ", sample-rate=" << frame.s32SampleRate << '\n';
            format_reported_ = true;
        }
        const auto *bytes = static_cast<const uint8_t *>(address);
        if (frame.enSoundMode == AUDIO_SOUND_MODE_MONO) {
            copy_mono_s16(bytes, frame.u32Len, data);
        } else if (frame.enSoundMode == AUDIO_SOUND_MODE_STEREO) {
            // RK MPI returns interleaved 16-bit mic/ref samples. Keep the processed mic (left) channel.
            extract_left_s16(bytes, frame.u32Len, data);
        } else {
            throw std::runtime_error("unsupported audio channel mode: " +
                                     std::to_string(static_cast<int>(frame.enSoundMode)));
        }
    } catch (...) {
        RK_MPI_AI_ReleaseFrame(kAiDevice, kAiChannel, &frame, RK_NULL);
        throw;
    }
    check(RK_MPI_AI_ReleaseFrame(kAiDevice, kAiChannel, &frame, RK_NULL), "RK_MPI_AI_ReleaseFrame");
    return true;
}

void RkAudioRecorder::cleanup() noexcept {
    if (vqe_enabled_) {
        RK_MPI_AI_DisableVqe(kAiDevice, kAiChannel);
        vqe_enabled_ = false;
    }
    if (channel_enabled_) {
        RK_MPI_AI_DisableChn(kAiDevice, kAiChannel);
        channel_enabled_ = false;
    }
    if (device_enabled_) {
        RK_MPI_AI_Disable(kAiDevice);
        device_enabled_ = false;
    }
    if (attr_set_) {
        RK_MPI_AI_ClrPubAttr(kAiDevice);
        attr_set_ = false;
    }
    if (loopback_changed_) {
        if (!previous_loopback_.empty()) {
            RK_MPI_AMIX_SetControl(kAiDevice, "I2STDM Digital Loopback Mode", previous_loopback_.data());
        }
        loopback_changed_ = false;
    }
    if (sys_initialized_) {
        RK_MPI_SYS_Exit();
        sys_initialized_ = false;
    }
}

}  // namespace hdra
