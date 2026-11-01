#include "config.hpp"
#include "csv_reader.hpp"
#include "geometry.hpp"
#include "optimizer.hpp"
#include "types.hpp"
#include "visualizer.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// 读取一行输入,空行返回默认值。
std::string ask(const std::string& prompt, const std::string& def) {
    std::cout << prompt;
    if (!def.empty()) std::cout << " [默认 " << def << "]";
    std::cout << ": ";
    std::string line;
    if (!std::getline(std::cin, line)) return def;
    line = trim(line);
    return line.empty() ? def : line;
}

// 把序号格式化成 3 位:1 -> "001"。
std::string seq3(size_t n) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%03lu", static_cast<unsigned long>(n));
    return std::string(buf);
}

// 生成一行结果文本(与需求格式一致)。
std::string formatLine(const Result& r) {
    return std::to_string(r.effectiveSlimeChunks) + " " +
           std::to_string(r.effectiveSpawnArea) + " (" +
           std::to_string(r.inputChunkX) + "," + std::to_string(r.inputChunkZ) +
           ") (" + std::to_string(r.playerBlockX) + "," +
           std::to_string(r.playerBlockZ) + ")";
}

}  // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    std::cout << "=========================================\n";
    std::cout << "   Minecraft 史莱姆农场 挂机点搜索工具\n";
    std::cout << "=========================================\n";

    // ---- 读取配置(不存在则自动初始化一份带中文注释的 config.json)----
    const std::string configPath = "config.json";
    Params params;
    bool existed = loadConfig(configPath, params);
    if (!existed) {
        std::cout << "未找到 config.json,已生成默认配置(种子=0)。\n";
        std::cout << "如需修改种子/半径等参数,请编辑 " << configPath << " 后重新运行。\n\n";
    } else {
        std::cout << "已读取配置 " << configPath << ":种子=" << params.worldSeed
                  << " 区块半径=" << params.chunkSearchRange
                  << " 刷怪范围=(" << params.innerRadius << ","
                  << params.outerRadius << "]\n\n";
    }

    if (params.innerRadius < 0 || params.outerRadius <= params.innerRadius) {
        std::cout << "错误:配置里必须满足 0 <= innerRadius < outerRadius。\n";
        return 2;
    }

    // ---- 只需交互输入 CSV 路径 + 是否批量输出 ----
    params.csvPath = ask("Slimy CSV 文件路径", "sample.csv");
    std::string batchAns = ask("是否批量输出?(每个候选点单独出图)(y/N)", "N");
    const bool batch = (batchAns == "y" || batchAns == "Y" ||
                        batchAns == "yes" || batchAns == "YES");

    std::cout << "\n----------------------------------------------\n";

    // ---- 读取并去重 CSV ----
    std::vector<Candidate> candidates;
    try {
        candidates = readCandidates(params.csvPath);
    } catch (const std::exception& e) {
        std::cout << "读取 CSV 失败:" << e.what() << "\n";
        return 1;
    }
    if (candidates.empty()) {
        std::cout << "CSV 中没有找到任何候选点。\n";
        return 1;
    }
    std::cout << "已载入 " << candidates.size() << " 个去重后的候选区域。"
              << (batch ? "(批量模式)" : "(单图模式)") << "\n";

    // ---- 确保输出目录存在 ----
    ensureDir(params.outDir);

    // ---- 预计算刷怪环:按行拆成连续 dx 区间(供逐行区间扫描用)----
    const auto ringSpans =
        computeRingSpans(params.innerRadius, params.outerRadius);
    int64_t ringBlocks = 0;
    for (const auto& sp : ringSpans) ringBlocks += (sp.hiDx - sp.loDx + 1);
    std::cout << "刷怪环:" << ringSpans.size() << " 行区间,共 "
              << ringBlocks << " 个环内方块\n\n";

    // ---- 逐候选点搜索最佳挂机点 ----
    std::vector<Result> results;
    results.reserve(candidates.size());
    for (size_t k = 0; k < candidates.size(); ++k) {
        results.push_back(optimizeCandidate(params, candidates[k], ringSpans));
        const Result& r = results.back();
        std::cout << "  [" << (k + 1) << "/" << candidates.size() << "] 候选 ("
                  << candidates[k].chunkX << "," << candidates[k].chunkZ
                  << ") -> 有效面积=" << r.effectiveSpawnArea
                  << " 有效史莱姆区块=" << r.effectiveSlimeChunks << "\n";
    }

    // ---- 排序:先按有效刷怪面积降序,再按有效史莱姆区块数降序 ----
    std::sort(results.begin(), results.end(),
              [](const Result& a, const Result& b) {
                  if (a.effectiveSpawnArea != b.effectiveSpawnArea)
                      return a.effectiveSpawnArea > b.effectiveSpawnArea;
                  return a.effectiveSlimeChunks > b.effectiveSlimeChunks;
              });

    std::cout << "\n============ 搜索结果(按面积排序)============\n";
    for (const auto& r : results) std::cout << formatLine(r) << "\n";

    if (batch) {
        // ---- 批量模式:每个候选点在 output/001、002... 子目录各出一份 ----
        // 001 = 有效面积最大的(已排序,顺序即排名)。
        for (size_t i = 0; i < results.size(); ++i) {
            const std::string sub = params.outDir + "/" + seq3(i + 1);
            ensureDir(sub);

            std::ofstream out(sub + "/result.csv");
            if (out) out << formatLine(results[i]) << "\n";

            renderMap(params, results[i], sub + "/map.png");
        }
        std::cout << "\n已批量输出 " << results.size() << " 个候选点到 "
                  << params.outDir << "/001 .. " << params.outDir << "/"
                  << seq3(results.size()) << "\n";
    } else {
        // ---- 单图模式:汇总 result.csv + 最佳点 map.png ----
        std::ofstream out(params.outDir + "/result.csv");
        if (out) for (const auto& r : results) out << formatLine(r) << "\n";

        if (renderMap(params, results.front(), params.outDir + "/map.png"))
            std::cout << "\n已写入 " << params.outDir << "/result.csv 和 "
                      << params.outDir << "/map.png\n";
        else
            std::cout << "\n警告:map.png 写入失败。\n";
    }

    std::cout << "\n完成。按回车键退出。";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    return 0;
}
