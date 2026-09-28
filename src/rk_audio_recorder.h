#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hdra {

class RkAudioRecorder {
public:
    RkAudioRecorder(bool denoise, std::string vqe_config_path);
    ~RkAudioRecorder();

    RkAudioRecorder(const RkAudioRecorder &) = delete;
    RkAudioRecorder &operator=(const RkAudioRecorder &) = delete;

    void initialize();
    bool read_frame(std::vector<uint8_t> &data, int timeout_ms);

private:
    void cleanup() noexcept;
    void check(int result, const char *operation);

    bool denoise_;
    std::string vqe_config_path_;
    bool sys_initialized_ = false;
    bool attr_set_ = false;
    bool device_enabled_ = false;
    bool channel_enabled_ = false;
    bool vqe_enabled_ = false;
    bool loopback_changed_ = false;
    bool format_reported_ = false;
    std::string previous_loopback_;
};

}  // namespace hdra
