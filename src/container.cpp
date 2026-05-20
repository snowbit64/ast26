// SPDX-License-Identifier: MIT
#include "container.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

namespace ast26::container {

namespace {

inline std::uint32_t rd_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(
        p[0] | (p[1] << 8) | (p[2] << 16) | (std::uint32_t{p[3]} << 24));
}
inline void wr_u32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
}

}  // namespace

bool is_gs2d(const std::uint8_t* data, std::size_t size) noexcept {
    if (size < 8) return false;
    return data[0] == 'G' && data[1] == 'S' && data[2] == '2' && data[3] == 'D';
}

std::uint32_t version_of(const std::uint8_t* data, std::size_t size) {
    if (!is_gs2d(data, size))
        throw std::runtime_error("not a GS2D container");
    return rd_u32(data + 4);
}

Header parse_header(const std::uint8_t* data, std::size_t size) {
    if (size < 8)
        throw std::runtime_error("file smaller than 8 bytes");
    if (!is_gs2d(data, size))
        throw std::runtime_error("invalid GS2D magic");

    Header h{};
    std::memcpy(h.magic.data(), data, 4);
    h.version = rd_u32(data + 4);
    if (h.version != 8)
        throw std::runtime_error("unsupported GS2D version " +
                                 std::to_string(h.version) + " (expected 8)");
    if (size < kHeaderV8)
        throw std::runtime_error("truncated GS2D v8 header");

    h.dim_x        = rd_u32(data + 0x08);
    h.dim_y        = rd_u32(data + 0x0C);
    h.dim_z        = rd_u32(data + 0x10);
    h.channels     = data[0x14];
    h.mip_count    = data[0x15];
    h.texture_type = data[0x16];
    h.reserved_17  = data[0x17];
    h.flags        = rd_u32(data + 0x18);
    std::memcpy(h.content_hash.data(), data + 0x1C, 16);
    h.format_code_a = data[0x2C];
    h.reserved_2d   = data[0x2D];
    h.flip_mode     = data[0x2E];
    h.format_code_b = data[0x2F];
    std::memcpy(h.padding, data + 0x30, 3);
    h.tail_byte     = data[0x33];
    return h;
}

std::vector<std::uint8_t> write_header(const Header& h) {
    std::vector<std::uint8_t> buf(kHeaderV8, 0);
    std::memcpy(buf.data(), h.magic.data(), 4);
    wr_u32(buf.data() + 0x04, h.version);
    wr_u32(buf.data() + 0x08, h.dim_x);
    wr_u32(buf.data() + 0x0C, h.dim_y);
    wr_u32(buf.data() + 0x10, h.dim_z);
    buf[0x14] = h.channels;
    buf[0x15] = h.mip_count;
    buf[0x16] = h.texture_type;
    buf[0x17] = h.reserved_17;
    wr_u32(buf.data() + 0x18, h.flags);
    std::memcpy(buf.data() + 0x1C, h.content_hash.data(), 16);
    buf[0x2C] = h.format_code_a;
    buf[0x2D] = h.reserved_2d;
    buf[0x2E] = h.flip_mode;
    buf[0x2F] = h.format_code_b;
    std::memcpy(buf.data() + 0x30, h.padding, 3);
    buf[0x33] = h.tail_byte;
    return buf;
}

}  // namespace ast26::container
