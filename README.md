# NoSlimeFinder / 无史莱姆区域查找工具

A GUI tool for Minecraft: Java Edition that finds **single-player AFK points whose 24–128 spawn ring has the least slime-chunk area**, while avoiding configured biomes.

在 Minecraft Java 版中寻找"单人挂机环内史莱姆区块面积最少"的挂机点，并同时避开配置的群系。

> 设计与验证记录见 [PLAN.md](PLAN.md)。

## 架构 / Architecture

本项目已拆成两个库：

| 库 | 位置 | 内容 |
| --- | --- | --- |
| 主库 `NoSlimeFinderGUI` | 本仓库 | Java Swing 界面、搜索线程、JNI 薄桥接 `jni/NoSlimeFinderBridge.cpp` |
| 计算库 `NoSlimeFinderBase` | git 子模块 `NoSlimeFinderBase/` | 全部 C++ 搜索核心、cubiomes、SlimeOptimizer、公共 C API、独立 CLI |

主库**不包含任何 C++ 计算逻辑**：`jni/NoSlimeFinderBridge.cpp` 只把 Java 方法转发到
`NoSlimeFinderBase/include/noslimefinder/base.h`。base 可以独立打包成
`NoSlimeFinderBase-cli.exe`，用 CLI 参数执行，并通过 `--out` 输出 txt 结果；
Gradle 构建时会把该 exe 与 JNI DLL 一起打进 JAR。

---

## 指标 / Metrics

- **挂机环 / AFK ring**：`24 < 距离 <= 128`（欧氏距离，方块），共 **49640** 个环内方块；内圈 `d<=24` 不计。
- **史莱姆面积 / slime**：环内落在史莱姆区块中的方块数（1 方块 = 1）。
- **史莱姆区块数 / chunks**：以 AFK 点所在区块为中心、`1 < dx²+dz² <= 64` 的 **192 个甜甜圈区块**内的史莱姆区块数；环边缘的其他区块不统计。
- **需避开群系面积 / avoid**：显示为 `x+y`：
  - `x` = 与史莱姆不重叠的避让面积；
  - `y` = 与史莱姆重叠的避让面积。
- **有效面积 / area**：`49640 - slime - x`（`avoid` 不重复扣）。

> `slime` 统计整个环（包括甜甜圈外的边缘区块）；`chunks` 只统计 192 甜甜圈。

- **AFK ring**: `24 < distance <= 128` blocks, 49,640 ring blocks; the inner circle (`d<=24`) is excluded.
- **slime**: ring blocks inside slime chunks.
- **chunks**: slime chunks among the 192-chunk donut (`1 < dx²+dz² <= 64`) centered on the AFK chunk; edge chunks outside the donut are not counted.
- **avoid**: shown as `x+y`; `x` is the non-overlapping avoided area, `y` is the avoided area that is also slime.
- **area**: `49,640 - slime - x` (no double subtraction).

## 输出 / Output

```
/tp X -64 Z area=<有效面积> slime=<史莱姆面积> chunks=<史莱姆区块数> avoid=<x>+<y>
```

Example / 示例：

```
/tp 11 -64 94 area=44241 slime=3225 chunks=12 avoid=2174+107
```

- 结果按 `area` 降序、`area` 相同按距原点距离升序排列。
- 搜索完成后的默认顺序即"按面积排序"。
- 两个排序按钮：**按面积排序**、**按距离排序**。
- 固定输出前 **100** 条，且只输出 `area >= 最低有效面积` 的结果（默认 **49000**）；同一 64×64 方块邻域（包括跨格边界）只保留最佳点，最终结果也会再次按此规则去重。
- 找到 `area=49640` 的理论最优也不提前结束。

- Results are sorted by `area` descending, then by distance from the origin ascending.
- Two sort buttons: **Sort by area** and **Sort by distance**.
- The top **100** results are fixed (no result-count input field); only results with `area >= min area` are kept (default **49000**); within each 64×64 block neighborhood (including across cell boundaries) only the best point is kept, and the final results are deduplicated again.
- The search does not stop early when `area=49640` is found.

