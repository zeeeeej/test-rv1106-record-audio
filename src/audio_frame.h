#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hdra {

void copy_mono_s16(const uint8_t *input, size_t input_size, std::vector<uint8_t> &output);
void extract_left_s16(const uint8_t *interleaved_stereo, size_t input_size,
                      std::vector<uint8_t> &output);

}  // namespace hdra
