# SlimeOptimizer

Minecraft 史莱姆农场挂机点搜索工具。

给定一批候选史莱姆区块（[Slimy](https://github.com/silversquirl/slimy)导出的csv文件），本程序会在每个候选区块附近搜索**最佳挂机（AFK）坐标**，使玩家刷怪环内落在史莱姆区块中的有效刷怪面积最大化，并按面积排序输出结果和可视化图。

## 构建

需要 CMake 3.15+ 和支持 C++17 的编译器（MSVC / GCC / MinGW / Clang）。无第三方依赖，PNG 编码器内置。

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

生成的可执行文件为 `SlimeOptimizer`。GCC/MinGW 下会静态链接运行库，产物可脱离编译环境直接分发。

## 使用

将可执行文件、`config.json` 和候选 CSV 放在同一目录，运行：

```bash
./SlimeOptimizer
```

程序会：

1. 读取 `config.json`（不存在则自动生成一份默认配置，种子为 0）。
2. 交互式询问 **CSV 文件路径** 和 **是否批量输出**。
3. 计算并按面积排序输出所有候选点，写入 `output/` 目录。

### 输入 CSV 格式

```
chunkX,chunkZ,count
12,-8,7
...
```

- 首行若为非数字表头会自动跳过。
- 重复的 `(chunkX, chunkZ)` 会合并，保留较大的 `count`。

### 输出

| 模式 | 输出 |
| --- | --- |
| 单图模式（默认） | `output/result.csv`（全部结果）+ `output/map.png`（最佳点地图） |
| 批量模式（选 y） | 每个候选点单独一个子目录 `output/001`、`002`…（按排名排序），各含 `result.csv` 与 `map.png` |

结果行格式：

```
有效史莱姆区块数 有效刷怪面积 (输入区块X,输入区块Z) (挂机点方块X,挂机点方块Z)
```

`map.png` 展示区块网格（史莱姆区块为绿色）、最佳挂机点（红色）、内外刷怪半径圆环（黑色）以及文字信息面板。

## 配置（config.json）

```json
{
  "seed": 0,
  "chunkSearchRange": 20,
  "playerSearchRange": 5,
  "innerRadius": 24,
  "outerRadius": 128,
  "outDir": "output"
}
```

| 字段 | 含义 | 默认 |
| --- | --- | --- |
| `seed` | 世界种子 | 0 |
| `chunkSearchRange` | 候选区块周围加载史莱姆图的区块半径 | 20 |
| `playerSearchRange` | 候选区块周围搜索挂机点的区块半径 | 5 |
| `innerRadius` | 刷怪环内半径（格），需满足 `0 <= innerRadius` | 24 |
| `outerRadius` | 刷怪环外半径（格），需满足 `innerRadius < outerRadius` | 128 |
| `outDir` | 输出目录 | `output` |

## 项目结构

```
main.cpp            交互入口、搜索调度与结果输出
include/  src/
  config.*          config.json 读写、目录创建
  csv_reader.*      候选 CSV 解析与去重
  java_random.*     Java LCG 随机数移植
  slime_check.*     史莱姆区块判定
  geometry.*        floorDiv、刷怪环偏移/行区间计算
  optimizer.*       单候选点最佳挂机点搜索
  visualizer.*      内置 PNG 编码与地图渲染
```