## 使用 / Usage

- **单种子 / Single seed only**：多种子列表标签页已移除。
- 输入：种子、线程数、游戏版本、群系采样高度 Y、避开群系、最低有效面积（默认 49000）、搜索范围。
- 控制：开始 / 暂停 / 继续 / 停止；停止时**丢弃本次结果**。
- 支持导出结果文本、中英文切换；版本/群系选择/线程数等配置保存在 `%USERPROFILE%/.noslimefinder/config.properties`。

- Single-seed search only (the multi-seed list tab has been removed).
- Inputs: seed, threads, game version, biome sampling Y, avoided biomes, min area (default 49000), search range.
- Controls: Start / Pause / Resume / Stop; **stopping discards the current results**.
- Text export, Chinese/English UI; settings are stored in `%USERPROFILE%/.noslimefinder/config.properties`.

## 算法 / Pipeline

固定流程（没有快速模式勾选）：

1. **低史莱姆粗筛（radar 风格）**：在 **8192×8192 方块**的大 tile 上建立 1 bit/区块的史莱姆位图与二维前缀和（SAT），对每个区块中心计算精确的 17×17 甜甜圈计数；每个 64×64 方块格取 4×4 个区块中心的最小值。**只保留 192 甜甜圈中史莱姆区块数 ≤10 的格**，并且最多保留分数最低的 **1000 个候选格**。
2. **候选格史莱姆扫描**：只对这 1000 个 64×64 格逐方块计算环内史莱姆面积，取每格最优挂机点；随后做 **64×64 邻域去重**（跨格边界也检查），同一邻域只留最佳点。
3. **群系粗估（4×4）**：只对去重后的候选点按 4×4 采样群系，估算避开面积。
4. **精确复核（1×1）**：只对 4×4 排序后的**前 200 个**候选逐方块取群系，得到最终 `slime/x/y/chunks/area`。
5. **过滤与输出**：只保留 `area >= 最低有效面积`（默认 49000）的结果，并按 64×64 邻域再次去重，按有效面积降序、距离升序取前 100。

> 粗筛阶段模仿了 SlimeRadar 的大 tile + SAT + 17×17 甜甜圈逻辑，但目标是找"史莱姆少"的区域；SlimeRadar 本身不参与运行。粗筛是启发式的：如果最优区域在粗筛中被误判，可能被漏掉。

Fixed pipeline (no fast-mode checkbox):

1. **Low-slime prefilter (radar-style)**: build a 1-bit-per-chunk slime bitmap and a 2D summed-area table on **8192×8192 block** tiles; compute the exact 17×17 donut count for every chunk center; take the minimum over the 4×4 chunk centers of each 64×64 block cell. **Keep only cells with ≤10 slime chunks in the 192-chunk donut**, then keep at most the **1,000 cells** with the lowest counts.
2. **Candidate-cell slime scan**: compute the exact ring slime area for the AFK points of those 1,000 cells only; then apply a **64×64 neighborhood dedup** that also checks across cell boundaries, keeping only the best point per neighborhood.
3. **Biome estimate (4×4)** on the deduplicated candidate points.
4. **Exact recomputation (1×1)** for at most the **top 200** candidates after the 4×4 sort.
5. **Filter and output**: keep `area >= min area` (default 49000), deduplicate again by 64×64 neighborhood, and output the top 100 by area/distance.

> The prefilter imitates SlimeRadar's large-tile + SAT + 17×17 donut logic, inverted to find low-slime regions; SlimeRadar itself is not linked. The prefilter is heuristic and may miss the true optimum.

## 采样 / Biome sampling

- **采样高度 Y**：界面可填，默认 `-63`；仅主世界（Overworld）。
- **最终输出**：逐方块采样（cubiomes `scale=1`）。
- **候选阶段**：4×4 粗估。
- 中文界面显示中文群系名；英文界面显示 cubiomes 群系名。

