#include "audio_frame.h"

#include <stdexcept>

namespace hdra {

void copy_mono_s16(const uint8_t *input, size_t input_size, std::vector<uint8_t> &output) {
    if (input_size % 2 != 0) throw std::runtime_error("mono audio frame is not 16-bit aligned");
    output.assign(input, input + input_size);
}

void extract_left_s16(const uint8_t *interleaved_stereo, size_t input_size,
                      std::vector<uint8_t> &output) {
    if (input_size % 4 != 0) throw std::runtime_error("stereo audio frame is not 16-bit aligned");
    output.resize(input_size / 2);
    for (size_t input = 0, out = 0; input < input_size; input += 4, out += 2) {
        output[out] = interleaved_stereo[input];
        output[out + 1] = interleaved_stereo[input + 1];
    }
}

}  // namespace hdra
