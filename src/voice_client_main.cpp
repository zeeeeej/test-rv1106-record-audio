#include "rk_audio_recorder.h"
#include "voice_client.h"
#include "voice_client_cli.h"
#include "wav_writer.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits.h>
#include <poll.h>
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
    return dirname(dirname(path)) + "/share/hd-record-audio/config_aivqe_anr.json";
}

uint64_t target_data_bytes(double duration) {
    const long double samples = std::floor(static_cast<long double>(duration) * kSampleRate);
    if (samples < 1.0L) throw hdra::VoiceCliError("duration is shorter than one sample");
    const long double bytes = samples * kBytesPerSample;
    if (bytes > static_cast<long double>(UINT32_MAX - 36U)) {
        throw hdra::VoiceCliError("duration exceeds the WAV RIFF size limit");
    }
    return static_cast<uint64_t>(bytes);
}

bool stdin_requested_stop() {
    struct pollfd descriptor {};
    descriptor.fd = STDIN_FILENO;
    descriptor.events = POLLIN;
    const int result = ::poll(&descriptor, 1, 0);
    if (result < 0) {
        if (errno == EINTR) return false;
        throw std::runtime_error(std::string("cannot poll standard input: ") + std::strerror(errno));
    }
    if (result > 0 && (descriptor.revents & POLLIN)) {
        char buffer[256];
        while (::read(STDIN_FILENO, buffer, sizeof(buffer)) < 0 && errno == EINTR) {
        }
        return true;
    }
    return false;
}

void record_command(const hdra::VoiceClientOptions &options, uint64_t requested_bytes) {
    hdra::RkAudioRecorder recorder(options.denoise, options.vqe_config_path);
    recorder.initialize();
    hdra::WavWriter writer(options.output_path, kSampleRate, kChannels, kBitsPerSample);
    std::cout << "Recording 16000 Hz, 16-bit, mono; ANR="
              << (options.denoise ? "enabled" : "disabled") << '\n';
    if (!options.duration_seconds) {
        std::cout << "Press Enter to stop recording and send the command.\n";
    }

    std::vector<uint8_t> frame;
    unsigned int consecutive_timeouts = 0;
    while (!g_stop_requested && (requested_bytes == 0 || writer.data_bytes() < requested_bytes)) {
        if (stdin_requested_stop()) break;
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
    if (writer.data_bytes() == 0) throw std::runtime_error("recording contains no audio frames");
    writer.finalize();
    const double seconds = static_cast<double>(writer.data_bytes()) /
                           (kSampleRate * kChannels * (kBitsPerSample / 8));
    std::cout << "Saved " << options.output_path << " (" << seconds << " seconds)\n";
}

}  // namespace

int main(int argc, const char *argv[]) {
    hdra::VoiceClientOptions options;
    try {
        options = hdra::parse_voice_client_cli(argc, argv);
    } catch (const hdra::VoiceCliError &error) {
        std::cerr << "Argument error: " << error.what() << "\n\n" << hdra::voice_client_usage(argv[0]);
        return 2;
    }

    if (options.show_help) {
        std::cout << hdra::voice_client_usage(argv[0]);
        return 0;
    }
    if (options.show_version) {
        std::cout << "hd_voice_client " << kVersion << '\n';
        return 0;
    }
    if (options.vqe_config_path.empty()) options.vqe_config_path = default_vqe_config(argv[0]);

    const char *api_key_env = std::getenv("HD_VOICE_API_KEY");
    if (api_key_env == nullptr || *api_key_env == '\0') {
        std::cerr << "Configuration error: HD_VOICE_API_KEY is not set\n";
        return 5;
    }
    const std::string api_key = api_key_env;

    uint64_t requested_bytes = 0;
    try {
        hdra::validate_output_path(options.output_path);
        hdra::validate_output_path(options.reply_output_path);
        if (options.duration_seconds) {
            requested_bytes = target_data_bytes(*options.duration_seconds);
            hdra::ensure_space_for_duration(options.output_path, requested_bytes + 44);
        }
    } catch (const hdra::VoiceCliError &error) {
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
        record_command(options, requested_bytes);
    } catch (const std::exception &error) {
        std::cerr << "Recording failed: " << error.what() << '\n';
        return 3;
    }

    try {
        std::cout << "Sending command to " << options.server_url << " ...\n";
        hdra::download_voice_reply(
            options.server_url,
            api_key,
            options.output_path,
            options.reply_output_path,
            options.connect_timeout_seconds,
            options.request_timeout_seconds);
        std::cout << "Saved reply " << options.reply_output_path << '\n';
    } catch (const hdra::NetworkError &error) {
        std::cerr << "Network error: " << error.what() << '\n';
        return 6;
    } catch (const hdra::ServiceError &error) {
        std::cerr << "Service error: " << error.what() << '\n';
        return 7;
    } catch (const hdra::ResponseError &error) {
        std::cerr << "Response error: " << error.what() << '\n';
        return 8;
    } catch (const std::exception &error) {
        std::cerr << "Reply output error: " << error.what() << '\n';
        return 4;
    }

    try {
        std::cout << "Playing reply on " << options.playback_device << " ...\n";
        hdra::play_wav(options.reply_output_path, options.playback_device);
    } catch (const std::exception &error) {
        std::cerr << "Playback error: " << error.what() << '\n';
        return 9;
    }
    return 0;
}
