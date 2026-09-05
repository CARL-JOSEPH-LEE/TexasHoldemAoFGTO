# Texas hold'em AoF GTO

**Texas hold'em AoF GTO** is an offline near-GTO solver and range browser for short-stack **No-Limit Texas Hold'em All-In or Fold**.

It answers one focused poker question:

> With 2, 3, or 4 players, fixed blinds, fixed stack depth, configurable pot rake, and only two legal actions, **ALL IN** or **FOLD**, what should the preflop ranges look like?

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

- **4-player AoF**
  - CO all-in range
  - BTN ranges after either CO action
  - SB ranges after all CO/BTN action histories
  - BB call ranges against every nonempty combination of previous all-ins
  - 14 decision nodes in total, in CO → BTN → SB → BB order

Open the GUI, choose `2P`, `3P`, or `4P`, select a strategy and action history, and read the range grids.

- Teal shows the **ALL IN / CALL** frequency.
- Dark blue shows the **FOLD** frequency.
- Upper triangle: suited hands.
- Lower triangle: offsuit hands.
- Diagonal: pocket pairs.

The GUI also supports local training, checkpoint resume, individual-hand evaluation, strategy comparison, CSV export, training history, and light/dark themes.

This is not a postflop solver and not a real-time solver. It is an offline trainer for a clean all-in-or-fold preflop model.

---

### Game Rules

Default model:

- No-Limit Texas Hold'em
- All-in or fold only
- 2, 3, or 4 players
- SB = 0.5 BB
- BB = 1 BB
- Equal total stacks = 10 BB
- Blinds are included in the stack
- GUI default rake = 3% of the pot, uncapped, with no rake on uncontested pots
- Optional fixed rake: charge a fixed amount, such as 0.5 BB, per eligible pot
- CLI default rake = 0%; use `--rake-percent 3` to enable 3% rake
- No flop, turn, or river decisions
- If all-in is called, five board cards are dealt and hands go to showdown

At 10 BB:

- SB has posted 0.5 BB, so SB adds 9.5 BB to jam.
- BB has posted 1 BB, so BB adds 9 BB to call.

Uncalled excess bets are returned before rake. Rake is taken once from the pot, including folded blinds, before distributing winnings or splitting ties. Percentage mode supports a configurable per-hand cap; cap 0 means uncapped.

Fixed mode charges the configured BB amount regardless of pot size: both a 21.5 BB pot and a 40 BB pot pay 0.5 BB when fixed rake is 0.5 BB. The charge never exceeds the pot. The “no rake on uncontested pots” setting applies in both modes; fixed rake is not combined with percentage rake or its cap. Select “固定金额” in the GUI and train a new strategy for that rule.

For example, four players each contributing 10 BB create a 40 BB pot: 3% rake is 1.2 BB, leaving 38.8 BB for the winner. The current model uses equal stacks and no ante; unequal-stack side pots are not supported.

---

### Core Principle

The default engine has three phases:

1. **Conditional stratified sampling**
2. **Linear CFR strategy training**
3. **Independent evaluation with uncertainty estimates**

Texas Hold'em has 1,326 two-card combinations. Preflop ranges are normally compressed into 169 hand classes:

- 13 pairs
- 78 suited hands
- 78 offsuit hands

The solver allocates samples to each action history, hand class, and continuation path. It samples physical cards, preserves card removal from folded hands, and applies importance weights when sampling opponent ranges.

It then runs linear CFR over the all-in-or-fold decision tree. The output is a full range strategy: every hand class receives an action frequency. Independent evaluators report EV, standard errors, effective sample sizes, and a conservative deviation bound.

The expensive computation happens offline. The GUI loads saved strategies instantly and can launch training or evaluate individual hands without modifying the original strategy.

---

### Why CFR

Counterfactual Regret Minimization is a standard method for approximating equilibrium strategies in imperfect-information games.

CFR fits this project because:

- It naturally produces mixed strategies.
- It supports regret-based updates and averaged strategies.
- It works well for poker-like games.
- Its output can be displayed directly as range frequencies.

For 2-player zero-sum AoF, exploitability is a strong convergence signal. For multiplayer or raked AoF, the reported deviation estimate and confidence bound are useful diagnostics; they are not an exact Nash certificate. An EV interval crossing zero means the current samples cannot clearly distinguish the actions, not that the displayed mixing frequency is exact.

