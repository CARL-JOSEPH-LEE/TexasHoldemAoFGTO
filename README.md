Texas Hold’em AoF Studio
2–4 人德州扑克 All-In or Fold 离线研究台 · 3.0.0

研究短筹码、等额总筹码下的翻前全下与弃牌策略。支持四人完整行动树、可配置底池抽水、本地训练和策略分析，训练与评估均在本机完成。

本项目的范围是 AoF，不包含完整翻后无限注德州。策略通过采样得到，界面同时提供误差指标；显示的频率不能直接视为已经证明的精确 GTO。


主要功能

支持 2、3、4 人。四人按 CO、BTN、SB、BB 行动，覆盖全部 14 个决策节点；三人为 6 个，两人为 2 个。BB 前方全部弃牌时直接获胜。

支持按底池比例抽水、单局封顶和“无人跟注不抽水”，处理弃牌盲注、未跟注下注退回与平局分池。

默认使用条件分层采样与线性 CFR；提供 DCFR、HS-DCFR 实验选项、可调整的每层样本数、完整检查点和多线程训练。

工作台包含行动频率、行动 EV 差值、判断确定性和策略频率差异四种热图，支持手牌查询、混合策略筛选、键盘导航、深浅主题和 CSV 导出。

单手独立评估将新样本集中到指定场景，保存预算、种子、策略指纹、有效样本数和误差区间。训练历史、完整日志和结果均保存在本机。


快速开始

Windows 用户可以使用构建好的发行包，无需安装 Python 或编译器。已发布的附件请查看仓库 Releases；没有对应版本时，可按下文从源码构建。
https://github.com/CARL-JOSEPH-LEE/TexasHoldemAoFGTO/releases

完整目录版解压后，运行 TexasHoldemAoFGTO.exe。目录版需要整体保留，不能只复制其中的 exe。单文件便携版可以直接启动；对外分发时还应提供对应的源码与许可材料。

源码仓库保留代码、必要文档、第三方许可和内置策略。可执行程序、编译产物、发行压缩包和个人训练数据不再纳入 Git 跟踪。

从源码运行需要 Python 3.12、CMake 3.16 或更新版本，以及支持 C++17 的编译器。Windows 可使用 MinGW-w64 或 Visual Studio；使用 Ninja 时，编译器需已加入当前终端的 PATH。建议使用独立 Python 虚拟环境。

在项目根目录依次运行：

python -m pip install -r requirements-dev.txt
python tools/build.py
python gui/aof2_gui.py

构建脚本会编译原生引擎并执行 C++ 测试。OMPEval 源码已随仓库提供，无需另外下载预编译静态库。


使用工作台

选择人数和内置策略，再选择位置与行动历史。点击范围表中的手牌，或输入 AA、AKs、T9o 查看行动频率和 EV。范围表上三角为同花，下三角为非同花，对角线为对子；总体频率按真实组合数加权。

左侧修改筹码、盲注、抽水与预算后，点击“开始训练”。修改参数不会改变已载入的策略；新结果完成后自动保存并载入。进度包含训练、全局评估和逐手审计三个阶段。

“训练与日志”可查看历史记录、载入结果和恢复检查点。取消会停止当前训练；恢复时从最近成功保存的检查点继续，追加当前设置的采样预算。

选择另一份策略并切换到“频率差异”，可查看两份策略的行动频率差，单位为百分点。规则不同时会显示提示，应结合各自的筹码和抽水解释差异。

手牌详情中的“独立精算此手”使用新样本进行 Monte Carlo 评估，不修改原策略。单次区间针对该手牌的本次固定预算评估，不能替代原文件的全局置信指标。


抽水与游戏规则

界面默认：10 BB 总筹码、小盲 0.5 BB、大盲 1 BB、3% 抽水、不封顶、无人跟注不抽水。
命令行默认抽水为 0%；启用 3% 抽水时，需要显式传入 --rake-percent 3。

抽水先按整个可抽水底池计算一次，再分配给胜者。设置单局封顶时取较小值，封顶 0 表示不封顶。平局平分扣除抽水后的底池，未被跟注的下注退回，不参与抽水。

四人各投入 10 BB：底池 40 BB，3% 抽水为 1.2 BB，胜者分配 38.8 BB；两人平局时各分得 19.4 BB。
CO 全下、BTN 跟注、两位盲注弃牌：底池 21.5 BB，3% 抽水为 0.645 BB。
CO 全下、其余全弃：默认不抽水。启用无人跟注抽水后，退回 9 BB 未跟注下注，按 2.5 BB 底池抽取 0.075 BB。

