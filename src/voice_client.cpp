#include "voice_client.h"

#include "wav_writer.h"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace hdra {
namespace {

std::runtime_error system_error(const std::string &operation) {
    return std::runtime_error(operation + ": " + std::strerror(errno));
}

void close_fd(int fd) {
    if (fd >= 0) {
        while (::close(fd) < 0 && errno == EINTR) {
        }
    }
}

void write_all(int fd, const std::string &data) {
    const char *cursor = data.data();
    size_t remaining = data.size();
    while (remaining > 0) {
        const ssize_t written = ::write(fd, cursor, remaining);
        if (written < 0) {
            if (errno == EINTR) continue;
            if (errno == EPIPE) return;
            throw system_error("cannot write child process input");
        }
        cursor += written;
        remaining -= static_cast<size_t>(written);
    }
}

std::string read_all(int fd) {
    std::string output;
    char buffer[256];
    while (true) {
        const ssize_t size = ::read(fd, buffer, sizeof(buffer));
        if (size > 0) {
            output.append(buffer, static_cast<size_t>(size));
        } else if (size == 0) {
            break;
        } else if (errno != EINTR) {
            throw system_error("cannot read child process output");
        }
    }
    return output;
}

std::string number(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(3) << value;
    return out.str();
}

std::string trim(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string curl_config_header(const std::string &api_key) {
    if (api_key.empty()) throw ServiceError("HD_VOICE_API_KEY is empty");
    std::string escaped;
    escaped.reserve(api_key.size());
    for (unsigned char ch : api_key) {
        if (ch == '\r' || ch == '\n' || ch == 0) {
            throw ServiceError("HD_VOICE_API_KEY contains an invalid character");
        }
        if (ch == '\\' || ch == '"') escaped.push_back('\\');
        escaped.push_back(static_cast<char>(ch));
    }
    return "header = \"X-API-Key: " + escaped + "\"\n";
}

std::string response_excerpt(const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    std::string text(512, '\0');
    input.read(&text[0], static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<size_t>(input.gcount()));
    for (char &ch : text) {
        const unsigned char value = static_cast<unsigned char>(ch);
        if (value < 0x20 && ch != '\t') ch = ' ';
    }
    return trim(text);
}

void remove_partial(const std::string &path) {
    if (::unlink(path.c_str()) != 0 && errno != ENOENT) {
        // The primary network/service error remains more useful than cleanup failure.
    }
}

void publish_no_overwrite(const std::string &partial, const std::string &output) {
    if (::link(partial.c_str(), output.c_str()) != 0) {
        throw system_error("cannot publish reply '" + output + "'");
    }
    if (::unlink(partial.c_str()) != 0) {
        const int saved = errno;
        ::unlink(output.c_str());
        errno = saved;
        throw system_error("cannot remove partial reply '" + partial + "'");
    }
}

}  // namespace

ProcessResult run_process_with_input(
    const std::string &executable,
    const std::vector<std::string> &arguments,
    const std::string &stdin_text) {
    int input_pipe[2] = {-1, -1};
    int output_pipe[2] = {-1, -1};
    if (::pipe(input_pipe) != 0) throw system_error("cannot create input pipe");
    if (::pipe(output_pipe) != 0) {
        const int saved = errno;
        close_fd(input_pipe[0]);
        close_fd(input_pipe[1]);
        errno = saved;
        throw system_error("cannot create output pipe");
    }

    const pid_t child = ::fork();
    if (child < 0) {
        const int saved = errno;
        close_fd(input_pipe[0]);
        close_fd(input_pipe[1]);
        close_fd(output_pipe[0]);
        close_fd(output_pipe[1]);
        errno = saved;
        throw system_error("cannot fork");
    }
    if (child == 0) {
        ::dup2(input_pipe[0], STDIN_FILENO);
        ::dup2(output_pipe[1], STDOUT_FILENO);
        close_fd(input_pipe[0]);
        close_fd(input_pipe[1]);
        close_fd(output_pipe[0]);
        close_fd(output_pipe[1]);

        std::vector<char *> argv;
        argv.reserve(arguments.size() + 2);
        argv.push_back(const_cast<char *>(executable.c_str()));
        for (const std::string &argument : arguments) {
            argv.push_back(const_cast<char *>(argument.c_str()));
        }
        argv.push_back(nullptr);
        ::unsetenv("HD_VOICE_API_KEY");
        ::execv(executable.c_str(), argv.data());
        _exit(127);
    }

    close_fd(input_pipe[0]);
    close_fd(output_pipe[1]);
    try {
        write_all(input_pipe[1], stdin_text);
        close_fd(input_pipe[1]);
        input_pipe[1] = -1;
        std::string output = read_all(output_pipe[0]);
        close_fd(output_pipe[0]);
        output_pipe[0] = -1;

        int status = 0;
        while (::waitpid(child, &status, 0) < 0) {
            if (errno != EINTR) throw system_error("cannot wait for child process");
        }
        if (WIFEXITED(status)) return {WEXITSTATUS(status), std::move(output)};
        if (WIFSIGNALED(status)) return {128 + WTERMSIG(status), std::move(output)};
        return {125, std::move(output)};
    } catch (...) {
        close_fd(input_pipe[1]);
        close_fd(output_pipe[0]);
        ::kill(child, SIGTERM);
        while (::waitpid(child, nullptr, 0) < 0 && errno == EINTR) {
        }
        throw;
    }
}

bool is_wav_file(const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    char header[12] = {};
    input.read(header, sizeof(header));
    return input.gcount() == static_cast<std::streamsize>(sizeof(header)) &&
           std::memcmp(header, "RIFF", 4) == 0 &&
           std::memcmp(header + 8, "WAVE", 4) == 0;
}

void download_voice_reply(
    const std::string &server_url,
    const std::string &api_key,
    const std::string &recording_path,
    const std::string &reply_path,
    double connect_timeout_seconds,
    double request_timeout_seconds,
    const std::string &curl_path) {
    validate_output_path(reply_path);
    const std::string partial = reply_path + ".part";
    const int fd = ::open(partial.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) throw system_error("cannot create partial reply '" + partial + "'");
    close_fd(fd);

    const std::vector<std::string> arguments = {
        "--config", "-",
        "--silent", "--show-error",
        "--connect-timeout", number(connect_timeout_seconds),
        "--max-time", number(request_timeout_seconds),
        "--request", "POST",
        "--form", "file=@" + recording_path + ";type=audio/wav",
        "--output", partial,
        "--write-out", "%{http_code}",
        server_url + "/v1/voice-command",
    };

    ProcessResult result;
    try {
        result = run_process_with_input(curl_path, arguments, curl_config_header(api_key));
    } catch (...) {
        remove_partial(partial);
        throw;
    }
    if (result.exit_code != 0) {
        remove_partial(partial);
        throw NetworkError("curl failed with exit code " + std::to_string(result.exit_code));
    }

    const std::string status = trim(result.stdout_text);
    if (status.size() != 3 || status[0] < '0' || status[0] > '9' ||
        status[1] < '0' || status[1] > '9' || status[2] < '0' || status[2] > '9') {
        remove_partial(partial);
        throw ResponseError("gateway returned an invalid HTTP status");
    }
    if (status[0] != '2') {
        const std::string detail = response_excerpt(partial);
        remove_partial(partial);
        throw ServiceError("gateway returned HTTP " + status + (detail.empty() ? "" : ": " + detail));
    }
    if (!is_wav_file(partial)) {
        remove_partial(partial);
        throw ResponseError("gateway response is not a RIFF/WAVE file");
    }
    try {
        publish_no_overwrite(partial, reply_path);
    } catch (...) {
        remove_partial(partial);
        throw;
    }
}

void play_wav(const std::string &wav_path, const std::string &device, const std::string &aplay_path) {
    const ProcessResult result = run_process_with_input(
        aplay_path,
        {"-D", device, wav_path},
        "");
    if (result.exit_code != 0) {
        throw PlaybackError("aplay failed with exit code " + std::to_string(result.exit_code));
    }
}

}  // namespace hdra
