// SPDX-License-Identifier: MIT
// GS2D container structures for ast26 (v8 only, Farming Simulator 2026).

#ifndef AST26_SRC_CONTAINER_HPP_INCLUDED
#define AST26_SRC_CONTAINER_HPP_INCLUDED

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ast26::container {

// v8 header is exactly 52 bytes (0x34).
inline constexpr std::size_t kHeaderV8 = 0x34;

struct Header {
    std::array<std::uint8_t, 4> magic{'G', 'S', '2', 'D'};
    std::uint32_t version       = 8;
    std::uint32_t dim_x         = 0;
    std::uint32_t dim_y         = 0;
    std::uint32_t dim_z         = 1;
    std::uint8_t  channels      = 4;   // 1, 3, or 4
    std::uint8_t  mip_count     = 0;   // additional mip levels (total = mip_count + 1)
    std::uint8_t  texture_type  = 1;   // 1=2D, 2=2Darray, 3=cube, 50=special
    std::uint8_t  reserved_17   = 0;
    std::uint32_t flags         = 4;
    std::array<std::uint8_t, 16> content_hash{};
    std::uint8_t  format_code_a = 0x22; // 0x22=ASTC, 0x05=mixed, 0x01=raw
    std::uint8_t  reserved_2d   = 0;
    std::uint8_t  flip_mode     = 1;    // 0=topLeft, 1=bottomLeft
    std::uint8_t  format_code_b = 1;
    std::uint8_t  padding[3]    = {};
    std::uint8_t  tail_byte     = 0;
};

bool is_gs2d(const std::uint8_t* data, std::size_t size) noexcept;
std::uint32_t version_of(const std::uint8_t* data, std::size_t size);
Header parse_header(const std::uint8_t* data, std::size_t size);
std::vector<std::uint8_t> write_header(const Header& h);

}  // namespace ast26::container

#endif  // AST26_SRC_CONTAINER_HPP_INCLUDED