- **Y**: configurable, default `-63`; Overworld only.
- **Final output**: per-block sampling (`scale=1`).
- **Candidate stage**: 4x4 estimate.
- Chinese biome names are shown in the Chinese UI; English names come from cubiomes.

## 版本 / Game versions

版本下拉固定为 8 组（没有"显示全部版本"按钮）：

| 下拉显示 / Label | 代表版本 / Representative | 说明 |
|---|---|---|
| `1.18` | `MC_1_18` | 无 deep_dark / mangrove / cherry / pale_garden / sulfur_caves / dappled_forest |
| `1.19` | `MC_1_19_2` | + deep_dark, mangrove_swamp |
| `1.20` | `MC_1_19_4` | 群系集合同 1.19 |
| `1.21~1.21.3` | `MC_1_21_3` | 合并 1.20.6~1.21.3；+ cherry_grove |
| `1.21.4` | `MC_1_21_4` | + pale_garden；独立 btree21wd |
| `1.21.5~26.1` | `MC_26_1` | 合并 1.21.5~26.1；btree215 |
| `26.2` | `MC_26_2` | + sulfur_caves |
| `26.3` | `MC_26_3` | + dappled_forest |

- **1.21.4 不能并入 26.1**（实测生成有少量差异）。
- 默认版本为 `26.3`。
- 史莱姆区块判定与版本无关；版本只影响群系与避开面积。

- **1.21.4 cannot be merged into 26.1** (small generation differences).
- Slime-chunk determination is version-independent.

## 避开群系 / Biome avoidance

- 可搜索/过滤的多选列表（复选框），按群系 ID 从高到低排列。
- 分组显示：**必选**（蘑菇岛、深暗之域）、**河流**（河流、冻河）、**洞穴**（溶洞、繁茂洞穴、硫磺洞穴）、**海洋**（所有海洋群系）、**默认**（其余群系）；每组都有“全选/全不选”。
- 首次启动默认只选中“必选”组的 2 个群系，其余不选；如果之前保存过选择，则沿用保存的选择。
- 只显示当前版本主世界实际存在的群系；下界/末地/1.17 及以前独有群系（如蘑菇岛岸 `mushroom_field_shore`）不出现。
- 中文界面显示中文群系名（`biome_zh.properties`，参考 `locales/zh.json`）。
- 导入/导出 TXT（每行一个群系 ID 或英文名）。
- 预设机制：`src/main/resources/biome_presets.properties`，格式
  `preset.<key>.label=...` + `preset.<key>.biomes=...`；当前文件只有注释，等你提供预设内容后填入即可。

- Searchable multi-select list; sorted by biome ID descending.
- Groups: **Required** (mushroom_fields, deep_dark), **Rivers** (river, frozen_river), **Caves** (dripstone_caves, lush_caves, sulfur_caves), **Ocean** (all ocean biomes), **Default** (everything else); each group has All/None.
- On first launch, only the 2 Required biomes are selected by default; other groups are not selected. A previously saved selection is restored instead.
- Only biomes that actually exist in the selected version and are Overworld; Nether/End/pre-1.18-only biomes (e.g. `mushroom_field_shore`) are hidden.
- Chinese biome names in the Chinese UI.
- TXT import/export (one biome ID or English name per line).
- Preset mechanism in `biome_presets.properties`; contents are intentionally empty until provided.

## 性能 / Performance

- 粗筛使用大 tile + SAT，先排除 192 甜甜圈史莱姆区块数 >10 的格，再最多保留 1000 个候选格；因此不再需要对全图每个 AFK 点做精确环扫描。
- 候选格史莱姆扫描最多 1000 × 4096 ≈ 410 万点；之后 4×4 只针对这些候选，1×1 最多只算 200 个环。
- 实测 ±2048（最低有效面积设为 0）：无避让约 0.2 秒、95 条结果；带避让约 4.5 秒、95 条结果（8 线程）。
- 默认范围仍是 `±65536`；粗筛会先扫全范围区块，再只精算候选格。界面只在**边长 ≥ 2,000,001** 时弹框警告。建议先用小范围确认结果与性能。

