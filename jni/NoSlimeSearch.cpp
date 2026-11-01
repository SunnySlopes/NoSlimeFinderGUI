#include "NoSlimeSearch.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "cubiomes/biomes.h"
#include "cubiomes/biomenoise.h"
#include "cubiomes/generator.h"
#include "cubiomes/util.h"

#include "geometry.hpp"
#include "slime_check.hpp"

namespace
{

constexpr int kInner = NOSLIME_RING_INNER;
constexpr int kOuter = NOSLIME_RING_OUTER;
constexpr int kRingTotal = NOSLIME_RING_TOTAL;
constexpr int kDonutChunks = NOSLIME_DONUT_CHUNKS;
constexpr int kCell = 64;
constexpr int kCoarseCellTile = 128; /* 128x128 cells = 8192x8192 blocks */
constexpr int kCoarseTargetMin = 1000;
constexpr int kCoarseTargetPerN = 10;
constexpr int kCoarseTargetMax = 1000;
constexpr int kExactLimit = 200;
constexpr int kCoarseDonutLimit = 10;
constexpr int kMaxThreads = 256;
constexpr int kChunksPerCell = kCell / 16;

struct Span
{
    int32_t dz;
    int32_t lo;
    int32_t hi;
};

struct CellBest
{
    int32_t slime = std::numeric_limits<int32_t>::max();
    int64_t dist = std::numeric_limits<int64_t>::max();
    int32_t x = 0;
    int32_t z = 0;
};

/* Coarse cell candidate selected by the radar-like donut prefilter. */
struct PreCell
{
    int32_t cell_x = 0;
    int32_t cell_z = 0;
    int32_t min_donut = std::numeric_limits<int32_t>::max();
    int64_t dist = 0;
};

struct PreWorse
{
    bool operator()(const PreCell &a, const PreCell &b) const
    {
        if (a.min_donut != b.min_donut)
            return a.min_donut < b.min_donut;
        if (a.dist != b.dist)
            return a.dist < b.dist;
        if (a.cell_x != b.cell_x)
            return a.cell_x < b.cell_x;
        return a.cell_z < b.cell_z;
    }
};

bool better_pre(const PreCell &a, const PreCell &b)
{
    if (a.min_donut != b.min_donut)
        return a.min_donut < b.min_donut;
    if (a.dist != b.dist)
        return a.dist < b.dist;
    if (a.cell_x != b.cell_x)
        return a.cell_x < b.cell_x;
    return a.cell_z < b.cell_z;
}

struct Cand
{
    int32_t x = 0;
    int32_t z = 0;
    int32_t slime = 0;
    int64_t dist = 0;
};

struct Coarse
{
    int32_t x = 0;
    int32_t z = 0;
    int32_t slime = 0;
    int32_t avoid_x = 0;
    int32_t avoid_y = 0;
    int32_t area = 0;
    int64_t dist = 0;
};

struct Exact
{
    NoSlimeResult r{};
    int64_t dist = 0;
};

inline int64_t now_ms()
{
    using namespace std::chrono;
    return static_cast<int64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
            .count());
}

std::vector<Span> build_spans()
{
    const std::vector<RingSpan> in = computeRingSpans(kInner, kOuter);
    std::vector<Span> out;
    out.reserve(in.size());
    int64_t total = 0;
    for (const RingSpan &s : in)
    {
        out.push_back(Span{s.dz, s.loDx, s.hiDx});
        total += static_cast<int64_t>(s.hiDx) - s.loDx + 1;
    }
    if (total != kRingTotal)
    {
        out.clear();
    }
    return out;
}

/* The 192-chunk donut is a fixed metric: 1 < dx^2+dz^2 <= 64. */
std::vector<std::pair<int8_t, int8_t>> build_donut()
{
    std::vector<std::pair<int8_t, int8_t>> out;
    out.reserve(kDonutChunks);
    for (int dz = -8; dz <= 8; ++dz)
    {
        for (int dx = -8; dx <= 8; ++dx)
        {
            const int d2 = dx * dx + dz * dz;
            if (d2 > 1 && d2 <= 64)
                out.emplace_back(static_cast<int8_t>(dx),
                                 static_cast<int8_t>(dz));
        }
    }
    if (static_cast<int>(out.size()) != kDonutChunks)
        out.clear();
    return out;
}

const std::vector<Span> g_spans = build_spans();
const std::vector<std::pair<int8_t, int8_t>> g_donut = build_donut();

inline bool in_donut(int dx, int dz)
{
    const int d2 = dx * dx + dz * dz;
    return d2 > 1 && d2 <= 64;
}

struct DonutRun
{
    int dz;
    int lo;
    int hi;
};

/* 17x17 donut decomposed into horizontal runs (radar-style). */
std::vector<DonutRun> build_donut_runs()
{
    std::vector<DonutRun> runs;
    runs.reserve(32);
    for (int dz = -8; dz <= 8; ++dz)
    {
        int lo = 0;
        bool open = false;
        for (int dx = -8; dx <= 8; ++dx)
        {
            if (in_donut(dx, dz))
            {
                if (!open)
                {
                    lo = dx;
                    open = true;
                }
            }
            else if (open)
            {
                runs.push_back(DonutRun{dz, lo, dx - 1});
                open = false;
            }
        }
        if (open)
            runs.push_back(DonutRun{dz, lo, 8});
    }
    return runs;
}

const std::vector<DonutRun> g_donut_runs = build_donut_runs();

