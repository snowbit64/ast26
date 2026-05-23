// SPDX-License-Identifier: MIT
#ifndef GSTEXTCONV_SRC_MIPGEN_HPP_INCLUDED
#define GSTEXTCONV_SRC_MIPGEN_HPP_INCLUDED
#include <cstdint>
#include <vector>

namespace ast26::mip {

/// Downsample an RGBA8 image by 2×2 box filter. `w`/`h` are source dims.
std::vector<std::uint8_t> downsample_rgba8(
    const std::uint8_t* rgba, int w, int h, int& out_w, int& out_h);

/// Resize an RGBA8 image to arbitrary dimensions using bilinear filtering.
std::vector<std::uint8_t> resize_rgba8(
    const std::uint8_t* rgba, int src_w, int src_h, int dst_w, int dst_h);

/// Generate a mip chain of RGBA8 images starting from `base`. The returned
/// vector contains `count` entries starting with `base` at index 0.
std::vector<std::vector<std::uint8_t>> build_chain_rgba8(
    const std::uint8_t* base, int w, int h, int count,
    std::vector<std::pair<int, int>>& out_sizes);

}  // namespace ast26::mip
#endif
