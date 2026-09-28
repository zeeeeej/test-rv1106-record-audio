#include "audio_frame.h"
#include "cli.h"
#include "voice_client.h"
#include "voice_client_cli.h"
#include "wav_writer.h"

#include <array>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

int failures = 0;

#define CHECK(condition)                                                                            \
    do {                                                                                            \
        if (!(condition)) {                                                                         \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #condition << '\n';   \
            ++failures;                                                                             \
        }                                                                                           \
    } while (false)

uint32_t read_u32(const uint8_t *bytes) {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

template <typename Function>
void expect_throw(Function function) {
    try {
        function();
        CHECK(false);
    } catch (const std::exception &) {
    }
}

std::string make_temp_dir() {
    std::array<char, 64> pattern {};
    std::strcpy(pattern.data(), "/tmp/hd-record-audio-test-XXXXXX");
    char *result = ::mkdtemp(pattern.data());
    if (!result) throw std::runtime_error(std::strerror(errno));
    return result;
}

void test_cli() {
    const char *valid[] = {"rec", "-o", "/tmp/a.wav", "-d", "1.25", "--no-denoise"};
    const auto options = hdra::parse_cli(6, valid);
    CHECK(options.output_path == "/tmp/a.wav");
    CHECK(options.duration_seconds.has_value());
    CHECK(std::abs(*options.duration_seconds - 1.25) < 0.0001);
    CHECK(!options.denoise);

    const char *missing[] = {"rec", "--no-denoise"};
    expect_throw([&] { hdra::parse_cli(2, missing); });
    const char *invalid_duration[] = {"rec", "-o", "a.wav", "-d", "0"};
    expect_throw([&] { hdra::parse_cli(5, invalid_duration); });
    const char *unknown[] = {"rec", "-o", "a.wav", "--raw"};
    expect_throw([&] { hdra::parse_cli(4, unknown); });
}

void test_voice_client_cli() {
    const char *valid[] = {
        "voice", "--server-url", "http://192.168.1.10:8080/", "-o", "/tmp/command.wav",
        "--reply-output", "/tmp/reply.wav", "--playback-device", "default", "-d", "2.5",
    };
    const auto options = hdra::parse_voice_client_cli(11, valid);
    CHECK(options.server_url == "http://192.168.1.10:8080");
    CHECK(options.output_path == "/tmp/command.wav");
    CHECK(options.reply_output_path == "/tmp/reply.wav");
    CHECK(options.playback_device == "default");
    CHECK(options.duration_seconds.has_value());
    CHECK(std::abs(*options.duration_seconds - 2.5) < 0.0001);

    const char *missing_server[] = {"voice", "-o", "a.wav", "--reply-output", "b.wav"};
    expect_throw([&] { hdra::parse_voice_client_cli(5, missing_server); });
    const char *same_output[] = {
        "voice", "--server-url", "http://host", "-o", "a.wav", "--reply-output", "a.wav",
    };
    expect_throw([&] { hdra::parse_voice_client_cli(7, same_output); });
    expect_throw([&] { hdra::normalize_server_url("ftp://host"); });
    expect_throw([&] { hdra::normalize_server_url("http://bad host"); });
}

void test_audio_frame_conversion() {
    const std::array<uint8_t, 8> stereo = {
        0x01, 0x02, 0x11, 0x12,
        0x03, 0x04, 0x13, 0x14,
    };
    std::vector<uint8_t> mono;
    hdra::extract_left_s16(stereo.data(), stereo.size(), mono);
    CHECK((mono == std::vector<uint8_t>{0x01, 0x02, 0x03, 0x04}));
    expect_throw([&] { hdra::extract_left_s16(stereo.data(), 6, mono); });

    hdra::copy_mono_s16(stereo.data(), 4, mono);
    CHECK((mono == std::vector<uint8_t>{0x01, 0x02, 0x11, 0x12}));
    expect_throw([&] { hdra::copy_mono_s16(stereo.data(), 3, mono); });
}

void test_wav_writer() {
    const std::string directory = make_temp_dir();
    const std::string output = directory + "/sample.wav";
    std::vector<int16_t> samples(160, 1234);
    {
        hdra::WavWriter writer(output, 16000, 1, 16);
        writer.write(samples.data(), samples.size() * sizeof(samples[0]));
        CHECK(writer.data_bytes() == 320);
        writer.finalize();
    }

    std::ifstream input(output, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
    CHECK(bytes.size() == 364);
    CHECK(std::memcmp(bytes.data(), "RIFF", 4) == 0);
    CHECK(std::memcmp(bytes.data() + 8, "WAVE", 4) == 0);
    CHECK(read_u32(bytes.data() + 4) == 356);
    CHECK(read_u32(bytes.data() + 24) == 16000);
    CHECK(read_u32(bytes.data() + 28) == 32000);
    CHECK(read_u32(bytes.data() + 40) == 320);
    expect_throw([&] { hdra::WavWriter duplicate(output, 16000, 1, 16); });

    ::unlink(output.c_str());
    ::rmdir(directory.c_str());
}

void test_partial_is_valid() {
    const std::string directory = make_temp_dir();
    const std::string output = directory + "/partial.wav";
    const std::string partial = output + ".part";
    std::vector<int16_t> samples(32, -100);
    {
        hdra::WavWriter writer(output, 16000, 1, 16);
        writer.write(samples.data(), samples.size() * sizeof(samples[0]));
    }
    std::ifstream input(partial, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
    CHECK(bytes.size() == 108);
    CHECK(read_u32(bytes.data() + 40) == 64);
    CHECK(::access(output.c_str(), F_OK) != 0);
    ::unlink(partial.c_str());
    ::rmdir(directory.c_str());
}

void test_voice_client_helpers() {
    const auto process = hdra::run_process_with_input(
        "/bin/sh", {"-c", "IFS= read -r line; printf 'seen:%s' \"$line\""}, "hello\n");
    CHECK(process.exit_code == 0);
    CHECK(process.stdout_text == "seen:hello");

    const std::string directory = make_temp_dir();
    const std::string good = directory + "/good.wav";
    const std::string bad = directory + "/bad.wav";
    {
        std::ofstream output(good, std::ios::binary);
        output.write("RIFF\x24\x00\x00\x00WAVE", 12);
    }
    {
        std::ofstream output(bad, std::ios::binary);
        output << "not audio";
    }
    CHECK(hdra::is_wav_file(good));
    CHECK(!hdra::is_wav_file(bad));
    ::unlink(good.c_str());
    ::unlink(bad.c_str());
    ::rmdir(directory.c_str());
}

}  // namespace

int main() {
    test_cli();
    test_voice_client_cli();
    test_audio_frame_conversion();
    test_wav_writer();
    test_partial_is_valid();
    test_voice_client_helpers();
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All tests passed\n";
    return 0;
}
