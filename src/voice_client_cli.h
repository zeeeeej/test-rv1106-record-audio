#pragma once

#include <optional>
#include <stdexcept>
#include <string>

namespace hdra {

struct VoiceClientOptions {
    std::string server_url;
    std::string output_path;
    std::string reply_output_path;
    std::optional<double> duration_seconds;
    bool denoise = true;
    std::string vqe_config_path;
    std::string playback_device = "plughw:0,0";
    double connect_timeout_seconds = 10.0;
    double request_timeout_seconds = 180.0;
    bool show_help = false;
    bool show_version = false;
};

class VoiceCliError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

VoiceClientOptions parse_voice_client_cli(int argc, const char *const argv[]);
std::string voice_client_usage(const char *program_name);
std::string normalize_server_url(const std::string &url);

}  // namespace hdra