- The prefilter uses large tiles + SAT, drops cells with >10 slime chunks in the 192-chunk donut, and keeps at most 1,000 candidate cells, so the exhaustive per-point ring scan is no longer needed.
- Candidate-cell slime scan = up to 1,000 × 4,096 ≈ 4.1M points; the 4x4 stage runs on those candidates, and the 1x1 stage runs on at most 200 candidates.
- Measured: ±2048 (min area 0), no avoid, about 0.2 seconds / 95 results; with avoid, about 4.5 seconds / 95 results on 8 threads.
- The default range is still `±65536`; the prefilter scans all chunks first and only the candidate cells are refined. The UI warns only when the **side length is >= 2,000,001**. Test smaller ranges first.

## 构建 / Build

**Java 17+ is required. / 需要 Java 17+。**

1. 初始化子模块：
   ```bat
   git submodule update --init --recursive
   ```
   这会同时初始化 `NoSlimeFinderBase` 以及它内部的 `cubiomes`、`slimeoptimizer`。
2. 构建 JAR（推荐，会联动构建子模块）：
   ```bat
   gradlew buildMainJar
   ```
   `build.gradle` 先校验子模块，再调用 `jni/build-jni.bat`：
   - 用 base 的 `scripts/build_msys2.ps1` 构建静态库与 CLI；
   - 薄桥接链接静态库，生成 `native/windows/libNoSlimeFinderLibJ.dll`；
   - 把 CLI 复制为 `native/windows/NoSlimeFinderBase-cli.exe`；
   - 两个文件都打进 JAR；子模块缺失或为空时构建直接失败并给出提示。
3. 也可单独构建原生部分：
   ```bat
   jni\build-jni.bat
   ```
4. 运行 / Run:
   ```bat
   java -jar build\libs\NoSlimeFinder-1.0.0.jar
   ```
5. 独立 CLI（可选）：
   ```bat
   NoSlimeFinderBase\build\bin\NoSlimeFinderBase-cli.exe --seed 0 --square 4096 --out result.txt
   ```
   JAR 内也带有同一个可执行文件：`native/windows/NoSlimeFinderBase-cli.exe`。

- 构建链：MSYS2 UCRT64 GCC（`C:\msys64\ucrt64\bin`）+ Gradle wrapper。
- 只改 Java 时无需重跑原生构建；改 `NoSlimeFinderBase` 源码后重跑 `gradlew buildMainJar` 即可。
- JNI 库会打进 JAR，运行时解压加载；CLI 会随 JAR 一起打包。

- Toolchain: MSYS2 UCRT64 GCC + Gradle wrapper.
- The JNI DLL is packaged into the JAR and extracted at runtime; the base CLI is packaged alongside it.
- Rebuild with `gradlew buildMainJar` after changing native sources.

## 核心依赖 / Libraries

