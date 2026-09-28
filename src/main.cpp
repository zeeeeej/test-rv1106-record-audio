#include "cli.h"
#include "rk_audio_recorder.h"
#include "wav_writer.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits.h>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

constexpr uint32_t kSampleRate = 16000;
constexpr uint16_t kChannels = 1;
constexpr uint16_t kBitsPerSample = 16;
constexpr uint64_t kBytesPerSample = kBitsPerSample / 8;
constexpr const char *kVersion = "1.0.0";
volatile std::sig_atomic_t g_stop_requested = 0;

void signal_handler(int) {
    g_stop_requested = 1;
}

std::string dirname(const std::string &path) {
    const auto slash = path.find_last_of('/');
    if (slash == std::string::npos) return ".";
    if (slash == 0) return "/";
    return path.substr(0, slash);
}

std::string default_vqe_config(const char *argv0) {
    char executable[PATH_MAX] = {};
    const ssize_t size = ::readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    std::string path;
    if (size > 0) {
        executable[size] = '\0';
        path = executable;
    } else {
        path = argv0;
    }
    const std::string prefix = dirname(dirname(path));
    return prefix + "/share/hd-record-audio/config_aivqe_anr.json";
}

uint64_t target_data_bytes(double duration) {
    const long double samples = std::floor(static_cast<long double>(duration) * kSampleRate);
    if (samples < 1.0L) throw hdra::CliError("duration is shorter than one sample");
    const long double bytes = samples * kBytesPerSample;
    if (bytes > static_cast<long double>(UINT32_MAX - 36U)) {
        throw hdra::CliError("duration exceeds the WAV RIFF size limit");
    }
    return static_cast<uint64_t>(bytes);
}

}  // namespace

int main(int argc, const char *argv[]) {
    hdra::Options options;
    try {
        options = hdra::parse_cli(argc, argv);
    } catch (const hdra::CliError &error) {
        std::cerr << "Argument error: " << error.what() << "\n\n" << hdra::usage(argv[0]);
        return 2;
    }

    if (options.show_help) {
        std::cout << hdra::usage(argv[0]);
        return 0;
    }
    if (options.show_version) {
        std::cout << "hd_record_audio " << kVersion << '\n';
        return 0;
    }
    if (options.vqe_config_path.empty()) options.vqe_config_path = default_vqe_config(argv[0]);

    uint64_t requested_bytes = 0;
    try {
        hdra::validate_output_path(options.output_path);
        if (options.duration_seconds) {
            requested_bytes = target_data_bytes(*options.duration_seconds);
            hdra::ensure_space_for_duration(options.output_path, requested_bytes + 44);
        }
    } catch (const hdra::CliError &error) {
        std::cerr << "Argument error: " << error.what() << '\n';
        return 2;
    } catch (const std::exception &error) {
        std::cerr << "Output error: " << error.what() << '\n';
        return 4;
    }

    struct sigaction action {};
    action.sa_handler = signal_handler;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);

    try {
        hdra::RkAudioRecorder recorder(options.denoise, options.vqe_config_path);
        try {
            recorder.initialize();
        } catch (const std::exception &error) {
            std::cerr << "Audio initialization failed: " << error.what() << '\n';
            return 3;
        }

        hdra::WavWriter writer(options.output_path, kSampleRate, kChannels, kBitsPerSample);
        std::cout << "Recording 16000 Hz, 16-bit, mono; ANR="
                  << (options.denoise ? "enabled" : "disabled") << '\n';

        std::vector<uint8_t> frame;
        unsigned int consecutive_timeouts = 0;
        while (!g_stop_requested && (requested_bytes == 0 || writer.data_bytes() < requested_bytes)) {
            if (!recorder.read_frame(frame, 200)) {
                if (++consecutive_timeouts >= 10) {
                    throw std::runtime_error("no audio frames received for 2 seconds");
                }
                continue;
            }
            consecutive_timeouts = 0;
            size_t write_size = frame.size();
            if (requested_bytes != 0) {
                const uint64_t remaining = requested_bytes - writer.data_bytes();
                write_size = static_cast<size_t>(std::min<uint64_t>(write_size, remaining));
            }
            write_size -= write_size % kBytesPerSample;
            writer.write(frame.data(), write_size);
        }

        writer.finalize();
        const double seconds = static_cast<double>(writer.data_bytes()) /
                               (kSampleRate * kChannels * (kBitsPerSample / 8));
        std::cout << "Saved " << options.output_path << " (" << seconds << " seconds)\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Recording failed: " << error.what() << '\n';
        return 4;
    }
}
