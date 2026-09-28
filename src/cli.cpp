#include "cli.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>

namespace hdra {
namespace {

double parse_positive_number(const std::string &text, const char *option) {
    if (text.empty()) {
        throw CliError(std::string(option) + " requires a value");
    }
    char *end = nullptr;
    errno = 0;
    const double value = std::strtod(text.c_str(), &end);
    if (errno != 0 || end == text.c_str() || *end != '\0' || !std::isfinite(value) || value <= 0.0) {
        throw CliError(std::string(option) + " must be a positive number");
    }
    if (value > static_cast<double>(std::numeric_limits<unsigned int>::max())) {
        throw CliError(std::string(option) + " is too large");
    }
    return value;
}

std::string require_next(int argc, const char *const argv[], int &index, const char *option) {
    if (++index >= argc) {
        throw CliError(std::string(option) + " requires a value");
    }
    return argv[index];
}

}  // namespace

Options parse_cli(int argc, const char *const argv[]) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            options.show_help = true;
        } else if (arg == "--version") {
            options.show_version = true;
        } else if (arg == "-o" || arg == "--output") {
            options.output_path = require_next(argc, argv, i, arg.c_str());
        } else if (arg.rfind("--output=", 0) == 0) {
            options.output_path = arg.substr(9);
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
        } else {
            throw CliError("unknown argument: " + arg);
        }
    }

    if (!options.show_help && !options.show_version && options.output_path.empty()) {
        throw CliError("-o/--output is required");
    }
    if (options.denoise && !options.show_help && !options.show_version && options.vqe_config_path.empty()) {
        // Resolved relative to the installed executable by main().
    }
    return options;
}

std::string usage(const char *program_name) {
    std::ostringstream out;
    out << "Usage: " << program_name << " -o FILE.wav [options]\n"
        << "\n"
        << "Options:\n"
        << "  -o, --output FILE       Output WAV path (required; never overwritten)\n"
        << "  -d, --duration SECONDS  Stop after a positive number of seconds\n"
        << "      --no-denoise        Record raw microphone audio without VQE ANR\n"
        << "      --vqe-config FILE   Override the default ANR configuration\n"
        << "      --version           Print version\n"
        << "  -h, --help              Print this help\n";
    return out.str();
}

}  // namespace hdra