std::mutex g_api_mutex;
std::atomic<int> g_stage{NOSLIME_STAGE_IDLE};
std::atomic<int> g_done{1};
std::atomic<int> g_paused{0};
std::atomic<int> g_cancelled{0};
std::atomic<int> g_error{0};
std::atomic<int64_t> g_cur{0};
std::atomic<int64_t> g_total{0};
std::atomic<int64_t> g_start_ms{0};

std::mutex g_message_mutex;
std::string g_message;

std::mutex g_result_mutex;
std::vector<NoSlimeResult> g_results;

void set_error(const char *msg)
{
    {
        std::lock_guard<std::mutex> lock(g_message_mutex);
        g_message = msg ? msg : "internal error";
    }
    g_error.store(1, std::memory_order_relaxed);
}

bool cancelled()
{
    return g_cancelled.load(std::memory_order_relaxed) != 0;
}

/* Returns false when the search must stop. */
bool checkpoint()
{
    if (cancelled())
        return false;
    while (g_paused.load(std::memory_order_relaxed) != 0)
    {
        if (cancelled())
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return !cancelled();
}

struct ThreadGenerator
{
    Generator g{};
    int mc = -1;
    uint64_t seed = 0;
    bool ready = false;
};

ThreadGenerator &thread_generator(int mc, uint64_t seed)
{
    static thread_local ThreadGenerator tg;
    if (!tg.ready || tg.mc != mc)
    {
        setupGenerator(&tg.g, mc, 0);
        tg.mc = mc;
        tg.seed = seed;
        tg.ready = true;
        applySeed(&tg.g, DIM_OVERWORLD, seed);
    }
    else if (tg.seed != seed)
    {
        applySeed(&tg.g, DIM_OVERWORLD, seed);
        tg.seed = seed;
    }
    return tg;
}

struct AvoidTable
{
    bool flag[256] = {};
};

bool is_avoid(const AvoidTable &a, int id)
{
    return id >= 0 && id < 256 && a.flag[id];
}

/* Both evaluate the same fixed ring; see slimeoptimizer/geometry.cpp. */
int64_t ring_area_sum()
{
    int64_t total = 0;
    for (const Span &s : g_spans)
        total += static_cast<int64_t>(s.hi) - s.lo + 1;
    return total;
}

/*
 * Radar-like coarse prefilter: over large 128x128-cell tiles (8192x8192
 * blocks) build a 1-bit-per-chunk slime bitmap and a summed-area table,
 * then compute the exact 17x17 donut count for every chunk center in each
 * 4x4-chunk cell. Cells with the smallest minimum donut count are kept.
 * This is the low-slime counterpart of SlimeRadar's hotspot scan.
 */
std::vector<PreCell> select_low_slime_cells(const NoSlimeParams &p,
    int64_t ax0, int64_t ax1, int64_t az0, int64_t az1,
    int32_t target, int threads)
{
    if (target <= 0)
        target = 1;
    const int64_t cell_x0 = floorDiv(ax0, kCell);
    const int64_t cell_x1 = floorDiv(ax1, kCell);
    const int64_t cell_z0 = floorDiv(az0, kCell);
    const int64_t cell_z1 = floorDiv(az1, kCell);
    const int64_t cols = cell_x1 - cell_x0 + 1;
    const int64_t rows = cell_z1 - cell_z0 + 1;
    const int64_t total_cells = cols * rows;
    if (total_cells <= 0)
        return {};

    if (total_cells <= target)
    {
        std::vector<PreCell> all;
        all.reserve(static_cast<size_t>(total_cells));
        for (int64_t cz = cell_z0; cz <= cell_z1; ++cz)
        {
            for (int64_t cx = cell_x0; cx <= cell_x1; ++cx)
            {
                const int64_t bx = cx * kCell + kCell / 2;
                const int64_t bz = cz * kCell + kCell / 2;
                all.push_back(PreCell{static_cast<int32_t>(cx),
                                      static_cast<int32_t>(cz), 0,
                                      bx * bx + bz * bz});
            }
        }
        return all;
    }

    g_stage.store(NOSLIME_STAGE_PREFILTER, std::memory_order_relaxed);
    g_cur.store(0, std::memory_order_relaxed);
    g_total.store(total_cells, std::memory_order_relaxed);

    const int tile_cols = static_cast<int>(
        (cols + kCoarseCellTile - 1) / kCoarseCellTile);
    const int tile_rows = static_cast<int>(
        (rows + kCoarseCellTile - 1) / kCoarseCellTile);
    const int64_t total_tiles =
        static_cast<int64_t>(tile_cols) * tile_rows;
    const int nthreads = std::max(1, std::min(threads,
        static_cast<int>(std::min<int64_t>(total_tiles, kMaxThreads))));

    std::vector<std::priority_queue<PreCell, std::vector<PreCell>, PreWorse>>
        heaps(static_cast<size_t>(nthreads));
    std::atomic<int64_t> next_tile{0};
    std::atomic<bool> failed{false};

    auto worker = [&](int tid)
    {
        std::vector<uint8_t> slime;
        std::vector<uint32_t> sat;
        auto &heap = heaps[static_cast<size_t>(tid)];
        for (;;)
        {
            const int64_t tile = next_tile.fetch_add(1, std::memory_order_relaxed);
            if (tile >= total_tiles)
                break;
            if (!checkpoint())
                break;
            try
            {
                const int64_t tx = tile % tile_cols;
                const int64_t tz = tile / tile_cols;
                const int32_t cell_x = static_cast<int32_t>(
                    cell_x0 + tx * kCoarseCellTile);
                const int32_t cell_z = static_cast<int32_t>(
                    cell_z0 + tz * kCoarseCellTile);
                const int cell_w = static_cast<int>(
                    std::min<int64_t>(kCoarseCellTile, cell_x1 - cell_x + 1));
                const int cell_h = static_cast<int>(
                    std::min<int64_t>(kCoarseCellTile, cell_z1 - cell_z + 1));
                if (cell_w <= 0 || cell_h <= 0)
                    continue;

                const int32_t bx0 = cell_x * 4 - 8;
                const int32_t bz0 = cell_z * 4 - 8;
                const int bw = cell_w * 4 + 16;
                const int bh = cell_h * 4 + 16;
                slime.assign(static_cast<size_t>(bw) * bh, 0);
                for (int y = 0; y < bh; ++y)
                {
                    const int32_t cz = bz0 + y;
                    uint8_t *row = slime.data() + static_cast<size_t>(y) * bw;
                    for (int x = 0; x < bw; ++x)
                        row[x] = isSlimeChunk(p.seed, bx0 + x, cz) ? 1u : 0u;
                }

                const int sw = bw + 1;
                sat.assign(static_cast<size_t>(sw) * (bh + 1), 0);
                for (int y = 0; y < bh; ++y)
                {
                    uint32_t row_sum = 0;
                    const uint8_t *srow = slime.data() + static_cast<size_t>(y) * bw;
                    for (int x = 0; x < bw; ++x)
                    {
                        row_sum += srow[x];
                        sat[static_cast<size_t>(y + 1) * sw + (x + 1)] =
                            sat[static_cast<size_t>(y) * sw + (x + 1)] + row_sum;
                    }
                }

                for (int cj = 0; cj < cell_h; ++cj)
                {
                    for (int ci = 0; ci < cell_w; ++ci)
                    {
                        const int32_t cx = cell_x + ci;
                        const int32_t cz = cell_z + cj;
                        int32_t min_donut = std::numeric_limits<int32_t>::max();
                        for (int oz = 0; oz < 4; ++oz)
                        {
                            for (int ox = 0; ox < 4; ++ox)
                            {
                                const int lx = static_cast<int>(cx * 4 + ox - bx0);
                                const int lz = static_cast<int>(cz * 4 + oz - bz0);
                                int32_t donut = 0;
                                for (const DonutRun &run : g_donut_runs)
                                {
                                    const int row = lz + run.dz;
                                    const int xa = lx + run.lo;
                                    const int xb = lx + run.hi + 1;
                                    const size_t r0 =
                                        static_cast<size_t>(row) * sw;
                                    const size_t r1 = r0 + sw;
                                    donut += static_cast<int32_t>(
                                        (sat[r1 + xb] - sat[r0 + xb]) -
                                        (sat[r1 + xa] - sat[r0 + xa]));
                                }
                                if (donut < min_donut)
                                    min_donut = donut;
                            }
                        }
                        if (min_donut > kCoarseDonutLimit)
                            continue;
                        const int64_t ddx = static_cast<int64_t>(cx) * kCell + kCell / 2;
                        const int64_t ddz = static_cast<int64_t>(cz) * kCell + kCell / 2;
                        PreCell pc{static_cast<int32_t>(cx),
                                   static_cast<int32_t>(cz), min_donut,
                                   ddx * ddx + ddz * ddz};
                        if (static_cast<int>(heap.size()) < target)
                        {
                            heap.push(pc);
                        }
                        else if (better_pre(pc, heap.top()))
                        {
                            heap.pop();
                            heap.push(pc);
                        }
                    }
                }
                g_cur.fetch_add(static_cast<int64_t>(cell_w) * cell_h,
                                std::memory_order_relaxed);
            }
            catch (const std::bad_alloc &)
            {
                failed.store(true, std::memory_order_relaxed);
                set_error("out of memory during coarse low-slime prefilter");
                break;
            }
            catch (const std::exception &e)
            {
                failed.store(true, std::memory_order_relaxed);
                set_error(e.what());
                break;
            }
            catch (...)
            {
                failed.store(true, std::memory_order_relaxed);
                set_error("unknown error during coarse prefilter");
                break;
            }
        }
    };

    {
        std::vector<std::thread> pool;
        pool.reserve(static_cast<size_t>(nthreads - 1));
        for (int i = 0; i < nthreads - 1; ++i)
            pool.emplace_back(worker, i);
        worker(nthreads - 1);
        for (auto &th : pool)
            th.join();
    }

    if (cancelled() || failed.load() || g_error.load())
        return {};

    std::priority_queue<PreCell, std::vector<PreCell>, PreWorse> merged;
    for (auto &heap : heaps)
    {
        while (!heap.empty())
        {
            PreCell pc = heap.top();
            heap.pop();
            if (static_cast<int>(merged.size()) < target)
                merged.push(pc);
            else if (better_pre(pc, merged.top()))
            {
                merged.pop();
                merged.push(pc);
            }
        }
    }

    std::vector<PreCell> out;
    out.reserve(static_cast<size_t>(merged.size()));
    while (!merged.empty())
    {
        out.push_back(merged.top());
        merged.pop();
    }
    std::sort(out.begin(), out.end(), [](const PreCell &a, const PreCell &b) {
        if (a.min_donut != b.min_donut)
            return a.min_donut < b.min_donut;
        if (a.dist != b.dist)
            return a.dist < b.dist;
        if (a.cell_x != b.cell_x)
            return a.cell_x < b.cell_x;
        return a.cell_z < b.cell_z;
    });
    return out;
}

/* Exact slime-area scan for the selected 64x64 cells only. */
std::vector<Cand> scan_selected_cells(const NoSlimeParams &p,
    int64_t ax0, int64_t ax1, int64_t az0, int64_t az1,
    const std::vector<PreCell> &cells, int threads)
{
    if (cells.empty())
        return {};
    g_stage.store(NOSLIME_STAGE_SCAN, std::memory_order_relaxed);
    g_cur.store(0, std::memory_order_relaxed);
    g_total.store(static_cast<int64_t>(cells.size()), std::memory_order_relaxed);

    std::vector<Cand> out(cells.size());
    std::vector<uint8_t> valid(cells.size(), 0);
    std::atomic<int64_t> next_cell{0};
    std::atomic<bool> failed{false};

    auto worker = [&]()
    {
        std::vector<uint8_t> mask;
        std::vector<uint32_t> pref;
        for (;;)
        {
            const int64_t i = next_cell.fetch_add(1, std::memory_order_relaxed);
            if (i >= static_cast<int64_t>(cells.size()))
                break;
            if (!checkpoint())
                break;
            try
            {
                const PreCell &pc = cells[static_cast<size_t>(i)];
                const int64_t x0 = std::max(ax0, static_cast<int64_t>(pc.cell_x) * kCell);
                const int64_t x1 = std::min(ax1, static_cast<int64_t>(pc.cell_x) * kCell + kCell - 1);
                const int64_t z0 = std::max(az0, static_cast<int64_t>(pc.cell_z) * kCell);
                const int64_t z1 = std::min(az1, static_cast<int64_t>(pc.cell_z) * kCell + kCell - 1);
                if (x0 > x1 || z0 > z1)
                    continue;

                const int64_t mx0 = x0 - kOuter;
                const int64_t mx1 = x1 + kOuter;
                const int64_t mz0 = z0 - kOuter;
                const int64_t mz1 = z1 + kOuter;
                const int64_t cmin = floorDiv(mx0, 16);
                const int64_t cmax = floorDiv(mx1, 16);
                const int64_t rmin = floorDiv(mz0, 16);
                const int64_t rmax = floorDiv(mz1, 16);
                const int cw = static_cast<int>(cmax - cmin + 1);
                const int ch = static_cast<int>(rmax - rmin + 1);
                if (cw <= 0 || ch <= 0)
                    continue;

                mask.assign(static_cast<size_t>(cw) * ch, 0);
                for (int r = 0; r < ch; ++r)
                {
                    const int32_t cz = static_cast<int32_t>(rmin + r);
                    for (int c = 0; c < cw; ++c)
                    {
                        const int32_t cx = static_cast<int32_t>(cmin + c);
                        mask[static_cast<size_t>(r) * cw + c] =
                            isSlimeChunk(p.seed, cx, cz) ? 1u : 0u;
                    }
                }

                const int64_t bw = mx1 - mx0 + 1;
                const int64_t br = mz1 - mz0 + 1;
                pref.assign(static_cast<size_t>(br) * static_cast<size_t>(bw + 1), 0);
                for (int64_t r = 0; r < br; ++r)
                {
                    const int64_t z = mz0 + r;
                    const int64_t cz = floorDiv(z, 16) - rmin;
                    const uint8_t *srow = mask.data() +
                        static_cast<size_t>(cz) * cw;
                    uint32_t *row = pref.data() +
                        static_cast<size_t>(r) * (bw + 1);
                    uint32_t acc = 0;
                    row[0] = 0;
                    for (int64_t x = mx0; x <= mx1;)
                    {
                        const int64_t cx = floorDiv(x, 16);
                        const int64_t seg_end = std::min(mx1, cx * 16 + 15);
                        const uint32_t v = srow[cx - cmin];
                        const int64_t base = x - mx0;
                        for (int64_t k = 0; k <= seg_end - x; ++k)
                        {
                            acc += v;
                            row[base + k + 1] = acc;
                        }
                        x = seg_end + 1;
                    }
                }

                CellBest best;
                for (int64_t pz = z0; pz <= z1; ++pz)
                {
                    if (((pz - z0) & 15) == 0 && !checkpoint())
                        break;
                    const int64_t row_off_base = (pz - mz0) * (bw + 1);
                    for (int64_t px = x0; px <= x1; ++px)
                    {
                        uint32_t area = 0;
                        for (const Span &s : g_spans)
                        {
                            const int64_t row_off =
                                row_off_base + s.dz * (bw + 1);
                            const uint32_t *row = pref.data() + row_off;
                            const int64_t lo = px + s.lo - mx0;
                            const int64_t hi1 = px + s.hi + 1 - mx0;
                            area += row[hi1] - row[lo];
                        }
                        const int64_t dist = px * px + pz * pz;
                        const int32_t slime = static_cast<int32_t>(area);
                        if (slime < best.slime ||
                            (slime == best.slime &&
                             (dist < best.dist ||
                              (dist == best.dist &&
                               (px < best.x ||
                                (px == best.x && pz < best.z))))))
                        {
                            best.slime = slime;
                            best.dist = dist;
                            best.x = static_cast<int32_t>(px);
                            best.z = static_cast<int32_t>(pz);
                        }
                    }
                }
                if (best.slime != std::numeric_limits<int32_t>::max())
                {
                    out[static_cast<size_t>(i)] =
                        Cand{best.x, best.z, best.slime, best.dist};
                    valid[static_cast<size_t>(i)] = 1;
                }
                g_cur.fetch_add(1, std::memory_order_relaxed);
            }
            catch (const std::bad_alloc &)
            {
                failed.store(true, std::memory_order_relaxed);
                set_error("out of memory during candidate-cell slime scan");
                break;
            }
            catch (const std::exception &e)
            {
                failed.store(true, std::memory_order_relaxed);
                set_error(e.what());
                break;
            }
            catch (...)
            {
                failed.store(true, std::memory_order_relaxed);
                set_error("unknown error during candidate-cell scan");
                break;
            }
        }
    };

    {
        std::vector<std::thread> pool;
        const int nthreads = std::max(1, std::min(threads,
            static_cast<int>(cells.size())));
        pool.reserve(static_cast<size_t>(nthreads - 1));
        for (int i = 0; i < nthreads - 1; ++i)
            pool.emplace_back(worker);
        worker();
        for (auto &th : pool)
            th.join();
    }

    if (cancelled() || failed.load() || g_error.load())
        return {};

    std::vector<Cand> result;
    result.reserve(cells.size());
    for (size_t i = 0; i < cells.size(); ++i)
    {
        if (valid[i])
            result.push_back(out[i]);
    }
    std::sort(result.begin(), result.end(), [](const Cand &a, const Cand &b) {
        if (a.slime != b.slime)
            return a.slime < b.slime;
        if (a.dist != b.dist)
            return a.dist < b.dist;
        if (a.x != b.x)
            return a.x < b.x;
        return a.z < b.z;
    });
    return result;
}

static inline uint64_t spatial_key_64(int32_t cx, int32_t cz)
{
    return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) |
           static_cast<uint32_t>(cz);
}

