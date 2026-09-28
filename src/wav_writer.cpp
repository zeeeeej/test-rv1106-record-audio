#include "wav_writer.h"

#include <cerrno>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace hdra {
namespace {

constexpr uint64_t kWavHeaderSize = 44;
constexpr uint64_t kMaxWavDataSize = UINT32_MAX - 36ULL;
constexpr uint64_t kSpaceReserve = 1024ULL * 1024ULL;

std::runtime_error system_error(const std::string &operation, const std::string &path) {
    return std::runtime_error(operation + " '" + path + "': " + std::strerror(errno));
}

void put_u16(uint8_t *dst, uint16_t value) {
    dst[0] = static_cast<uint8_t>(value);
    dst[1] = static_cast<uint8_t>(value >> 8);
}

void put_u32(uint8_t *dst, uint32_t value) {
    dst[0] = static_cast<uint8_t>(value);
    dst[1] = static_cast<uint8_t>(value >> 8);
    dst[2] = static_cast<uint8_t>(value >> 16);
    dst[3] = static_cast<uint8_t>(value >> 24);
}

std::string parent_directory(const std::string &path) {
    const auto slash = path.find_last_of('/');
    if (slash == std::string::npos) return ".";
    if (slash == 0) return "/";
    return path.substr(0, slash);
}

bool path_exists(const std::string &path) {
    struct stat st {};
    if (::lstat(path.c_str(), &st) == 0) return true;
    if (errno == ENOENT) return false;
    throw system_error("cannot inspect", path);
}

void write_all(int fd, const void *buffer, size_t size, const std::string &path) {
    const auto *cursor = static_cast<const uint8_t *>(buffer);
    while (size > 0) {
        const ssize_t written = ::write(fd, cursor, size);
        if (written < 0) {
            if (errno == EINTR) continue;
            throw system_error("cannot write", path);
        }
        if (written == 0) throw std::runtime_error("short write to '" + path + "'");
        cursor += written;
        size -= static_cast<size_t>(written);
    }
}

}  // namespace

void validate_output_path(const std::string &output_path) {
    if (output_path.empty()) throw std::runtime_error("output path is empty");
    if (path_exists(output_path)) throw std::runtime_error("output already exists: " + output_path);
    const std::string partial = output_path + ".part";
    if (path_exists(partial)) throw std::runtime_error("partial output already exists: " + partial);

    struct stat st {};
    const std::string parent = parent_directory(output_path);
    if (::stat(parent.c_str(), &st) != 0) throw system_error("cannot inspect output directory", parent);
    if (!S_ISDIR(st.st_mode)) throw std::runtime_error("output parent is not a directory: " + parent);
}

void ensure_space_for_duration(const std::string &output_path, uint64_t required_bytes) {
    struct statvfs fs {};
    const std::string parent = parent_directory(output_path);
    if (::statvfs(parent.c_str(), &fs) != 0) throw system_error("cannot query free space", parent);
    const uint64_t available = static_cast<uint64_t>(fs.f_bavail) * fs.f_frsize;
    if (required_bytes > UINT64_MAX - kSpaceReserve || available < required_bytes + kSpaceReserve) {
        throw std::runtime_error("insufficient free space for requested duration");
    }
}

WavWriter::WavWriter(std::string output_path, uint32_t sample_rate, uint16_t channels,
                     uint16_t bits_per_sample)
    : output_path_(std::move(output_path)),
      partial_path_(output_path_ + ".part"),
      sample_rate_(sample_rate),
      channels_(channels),
      bits_per_sample_(bits_per_sample) {
    if (sample_rate_ == 0 || channels_ == 0 || bits_per_sample_ == 0 || bits_per_sample_ % 8 != 0) {
        throw std::invalid_argument("invalid WAV format");
    }
    validate_output_path(output_path_);
    fd_ = ::open(partial_path_.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd_ < 0) throw system_error("cannot create", partial_path_);
    try {
        update_header(false);
        if (::lseek(fd_, static_cast<off_t>(kWavHeaderSize), SEEK_SET) < 0) {
            throw system_error("cannot seek", partial_path_);
        }
        next_header_update_ = static_cast<uint64_t>(sample_rate_) * channels_ * (bits_per_sample_ / 8);
    } catch (...) {
        close_fd();
        throw;
    }
}

WavWriter::~WavWriter() {
    if (fd_ >= 0 && !finalized_) {
        try {
            update_header(true);
        } catch (...) {
        }
        close_fd();
    }
}

void WavWriter::write(const void *data, size_t size) {
    if (fd_ < 0 || finalized_) throw std::runtime_error("WAV writer is closed");
    if (size == 0) return;
    if (data_bytes_ + size > kMaxWavDataSize) throw std::runtime_error("WAV exceeds RIFF 4 GiB limit");
    write_all(fd_, data, size, partial_path_);
    data_bytes_ += size;
    if (data_bytes_ >= next_header_update_) {
        update_header(false);
        const uint64_t per_second = static_cast<uint64_t>(sample_rate_) * channels_ * (bits_per_sample_ / 8);
        next_header_update_ = data_bytes_ + per_second;
    }
}

void WavWriter::update_header(bool sync_to_disk) {
    uint8_t header[kWavHeaderSize] = {};
    std::memcpy(header + 0, "RIFF", 4);
    put_u32(header + 4, static_cast<uint32_t>(36 + data_bytes_));
    std::memcpy(header + 8, "WAVE", 4);
    std::memcpy(header + 12, "fmt ", 4);
    put_u32(header + 16, 16);
    put_u16(header + 20, 1);
    put_u16(header + 22, channels_);
    put_u32(header + 24, sample_rate_);
    const uint32_t byte_rate = sample_rate_ * channels_ * (bits_per_sample_ / 8);
    put_u32(header + 28, byte_rate);
    put_u16(header + 32, channels_ * (bits_per_sample_ / 8));
    put_u16(header + 34, bits_per_sample_);
    std::memcpy(header + 36, "data", 4);
    put_u32(header + 40, static_cast<uint32_t>(data_bytes_));

    size_t remaining = sizeof(header);
    const uint8_t *cursor = header;
    off_t offset = 0;
    while (remaining > 0) {
        const ssize_t written = ::pwrite(fd_, cursor, remaining, offset);
        if (written < 0) {
            if (errno == EINTR) continue;
            throw system_error("cannot update WAV header", partial_path_);
        }
        if (written == 0) throw std::runtime_error("short WAV header write");
        cursor += written;
        remaining -= static_cast<size_t>(written);
        offset += written;
    }
    if (sync_to_disk && ::fsync(fd_) != 0) throw system_error("cannot sync", partial_path_);
}

void WavWriter::finalize() {
    if (finalized_) return;
    if (fd_ < 0) throw std::runtime_error("WAV writer is closed");
    update_header(true);
    close_fd();

    // link() gives no-overwrite semantics; output and .part are always on the same filesystem.
    if (::link(partial_path_.c_str(), output_path_.c_str()) != 0) {
        throw system_error("cannot publish recording", output_path_);
    }
    if (::unlink(partial_path_.c_str()) != 0) {
        const int saved = errno;
        ::unlink(output_path_.c_str());
        errno = saved;
        throw system_error("cannot remove partial recording", partial_path_);
    }
    finalized_ = true;
}

void WavWriter::close_fd() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

}  // namespace hdra
