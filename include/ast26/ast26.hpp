// SPDX-License-Identifier: MIT
// Copyright (c) 2026
//
// ast26 — Public C++ API for reading and writing Giants Engine
// texture containers (GS2D v8 / .ast) used by Farming Simulator 2026.
//
// Embeds astcenc (Apache-2.0) for ASTC compression/decompression,
// bcdec for BCn/DDS block decoding, and stb_image_write for PNG/JPG output.

#ifndef AST26_HPP_INCLUDED
#define AST26_HPP_INCLUDED

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ast26 {

enum class ColorFormat {
    R8, RG16, RGB24, BGR24, RGBA32, BGRA32, RGBA64F, RGBA128F,
};

enum class ColorSpace { Srgb, Linear, Alpha };
enum class Quality    { Fast, Medium, Thorough };
enum class Origin     { TopLeft, BottomLeft };
enum class TextureType{ TwoD, TwoDArray };
enum class NormalMapFormat { RG, RGB };

struct BlockSize {
    int x = 6;
    int y = 6;
};

struct MipLevel {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> data;
};

struct Image {
    int width = 0;
    int height = 0;
    int num_layers = 1;
    int num_mipmaps = 1;
    ColorFormat color_format = ColorFormat::RGBA32;
    ColorSpace  color_space  = ColorSpace::Srgb;
    Origin      origin       = Origin::TopLeft;
    TextureType type         = TextureType::TwoD;
    BlockSize   compression  = {0, 0};
    std::vector<std::vector<MipLevel>> layers;

    [[nodiscard]] const std::vector<std::uint8_t>& raw() const noexcept {
        return layers.at(0).at(0).data;
    }
};

struct EncodeOptions {
    int         mipmaps     = 0;
    BlockSize   block_size  = {6, 6};
    Quality     quality     = Quality::Fast;
    ColorSpace  color_space = ColorSpace::Srgb;
    ColorFormat color_format = ColorFormat::RGBA32;
    std::optional<std::pair<int, int>> resize;
    Origin      ideal_origin    = Origin::TopLeft;
    TextureType texture_type    = TextureType::TwoD;
    std::array<char, 4> roughness_channel = {'r', 'g', 'b', 'a'};
    NormalMapFormat normal_map_format = NormalMapFormat::RGB;
    bool inherit_mipmaps = false;
    bool inherit_layers  = false;
};

/// Decode GS2D v8 (.ast), ASTC (.astc) or raw-RGBA into an Image.
[[nodiscard]] Image decode(const std::uint8_t* data, std::size_t size);
[[nodiscard]] inline Image decode(const std::vector<std::uint8_t>& d) {
    return decode(d.data(), d.size());
}

/// Parse an .ast file without ASTC decompression.
[[nodiscard]] Image load(const std::uint8_t* data, std::size_t size);
[[nodiscard]] inline Image load(const std::vector<std::uint8_t>& d) {
    return load(d.data(), d.size());
}

/// Encode an Image into a GS2D v8 .ast container.
[[nodiscard]] std::vector<std::uint8_t> encode(
    const Image& src, const EncodeOptions& opts);

[[nodiscard]] std::vector<std::uint8_t> encode(
    const std::vector<Image>& sources, const EncodeOptions& opts);

/// Read an image file from memory (PNG, JPG, DDS, or raw RGBA).
[[nodiscard]] Image load_source_image(
    const std::uint8_t* data, std::size_t size,
    int raw_width = 0, int raw_height = 0,
    ColorFormat raw_format = ColorFormat::RGBA32);

/// Serialise a decoded Image to a target file format.
enum class OutputFormat { PNG, JPG, RawRGBA, ASTC };

struct DecodeWriteOptions {
    OutputFormat format = OutputFormat::PNG;
    int   jpg_quality   = 90;
    int   mip_index     = 0;
    int   layer_index   = 0;
    bool  undo_flip     = true;
    std::array<char, 4> channels = {'r', 'g', 'b', 'a'};
};

[[nodiscard]] std::vector<std::uint8_t> write_image(
    const Image& img, const DecodeWriteOptions& opts);

/// Return library version string.
[[nodiscard]] const char* library_version() noexcept;
[[nodiscard]] const char* build_release() noexcept;
[[nodiscard]] const char* build_date() noexcept;
[[nodiscard]] const char* astcenc_version_string() noexcept;

/// Exception type.
class Error : public std::exception {
public:
    enum class Code {
        InvalidFile       = 1,
        UnsupportedFormat = 2,
        ConversionFailed  = 3,
    };
    Error(Code code, std::string msg)
        : code_{code}, msg_{std::move(msg)} {}
    [[nodiscard]] const char* what() const noexcept override { return msg_.c_str(); }
    [[nodiscard]] Code code() const noexcept { return code_; }
private:
    Code        code_;
    std::string msg_;
};

}  // namespace ast26

#endif  // AST26_HPP_INCLUDED
