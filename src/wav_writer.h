#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace hdra {

class WavWriter {
public:
    WavWriter(std::string output_path, uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample);
    ~WavWriter();

    WavWriter(const WavWriter &) = delete;
    WavWriter &operator=(const WavWriter &) = delete;

    void write(const void *data, size_t size);
    void finalize();

    uint64_t data_bytes() const { return data_bytes_; }
    const std::string &partial_path() const { return partial_path_; }

private:
    void update_header(bool sync_to_disk);
    void close_fd();

    std::string output_path_;
    std::string partial_path_;
    uint32_t sample_rate_;
    uint16_t channels_;
    uint16_t bits_per_sample_;
    int fd_ = -1;
    uint64_t data_bytes_ = 0;
    uint64_t next_header_update_ = 0;
    bool finalized_ = false;
};

void validate_output_path(const std::string &output_path);
void ensure_space_for_duration(const std::string &output_path, uint64_t required_bytes);

}  // namespace hdra