static inline bool within_64(int32_t x1, int32_t z1, int32_t x2, int32_t z2)
{
    return std::llabs(static_cast<int64_t>(x1) - x2) < 64 &&
           std::llabs(static_cast<int64_t>(z1) - z2) < 64;
}

/*
 * Keep only the best candidate in each 64x64 block neighborhood. Candidates
 * arrive sorted by slime (then distance), so the first point of a cluster is
 * the best. Checking the 3x3 neighboring buckets suppresses points that are
 * close across 64x64 cell boundaries as well.
 */
std::vector<Cand> dedup_candidates_64(const std::vector<Cand> &in)
{
    std::vector<Cand> out;
    out.reserve(in.size());
    std::unordered_map<uint64_t, std::vector<Cand>> accepted;
    accepted.reserve(in.size() * 2 + 1);
    for (const Cand &c : in)
    {
        const int32_t cx = static_cast<int32_t>(floorDiv(c.x, 64));
        const int32_t cz = static_cast<int32_t>(floorDiv(c.z, 64));
        bool too_close = false;
        for (int dz = -1; dz <= 1 && !too_close; ++dz)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                auto it = accepted.find(spatial_key_64(cx + dx, cz + dz));
                if (it == accepted.end())
                    continue;
                for (const Cand &a : it->second)
                {
                    if (within_64(c.x, c.z, a.x, a.z))
                    {
                        too_close = true;
                        break;
                    }
                }
                if (too_close)
                    break;
            }
        }
        if (too_close)
            continue;
        out.push_back(c);
        accepted[spatial_key_64(cx, cz)].push_back(c);
    }
    return out;
}

