#pragma once

#include <stdexcept>
#include <string>
#include <vector>

namespace hdra {

class NetworkError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ServiceError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ResponseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class PlaybackError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct ProcessResult {
    int exit_code;
    std::string stdout_text;
};

ProcessResult run_process_with_input(
    const std::string &executable,
    const std::vector<std::string> &arguments,
    const std::string &stdin_text);

bool is_wav_file(const std::string &path);
void download_voice_reply(
    const std::string &server_url,
    const std::string &api_key,
    const std::string &recording_path,
    const std::string &reply_path,
    double connect_timeout_seconds,
    double request_timeout_seconds,
    const std::string &curl_path = "/usr/bin/curl");
void play_wav(
    const std::string &wav_path,
    const std::string &device,
    const std::string &aplay_path = "/usr/bin/aplay");

}  // namespace hdra
