# Texas hold'em AoF GTO

**Texas hold'em AoF GTO** is an offline near-GTO solver and range browser for short-stack **No-Limit Texas Hold'em All-In or Fold**.

It answers one focused poker question:

> With 2 or 3 players, fixed blinds, fixed stack depth, and only two legal actions, **ALL IN** or **FOLD**, what should the preflop ranges look like?

[English](#english) | [中文](#中文)

---

## English

### What This Project Does

Texas hold'em AoF GTO computes and displays push/fold strategies for short-stack Texas Hold'em.

Supported ranges:

- **2-player AoF**
  - SB push range
  - BB call range

- **3-player AoF**
  - BTN all-in range
  - SB call range vs BTN
  - SB all-in range after BTN folds
  - BB call range vs BTN+SB
  - BB call range vs BTN only
  - BB call range vs SB only

The GUI is intentionally simple: open it, choose `2P` or `3P`, and read the red/green range grids.

- Green means **ALL IN / CALL** is the main action.
- Red means **FOLD** is the main action.
- Upper triangle: suited hands.
- Lower triangle: offsuit hands.
- Diagonal: pocket pairs.

This is not a postflop solver and not a real-time solver. It is an offline trainer for a clean all-in-or-fold preflop model.

---

### Game Rules

Default model:

- No-Limit Texas Hold'em
- All-in or fold only
- 2 or 3 players
- SB = 0.5 BB
- BB = 1 BB
- Effective stack = 10 BB
- Blinds are included in the stack
- No flop, turn, or river decisions
- If all-in is called, five board cards are dealt and hands go to showdown

At 10 BB:

- SB has posted 0.5 BB, so SB adds 9.5 BB to jam.
- BB has posted 1 BB, so BB adds 9 BB to call.

---

### Core Principle

The project has two phases:

1. **Equity precomputation**
2. **CFR+ strategy training**

Texas Hold'em has 1,326 two-card combinations. Preflop ranges are normally compressed into 169 hand classes:

- 13 pairs
- 78 suited hands
- 78 offsuit hands

The solver first computes exact all-in equities between those hand classes:

- 2-way equity for heads-up all-ins
- 3-way equity for BTN/SB/BB all-ins

Then it runs CFR+ over the all-in-or-fold decision tree. The output is a full range strategy: every hand class receives an action frequency.

The expensive computation happens offline. The GUI only loads saved strategies and displays them instantly.

---

### Why CFR+

Counterfactual Regret Minimization is a standard method for approximating equilibrium strategies in imperfect-information games.

CFR+ fits this project because:

- It naturally produces mixed strategies.
- More iterations usually improve convergence.
- It works well for poker-like games.
- Its output can be displayed directly as range frequencies.

For 2-player zero-sum AoF, exploitability is a strong convergence signal. For 3-player AoF, the reported deviation metric is useful as a training diagnostic, but it is not identical to heads-up exploitability.

---

### Thanks To OMPEval

This project uses [OMPEval](https://github.com/zekyll/OMPEval) for poker hand evaluation and all-in equity calculation.

OMPEval is fast, mature, and well suited for exact range equity enumeration. Thanks to OMPEval, this project can focus on the AoF game model, CFR+ training, strategy storage, and GUI visualization instead of reinventing a hand evaluator.

Thank you to the OMPEval author and contributors.

---

### License

This project is released under the MIT License.

OMPEval is included under its own ISC License. The ISC License allows use, copying, modification, and distribution, including open-source distribution, as long as the copyright notice and license text are preserved.

The OMPEval license files remain in the `OMPEval/` directory.

---

### One-Click GUI

For normal users:

1. Open the GUI portable package.
2. Double-click `TexasHoldemAoFGTO.exe`.
3. Choose `2P` or `3P`.
4. Read the range grids.

No Python, CMake, compiler, command line, or external `data/` folder is required for GUI use.
The release executable embeds the strategy files, so `TexasHoldemAoFGTO.exe` can be copied to another folder and launched by itself.

Build the GUI package:

```powershell
package_gui.bat
```

---

### Training From Source

Build:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

Train 2-player:

```powershell
build\aof2_train.exe --players 2 --iters 200000 --threads 8
```

Train 3-player:

```powershell
build\aof2_train.exe --players 3 --iters 100000 --threads 32
```

The first 3-player run may spend a long time building the 3-way equity table. Later runs reuse the cached table.

---

## 中文

### 这个项目是做什么的

**德扑 AoF GTO** 是一个短筹码德州扑克 **All-In or Fold** 离线近似 GTO 求解器和范围表浏览器。

它回答一个很明确的问题：

> 在 2 人或 3 人、固定盲注、固定筹码深度、只能 **ALL IN** 或 **FOLD** 的情况下，翻前范围应该是什么样？

支持的范围：

- **2 人 AoF**
  - SB 推全下范围
  - BB 跟注范围

- **3 人 AoF**
  - BTN 全下范围
  - BTN 全下后 SB 跟注范围
  - BTN 弃牌后 SB 全下范围
  - BTN+SB 都全下时 BB 跟注范围
  - 只有 BTN 全下时 BB 跟注范围
  - 只有 SB 全下时 BB 跟注范围

GUI 很简单：打开后选择 `2P` 或 `3P`，直接看红绿范围表。

- 绿色表示 **ALL IN / CALL** 是主频动作。
- 红色表示 **FOLD** 是主频动作。
- 右上三角是同花牌。
- 左下三角是非同花牌。
- 对角线是对子。

这不是翻后求解器，也不是实时求解器。它只专注一个清晰的翻前 all-in-or-fold 模型。

---

### 游戏规则

默认模型：

- 无限注德州扑克
- 只能全下或弃牌
- 2 人或 3 人
- SB = 0.5 BB
- BB = 1 BB
- 有效筹码 = 10 BB
- 盲注已经计入总筹码
- 没有翻牌、转牌、河牌决策
- 如果 all-in 被 call，就直接发五张公共牌比牌

10 BB 筹码下：

- SB 已经下了 0.5 BB，所以全下时再补 9.5 BB。
- BB 已经下了 1 BB，所以跟注时再补 9 BB。

---

### 核心原理

项目分两步：

1. **预计算 equity**
2. **用 CFR+ 训练策略**

德州扑克两张底牌共有 1,326 种组合。翻前范围通常压缩成 169 个手牌类别：

- 13 个对子
- 78 个同花
- 78 个非同花

求解器先计算这些手牌类别之间的精确 all-in equity：

- 2 人 all-in equity
- 3 人 BTN/SB/BB all-in equity

然后在 all-in-or-fold 决策树上运行 CFR+。最终输出的是完整范围策略：每个手牌类别都有动作频率。

重计算都发生在离线训练阶段。GUI 只负责加载策略并瞬时显示。

---

### 为什么用 CFR+

Counterfactual Regret Minimization 是不完美信息博弈中常用的均衡近似方法。

CFR+ 适合本项目，因为：

- 能自然产生混合策略。
- 迭代越多通常越接近均衡。
- 适合扑克类游戏。
- 输出频率可以直接展示成范围表。

2 人零和 AoF 的 exploitability 比较容易严格解释。3 人 AoF 的偏离指标更适合作为训练诊断，不应和 heads-up exploitability 完全等同。

---

### 感谢 OMPEval

本项目使用 [OMPEval](https://github.com/zekyll/OMPEval) 做扑克手牌评估和 all-in equity 计算。

OMPEval 很快、成熟，并且非常适合本项目需要的 exact range equity 枚举。有了 OMPEval，本项目可以专注于 AoF 游戏建模、CFR+ 训练、策略保存和 GUI 展示，而不是重复造手牌评估轮子。

感谢 OMPEval 的作者和贡献者。

---

### 许可证

本项目使用 MIT License。

OMPEval 使用它自己的 ISC License。ISC License 允许使用、复制、修改和分发，也包括开源分发，只要保留版权声明和许可证文本。

OMPEval 的许可证文件保留在 `OMPEval/` 目录下。

---

### 傻瓜式 GUI 使用

普通用户：

1. 打开 GUI 便携包。
2. 双击 `TexasHoldemAoFGTO.exe`。
3. 选择 `2P` 或 `3P`。
4. 直接看范围表。

正常 GUI 使用不需要 Python、CMake、编译器、命令行或额外的 `data/` 文件夹。
发布版 exe 已内置策略文件，所以可以只复制 `TexasHoldemAoFGTO.exe` 到其他目录并直接启动。

构建 GUI 包：

```powershell
package_gui.bat
```

---

### 从源码训练

编译：

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

训练 2 人：

```powershell
build\aof2_train.exe --players 2 --iters 200000 --threads 8
```

训练 3 人：

```powershell
build\aof2_train.exe --players 3 --iters 100000 --threads 32
```

第一次 3 人训练可能会花很久生成 3-way equity 表。之后会复用缓存。