int run_search(const NoSlimeParams &p)
{
    if (g_spans.empty() || g_donut.empty() || ring_area_sum() != kRingTotal)
    {
        set_error("ring geometry mismatch");
        return -2;
    }
    int32_t donut_cells = 0;
    for (const DonutRun &run : g_donut_runs)
        donut_cells += run.hi - run.lo + 1;
    if (g_donut_runs.empty() || donut_cells != kDonutChunks)
    {
        set_error("donut geometry mismatch");
        return -2;
    }

    const int threads = std::max(1, std::min(p.threads, kMaxThreads));
    const int32_t n = std::max(1, p.max_results);
    const int32_t min_area = std::max(0, std::min(p.min_area, kRingTotal));
    int32_t candidate_limit = p.candidate_limit;
    if (candidate_limit <= 0)
        candidate_limit = std::max(kCoarseTargetMin, kCoarseTargetPerN * n);
    candidate_limit = std::min(candidate_limit, kCoarseTargetMax);

    if (p.afk_x1 < p.afk_x0 || p.afk_z1 < p.afk_z0)
    {
        set_error("invalid search rectangle");
        return -3;
    }
    if (p.biome_y < -64 || p.biome_y > 319)
    {
        set_error("biome Y must be in [-64, 319]");
        return -3;
    }
    if (p.mc < MC_1_18 || p.mc > MC_NEWEST)
    {
        set_error("unsupported Minecraft version");
        return -3;
    }
    if (p.avoid_count < 0 || (p.avoid_count > 0 && p.avoid_ids == nullptr))
    {
        set_error("invalid avoid list");
        return -3;
    }

    const int64_t ax0 = p.afk_x0;
    const int64_t ax1 = p.afk_x1;
    const int64_t az0 = p.afk_z0;
    const int64_t az1 = p.afk_z1;
    if (ax0 < INT32_MIN + kOuter || ax1 > INT32_MAX - kOuter ||
        az0 < INT32_MIN + kOuter || az1 > INT32_MAX - kOuter)
    {
        set_error("search rectangle is outside the block coordinate range");
        return -3;
    }

    AvoidTable avoid{};
    for (int32_t i = 0; i < p.avoid_count; ++i)
    {
        const int32_t id = p.avoid_ids[i];
        if (id < 0 || id > 255)
        {
            set_error("invalid biome id in avoid list");
            return -3;
        }
        avoid.flag[id] = true;
    }

    const int64_t width = ax1 - ax0 + 1;
    const int64_t height = az1 - az0 + 1;
    const int64_t total_points = width * height;
    if (total_points <= 0)
    {
        set_error("empty search rectangle");
        return -3;
    }

    /* ---- Stage 1a: radar-like low-slime prefilter over large tiles.
     * Only the best candidate cells go to the exact scan below. ---- */
    const int32_t target_cells = candidate_limit;
    std::vector<PreCell> selected = select_low_slime_cells(
        p, ax0, ax1, az0, az1, target_cells, threads);
    if (cancelled())
        return 1;
    if (g_error.load(std::memory_order_relaxed))
        return -1;
    if (selected.empty())
    {
        std::lock_guard<std::mutex> lock(g_result_mutex);
        g_results.clear();
        g_stage.store(NOSLIME_STAGE_DONE, std::memory_order_relaxed);
        return 0;
    }

    /* ---- Stage 1b: exact slime area for the selected 64x64 cells only. ---- */
    std::vector<Cand> candidates = scan_selected_cells(
        p, ax0, ax1, az0, az1, selected, threads);
    if (cancelled())
        return 1;
    if (g_error.load(std::memory_order_relaxed))
        return -1;
    if (candidates.empty())
    {
        g_stage.store(NOSLIME_STAGE_DONE, std::memory_order_relaxed);
        return 0;
    }

    /* Before the 4x4/1x1 stages, suppress points from neighboring 64x64
     * cells that are actually within the same 64x64 block neighborhood. */
    candidates = dedup_candidates_64(candidates);
    if (candidates.empty())
    {
        g_stage.store(NOSLIME_STAGE_DONE, std::memory_order_relaxed);
        return 0;
    }

    /* ---- Stage 2: 4x4 coarse biome estimate only for the low-slime
     * candidate pool. ---- */
    std::atomic<bool> failed{false};
    const bool has_avoid = p.avoid_count > 0;

    auto count_chunks = [&](int32_t x, int32_t z) {
        const int32_t ccx = static_cast<int32_t>(floorDiv(x, 16));
        const int32_t ccz = static_cast<int32_t>(floorDiv(z, 16));
        int32_t chunks = 0;
        for (const auto &off : g_donut)
        {
            if (isSlimeChunk(p.seed, ccx + off.first, ccz + off.second))
                ++chunks;
        }
        return chunks;
    };

    if (!has_avoid)
    {
        /* No avoid list: the exact slime area from stage 1 is the whole score. */
        std::vector<NoSlimeResult> results;
        results.reserve(static_cast<size_t>(n));
        for (const Cand &c : candidates)
        {
            NoSlimeResult r{};
            r.x = c.x;
            r.z = c.z;
            r.slime = c.slime;
            r.avoid_x = 0;
            r.avoid_y = 0;
            r.area = kRingTotal - c.slime;
            if (r.area < min_area)
                break;
            r.chunks = count_chunks(c.x, c.z);
            results.push_back(r);
            if (static_cast<int32_t>(results.size()) >= n)
                break;
        }
        {
            std::lock_guard<std::mutex> lock(g_result_mutex);
            g_results.swap(results);
        }
        g_stage.store(NOSLIME_STAGE_DONE, std::memory_order_relaxed);
        return 0;
    }

    g_stage.store(NOSLIME_STAGE_COARSE, std::memory_order_relaxed);
    g_cur.store(0, std::memory_order_relaxed);
    g_total.store(static_cast<int64_t>(candidates.size()),
                  std::memory_order_relaxed);
    std::vector<Coarse> coarse(candidates.size());

    std::atomic<int64_t> next_coarse{0};
    auto coarse_worker = [&]()
    {
        for (;;)
        {
            const int64_t i =
                next_coarse.fetch_add(1, std::memory_order_relaxed);
            if (i >= static_cast<int64_t>(candidates.size()))
                break;
            if (!checkpoint())
                break;

            const Cand &c = candidates[static_cast<size_t>(i)];
            try
            {
                ThreadGenerator &tg = thread_generator(p.mc, static_cast<uint64_t>(p.seed));
                const int64_t qx0 = floorDiv(static_cast<int64_t>(c.x) - kOuter, 4);
                const int64_t qx1 = floorDiv(static_cast<int64_t>(c.x) + kOuter, 4);
                const int64_t qz0 = floorDiv(static_cast<int64_t>(c.z) - kOuter, 4);
                const int64_t qz1 = floorDiv(static_cast<int64_t>(c.z) + kOuter, 4);
                const int qw = static_cast<int>(qx1 - qx0 + 1);
                const int qh = static_cast<int>(qz1 - qz0 + 1);
                if (qw <= 0 || qh <= 0)
                    continue;

                std::vector<int> biomes(static_cast<size_t>(qw) * qh);
                Range range{};
                range.scale = 4;
                range.x = static_cast<int>(qx0);
                range.z = static_cast<int>(qz0);
                range.sx = qw;
                range.sz = qh;
                range.y = static_cast<int>(floorDiv(p.biome_y, 4));
                range.sy = 1;
                if (genBiomes(&tg.g, biomes.data(), range) != 0)
                {
                    failed.store(true, std::memory_order_relaxed);
                    set_error("genBiomes failed in coarse stage");
                    break;
                }

                int32_t avoid_x = 0;
                int32_t avoid_y = 0;
                for (const Span &s : g_spans)
                {
                    const int64_t bz = static_cast<int64_t>(c.z) + s.dz;
                    const int qz = static_cast<int>(floorDiv(bz, 4) - qz0);
                    for (int64_t bx = static_cast<int64_t>(c.x) + s.lo;
                         bx <= static_cast<int64_t>(c.x) + s.hi; ++bx)
                    {
                        const int qx = static_cast<int>(floorDiv(bx, 4) - qx0);
                        const bool av = is_avoid(avoid,
                            biomes[static_cast<size_t>(qz) * qw + qx]);
                        if (!av)
                            continue;
                        const bool sl = isSlimeChunk(
                            p.seed,
                            static_cast<int32_t>(floorDiv(bx, 16)),
                            static_cast<int32_t>(floorDiv(bz, 16)));
                        if (sl)
                            ++avoid_y;
                        else
                            ++avoid_x;
                    }
                }

                Coarse &out = coarse[static_cast<size_t>(i)];
                out.x = c.x;
                out.z = c.z;
                out.slime = c.slime;
                out.avoid_x = avoid_x;
                out.avoid_y = avoid_y;
                out.area = kRingTotal - c.slime - avoid_x;
                out.dist = c.dist;
            }
            catch (const std::bad_alloc &)
            {
                failed.store(true, std::memory_order_relaxed);
                set_error("out of memory during coarse biome sampling");
                break;
            }
            catch (const std::exception &e)
            {
                failed.store(true, std::memory_order_relaxed);
                set_error(e.what());
                break;
            }
            g_cur.fetch_add(1, std::memory_order_relaxed);
        }
    };

    {
        std::vector<std::thread> pool;
        const int nthreads = std::max(1, std::min(threads,
            static_cast<int>(candidates.size())));
        pool.reserve(static_cast<size_t>(nthreads - 1));
        for (int i = 0; i < nthreads - 1; ++i)
            pool.emplace_back(coarse_worker);
        coarse_worker();
        for (auto &th : pool)
            th.join();
    }

    if (cancelled())
        return 1;
    if (failed.load() || g_error.load())
        return -1;

    std::sort(coarse.begin(), coarse.end(),
              [](const Coarse &a, const Coarse &b) {
                  if (a.area != b.area)
                      return a.area > b.area;
                  if (a.dist != b.dist)
                      return a.dist < b.dist;
                  if (a.x != b.x)
                      return a.x < b.x;
                  return a.z < b.z;
              });

    const int32_t exact_limit = std::min<int32_t>(
        kExactLimit, static_cast<int32_t>(coarse.size()));

    /* ---- Stage 3: per-block (1x1) exact biome sampling for the best coarse
     * candidates, then the final top-N. ---- */
    g_stage.store(NOSLIME_STAGE_EXACT, std::memory_order_relaxed);
    g_cur.store(0, std::memory_order_relaxed);
    g_total.store(exact_limit, std::memory_order_relaxed);

    std::vector<Exact> exact(static_cast<size_t>(exact_limit));
    std::atomic<int64_t> next_exact{0};
    auto exact_worker = [&]()
    {
        for (;;)
        {
            const int64_t i =
                next_exact.fetch_add(1, std::memory_order_relaxed);
            if (i >= exact_limit)
                break;
            if (!checkpoint())
                break;

            const Coarse &c = coarse[static_cast<size_t>(i)];
            try
            {
                ThreadGenerator &tg = thread_generator(p.mc, static_cast<uint64_t>(p.seed));
                int32_t avoid_x = 0;
                int32_t avoid_y = 0;
                for (const Span &s : g_spans)
                {
                    const int64_t bz = static_cast<int64_t>(c.z) + s.dz;
                    for (int64_t bx = static_cast<int64_t>(c.x) + s.lo;
                         bx <= static_cast<int64_t>(c.x) + s.hi; ++bx)
                    {
                        const int id = getBiomeAt(&tg.g, 1,
                            static_cast<int>(bx), p.biome_y,
                            static_cast<int>(bz));
                        if (!is_avoid(avoid, id))
                            continue;
                        const bool sl = isSlimeChunk(
                            p.seed,
                            static_cast<int32_t>(floorDiv(bx, 16)),
                            static_cast<int32_t>(floorDiv(bz, 16)));
                        if (sl)
                            ++avoid_y;
                        else
                            ++avoid_x;
                    }
                }

                const int32_t chunks = count_chunks(c.x, c.z);

                Exact &e = exact[static_cast<size_t>(i)];
                e.r.x = c.x;
                e.r.z = c.z;
                e.r.slime = c.slime;
                e.r.avoid_x = avoid_x;
                e.r.avoid_y = avoid_y;
                e.r.chunks = chunks;
                e.r.area = kRingTotal - c.slime - avoid_x;
                e.dist = c.dist;
            }
            catch (const std::bad_alloc &)
            {
                failed.store(true, std::memory_order_relaxed);
                set_error("out of memory during exact biome sampling");
                break;
            }
            catch (const std::exception &e)
            {
                failed.store(true, std::memory_order_relaxed);
                set_error(e.what());
                break;
            }
            g_cur.fetch_add(1, std::memory_order_relaxed);
        }
    };

    {
        std::vector<std::thread> pool;
        const int nthreads = std::max(1, std::min(threads, exact_limit));
        pool.reserve(static_cast<size_t>(nthreads - 1));
        for (int i = 0; i < nthreads - 1; ++i)
            pool.emplace_back(exact_worker);
        exact_worker();
        for (auto &th : pool)
            th.join();
    }

    if (cancelled())
        return 1;
    if (failed.load() || g_error.load())
        return -1;

    std::sort(exact.begin(), exact.end(),
              [](const Exact &a, const Exact &b) {
                  if (a.r.area != b.r.area)
                      return a.r.area > b.r.area;
                  if (a.dist != b.dist)
                      return a.dist < b.dist;
                  if (a.r.x != b.r.x)
                      return a.r.x < b.r.x;
                  return a.r.z < b.r.z;
              });

    std::vector<NoSlimeResult> results;
    results.reserve(static_cast<size_t>(n));
    std::unordered_map<uint64_t, std::vector<NoSlimeResult>> accepted;
    accepted.reserve(static_cast<size_t>(exact.size()) * 2 + 1);
    for (const Exact &e : exact)
    {
        if (e.r.area < min_area)
            break;
        const int32_t cx = static_cast<int32_t>(floorDiv(e.r.x, 64));
        const int32_t cz = static_cast<int32_t>(floorDiv(e.r.z, 64));
        bool too_close = false;
        for (int dz = -1; dz <= 1 && !too_close; ++dz)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                auto it = accepted.find(spatial_key_64(cx + dx, cz + dz));
                if (it == accepted.end())
                    continue;
                for (const NoSlimeResult &a : it->second)
                {
                    if (within_64(e.r.x, e.r.z, a.x, a.z))
                    {
                        too_close = true;
                        break;
                    }
                }
                if (too_close)
                    break;
            }
        }
        if (too_close)
            continue;
        accepted[spatial_key_64(cx, cz)].push_back(e.r);
        results.push_back(e.r);
        if (static_cast<int32_t>(results.size()) >= n)
            break;
    }

    {
        std::lock_guard<std::mutex> lock(g_result_mutex);
        g_results.swap(results);
    }
    g_stage.store(NOSLIME_STAGE_DONE, std::memory_order_relaxed);
    return 0;
}

} // namespace