金额统一使用 BB，EV 从下盲注前计算。每局玩家 EV 之和加上抽水金额为 0。
当前不支持不等筹码边池、前注、保险、jackpot、返佣或货币最小单位取整。使用时需按目标游戏的实际规则配置。


算法与准确性

默认分层线性 CFR 按行动场景、本人手牌及后续行动路径分配样本，保留实体牌花色、弃牌者移牌和逐次移牌后的重要性权重。弃牌及无人跟注收益直接计算，摊牌才采样公共牌。

四人每轮覆盖 5239 个手牌与路径组合，默认每层 4 个样本。预算向上取整至完整轮次，结果记录实际采样次数。不同引擎的采样计数含义不同，不能只按“迭代次数”比较速度。

全局评估使用独立均匀发牌，逐手审计使用独立条件样本，并报告 EV 标准误、有效样本数、偏离和点估计及保守统计上界。EV 差值区间跨过 0 时，当前样本不足以明确区分动作价值；EV 区间不是行动频率区间。

3.0 的四人、10 BB、3% 抽水基准比较三种算法、三个种子与两种每层样本数，共 18 次实验。DCFR、HS-DCFR 未显示出替代线性 CFR 的优势，因此保持为实验选项。完整预算、命令、时间、指标和限制见 docs/RELEASE_NOTES.txt 与 docs/benchmark-v3.json。

相同构建、配置与随机种子下，改变物理线程数或从检查点恢复可保持结果逐位一致；不承诺跨编译器、标准库或不同算法版本逐位相同。检查点校验规则、种子、算法和每层采样数；HS 调度周期保存在检查点内。

四人及抽水模型不能直接套用双人零和 CFR 的均衡保证。本项目尚未完成独立商业求解器对照认证，不宣称四人精确 GTO。


内置策略与文件

保留两份历史无抽水策略，以及 2/3/4 人 × 0%/3% 抽水的六份整桌采样策略和六份分层策略。默认优先显示分层策略。

六份分层策略均为 10 BB、小盲 0.5 BB、大盲 1 BB、不封顶、无人跟注不抽水；生成预算为十亿训练样本、两千万全局评估与两亿条件审计，种子 2026。实际计数、命令及 SHA-256 见 data/PROVENANCE_V2.txt，旧策略来源见 data/PROVENANCE.txt。

新策略使用带 CRC-32 的 v3 文件格式，兼容读取旧版本；内置策略另有打包时生成的 SHA-256 清单。CRC 用于发现内容损坏，不是数字签名。

源码运行的个人结果默认保存在 data/local；便携版使用系统应用数据目录。可用 AOF_STRATEGY_DIR 指定其他可写目录，实际路径会在界面显示。个人策略、检查点、数据库、日志和导出文件均不应提交到源码仓库。


命令行示例

以下为 Windows 示例；Linux 使用 build/aof2_train 和 build/aof2_eval。

训练四人 10 BB、3% 抽水策略，并保存检查点：
build/aof2_train.exe --players 4 --stack 10 --rake-percent 3 --iters 100000000 --eval-samples 1000000 --audit-samples 20000000 --seed 2026 --strategy data/local/my_4p.bin --checkpoint data/local/my_4p.checkpoint

从相同规则与种子的检查点继续，--iters 表示累计目标预算：
build/aof2_train.exe --players 4 --stack 10 --rake-percent 3 --seed 2026 --iters 1000000000 --resume data/local/my_4p.checkpoint --strategy data/local/my_4p_long.bin --checkpoint data/local/my_4p_long.checkpoint

独立重新评估并导出 CSV，不修改原策略：
build/aof2_eval.exe --strategy data/local/my_4p.bin --samples 5000000 --audit-samples 200000000 --seed 42 --csv data/local/my_4p_eval.csv

对内置四人策略中 BB 面对三家全下的 JTs 进行单手评估：
build/aof2_eval.exe --strategy data/strategy_stratified_4p_10bb_rake3.bin --node 13 --hand JTs --samples 1000000 --seed 20260905 --threads 4

可选参数：--rake-cap 0.5 设置封顶；--rake-uncontested 对无人跟注底池抽水；--discounting dcfr 或 --discounting hs-dcfr 使用实验算法；--batch-samples 调整每层采样数。完整参数见 --help，引擎版本见 --version。

