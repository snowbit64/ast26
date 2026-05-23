// SPDX-License-Identifier: MIT
#include "mipgen.hpp"

#include <algorithm>
#include <cstring>

namespace ast26::mip {

std::vector<std::uint8_t> downsample_rgba8(
    const std::uint8_t* rgba, int w, int h, int& out_w, int& out_h) {
    out_w = std::max(1, w / 2);
    out_h = std::max(1, h / 2);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(out_w) * out_h * 4);
    for (int y = 0; y < out_h; ++y) {
        const int sy0 = y * 2;
        const int sy1 = std::min(sy0 + 1, h - 1);
        for (int x = 0; x < out_w; ++x) {
            const int sx0 = x * 2;
            const int sx1 = std::min(sx0 + 1, w - 1);
            for (int c = 0; c < 4; ++c) {
                unsigned s = rgba[(sy0 * w + sx0) * 4 + c]
                           + rgba[(sy0 * w + sx1) * 4 + c]
                           + rgba[(sy1 * w + sx0) * 4 + c]
                           + rgba[(sy1 * w + sx1) * 4 + c];
                out[(y * out_w + x) * 4 + c] = static_cast<std::uint8_t>((s + 2) / 4);
            }
        }
    }
    return out;
}

std::vector<std::uint8_t> resize_rgba8(
    const std::uint8_t* rgba, int src_w, int src_h, int dst_w, int dst_h) {
    std::vector<std::uint8_t> out(static_cast<std::size_t>(dst_w) * dst_h * 4);
    const float sx = static_cast<float>(src_w) / static_cast<float>(dst_w);
    const float sy = static_cast<float>(src_h) / static_cast<float>(dst_h);
    for (int y = 0; y < dst_h; ++y) {
        const float fy = (static_cast<float>(y) + 0.5f) * sy - 0.5f;
        const int y0 = std::max(0, static_cast<int>(fy));
        const int y1 = std::min(y0 + 1, src_h - 1);
        const float yt = fy - static_cast<float>(y0);
        for (int x = 0; x < dst_w; ++x) {
            const float fx = (static_cast<float>(x) + 0.5f) * sx - 0.5f;
            const int x0 = std::max(0, static_cast<int>(fx));
            const int x1 = std::min(x0 + 1, src_w - 1);
            const float xt = fx - static_cast<float>(x0);
            for (int c = 0; c < 4; ++c) {
                float v00 = rgba[(y0 * src_w + x0) * 4 + c];
                float v10 = rgba[(y0 * src_w + x1) * 4 + c];
                float v01 = rgba[(y1 * src_w + x0) * 4 + c];
                float v11 = rgba[(y1 * src_w + x1) * 4 + c];
                float v = v00 * (1 - xt) * (1 - yt) + v10 * xt * (1 - yt)
                        + v01 * (1 - xt) * yt       + v11 * xt * yt;
                out[(y * dst_w + x) * 4 + c] =
                    static_cast<std::uint8_t>(std::min(255.0f, std::max(0.0f, v + 0.5f)));
            }
        }
    }
    return out;
}

std::vector<std::vector<std::uint8_t>> build_chain_rgba8(
    const std::uint8_t* base, int w, int h, int count,
    std::vector<std::pair<int, int>>& out_sizes) {
    std::vector<std::vector<std::uint8_t>> out;
    out_sizes.clear();
    out.emplace_back(base, base + static_cast<std::size_t>(w) * h * 4);
    out_sizes.emplace_back(w, h);
    int cur_w = w, cur_h = h;
    for (int i = 1; i < count; ++i) {
        if (cur_w <= 1 && cur_h <= 1) break;
        int nw = 0, nh = 0;
        auto next = downsample_rgba8(out.back().data(), cur_w, cur_h, nw, nh);
        out.push_back(std::move(next));
        out_sizes.emplace_back(nw, nh);
        cur_w = nw;
        cur_h = nh;
    }
    return out;
}

}  // namespace ast26::mip
