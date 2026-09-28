#pragma once

#include <optional>
#include <stdexcept>
#include <string>

namespace hdra {

struct Options {
    std::string output_path;
    std::optional<double> duration_seconds;
    bool denoise = true;
    std::string vqe_config_path;
    bool show_help = false;
    bool show_version = false;
};

class CliError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

Options parse_cli(int argc, const char *const argv[]);
std::string usage(const char *program_name);

}  // namespace hdra
