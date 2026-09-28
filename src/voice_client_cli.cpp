#include "voice_client_cli.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>

namespace hdra {
namespace {

double parse_positive_number(const std::string &text, const char *option) {
    if (text.empty()) throw VoiceCliError(std::string(option) + " requires a value");
    char *end = nullptr;
    errno = 0;
    const double value = std::strtod(text.c_str(), &end);
    if (errno != 0 || end == text.c_str() || *end != '\0' || !std::isfinite(value) || value <= 0.0) {
        throw VoiceCliError(std::string(option) + " must be a positive number");
    }
    if (value > static_cast<double>(std::numeric_limits<unsigned int>::max())) {
        throw VoiceCliError(std::string(option) + " is too large");
    }
    return value;
}

std::string require_next(int argc, const char *const argv[], int &index, const char *option) {
    if (++index >= argc) throw VoiceCliError(std::string(option) + " requires a value");
    return argv[index];
}

bool contains_whitespace_or_control(const std::string &text) {
    for (unsigned char ch : text) {
        if (ch <= 0x20 || ch == 0x7f) return true;
    }
    return false;
}

}  // namespace

std::string normalize_server_url(const std::string &url) {
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) {
        throw VoiceCliError("--server-url must start with http:// or https://");
    }
    if (contains_whitespace_or_control(url)) {
        throw VoiceCliError("--server-url must not contain whitespace");
    }
    const size_t scheme_end = url.find("://") + 3;
    std::string normalized = url;
    while (normalized.size() > scheme_end && normalized.back() == '/') normalized.pop_back();
    if (normalized.size() == scheme_end) throw VoiceCliError("--server-url must include a host");
    return normalized;
}

VoiceClientOptions parse_voice_client_cli(int argc, const char *const argv[]) {
    VoiceClientOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            options.show_help = true;
        } else if (arg == "--version") {
            options.show_version = true;
        } else if (arg == "--server-url") {
            options.server_url = require_next(argc, argv, i, arg.c_str());
        } else if (arg.rfind("--server-url=", 0) == 0) {
            options.server_url = arg.substr(13);
        } else if (arg == "-o" || arg == "--output") {
            options.output_path = require_next(argc, argv, i, arg.c_str());
        } else if (arg.rfind("--output=", 0) == 0) {
            options.output_path = arg.substr(9);
        } else if (arg == "--reply-output") {
            options.reply_output_path = require_next(argc, argv, i, arg.c_str());
        } else if (arg.rfind("--reply-output=", 0) == 0) {
            options.reply_output_path = arg.substr(15);
        } else if (arg == "-d" || arg == "--duration") {
            options.duration_seconds = parse_positive_number(require_next(argc, argv, i, arg.c_str()), arg.c_str());
        } else if (arg.rfind("--duration=", 0) == 0) {
            options.duration_seconds = parse_positive_number(arg.substr(11), "--duration");
        } else if (arg == "--no-denoise") {
            options.denoise = false;
        } else if (arg == "--vqe-config") {
            options.vqe_config_path = require_next(argc, argv, i, arg.c_str());
        } else if (arg.rfind("--vqe-config=", 0) == 0) {
            options.vqe_config_path = arg.substr(13);
        } else if (arg == "--playback-device") {
            options.playback_device = require_next(argc, argv, i, arg.c_str());
        } else if (arg.rfind("--playback-device=", 0) == 0) {
            options.playback_device = arg.substr(18);
        } else if (arg == "--connect-timeout") {
            options.connect_timeout_seconds = parse_positive_number(require_next(argc, argv, i, arg.c_str()), arg.c_str());
        } else if (arg.rfind("--connect-timeout=", 0) == 0) {
            options.connect_timeout_seconds = parse_positive_number(arg.substr(18), "--connect-timeout");
        } else if (arg == "--request-timeout") {
            options.request_timeout_seconds = parse_positive_number(require_next(argc, argv, i, arg.c_str()), arg.c_str());
        } else if (arg.rfind("--request-timeout=", 0) == 0) {
            options.request_timeout_seconds = parse_positive_number(arg.substr(18), "--request-timeout");
        } else {
            throw VoiceCliError("unknown argument: " + arg);
        }
    }

    if (!options.show_help && !options.show_version) {
        if (options.server_url.empty()) throw VoiceCliError("--server-url is required");
        if (options.output_path.empty()) throw VoiceCliError("-o/--output is required");
        if (options.reply_output_path.empty()) throw VoiceCliError("--reply-output is required");
        if (options.output_path == options.reply_output_path) {
            throw VoiceCliError("recording and reply output paths must be different");
        }
        if (options.playback_device.empty()) throw VoiceCliError("--playback-device must not be empty");
        options.server_url = normalize_server_url(options.server_url);
    }
    return options;
}

std::string voice_client_usage(const char *program_name) {
    std::ostringstream out;
    out << "Usage: " << program_name << " --server-url URL -o FILE.wav --reply-output FILE.wav [options]\n"
        << "\n"
        << "Records a command, sends it to the voice gateway, and plays the WAV reply.\n"
        << "Set HD_VOICE_API_KEY in the environment before running.\n"
        << "\n"
        << "Options:\n"
        << "      --server-url URL          Gateway base URL (required)\n"
        << "  -o, --output FILE             Recorded command WAV (required; never overwritten)\n"
        << "      --reply-output FILE       Reply WAV (required; never overwritten)\n"
        << "  -d, --duration SECONDS        Stop automatically; otherwise press Enter\n"
        << "      --no-denoise              Disable VQE ANR\n"
        << "      --vqe-config FILE         Override the default ANR configuration\n"
        << "      --playback-device DEVICE  ALSA device (default: plughw:0,0)\n"
        << "      --connect-timeout SEC     Connection timeout (default: 10)\n"
        << "      --request-timeout SEC     Whole request timeout (default: 180)\n"
        << "      --version                 Print version\n"
        << "  -h, --help                    Print this help\n";
    return out.str();
}

}  // namespace hdra
