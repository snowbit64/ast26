// SPDX-License-Identifier: MIT
// GS2D v8 codec for Farming Simulator 2026 AST files.

#include "codec.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "astc_bridge.hpp"
#include "color_format.hpp"
#include "container.hpp"
#include "flip.hpp"
#include "mipgen.hpp"

namespace ast26::codec {

namespace {

constexpr std::size_t kAstcBlockBytes = 16;

inline std::size_t mip_dim(int base, int level) {
    int v = base >> level;
    return static_cast<std::size_t>(v > 0 ? v : 1);
}

inline std::size_t mip_raw_astc(std::size_t w, std::size_t h, int bx, int by) {
    return ((w + bx - 1) / bx) * ((h + by - 1) / by) * kAstcBlockBytes;
}

inline std::size_t mip_raw_uncompressed(std::size_t w, std::size_t h, int bpp) {
    return w * h * static_cast<std::size_t>(bpp);
}

constexpr std::array<std::pair<int, int>, 14> kAstcBlocks = {{
    {4, 4}, {5, 4}, {5, 5}, {6, 5}, {6, 6}, {8, 5}, {8, 6},
    {8, 8}, {10, 5}, {10, 6}, {10, 8}, {10, 10}, {12, 10}, {12, 12},
}};

}  // namespace

DecodedContainer decode_container(const std::uint8_t* data, std::size_t size) {
    DecodedContainer out{};
    auto hdr = container::parse_header(data, size);
    out.header = hdr;

    const std::size_t hdr_size = container::kHeaderV8;
    if (size <= hdr_size)
        throw std::runtime_error("v8: no payload data");

    const std::uint8_t* payload = data + hdr_size;
    const std::size_t payload_size = size - hdr_size;
    const int n_mips = hdr.mip_count + 1;

    // Try single-layer ASTC with known mip count.
    for (auto [bx, by] : kAstcBlocks) {
        std::size_t chain = 0;
        for (int m = 0; m < n_mips; ++m) {
            std::size_t w = mip_dim(hdr.dim_x, m);
            std::size_t h = mip_dim(hdr.dim_y, m);
            chain += mip_raw_astc(w, h, bx, by);
        }
        if (chain > 0 && payload_size % chain == 0) {
            std::size_t layers = payload_size / chain;
            if (layers >= 1 && layers <= 256) {
                out.block_x = bx;
                out.block_y = by;
                out.is_astc = true;
                out.num_layers = static_cast<int>(layers);
                out.mips.resize(n_mips);

                // Fill mip dimensions from layer 0.
                for (int m = 0; m < n_mips; ++m) {
                    out.mips[m].width  = static_cast<int>(mip_dim(hdr.dim_x, m));
                    out.mips[m].height = static_cast<int>(mip_dim(hdr.dim_y, m));
                }
                // Copy layer 0 mip data into out.mips.
                {
                    std::size_t off = 0;
                    for (int m = 0; m < n_mips; ++m) {
                        std::size_t ms = mip_raw_astc(
                            out.mips[m].width, out.mips[m].height, bx, by);
                        out.mips[m].data.assign(payload + off, payload + off + ms);
                        off += ms;
                    }
                }
                // Populate layer_data for all layers.
                out.layer_data.assign(
                    layers,
                    std::vector<std::vector<std::uint8_t>>(n_mips));
                for (std::size_t L = 0; L < layers; ++L) {
                    std::size_t lo = L * chain;
                    for (int m = 0; m < n_mips; ++m) {
                        std::size_t ms = mip_raw_astc(
                            out.mips[m].width, out.mips[m].height, bx, by);
                        out.layer_data[L][m].assign(
                            payload + lo, payload + lo + ms);
                        lo += ms;
                    }
                }
                return out;
            }
        }
    }

    // Try uncompressed raw (single or multi-layer).
    for (int bpp : {1, 2, 3, 4, 8, 12, 16}) {
        std::size_t chain = 0;
        for (int m = 0; m < n_mips; ++m) {
            std::size_t w = mip_dim(hdr.dim_x, m);
            std::size_t h = mip_dim(hdr.dim_y, m);
            chain += mip_raw_uncompressed(w, h, bpp);
        }
        if (chain > 0 && payload_size % chain == 0) {
            std::size_t layers = payload_size / chain;
            if (layers >= 1 && layers <= 256) {
                out.is_astc = false;
                out.bytes_per_pixel = bpp;
                out.num_layers = static_cast<int>(layers);
                out.mips.resize(n_mips);

                std::size_t off = 0;
                for (int m = 0; m < n_mips; ++m) {
                    std::size_t w = mip_dim(hdr.dim_x, m);
                    std::size_t h = mip_dim(hdr.dim_y, m);
                    std::size_t ms = mip_raw_uncompressed(w, h, bpp);
                    out.mips[m].width  = static_cast<int>(w);
                    out.mips[m].height = static_cast<int>(h);
                    out.mips[m].data.assign(payload + off, payload + off + ms);
                    off += ms;
                }

                if (layers > 1) {
                    out.layer_data.assign(
                        layers,
                        std::vector<std::vector<std::uint8_t>>(n_mips));
                    for (std::size_t L = 0; L < layers; ++L) {
                        std::size_t lo = L * chain;
                        for (int m = 0; m < n_mips; ++m) {
                            std::size_t w = mip_dim(hdr.dim_x, m);
                            std::size_t h = mip_dim(hdr.dim_y, m);
                            std::size_t ms = mip_raw_uncompressed(w, h, bpp);
                            out.layer_data[L][m].assign(
                                payload + lo, payload + lo + ms);
                            lo += ms;
                        }
                    }
                }
                return out;
            }
        }
    }

    throw std::runtime_error(
        "cannot decode v8 payload (" + std::to_string(payload_size) +
        " bytes) for " + std::to_string(hdr.dim_x) + "x" +
        std::to_string(hdr.dim_y) + " with " + std::to_string(n_mips) + " mip(s)");
}

std::vector<std::uint8_t> encode_container_v8(
    const std::vector<std::vector<std::uint8_t>>& chunks,
    int dim_x, int dim_y, int channels, int num_layers,
    bool bottom_left, int block_x, int block_y) {

    if (chunks.empty())
        throw std::runtime_error("encode: no payload chunks");

    const int mips_per_layer = static_cast<int>(chunks.size()) / num_layers;
    if (mips_per_layer * num_layers != static_cast<int>(chunks.size()))
        throw std::runtime_error("encode: chunk count not divisible by layer count");

    // Assemble payload: layer-major ordering.
    std::vector<std::uint8_t> payload;
    for (auto& chunk : chunks)
        payload.insert(payload.end(), chunk.begin(), chunk.end());

    container::Header h{};
    h.dim_x = static_cast<std::uint32_t>(dim_x);
    h.dim_y = static_cast<std::uint32_t>(dim_y);
    h.dim_z = 1;
    h.channels = static_cast<std::uint8_t>(channels);
    h.mip_count = static_cast<std::uint8_t>(mips_per_layer - 1);
    h.texture_type = (num_layers > 1) ? static_cast<std::uint8_t>(2) : static_cast<std::uint8_t>(1);
    h.flags = 4;
    h.format_code_a = 0x22;
    h.flip_mode = bottom_left ? static_cast<std::uint8_t>(1) : static_cast<std::uint8_t>(0);
    h.format_code_b = (num_layers > 1) ? static_cast<std::uint8_t>(6) : static_cast<std::uint8_t>(1);
    if (!payload.empty())
        h.tail_byte = payload[0];

    auto hdr_bytes = container::write_header(h);
    hdr_bytes.insert(hdr_bytes.end(), payload.begin(), payload.end());
    return hdr_bytes;
}

}  // namespace ast26::codec
