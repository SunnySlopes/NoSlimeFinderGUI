#include "visualizer.hpp"

#include "geometry.hpp"
#include "slime_check.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// RGB image
// ---------------------------------------------------------------------------
struct Image {
    int w, h;
    std::vector<uint8_t> px;  // RGB, row-major
    Image(int w_, int h_, uint8_t r, uint8_t g, uint8_t b)
        : w(w_), h(h_), px(static_cast<size_t>(w_) * h_ * 3) {
        for (size_t i = 0; i < px.size(); i += 3) { px[i] = r; px[i + 1] = g; px[i + 2] = b; }
    }
    void set(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        size_t i = (static_cast<size_t>(y) * w + x) * 3;
        px[i] = r; px[i + 1] = g; px[i + 2] = b;
    }
    // 按覆盖率 a(0~255)把颜色叠加到背景上,用于抗锯齿描边。
    void blend(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        if (x < 0 || y < 0 || x >= w || y >= h || a == 0) return;
        size_t i = (static_cast<size_t>(y) * w + x) * 3;
        px[i]     = static_cast<uint8_t>((r * a + px[i]     * (255 - a)) / 255);
        px[i + 1] = static_cast<uint8_t>((g * a + px[i + 1] * (255 - a)) / 255);
        px[i + 2] = static_cast<uint8_t>((b * a + px[i + 2] * (255 - a)) / 255);
    }
    void fillRect(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b) {
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) set(x, y, r, g, b);
    }
};

