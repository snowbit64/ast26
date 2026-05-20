// SPDX-License-Identifier: MIT
#ifndef AST26_SRC_CODEC_HPP_INCLUDED
#define AST26_SRC_CODEC_HPP_INCLUDED

#include <cstdint>
#include <optional>
#include <vector>

#include "container.hpp"

namespace ast26::codec {

struct Mip {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> data;
};

struct DecodedContainer {
    container::Header header{};
    bool is_astc = false;
    int  block_x = 0;
    int  block_y = 0;
    int  bytes_per_pixel = 0;
    int  num_layers = 1;
    std::vector<Mip> mips;
    std::vector<std::vector<std::vector<std::uint8_t>>> layer_data;
};

DecodedContainer decode_container(const std::uint8_t* data, std::size_t size);

std::vector<std::uint8_t> encode_container_v8(
    const std::vector<std::vector<std::uint8_t>>& chunks,
    int dim_x, int dim_y, int channels, int num_layers = 1,
    bool bottom_left = false, int block_x = 6, int block_y = 6);

}  // namespace ast26::codec

#endif  // AST26_SRC_CODEC_HPP_INCLUDED