- [cubiomes (xpple)](https://github.com/xpple/cubiomes) — biome generation (via `NoSlimeFinderBase/cubiomes`).
- [SlimeOptimizer (CITYWIDESIGN)](https://github.com/CITYWIDESIGN/SlimeOptimizer) — ring spans and exact slime-chunk test (via `NoSlimeFinderBase/slimeoptimizer`).
- **SlimeRadar is not linked.** The coarse prefilter imitates its large-tile SAT + 17×17 donut logic, but inverts the objective to low-slime and is implemented directly in `NoSlimeFinderBase/src/noslime_search.cpp`.

- **SlimeRadar is not linked**; only the prefilter idea is imitated.

## 致谢 / Credits

- 界面与项目：SunnySlopes
- 史莱姆区块判定与 192 甜甜圈定义：Bot_City（CITYWIDESIGN，SlimeOptimizer / SlimeRadar）
- 群系生成：Cubitect、xpple（cubiomes）
- 中文群系名参考：All-In-One Finder 的 `locales/zh.json`

- UI and project: SunnySlopes
- Slime-chunk math and the 192-chunk donut definition: Bot_City (CITYWIDESIGN)
- Biome generation: Cubitect, xpple (cubiomes)
- Chinese biome names: `locales/zh.json` from All-In-One Finder

## 注意 / Notes

- 结果坐标是 AFK 点的方块坐标，`/tp` 的 Y 固定为 `-64`。
- 停止会清空本次结果。
- 默认范围很大；建议先用小范围确认结果与性能。
- 最低有效面积默认 49000；如果长时间没有结果，可适当降低该阈值。
- 1.21.4 与 1.21.5 有少量生成差异；`1.21.4` 单独保留。
- 粗筛只保留 1000 个候选格、1×1 只取前 200 个，是启发式；如果最优区域在粗筛中被误判，可能被漏掉。
- 最终结果按 64×64 邻域去重，因此相邻格边界附近不会出现多个几乎相同的点位；去重后结果可能少于 100 条。
- “192 甜甜圈史莱姆区块 ≤10”是启发式过滤；192 区块在 10% 史莱姆率下期望约 19 个，≤10 属于明显低史莱姆区域，但可能漏掉 11+ 且史莱姆集中在环边缘的区域。

- Result coordinates are AFK block coordinates; the `/tp` Y is fixed at `-64`.
- Stopping clears the current results.
- The default range is very large; test with a smaller range first.
- The default min area is 49000; lower it if no results appear for a long time.
- Small generation differences remain between 1.21.4 and 1.21.5; `1.21.4` is kept separate.
- The prefilter keeps only 1,000 candidate cells and the 1x1 stage only the top 200; both are heuristic and may miss the true optimum.
- Final results are deduplicated by 64×64 neighborhood, so near-identical points across cell boundaries are removed; fewer than 100 results may remain.
- The "≤10 slime chunks in the 192-chunk donut" rule is a heuristic; with a 10% slime rate the expected count is about 19, so ≤10 is a notably low-slime region, but a 11+ region with slimes concentrated at the ring edge can be missed.

## 许可证 / License

本项目以 **MIT 许可证**发布，完整文本见 [LICENSE](LICENSE)。

This project is released under the **MIT License**. See [LICENSE](LICENSE) for the full text.

```text
MIT License

Copyright (c) 2026 SunnySlopes
```

第三方组件保留各自原有的许可 / Third-party components keep their own licenses:

- `NoSlimeFinderBase/cubiomes` — MIT, Copyright (c) 2020 Cubitect；许可证见 `NoSlimeFinderBase/cubiomes/LICENSE`。
- `NoSlimeFinderBase/slimeoptimizer` — 子模块，内容未修改；版权归 CITYWIDESIGN（原仓库未附许可证文件）。
- 打包进 JAR 的原生库 `libNoSlimeFinderLibJ.dll` 与 `NoSlimeFinderBase-cli.exe` 由上述源码构建，随本项目按 MIT 分发；cubiomes 的声明一并收录在 JAR 的 `META-INF/licenses/` 中。

- `NoSlimeFinderBase/cubiomes` — MIT, Copyright (c) 2020 Cubitect; see `NoSlimeFinderBase/cubiomes/LICENSE`.
- `NoSlimeFinderBase/slimeoptimizer` — unmodified submodule; copyright CITYWIDESIGN (the upstream repository ships no license file).
- The native library `libNoSlimeFinderLibJ.dll` and the CLI `NoSlimeFinderBase-cli.exe` packaged into the JAR are built from those sources and distributed under MIT together with this project; the cubiomes notice is bundled in the JAR under `META-INF/licenses/`.