Linear CFR remains the default. DCFR and HS-DCFR are experimental options: 18 four-player rake benchmark runs did not establish an advantage over the default. Checkpoints retain the complete training state; the same build and configuration reproduce results across thread counts and resume boundaries.

---

### Thanks To OMPEval

This project uses [OMPEval](https://github.com/zekyll/OMPEval) for poker hand evaluation and all-in equity calculation.

OMPEval handles showdown hand ranking efficiently. Thanks to OMPEval, this project can focus on the AoF game model, CFR training, strategy storage, and GUI visualization instead of reinventing a hand evaluator.

Thank you to the OMPEval author and contributors.

---

### License

This project is released under the MIT License.

OMPEval is included under its own ISC License. The ISC License allows use, copying, modification, and distribution, including open-source distribution, as long as the copyright notice and license text are preserved.

The OMPEval license files remain in the `OMPEval/` directory. The GUI uses PySide6; dependency notices, corresponding source, and relinking instructions are described in [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt) and [BUILD_AND_RELINK.txt](docs/BUILD_AND_RELINK.txt).

---

### One-Click GUI

For normal users:

1. Open the GUI portable package.
2. Double-click `TexasHoldemAoFGTO.exe`.
3. Choose `2P`, `3P`, or `4P`, then select a strategy and action history.
4. Read the range grids.

No Python, CMake, compiler, command line, or external `data/` folder is required for GUI use.
The directory release must be kept together. The optional single-file executable embeds its dependencies and strategies and can be copied to another folder by itself.

Executable packages are distributed separately from the source repository through [GitHub Releases](https://github.com/CARL-JOSEPH-LEE/TexasHoldemAoFGTO/releases), when available. Personal strategies, logs, and checkpoints are kept locally and ignored by Git.

Build the GUI package:

```powershell
python -m pip install -r requirements-build.txt
.\package_gui.bat
```

The default output is `dist/AoFStudio-3.1.0-windows-x64.zip`, including source and license materials. For the optional single-file build, run `python tools/package_gui.py --format onefile`.

---

### Training From Source

Requires Python 3.12, CMake 3.16 or newer, and a C++17 compiler. Build:

```powershell
python -m pip install -r requirements-dev.txt
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

Train 2-player:

```powershell
build\aof2_train.exe --players 2 --iters 100000000 --threads 8
```

Train 3-player:

```powershell
build\aof2_train.exe --players 3 --iters 100000000 --threads 8
```

Train 4-player with 3% rake and save a checkpoint:

```powershell
build\aof2_train.exe --players 4 --rake-percent 3 --iters 100000000 --threads 8 --strategy data/local/my_4p.bin --checkpoint data/local/my_4p.checkpoint
```

Train the same game with fixed 0.5 BB rake:

```powershell
build\aof2_train.exe --players 4 --rake-fixed 0.5 --iters 100000000 --threads 8 --strategy data/local/my_4p_fixed.bin --checkpoint data/local/my_4p_fixed.checkpoint
```

Fixed-rake strategies and checkpoints store their mode and amount. Existing percentage-rake files remain readable; changing rake requires a separate training run. CLI fixed mode uses `--rake-fixed` without `--rake-percent`, `--rake`, or `--rake-cap`.

The default stratified engine starts without an equity-table precomputation. The legacy `--engine table` remains available for historical 2/3-player, no-rake models; it does not support four players or rake. Sampling counts are not full poker hands or a guarantee of convergence.

Six built-in stratified strategies cover 2/3/4 players and 0%/3% rake, alongside the previous strategies. Their generation settings and hashes are recorded in [PROVENANCE_V2.txt](data/PROVENANCE_V2.txt).

Validation commands:

```powershell
ctest --test-dir build -C Release --output-on-failure
python -m pytest -q
python -m ruff check gui tests tools
```

For measured accuracy, benchmark results, and limitations, see [release notes](docs/RELEASE_NOTES.txt). Sampling and confidence formulas are documented in [算法与训练说明.txt](docs/算法与训练说明.txt).

---

## 中文

### 这个项目是做什么的

**德扑 AoF GTO** 是一个短筹码德州扑克 **All-In or Fold** 离线近似 GTO 求解器和范围表浏览器。

它回答一个很明确的问题：

> 在 2 人、3 人或 4 人、固定盲注、固定筹码深度、可配置底池抽水、只能 **ALL IN** 或 **FOLD** 的情况下，翻前范围应该是什么样？

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

- **4 人 AoF**
  - CO 全下范围
  - CO 全下或弃牌后 BTN 的范围
  - CO、BTN 各种行动历史下 SB 的范围
  - 前面任意非空组合全下时 BB 的跟注范围
  - 按 CO → BTN → SB → BB 行动，共 14 个决策节点

打开 GUI 后选择 `2P`、`3P` 或 `4P`，再选择策略与行动场景，即可查看范围表。

- 青绿色显示 **ALL IN / CALL** 频率。
- 深蓝色显示 **FOLD** 频率。
- 右上三角是同花牌。
- 左下三角是非同花牌。
- 对角线是对子。

GUI 还支持本地训练、断点续训、单手独立评估、策略对比、CSV 导出、训练历史及深浅主题。

这不是翻后求解器，也不是实时求解器。它只专注一个清晰的翻前 all-in-or-fold 模型。

---

### 游戏规则

默认模型：

- 无限注德州扑克
- 只能全下或弃牌
- 2 人、3 人或 4 人
- SB = 0.5 BB
- BB = 1 BB
- 各玩家总筹码相同，默认 10 BB
- 盲注已经计入总筹码
- GUI 默认抽水为底池的 3%，不封顶，无人跟注不抽水
- 可切换为固定抽水，例如每个符合条件的底池固定收取 0.5 BB
- 命令行默认不抽水，使用 `--rake-percent 3` 开启 3% 抽水
- 没有翻牌、转牌、河牌决策
- 如果 all-in 被 call，就直接发五张公共牌比牌

10 BB 筹码下：

- SB 已经下了 0.5 BB，所以全下时再补 9.5 BB。
- BB 已经下了 1 BB，所以跟注时再补 9 BB。

未被跟注的多余下注先退回，不参与抽水。弃牌玩家投入的盲注计入底池，先对整个底池抽水一次，再分配给胜者；平局平分扣除抽水后的底池。比例抽水可设置每局封顶，封顶 0 表示不封顶。

固定抽水模式按设定的 BB 金额收费，与底池大小无关：设为 0.5 BB 时，21.5 BB 和 40 BB 的底池都只抽 0.5 BB。抽水不超过实际底池；“无人跟注不抽水”开关在两种模式下都生效。固定抽水不叠加比例或比例封顶。在 GUI 中选择“固定金额”，输入金额后重新训练该规则下的策略。

例如四人各投入 10 BB，底池 40 BB，3% 抽水为 1.2 BB，胜者分得 38.8 BB。当前模型为等筹码、无前注，不支持不等筹码边池。

---

### 核心原理

默认引擎分三步：

1. **按行动场景与手牌进行条件分层采样**
2. **用线性 CFR 训练策略**
3. **独立评估并计算误差指标**

德州扑克两张底牌共有 1,326 种组合。翻前范围通常压缩成 169 个手牌类别：

- 13 个对子
- 78 个同花
- 78 个非同花

求解器按行动历史、本人手牌与后续行动路径分配样本。采样使用真实牌组合，保留弃牌者的移牌影响，并通过重要性权重校正对手范围采样。

然后在 all-in-or-fold 决策树上运行线性 CFR。最终输出的是完整范围策略：每个手牌类别都有动作频率。独立评估器同时报告 EV、标准误、有效样本数和保守的偏离上界。

重计算都发生在离线训练阶段。GUI 可以即时载入已有策略，也能启动训练或单独评估一手牌，独立评估不会修改原策略。

---

### 为什么用 CFR

Counterfactual Regret Minimization 是不完美信息博弈中常用的均衡近似方法。

CFR 适合本项目，因为：

- 能自然产生混合策略。
- 支持遗憾更新与平均策略积累。
- 适合扑克类游戏。
- 输出频率可以直接展示成范围表。

2 人零和 AoF 的 exploitability 比较容易严格解释。多人或抽水模型的偏离估计和置信上界适合作为训练诊断，不能视为精确 Nash 均衡证明。EV 区间跨过零时，当前样本还不足以明确区分动作价值，也不能把显示的混合频率当成唯一精确答案。

默认保留线性 CFR。DCFR 与 HS-DCFR 是实验选项，18 次四人抽水基准尚未证明它们优于默认方法。检查点保存完整训练状态，同一构建与配置下可以跨线程数、断点续训复现相同结果。

---

### 感谢 OMPEval

本项目使用 [OMPEval](https://github.com/zekyll/OMPEval) 做扑克手牌评估和 all-in equity 计算。

OMPEval 高效地完成摊牌牌力比较。有了 OMPEval，本项目可以专注于 AoF 游戏建模、CFR 训练、策略保存和 GUI 展示，而不是重复造手牌评估轮子。

感谢 OMPEval 的作者和贡献者。

---

### 许可证

本项目使用 MIT License。

OMPEval 使用它自己的 ISC License。ISC License 允许使用、复制、修改和分发，也包括开源分发，只要保留版权声明和许可证文本。

OMPEval 的许可证文件保留在 `OMPEval/` 目录下。GUI 使用 PySide6，其他依赖的许可、对应源码与重新链接说明见 [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt) 和 [BUILD_AND_RELINK.txt](docs/BUILD_AND_RELINK.txt)。

---

### 傻瓜式 GUI 使用

普通用户：

1. 打开 GUI 便携包。
2. 双击 `TexasHoldemAoFGTO.exe`。
3. 选择 `2P`、`3P` 或 `4P`，再选择策略与行动场景。
4. 直接看范围表。

正常 GUI 使用不需要 Python、CMake、编译器、命令行或额外的 `data/` 文件夹。
目录发行版需要完整保留文件夹；可选的单文件便携版已内置依赖和策略，可以只复制该 exe 到其他目录启动。

可执行包与源码分开管理，已发布附件可在 [GitHub Releases](https://github.com/CARL-JOSEPH-LEE/TexasHoldemAoFGTO/releases) 中查看。个人策略、日志与检查点保存在本机，不提交到 Git。

构建 GUI 包：

```powershell
python -m pip install -r requirements-build.txt
.\package_gui.bat
```

默认生成 `dist/AoFStudio-3.1.0-windows-x64.zip`，包含源码与许可材料。单文件版本使用 `python tools/package_gui.py --format onefile` 构建。

---

### 从源码训练

需要 Python 3.12、CMake 3.16 或更新版本，以及 C++17 编译器。编译：

```powershell
python -m pip install -r requirements-dev.txt
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

训练 2 人：

```powershell
build\aof2_train.exe --players 2 --iters 100000000 --threads 8
```

训练 3 人：

```powershell
build\aof2_train.exe --players 3 --iters 100000000 --threads 8
```

训练 4 人，并启用 3% 抽水与检查点保存：

```powershell
build\aof2_train.exe --players 4 --rake-percent 3 --iters 100000000 --threads 8 --strategy data/local/my_4p.bin --checkpoint data/local/my_4p.checkpoint
```

同样的四人规则，改用固定 0.5 BB 抽水：

```powershell
build\aof2_train.exe --players 4 --rake-fixed 0.5 --iters 100000000 --threads 8 --strategy data/local/my_4p_fixed.bin --checkpoint data/local/my_4p_fixed.checkpoint
```

策略与检查点会保存固定抽水的模式和金额。旧版比例策略仍可载入；更改抽水规则需单独训练。命令行固定模式只传 `--rake-fixed`，不要同时传 `--rake-percent`、`--rake` 或 `--rake-cap`。

默认分层引擎可以直接训练，无需预计算 equity 表。旧版 `--engine table` 仍可用于历史无抽水 2/3 人模型，不支持四人或抽水。采样次数不是完整牌局数，也不代表已经达到某个收敛精度。

内置六份分层策略，覆盖 2/3/4 人与 0%/3% 抽水，同时保留旧策略。生成参数与文件校验值见 [PROVENANCE_V2.txt](data/PROVENANCE_V2.txt)。

验证命令：

```powershell
ctest --test-dir build -C Release --output-on-failure
python -m pytest -q
python -m ruff check gui tests tools
```

实际精度、实验结果与适用边界见 [升级说明](docs/RELEASE_NOTES.txt)；采样和置信区间推导见 [算法与训练说明](docs/算法与训练说明.txt)。