// ---------------------------------------------------------------------------
// 内置 5x7 位图字体(大写字母 + 数字 + 少量符号)。每个字形 7 行,每行低 5 位,
// bit4 是最左列。纯位图绘制,无需任何外部字体文件,像素风清晰锐利。
// ---------------------------------------------------------------------------
const uint8_t* glyph(char c) {
    static const uint8_t SP[7]    = {0,0,0,0,0,0,0};
    static const uint8_t COLON[7] = {0,0x04,0,0,0x04,0,0};
    static const uint8_t MINUS[7] = {0,0,0,0x1F,0,0,0};
    static const uint8_t DOT[7]   = {0,0,0,0,0,0,0x04};
    static const uint8_t LP[7]    = {0x02,0x04,0x08,0x08,0x08,0x04,0x02};
    static const uint8_t RP[7]    = {0x08,0x04,0x02,0x02,0x02,0x04,0x08};
    static const uint8_t COMMA[7] = {0,0,0,0,0,0x04,0x08};
    static const uint8_t SLASH[7] = {0x01,0x02,0x02,0x04,0x08,0x08,0x10};

    static const uint8_t D[10][7] = {
        {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, // 0
        {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // 1
        {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, // 2
        {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, // 3
        {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // 4
        {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // 5
        {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, // 6
        {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, // 7
        {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // 8
        {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, // 9
    };
    static const uint8_t A[26][7] = {
        {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, // A
        {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // B
        {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, // C
        {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, // D
        {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, // E
        {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // F
        {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, // G
        {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // H
        {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // I
        {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, // J
        {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // K
        {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // L
        {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, // M
        {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, // N
        {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // O
        {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // P
        {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, // Q
        {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // R
        {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, // S
        {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // T
        {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // U
        {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, // V
        {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, // W
        {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // X
        {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, // Y
        {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // Z
    };

    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (c >= '0' && c <= '9') return D[c - '0'];
    if (c >= 'A' && c <= 'Z') return A[c - 'A'];
    switch (c) {
        case ':': return COLON;
        case '-': return MINUS;
        case '.': return DOT;
        case '(': return LP;
        case ')': return RP;
        case ',': return COMMA;
        case '/': return SLASH;
        default:  return SP;
    }
}

// 画一个字形。scale 为整数放大倍率。
void drawChar(Image& img, int x, int y, char c, int scale,
              uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t* gl = glyph(c);
    for (int row = 0; row < 7; ++row)
        for (int col = 0; col < 5; ++col)
            if (gl[row] & (1 << (4 - col)))
                img.fillRect(x + col * scale, y + row * scale,
                             x + (col + 1) * scale, y + (row + 1) * scale, r, g, b);
}

// 每个字符步进宽度 = 6*scale(5px 字形 + 1px 间隙)。
int textWidth(const std::string& s, int scale) {
    return static_cast<int>(s.size()) * 6 * scale;
}

// 左对齐画一行文字,返回结束时的 x。
int drawText(Image& img, int x, int y, const std::string& s, int scale,
             uint8_t r, uint8_t g, uint8_t b) {
    int cx = x;
    for (char c : s) {
        drawChar(img, cx, y, c, scale, r, g, b);
        cx += 6 * scale;
    }
    return cx;
}

// 把超采样的高分辨率图像用盒式滤波缩小 ss 倍(实现抗锯齿)。
Image downsample(const Image& hi, int ss) {
    Image out(hi.w / ss, hi.h / ss, 0, 0, 0);
    for (int y = 0; y < out.h; ++y) {
        for (int x = 0; x < out.w; ++x) {
            int sr = 0, sg = 0, sb = 0;
            for (int dy = 0; dy < ss; ++dy) {
                for (int dx = 0; dx < ss; ++dx) {
                    size_t i = (static_cast<size_t>(y * ss + dy) * hi.w +
                                (x * ss + dx)) * 3;
                    sr += hi.px[i]; sg += hi.px[i + 1]; sb += hi.px[i + 2];
                }
            }
            int n = ss * ss;
            out.set(x, y, static_cast<uint8_t>(sr / n),
                    static_cast<uint8_t>(sg / n), static_cast<uint8_t>(sb / n));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// PNG encoder (stored/uncompressed DEFLATE, no external deps)
// ---------------------------------------------------------------------------
uint32_t crcTable[256];
bool crcReady = false;
void initCrc() {
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crcTable[n] = c;
    }
    crcReady = true;
}
uint32_t crc32(const uint8_t* p, size_t n) {
    if (!crcReady) initCrc();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) c = crcTable[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
uint32_t adler32(const uint8_t* p, size_t n) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < n; ++i) { a = (a + p[i]) % 65521; b = (b + a) % 65521; }
    return (b << 16) | a;
}
void be32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((x >> 24) & 0xFF); v.push_back((x >> 16) & 0xFF);
    v.push_back((x >> 8) & 0xFF);  v.push_back(x & 0xFF);
}
void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    be32(out, static_cast<uint32_t>(data.size()));
    std::vector<uint8_t> td(data.begin(), data.end());
    td.insert(td.begin(), type, type + 4);
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    be32(out, crc32(td.data(), td.size()));
}

bool writePng(const Image& img, const std::string& path) {
    // Raw scanlines with a leading filter byte (0 = none).
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(img.h) * (img.w * 3 + 1));
    for (int y = 0; y < img.h; ++y) {
        raw.push_back(0);
        const uint8_t* row = &img.px[static_cast<size_t>(y) * img.w * 3];
        raw.insert(raw.end(), row, row + img.w * 3);
    }

    // zlib stream: header + stored DEFLATE blocks + adler32.
    std::vector<uint8_t> z;
    z.push_back(0x78); z.push_back(0x01);
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t n = std::min<size_t>(65535, raw.size() - pos);
        z.push_back(pos + n >= raw.size() ? 1 : 0);   // BFINAL
        z.push_back(n & 0xFF); z.push_back((n >> 8) & 0xFF);
        uint16_t nn = static_cast<uint16_t>(~n);
        z.push_back(nn & 0xFF); z.push_back((nn >> 8) & 0xFF);
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    }
    be32(z, adler32(raw.data(), raw.size()));

    std::vector<uint8_t> out = {137, 80, 78, 71, 13, 10, 26, 10};
    std::vector<uint8_t> ihdr;
    be32(ihdr, img.w); be32(ihdr, img.h);
    ihdr.push_back(8); ihdr.push_back(2); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});

    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
    return true;
}

std::string i64str(int64_t v) { return std::to_string(v); }

}  // namespace

bool renderMap(const Params& params, const Result& result, const std::string& path) {
    // 玩家所在区块,以及能同时容纳内外圆的视野窗口大小。
    const int32_t pcx = static_cast<int32_t>(floorDiv(result.playerBlockX, 16));
    const int32_t pcz = static_cast<int32_t>(floorDiv(result.playerBlockZ, 16));
    const int viewR = (params.outerRadius + 15) / 16 + 2;   // 每侧区块数
    const int viewChunks = 2 * viewR + 1;

    // ss = 超采样倍率。先在 ss 倍分辨率下绘制地图,再缩小,得到平滑边缘。
    const int ss   = 4;
    const int cell = 34 * ss;    // 每个区块的像素边长(高分辨率下)
    const int gridW = viewChunks * cell;
    const int gridH = viewChunks * cell;
    const double ppb = static_cast<double>(cell) / 16.0;   // 每方块像素数

    Image img(gridW, gridH, 245, 245, 245);

    // 视野窗口左上角对应的方块坐标。
    const int64_t viewMinBX = static_cast<int64_t>(pcx - viewR) * 16;
    const int64_t viewMinBZ = static_cast<int64_t>(pcz - viewR) * 16;

    // 区块底色:史莱姆区块绿色,普通区块白色。(x -> 屏幕X, z -> 屏幕Y)
    for (int ix = 0; ix < viewChunks; ++ix) {
        for (int iz = 0; iz < viewChunks; ++iz) {
            int32_t cx = pcx - viewR + ix;
            int32_t cz = pcz - viewR + iz;
            bool slime = isSlimeChunk(params.worldSeed, cx, cz);
            int x0 = ix * cell, y0 = iz * cell;
            if (slime) img.fillRect(x0, y0, x0 + cell, y0 + cell, 96, 200, 104);
            else       img.fillRect(x0, y0, x0 + cell, y0 + cell, 250, 250, 250);
        }
    }

    // 网格线(铺满整格宽度,消除右侧白边)。
    const int lw = std::max(1, ss / 2);
    for (int i = 0; i <= viewChunks; ++i) {
        int gx = std::min(i * cell, gridW - lw);
        int gy = std::min(i * cell, gridH - lw);
        img.fillRect(gx, 0, gx + lw, gridH, 205, 205, 205);
        img.fillRect(0, gy, gridW, gy + lw, 205, 205, 205);
    }

    // 方块坐标 -> 屏幕像素(方块中心)。
    auto bx2px = [&](int64_t bx) { return (static_cast<double>(bx - viewMinBX) + 0.5) * ppb; };
    auto bz2py = [&](int64_t bz) { return (static_cast<double>(bz - viewMinBZ) + 0.5) * ppb; };

    const double cxp = bx2px(result.playerBlockX);
    const double cyp = bz2py(result.playerBlockZ);

    // 刷怪有效区高亮:有效环形区(inner < 距离 <= outer)保持原色清晰,
    // 其余区域(中心内圈 + 外圈之外)叠加淡灰半透明蒙版压暗,突出有效范围。
    {
        double rIn  = params.innerRadius * ppb;
        double rOut = params.outerRadius * ppb;
        double in2  = rIn * rIn;
        double out2 = rOut * rOut;
        const double alpha = 0.22;   // 蒙版不透明度(越大越暗)
        for (int y = 0; y < gridH; ++y) {
            for (int x = 0; x < img.w; ++x) {
                double dx = x + 0.5 - cxp, dy = y + 0.5 - cyp;
                double d2 = dx * dx + dy * dy;
                // 有效环形区「之外」才压暗
                if (d2 <= in2 || d2 > out2) {
                    size_t i = (static_cast<size_t>(y) * img.w + x) * 3;
                    img.px[i]     = static_cast<uint8_t>(img.px[i]     * (1 - alpha) + 90 * alpha);
                    img.px[i + 1] = static_cast<uint8_t>(img.px[i + 1] * (1 - alpha) + 90 * alpha);
                    img.px[i + 2] = static_cast<uint8_t>(img.px[i + 2] * (1 - alpha) + 90 * alpha);
                }
            }
        }
    }

    // 内外半径黑色描边圆环(抗锯齿:按到目标半径的距离羽化)。
    {
        auto ring = [&](double radiusBlocks) {
            double rr = radiusBlocks * ppb;
            double thick = 1.4 * ss;   // 线宽(高分辨率像素)
            int x0 = std::max(0, static_cast<int>(cxp - rr - thick - 2));
            int x1 = std::min(gridW - 1, static_cast<int>(cxp + rr + thick + 2));
            int y0 = std::max(0, static_cast<int>(cyp - rr - thick - 2));
            int y1 = std::min(gridH - 1, static_cast<int>(cyp + rr + thick + 2));
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    double dx = x + 0.5 - cxp, dy = y + 0.5 - cyp;
                    double d = std::sqrt(dx * dx + dy * dy);
                    double t = std::fabs(d - rr);
                    if (t <= thick) {
                        double a = std::min(1.0, (thick - t));  // 边缘羽化
                        img.blend(x, y, 20, 20, 20, static_cast<uint8_t>(a * 255));
                    }
                }
            }
        };
        ring(params.innerRadius);
        ring(params.outerRadius);
    }

    // 红色玩家点:画成实心圆盘(超采样后为圆形)。
    {
        double rp = 4.5 * ss;
        double r2 = rp * rp;
        int x0 = static_cast<int>(cxp - rp - 1), x1 = static_cast<int>(cxp + rp + 1);
        int y0 = static_cast<int>(cyp - rp - 1), y1 = static_cast<int>(cyp + rp + 1);
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                double dx = x + 0.5 - cxp, dy = y + 0.5 - cyp;
                if (dx * dx + dy * dy <= r2) img.set(x, y, 224, 48, 48);
            }
        }
    }

    // 地图区超采样缩小 -> 抗锯齿。
    Image map = downsample(img, ss);
    const int W = map.w;

    // ---- 组装最终图:上方地图 + 下方深色文字面板(位图字体,纯像素绘制)----
    // 面板行:标题 + 6 个 (标签, 数值) 行。标签浅灰,数值高亮绿。
    struct Row { std::string label, value; };
    const std::vector<Row> rows = {
        {"X", i64str(result.playerBlockX)},
        {"Z", i64str(result.playerBlockZ)},
        {"EFFECTIVE SLIME CHUNKS", std::to_string(result.effectiveSlimeChunks)},
        {"EFFECTIVE SPAWN AREA",   i64str(result.effectiveSpawnArea)},
        {"INNER RADIUS",           std::to_string(params.innerRadius)},
        {"OUTER RADIUS",           std::to_string(params.outerRadius)},
    };

    // 字号随图宽自适应。scale 为整数放大倍率。
    const int scale  = std::max(2, W / 300);
    const int chW    = 6 * scale;               // 单字符步进宽度
    const int glyphH = 7 * scale;               // 字形高度
    const int pad    = std::max(16, W / 28);    // 面板内边距
    const int titleScale = scale + 1;
    const int lineH  = glyphH + std::max(8, scale * 5);  // 行高

    // 标签统一左对齐到 pad(与标题 PLAYER POSITION 左边缘对齐),数值对齐到同一列。
    int maxLabel = 0;
    for (const auto& r : rows)
        maxLabel = std::max(maxLabel, textWidth(r.label, scale));
    const int colonX  = pad + maxLabel + chW;   // 冒号所在 x(最长标签之后)
    const int valueX  = colonX + 2 * chW;       // 数值起始 x

    const int titleH  = 7 * titleScale;
    const int panelH  = pad + titleH + std::max(10, scale * 6) +
                        lineH * static_cast<int>(rows.size()) + pad;

    Image out(W, map.h + panelH, 0, 0, 0);
    for (int y = 0; y < map.h; ++y)
        for (int x = 0; x < W; ++x) {
            size_t s = (static_cast<size_t>(y) * W + x) * 3;
            out.set(x, y, map.px[s], map.px[s + 1], map.px[s + 2]);
        }
    out.fillRect(0, map.h, W, map.h + panelH, 26, 28, 34);   // 面板深色背景

    // 面板顶部一条高亮分隔线。
    out.fillRect(0, map.h, W, map.h + std::max(2, scale), 96, 200, 104);

    const uint8_t LBL_R = 170, LBL_G = 176, LBL_B = 188;  // 标签色(浅灰)
    const uint8_t VAL_R = 150, VAL_G = 214, VAL_B = 120;  // 数值色(淡绿)
    const uint8_t TTL_R = 236, TTL_G = 238, TTL_B = 242;  // 标题色(近白)

    int y = map.h + pad;
    // 标题
    drawText(out, pad, y, "PLAYER POSITION", titleScale, TTL_R, TTL_G, TTL_B);
    y += titleH + std::max(10, scale * 6);

    for (const auto& r : rows) {
        drawText(out, pad, y, r.label, scale, LBL_R, LBL_G, LBL_B);  // 左对齐到 pad
        drawChar(out, colonX, y, ':', scale, LBL_R, LBL_G, LBL_B);
        drawText(out, valueX, y, r.value, scale, VAL_R, VAL_G, VAL_B);
        y += lineH;
    }

    return writePng(out, path);
}