extern "C" {

int noslime_run(const NoSlimeParams *p)
{
    std::lock_guard<std::mutex> api_lock(g_api_mutex);
    if (p == nullptr)
        return -1;

    g_stage.store(NOSLIME_STAGE_SCAN, std::memory_order_relaxed);
    g_done.store(0, std::memory_order_relaxed);
    g_paused.store(0, std::memory_order_relaxed);
    g_cancelled.store(0, std::memory_order_relaxed);
    g_error.store(0, std::memory_order_relaxed);
    g_cur.store(0, std::memory_order_relaxed);
    g_total.store(0, std::memory_order_relaxed);
    g_start_ms.store(now_ms(), std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(g_message_mutex);
        g_message.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_result_mutex);
        g_results.clear();
    }

    int rc = 0;
    try
    {
        rc = run_search(*p);
    }
    catch (const std::bad_alloc &)
    {
        set_error("out of memory");
        rc = -1;
    }
    catch (const std::exception &e)
    {
        set_error(e.what());
        rc = -1;
    }
    catch (...)
    {
        set_error("unknown error");
        rc = -1;
    }

    if (cancelled() && rc == 1)
    {
        g_stage.store(NOSLIME_STAGE_CANCELLED, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(g_result_mutex);
        g_results.clear();
    }
    else if (g_error.load(std::memory_order_relaxed) != 0 || rc < 0)
    {
        g_stage.store(NOSLIME_STAGE_ERROR, std::memory_order_relaxed);
        g_error.store(1, std::memory_order_relaxed);
    }
    else
    {
        g_stage.store(NOSLIME_STAGE_DONE, std::memory_order_relaxed);
    }
    g_done.store(1, std::memory_order_relaxed);
    return rc;
}

int noslime_pause(void)
{
    if (g_done.load(std::memory_order_relaxed) != 0)
        return -1;
    g_paused.store(1, std::memory_order_relaxed);
    return 0;
}

int noslime_resume(void)
{
    g_paused.store(0, std::memory_order_relaxed);
    return 0;
}

int noslime_cancel(void)
{
    g_cancelled.store(1, std::memory_order_relaxed);
    g_paused.store(0, std::memory_order_relaxed);
    return 0;
}

void noslime_get_progress(int64_t out[8])
{
    if (out == nullptr)
        return;
    out[0] = g_stage.load(std::memory_order_relaxed);
    out[1] = g_done.load(std::memory_order_relaxed);
    out[2] = g_paused.load(std::memory_order_relaxed);
    out[3] = g_error.load(std::memory_order_relaxed);
    out[4] = g_cur.load(std::memory_order_relaxed);
    out[5] = g_total.load(std::memory_order_relaxed);
    const int64_t start = g_start_ms.load(std::memory_order_relaxed);
    out[6] = start > 0 ? now_ms() - start : 0;
    {
        std::lock_guard<std::mutex> lock(g_result_mutex);
        out[7] = static_cast<int64_t>(g_results.size());
    }
}

const char *noslime_last_error(void)
{
    static thread_local char buf[256];
    {
        std::lock_guard<std::mutex> lock(g_message_mutex);
        std::snprintf(buf, sizeof(buf), "%s", g_message.c_str());
    }
    return buf;
}

int32_t noslime_result_count(void)
{
    std::lock_guard<std::mutex> lock(g_result_mutex);
    return static_cast<int32_t>(g_results.size());
}

int32_t noslime_get_results(NoSlimeResult *out, int32_t capacity)
{
    std::lock_guard<std::mutex> lock(g_result_mutex);
    const int32_t total = static_cast<int32_t>(g_results.size());
    if (out != nullptr && capacity > 0)
    {
        const int32_t n = std::min(total, capacity);
        std::memcpy(out, g_results.data(),
                    static_cast<size_t>(n) * sizeof(NoSlimeResult));
    }
    return total;
}

} // extern "C"
