// SPDX-License-Identifier: MIT
// Public API implementation for ast26 (GS2D v8 / FS2026).

#include "ast26/ast26.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include "astc_bridge.hpp"
#include "codec.hpp"
#include "color_format.hpp"
#include "container.hpp"
#include "dds_io.hpp"
#include "flip.hpp"
#include "image_io.hpp"
#include "mipgen.hpp"

namespace ast26 {

namespace {

bool looks_like_png(const std::uint8_t* d, std::size_t n) {
    return n >= 8 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G';
}
bool looks_like_jpg(const std::uint8_t* d, std::size_t n) {
    return n >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF;
}
bool looks_like_astc(const std::uint8_t* d, std::size_t n) {
    return n >= 16 && d[0] == 0x13 && d[1] == 0xAB && d[2] == 0xA1 && d[3] == 0x5C;
}

int resolve_num_mipmaps(int requested, int w, int h) {
    if (requested < 0) {
        int levels = 1;
        int cw = w, ch = h;
        while ((cw > 1 || ch > 1) && levels < 16) {
            cw = std::max(1, cw / 2);
            ch = std::max(1, ch / 2);
            ++levels;
        }
        return levels;
    }
    return std::min(requested + 1, 16);
}

}  // namespace

const char* library_version() noexcept { return "1.0.0"; }
const char* astcenc_version_string() noexcept { return "astcenc 5.3.0 (embedded)"; }
const char* build_release() noexcept {
#ifdef AST26_BUILD_RELEASE
    return AST26_BUILD_RELEASE;
#else
    return "release";
#endif
}
const char* build_date() noexcept {
#ifdef AST26_BUILD_DATE
    return AST26_BUILD_DATE;
#else
    return __DATE__ " " __TIME__;
#endif
}

// -----------------------------------------------------------------------
// Decode: GS2D v8, PNG/JPG/DDS/ASTC → Image (RGBA8)
// -----------------------------------------------------------------------

Image decode(const std::uint8_t* data, std::size_t size) {
    if (!data || size < 4)
        throw std::runtime_error("empty input");

    // PNG / JPG
    if (looks_like_png(data, size) || looks_like_jpg(data, size)) {
        auto loaded = imageio::decode_png_jpg(data, size);
        Image img;
        img.width = loaded.width;
        img.height = loaded.height;
        img.color_format = ColorFormat::RGBA32;
        img.layers = {{MipLevel{loaded.width, loaded.height, std::move(loaded.rgba)}}};
        return img;
    }

    // DDS
    if (ddsio::looks_like_dds(data, size)) {
        auto dds = ddsio::decode_dds(data, size);
        Image img;
        img.width = dds.width;
        img.height = dds.height;
        img.color_format = ColorFormat::RGBA32;
        img.num_layers = std::max(1, dds.num_layers);
        img.num_mipmaps = std::max(1, dds.num_mipmaps);
        img.type = img.num_layers > 1 ? TextureType::TwoDArray : TextureType::TwoD;
        img.layers.resize(static_cast<std::size_t>(img.num_layers));
        for (int L = 0; L < img.num_layers; ++L) {
            const auto& sl = dds.layers[L];
            for (const auto& m : sl.mips)
                img.layers[L].push_back(MipLevel{m.width, m.height, m.rgba});
        }
        return img;
    }

    // Standalone ASTC
    if (looks_like_astc(data, size)) {
        int bx = data[4], by = data[5];
        auto rd24 = [&](std::size_t off) {
            return static_cast<int>(data[off] | (data[off+1] << 8) | (data[off+2] << 16));
        };
        int w = rd24(7), h = rd24(10);
        auto rgba = astc::decompress_to_rgba8(
            data + 16, size - 16, w, h, bx, by, ColorSpace::Srgb);
        Image img;
        img.width = w; img.height = h;
        img.color_format = ColorFormat::RGBA32;
        img.compression = {bx, by};
        img.layers = {{MipLevel{w, h, std::move(rgba)}}};
        return img;
    }

    // GS2D v8 container
    if (!container::is_gs2d(data, size))
        throw std::runtime_error("unsupported file format");

    auto dec = codec::decode_container(data, size);
    Image img;
    img.width = static_cast<int>(dec.header.dim_x);
    img.height = static_cast<int>(dec.header.dim_y);
    img.color_format = ColorFormat::RGBA32;
    img.origin = dec.header.flip_mode ? Origin::BottomLeft : Origin::TopLeft;
    const int num_layers = std::max<int>(1, dec.num_layers);
    img.num_layers = num_layers;
    img.num_mipmaps = static_cast<int>(dec.mips.size());
    img.type = num_layers > 1 ? TextureType::TwoDArray : TextureType::TwoD;
    img.compression = {dec.block_x, dec.block_y};

    ColorFormat src_fmt = ColorFormat::RGBA32;
    if (!dec.is_astc) {
        switch (dec.bytes_per_pixel) {
            case 1:  src_fmt = ColorFormat::R8; break;
            case 2:  src_fmt = ColorFormat::RG16; break;
            case 3:  src_fmt = ColorFormat::RGB24; break;
            case 4:  src_fmt = ColorFormat::RGBA32; break;
            case 8:  src_fmt = ColorFormat::RGBA64F; break;
            case 16: src_fmt = ColorFormat::RGBA128F; break;
            default: break;
        }
        img.color_format = src_fmt;
    }

    img.layers.assign(static_cast<std::size_t>(num_layers), {});
    for (int L = 0; L < num_layers; ++L) {
        img.layers[L].reserve(dec.mips.size());
        for (std::size_t m = 0; m < dec.mips.size(); ++m) {
            const auto& shape = dec.mips[m];
            const std::vector<std::uint8_t>* raw = nullptr;
            if (L < static_cast<int>(dec.layer_data.size()) &&
                m < dec.layer_data[L].size() &&
                !dec.layer_data[L][m].empty()) {
                raw = &dec.layer_data[L][m];
            } else if (!shape.data.empty()) {
                raw = &shape.data;
            } else {
                continue;
            }
            MipLevel lvl;
            lvl.width = shape.width;
            lvl.height = shape.height;
            if (dec.is_astc) {
                lvl.data = astc::decompress_to_rgba8(
                    raw->data(), raw->size(),
                    shape.width, shape.height, dec.block_x, dec.block_y,
                    ColorSpace::Srgb);
            } else {
                std::size_t need = static_cast<std::size_t>(shape.width) * shape.height *
                                   cfmt::bytes_per_pixel(src_fmt);
                if (raw->size() >= need) {
                    lvl.data = cfmt::to_rgba8(raw->data(),
                        static_cast<std::size_t>(shape.width),
                        static_cast<std::size_t>(shape.height), src_fmt);
                } else {
                    lvl.data.assign(
                        static_cast<std::size_t>(shape.width) * shape.height * 4, 0);
                }
            }
            img.layers[L].push_back(std::move(lvl));
        }
    }
    img.color_format = ColorFormat::RGBA32;
    img.num_mipmaps = img.layers.empty() ? 0 :
                      static_cast<int>(img.layers[0].size());
    return img;
}

// -----------------------------------------------------------------------
// Load: parse without ASTC decompression
// -----------------------------------------------------------------------

Image load(const std::uint8_t* data, std::size_t size) {
    if (data && size >= 4) {
        if (looks_like_png(data, size) || looks_like_jpg(data, size)) {
            auto loaded = imageio::decode_png_jpg(data, size);
            Image img;
            img.width = loaded.width; img.height = loaded.height;
            img.color_format = ColorFormat::RGBA32;
            img.layers = {{MipLevel{loaded.width, loaded.height, std::move(loaded.rgba)}}};
            return img;
        }
        if (ddsio::looks_like_dds(data, size)) {
            auto dds = ddsio::decode_dds(data, size);
            Image img;
            img.width = dds.width; img.height = dds.height;
            img.color_format = ColorFormat::RGBA32;
            img.num_layers = std::max(1, dds.num_layers);
            img.num_mipmaps = std::max(1, dds.num_mipmaps);
            img.type = img.num_layers > 1 ? TextureType::TwoDArray : TextureType::TwoD;
            img.layers.resize(static_cast<std::size_t>(img.num_layers));
            for (int L = 0; L < img.num_layers; ++L) {
                for (const auto& m : dds.layers[L].mips)
                    img.layers[L].push_back(MipLevel{m.width, m.height, m.rgba});
            }
            return img;
        }
    }
    if (!container::is_gs2d(data, size))
        throw std::runtime_error("expected GS2D v8, PNG, JPG or DDS");

    auto dec = codec::decode_container(data, size);
    Image img;
    img.width = static_cast<int>(dec.header.dim_x);
    img.height = static_cast<int>(dec.header.dim_y);
    img.origin = dec.header.flip_mode ? Origin::BottomLeft : Origin::TopLeft;
    const int num_layers = std::max<int>(1, dec.num_layers);
    img.num_layers = num_layers;
    img.num_mipmaps = static_cast<int>(dec.mips.size());
    img.type = num_layers > 1 ? TextureType::TwoDArray : TextureType::TwoD;
    img.compression = {dec.block_x, dec.block_y};
    img.layers.assign(static_cast<std::size_t>(num_layers), {});
    for (int L = 0; L < num_layers; ++L) {
        img.layers[L].reserve(dec.mips.size());
        for (std::size_t m = 0; m < dec.mips.size(); ++m) {
            const auto& shape = dec.mips[m];
            const std::vector<std::uint8_t>* raw = nullptr;
            if (L < static_cast<int>(dec.layer_data.size()) &&
                m < dec.layer_data[L].size() &&
                !dec.layer_data[L][m].empty()) {
                raw = &dec.layer_data[L][m];
            } else {
                raw = &shape.data;
            }
            img.layers[L].push_back(MipLevel{shape.width, shape.height, *raw});
        }
    }
    return img;
}

// -----------------------------------------------------------------------
// Encode: Image → GS2D v8 .ast
// -----------------------------------------------------------------------

namespace {

std::vector<std::vector<std::uint8_t>> build_layer_chain(
    const std::vector<MipLevel>& src_layer,
    int base_w, int base_h, int num_mips, bool inherit_mipmaps,
    bool flip, Origin src_origin, Origin dst_origin,
    std::vector<std::pair<int,int>>& out_sizes) {

    out_sizes.clear();
    std::vector<std::vector<std::uint8_t>> mips;
    auto maybe_flip = [&](std::vector<std::uint8_t>& px, int w, int h) {
        if (flip && src_origin != dst_origin)
            flipops::flip_vertical(px.data(), w, h, 4);
    };
    const bool inherit = inherit_mipmaps && src_layer.size() > 1 &&
                         static_cast<int>(src_layer.size()) >= num_mips;
    if (inherit) {
        for (int i = 0; i < num_mips; ++i) {
            const auto& m = src_layer[static_cast<std::size_t>(i)];
            auto px = m.data;
            maybe_flip(px, m.width, m.height);
            mips.push_back(std::move(px));
            out_sizes.emplace_back(m.width, m.height);
        }
    } else {
        auto base = src_layer.at(0).data;
        maybe_flip(base, base_w, base_h);
        mips = mip::build_chain_rgba8(base.data(), base_w, base_h, num_mips, out_sizes);
    }
    return mips;
}

}  // namespace

std::vector<std::uint8_t> encode(const Image& src, const EncodeOptions& opts) {
    if (src.layers.empty() || src.layers[0].empty())
        throw std::runtime_error("empty image");

    const bool treat_as_array =
        opts.inherit_layers && src.layers.size() > 1 &&
        opts.texture_type == TextureType::TwoDArray;

    const auto& base = src.layers[0][0];
    int w = base.width, h = base.height;

    int num_mips;
    const bool inherit_mipmaps =
        opts.inherit_mipmaps &&
        src.layers[0].size() > 1;
    if (inherit_mipmaps) {
        num_mips = static_cast<int>(src.layers[0].size());
    } else {
        num_mips = resolve_num_mipmaps(opts.mipmaps, w, h);
    }

    const bool flip = opts.ideal_origin != src.origin;
    const int num_layers = treat_as_array ? static_cast<int>(src.layers.size()) : 1;
    int channels = 4;
    if (src.color_format == ColorFormat::RGB24) channels = 3;
    else if (src.color_format == ColorFormat::R8) channels = 1;

    std::vector<std::vector<std::uint8_t>> combined_mips(
        static_cast<std::size_t>(num_mips));

    for (int L = 0; L < num_layers; ++L) {
        const auto& src_layer = src.layers[static_cast<std::size_t>(L)];
        std::vector<std::pair<int,int>> sizes;
        auto chain = build_layer_chain(
            src_layer, w, h, num_mips, inherit_mipmaps,
            flip, src.origin, opts.ideal_origin, sizes);
        for (int mip = 0; mip < num_mips; ++mip) {
            auto blocks = astc::compress_rgba8(
                chain[mip].data(), sizes[mip].first, sizes[mip].second,
                opts.block_size.x, opts.block_size.y,
                opts.quality, opts.color_space);
            combined_mips[mip].insert(combined_mips[mip].end(),
                                      blocks.begin(), blocks.end());
        }
    }

    const bool bottom_left = opts.ideal_origin == Origin::BottomLeft;
    return codec::encode_container_v8(
        combined_mips, w, h, channels, num_layers, bottom_left,
        opts.block_size.x, opts.block_size.y);
}

std::vector<std::uint8_t> encode(
    const std::vector<Image>& inputs, const EncodeOptions& opts) {
    if (inputs.empty()) throw std::runtime_error("no input images");
    if (opts.texture_type == TextureType::TwoD)
        return encode(inputs.front(), opts);

    const int base_w = inputs.front().width;
    const int base_h = inputs.front().height;
    const int num_mips = resolve_num_mipmaps(opts.mipmaps, base_w, base_h);
    const int num_layers = static_cast<int>(inputs.size());

    std::vector<std::vector<std::uint8_t>> chunks;
    chunks.reserve(static_cast<std::size_t>(num_layers) * num_mips);

    for (const auto& img : inputs) {
        const auto& layer0 = img.layers.at(0);
        const bool flip = opts.ideal_origin != img.origin;
        std::vector<std::pair<int,int>> sizes;
        auto chain = build_layer_chain(
            layer0, base_w, base_h, num_mips,
            opts.inherit_mipmaps && layer0.size() > 1,
            flip, img.origin, opts.ideal_origin, sizes);
        for (int mip = 0; mip < num_mips; ++mip) {
            chunks.push_back(astc::compress_rgba8(
                chain[mip].data(), sizes[mip].first, sizes[mip].second,
                opts.block_size.x, opts.block_size.y,
                opts.quality, opts.color_space));
        }
    }

    const bool bottom_left = opts.ideal_origin == Origin::BottomLeft;
    return codec::encode_container_v8(
        chunks, base_w, base_h, 4, num_layers, bottom_left,
        opts.block_size.x, opts.block_size.y);
}

// -----------------------------------------------------------------------
// load_source_image
// -----------------------------------------------------------------------

Image load_source_image(const std::uint8_t* data, std::size_t size,
                        int raw_width, int raw_height, ColorFormat raw_format) {
    if (!data || size == 0)
        throw Error(Error::Code::InvalidFile, "empty source");

    if (looks_like_png(data, size) || looks_like_jpg(data, size)) {
        auto loaded = imageio::decode_png_jpg(data, size);
        Image img;
        img.width = loaded.width; img.height = loaded.height;
        img.color_format = ColorFormat::RGBA32;
        img.layers = {{MipLevel{loaded.width, loaded.height, std::move(loaded.rgba)}}};
        return img;
    }
    if (ddsio::looks_like_dds(data, size)) {
        auto dds = ddsio::decode_dds(data, size);
        Image img;
        img.width = dds.width; img.height = dds.height;
        img.color_format = ColorFormat::RGBA32;
        img.num_layers = std::max(1, dds.num_layers);
        img.num_mipmaps = std::max(1, dds.num_mipmaps);
        img.type = img.num_layers > 1 ? TextureType::TwoDArray : TextureType::TwoD;
        img.layers.resize(static_cast<std::size_t>(img.num_layers));
        for (int L = 0; L < img.num_layers; ++L) {
            for (const auto& m : dds.layers[L].mips)
                img.layers[L].push_back(MipLevel{m.width, m.height, m.rgba});
        }
        return img;
    }
    if (raw_width <= 0 || raw_height <= 0)
        throw Error(Error::Code::UnsupportedFormat, "raw image requires dimensions");
    auto rgba = cfmt::to_rgba8(data, raw_width, raw_height, raw_format);
    Image img;
    img.width = raw_width; img.height = raw_height;
    img.color_format = ColorFormat::RGBA32;
    img.layers = {{MipLevel{raw_width, raw_height, std::move(rgba)}}};
    return img;
}

// -----------------------------------------------------------------------
// write_image
// -----------------------------------------------------------------------

std::vector<std::uint8_t> write_image(const Image& img, const DecodeWriteOptions& opts) {
    if (img.layers.empty())
        throw Error(Error::Code::InvalidFile, "empty image");
    const int layer_idx = std::min<int>(opts.layer_index,
                                         static_cast<int>(img.layers.size()) - 1);
    const auto& layer = img.layers[layer_idx];
    if (layer.empty())
        throw Error(Error::Code::InvalidFile, "layer without mip data");
    const int mip_idx = std::min<int>(opts.mip_index,
                                       static_cast<int>(layer.size()) - 1);
    const auto& src = layer[mip_idx];

    std::vector<std::uint8_t> rgba = src.data;
    int w = src.width, h = src.height;

    if (opts.undo_flip && img.origin == Origin::BottomLeft)
        flipops::flip_vertical(rgba.data(), w, h, 4);

    std::size_t channel_count = 0;
    for (char c : opts.channels)
        if (c == 'r' || c == 'g' || c == 'b' || c == 'a') ++channel_count;
    if (channel_count == 0) channel_count = 4;
    auto swizzled = cfmt::swizzle_rgba8(rgba.data(), w, h,
                                         opts.channels.data(), channel_count);
    switch (opts.format) {
        case OutputFormat::PNG:
            return imageio::encode_png(swizzled.data(), w, h,
                                        static_cast<int>(channel_count));
        case OutputFormat::JPG:
            return imageio::encode_jpg(swizzled.data(), w, h,
                                        std::min<int>(3, static_cast<int>(channel_count)),
                                        opts.jpg_quality);
        case OutputFormat::RawRGBA:
            return rgba;
        case OutputFormat::ASTC: {
            auto blocks = astc::compress_rgba8(
                rgba.data(), w, h,
                img.compression.x > 0 ? img.compression.x : 6,
                img.compression.y > 0 ? img.compression.y : 6,
                Quality::Fast, img.color_space);
            auto hdr = astc::build_astc_file_header(
                w, h, 1,
                img.compression.x > 0 ? img.compression.x : 6,
                img.compression.y > 0 ? img.compression.y : 6);
            hdr.insert(hdr.end(), blocks.begin(), blocks.end());
            return hdr;
        }
    }
    throw Error(Error::Code::ConversionFailed, "unsupported output format");
}

}  // namespace ast26