旧整桌采样引擎使用 --engine sampled。旧全表引擎使用 --engine table，仅支持历史无抽水 2/3 人模型，需要预计算胜率缓存；其三人弃牌情形采用历史近似，指标不能直接与新引擎比较。


验证与复现

python -m pytest -q
python -m ruff check gui tests tools
ctest --test-dir build -C Release --output-on-failure

3.0 本地验收通过 49 项 Python 测试和 4 组 C++ 测试。独立牌力参考实现穷举全部 2,598,960 组五张牌并核对 7462 个牌力等级，另对 50,000 组七张牌枚举全部五张子集，与 OMPEval 交叉校验。

测试还覆盖抽水守恒、分池、移牌、合法范围质量、重要性权重、文件完整性、中文路径、跨线程复现与续训一致性。Windows 目录版和单文件版通过隔离 PATH 的实际训练与评估验收。这些检查不等于所有游戏配置的形式化正确性证明。

Windows 下复现三种算法的多种子基准：
python tools/benchmark_release.py --output benchmarks/batch4 --budget 300000000 --eval 5000000 --audit 50000000 --threads 4 --batch 4
python tools/benchmark_release.py --output benchmarks/batch64 --budget 300000000 --eval 5000000 --audit 50000000 --threads 4 --batch 64

CI 配置包含 Windows / Linux 构建与测试、Linux ASan / UBSan；手动运行 workflow_dispatch 时还会构建并验收 Windows 目录发行包。最新执行状态以 GitHub Actions 页面为准。


打包与发布

准备固定版本的依赖源码与许可材料，再打包完整目录版：
python tools/prepare_sources.py
python tools/package_gui.py --format onedir

输出为 dist/AoFStudio-3.0.0-windows-x64.zip，包含应用、内置策略、训练与评估引擎、应用源码、Qt / PySide 对应源码、许可证及文件校验清单。

验收发行程序：
python tools/verify_release.py --exe dist/TexasHoldemAoFGTO/TexasHoldemAoFGTO.exe --work build/portable-check

可选单文件构建：
python tools/package_gui.py --format onefile

输出为 dist/TexasHoldemAoFGTO.exe 及其 .manifest.json。发行附件需要单独上传到 GitHub Releases；git push 不会上传被忽略的 dist 目录。单文件对外分发时，应同时提供完整源码与许可材料。

目录版的 Qt 共享库允许替换为 ABI 兼容的修改版。构建与重新链接说明见 docs/BUILD_AND_RELINK.txt。当前发行包未做商业代码签名。


项目结构与资料

src/core：游戏规则、抽水结算、采样、CFR、策略文件及统计评估。
src/train_main.cpp、src/eval_main.cpp：训练与评估命令行入口。
gui：PySide6 工作台、范围热图、策略解析与本地训练记录。
tests：原生参考校验、算法测试和 Python 集成测试。
tools：构建、基准、依赖源码准备、打包与发行验收。
data：经过评估的内置策略及生成来源。
OMPEval：第三方手牌评估器源码与原始许可证。
third_party：依赖版本、来源校验值与许可证。

docs/RELEASE_NOTES.txt：3.0 变更、实验结果与验证边界。
docs/算法与训练说明.txt：采样权重、训练过程和置信区间说明。
docs/research.txt：研究论文、实现参考与算法取舍。
docs/benchmark-v3.json：18 次算法对照实验的机器可读记录。
docs/BUILD_AND_RELINK.txt：发行构建和依赖替换说明。


许可

应用源码使用 MIT License。OMPEval 使用 ISC License，原始许可与 libdivide 许可随源码保留。GUI 使用 PySide6 Essentials；Qt、PySide、Shiboken 及其他发行依赖的许可和源码要求见 THIRD_PARTY_NOTICES.txt 与 third_party。

OMPEval 上游项目：https://github.com/zekyll/OMPEval


English overview

Offline 2–4 player, equal-stack all-in-or-fold poker research tool with configurable pot rake, stratified linear CFR training, reproducible checkpoints, independent uncertainty estimates, strategy comparison and focused hand evaluation. DCFR and HS-DCFR are experimental options. Strategies are sampled approximations, not exact Nash certificates. Build with Python 3.12, CMake and a C++17 compiler. Distribute Windows binaries through release attachments, with the corresponding source and license materials.
