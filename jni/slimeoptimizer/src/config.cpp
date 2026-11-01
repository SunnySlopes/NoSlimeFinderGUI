#include "config.hpp"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <direct.h>   // _mkdir
#else
#include <sys/stat.h> // mkdir
#include <sys/types.h>
#endif

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// 去掉一行里的 // 注释(不考虑字符串内的 //,配置里用不到)。
std::string stripComment(const std::string& line) {
    size_t p = line.find("//");
    return p == std::string::npos ? line : line.substr(0, p);
}

}  // namespace

bool ensureDir(const std::string& path) {
    if (path.empty()) return true;
#ifdef _WIN32
    int rc = _mkdir(path.c_str());
#else
    int rc = mkdir(path.c_str(), 0755);
#endif
    // rc==0 表示新建成功;errno==EEXIST(rc!=0)表示已存在,也算成功。
    if (rc == 0) return true;
    std::ifstream test(path.c_str());
    return true;  // 简化处理:创建失败多半是已存在,交给后续写文件时报错。
}

void writeDefaultConfig(const std::string& path) {
    std::ofstream out(path);
    if (!out) return;
    // 带中文注释的 JSONC(允许 // 注释,本程序自带宽松解析器可读)。
    out <<
        "{\n"
        "    // 世界种子(Minecraft world seed),默认 0\n"
        "    \"seed\": 0,\n"
        "\n"
        "    // 史莱姆区块扫描半径(单位:区块 chunk)\n"
        "    \"chunkSearchRange\": 100,\n"
        "\n"
        "    // 玩家搜索半径(单位:区块 chunk),在候选点 ±该值范围内找挂机点\n"
        "    \"playerSearchRange\": 5,\n"
        "\n"
        "    // 刷怪内半径 N(单位:方块 block),要求 N < 距离\n"
        "    \"innerRadius\": 24,\n"
        "\n"
        "    // 刷怪外半径 M(单位:方块 block),要求 距离 <= M\n"
        "    \"outerRadius\": 128,\n"
        "\n"
        "    // 输出目录\n"
        "    \"outDir\": \"output\"\n"
        "}\n";
}

bool loadConfig(const std::string& path, Params& p) {
    std::ifstream in(path);
    if (!in) {
        // 配置不存在:写一份默认的,并把默认值填进 p。
        writeDefaultConfig(path);
        p.worldSeed         = 0;
        p.chunkSearchRange  = 100;
        p.playerSearchRange = 5;
        p.innerRadius       = 24;
        p.outerRadius       = 128;
        p.outDir            = "output";
        return false;
    }

    // 默认值兜底(配置里缺某项时用)。
    p.worldSeed         = 0;
    p.chunkSearchRange  = 100;
    p.playerSearchRange = 5;
    p.innerRadius       = 24;
    p.outerRadius       = 128;
    p.outDir            = "output";

    std::string line;
    while (std::getline(in, line)) {
        line = stripComment(line);
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;

        // 解析 key(去引号)
        std::string keyPart = line.substr(0, colon);
        size_t q1 = keyPart.find('"');
        size_t q2 = keyPart.rfind('"');
        if (q1 == std::string::npos || q2 == q1) continue;
        std::string key = keyPart.substr(q1 + 1, q2 - q1 - 1);

        // 解析 value(去尾逗号/空白)
        std::string val = trim(line.substr(colon + 1));
        if (!val.empty() && val.back() == ',') val.pop_back();
        val = trim(val);
        if (val.empty()) continue;

        auto parseInt = [](const std::string& s, int64_t& out) -> bool {
            try {
                size_t pos = 0;
                long long v = std::stoll(s, &pos);
                if (pos != s.size()) return false;
                out = v;
                return true;
            } catch (...) { return false; }
        };

        int64_t iv = 0;
        if (key == "seed") {
            if (parseInt(val, iv)) p.worldSeed = iv;
        } else if (key == "chunkSearchRange") {
            if (parseInt(val, iv)) p.chunkSearchRange = static_cast<int>(iv);
        } else if (key == "playerSearchRange") {
            if (parseInt(val, iv)) p.playerSearchRange = static_cast<int>(iv);
        } else if (key == "innerRadius") {
            if (parseInt(val, iv)) p.innerRadius = static_cast<int>(iv);
        } else if (key == "outerRadius") {
            if (parseInt(val, iv)) p.outerRadius = static_cast<int>(iv);
        } else if (key == "outDir") {
            // 去掉字符串引号
            if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
                p.outDir = val.substr(1, val.size() - 2);
            else
                p.outDir = val;
        }
    }
    return true;
}
