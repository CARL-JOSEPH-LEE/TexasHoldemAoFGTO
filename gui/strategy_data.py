from __future__ import annotations

import csv
import hashlib
import math
import struct
import zlib
from dataclasses import dataclass, fields
from enum import Enum
from pathlib import Path

NUM_HAND_CLASSES = 169
NUM_RANKS = 13
RANKS = "23456789TJQKA"
STRAT2_MAGIC = b"AOF2STR2"
STRAT2_VERSION = 2
STRAT3_MAGIC = b"AOF2ST31"
STRAT3_VERSION = 1


class Kind(Enum):
    PAIR = 0
    SUITED = 1
    OFFSUIT = 2


@dataclass(frozen=True)
class HandClass:
    rank_high: int
    rank_low: int
    kind: Kind

    def __post_init__(self) -> None:
        if not (0 <= self.rank_high < 13):
            raise ValueError(f"rank_high out of range: {self.rank_high}")
        if not (0 <= self.rank_low < 13):
            raise ValueError(f"rank_low out of range: {self.rank_low}")
        if self.kind is Kind.PAIR and self.rank_high != self.rank_low:
            raise ValueError("pair requires rank_high == rank_low")
        if self.kind is not Kind.PAIR and self.rank_high <= self.rank_low:
            raise ValueError("non-pair requires rank_high > rank_low")

    def index(self) -> int:
        if self.kind is Kind.PAIR:
            return self.rank_high
        offset = self.rank_high * (self.rank_high - 1) // 2 + self.rank_low
        return (13 if self.kind is Kind.SUITED else 91) + offset

    def label(self) -> str:
        s = RANKS[self.rank_high] + RANKS[self.rank_low]
        if self.kind is Kind.SUITED:
            return s + "s"
        if self.kind is Kind.OFFSUIT:
            return s + "o"
        return s

    def num_combos(self) -> int:
        if self.kind is Kind.PAIR:
            return 6
        if self.kind is Kind.SUITED:
            return 4
        return 12

    @classmethod
    def from_index(cls, idx: int) -> HandClass:
        if not 0 <= idx < NUM_HAND_CLASSES:
            raise ValueError("invalid hand index")
        if idx < 13:
            return cls(idx, idx, Kind.PAIR)
        kind = Kind.SUITED if idx < 91 else Kind.OFFSUIT
        offset = idx - (13 if kind is Kind.SUITED else 91)
        rh = 1
        while rh * (rh - 1) // 2 + rh - 1 < offset:
            rh += 1
        return cls(rh, offset - rh * (rh - 1) // 2, kind)

    @classmethod
    def make(cls, rank_a: int, rank_b: int, suited: bool) -> HandClass:
        if rank_a == rank_b:
            return cls(rank_a, rank_a, Kind.PAIR)
        rh = max(rank_a, rank_b)
        rl = min(rank_a, rank_b)
        return cls(rh, rl, Kind.SUITED if suited else Kind.OFFSUIT)


class HandParseError(ValueError):
    pass


def parse_hand_input(text: str) -> HandClass:
    if text is None:
        raise HandParseError("empty input")
    s = text.strip().upper()
    if not s:
        raise HandParseError("empty input")
    if len(s) < 2 or len(s) > 3:
        raise HandParseError(f"length must be 2 or 3 (got '{text}'); examples: AA, KK, AKs, T9o")
    rank_set = set(RANKS)
    if s[0] not in rank_set:
        raise HandParseError(f"first char '{s[0]}' is not a valid rank (use 23456789TJQKA)")
    if s[1] not in rank_set:
        raise HandParseError(f"second char '{s[1]}' is not a valid rank (use 23456789TJQKA)")

    r1 = RANKS.index(s[0])
    r2 = RANKS.index(s[1])

    if r1 == r2:
        if len(s) != 2:
            raise HandParseError(
                f"pocket pair must be 2 chars only (got '{text}'); "
                "do NOT add 's' or 'o' (a pocket pair is always offsuit)"
            )
        return HandClass.make(r1, r1, False)

    if len(s) != 3:
        raise HandParseError(f"non-pair must end with 's' (suited) or 'o' (offsuit); got '{text}'")
    suit_char = s[2]
    if suit_char == "S":
        return HandClass.make(r1, r2, True)
    if suit_char == "O":
        return HandClass.make(r1, r2, False)
    raise HandParseError(f"last char must be 's' or 'o' (got '{s[2]}')")


@dataclass
class GameParams:
    sb_blind: float
    bb_blind: float
    stack: float
    rake_rate: float = 0.0
    rake_cap: float = 0.0
    no_flop_no_drop: bool = True
    rake_mode: str = "percentage"
    rake_fixed: float = 0.0

    def __post_init__(self) -> None:
        if not all(
            math.isfinite(v)
            for v in (
                self.sb_blind,
                self.bb_blind,
                self.stack,
                self.rake_rate,
                self.rake_cap,
                self.rake_fixed,
            )
        ):
            raise ValueError("规则参数必须是有限数字")
        if not 0 < self.sb_blind <= self.bb_blind < self.stack <= 1_000_000:
            raise ValueError("必须满足 0 < 小盲 ≤ 大盲 < 筹码 ≤ 1000000")
        if not 0 <= self.rake_rate <= 1 or self.rake_cap < 0:
            raise ValueError("抽水必须为 0–100%，封顶金额不能为负数")
        if self.rake_mode not in ("percentage", "fixed") or self.rake_fixed < 0:
            raise ValueError("抽水模式无效或固定金额为负数")
        if (self.rake_mode == "fixed" and (self.rake_rate != 0 or self.rake_cap != 0)) or (
            self.rake_mode == "percentage" and self.rake_fixed != 0
        ):
            raise ValueError("固定抽水不能与比例抽水、比例封顶同时使用")


@dataclass
class Strategy2P:
    params: GameParams
    sb_push: tuple[float, ...]
    bb_call: tuple[float, ...]
    sb_push_ev: tuple[float, ...]
    bb_call_ev: tuple[float, ...]
    sb_fold_ev: float
    bb_fold_ev: float
    sb_ev: float
    exploitability_bb: float
    iterations: int

    @classmethod
    def load(cls, path: Path) -> Strategy2P:
        data = path.read_bytes()
        cur = 0
        if data[cur : cur + 8] != STRAT2_MAGIC:
            raise ValueError(f"bad magic in 2P strategy file: {path}")
        cur += 8

        version, n = struct.unpack_from("<II", data, cur)
        cur += 8
        if version != STRAT2_VERSION:
            raise ValueError(f"unsupported 2P strategy version: {version}")
        if n != NUM_HAND_CLASSES:
            raise ValueError(f"hand class count mismatch: {n}")

        sb_blind, bb_blind, stack = struct.unpack_from("<ddd", data, cur)
        cur += 24

        sb_push = struct.unpack_from(f"<{NUM_HAND_CLASSES}d", data, cur)
        cur += 8 * NUM_HAND_CLASSES
        bb_call = struct.unpack_from(f"<{NUM_HAND_CLASSES}d", data, cur)
        cur += 8 * NUM_HAND_CLASSES

        sb_push_ev = struct.unpack_from(f"<{NUM_HAND_CLASSES}d", data, cur)
        cur += 8 * NUM_HAND_CLASSES
        bb_call_ev = struct.unpack_from(f"<{NUM_HAND_CLASSES}d", data, cur)
        cur += 8 * NUM_HAND_CLASSES

        sb_fold_ev, bb_fold_ev = struct.unpack_from("<dd", data, cur)
        cur += 16

        sb_ev, expl = struct.unpack_from("<dd", data, cur)
        cur += 16
        (iterations,) = struct.unpack_from("<Q", data, cur)
        cur += 8

        if cur != len(data):
            raise ValueError(f"trailing bytes in 2P strategy file ({len(data) - cur} bytes)")

        result = cls(
            params=GameParams(sb_blind=sb_blind, bb_blind=bb_blind, stack=stack),
            sb_push=sb_push,
            bb_call=bb_call,
            sb_push_ev=sb_push_ev,
            bb_call_ev=bb_call_ev,
            sb_fold_ev=sb_fold_ev,
            bb_fold_ev=bb_fold_ev,
            sb_ev=sb_ev,
            exploitability_bb=expl,
            iterations=iterations,
        )
        validate_legacy(result)
        return result


@dataclass
class Strategy3P:
    params: GameParams
    btn_push: tuple[float, ...]
    sb_call_vs_btn_push: tuple[float, ...]
    sb_push_vs_btn_fold: tuple[float, ...]
    bb_call_3way: tuple[float, ...]
    bb_call_vs_btn_only: tuple[float, ...]
    bb_call_vs_sb_only: tuple[float, ...]

    btn_push_ev: tuple[float, ...]
    sb_call_ev_vs_btn_push: tuple[float, ...]
    sb_push_ev_vs_btn_fold: tuple[float, ...]
    bb_call_ev_3way: tuple[float, ...]
    bb_call_ev_vs_btn_only: tuple[float, ...]
    bb_call_ev_vs_sb_only: tuple[float, ...]

    btn_fold_ev: float
    sb_fold_ev_vs_btn_push: float
    sb_fold_ev_vs_btn_fold: float
    bb_fold_ev: float

    btn_ev: float
    sb_ev: float
    bb_ev: float

    exploitability_bb: float
    iterations: int

    @classmethod
    def load(cls, path: Path) -> Strategy3P:
        data = path.read_bytes()
        cur = 0
        if data[cur : cur + 8] != STRAT3_MAGIC:
            raise ValueError(f"bad magic in 3P strategy file: {path}")
        cur += 8

        version, n = struct.unpack_from("<II", data, cur)
        cur += 8
        if version != STRAT3_VERSION:
            raise ValueError(f"unsupported 3P strategy version: {version}")
        if n != NUM_HAND_CLASSES:
            raise ValueError(f"hand class count mismatch: {n}")

        sb_blind, bb_blind, stack = struct.unpack_from("<ddd", data, cur)
        cur += 24

        def read_arr() -> tuple[float, ...]:
            nonlocal cur
            arr = struct.unpack_from(f"<{NUM_HAND_CLASSES}d", data, cur)
            cur += 8 * NUM_HAND_CLASSES
            return arr

        btn_push = read_arr()
        sb_call_vs_btn_push = read_arr()
        sb_push_vs_btn_fold = read_arr()
        bb_call_3way = read_arr()
        bb_call_vs_btn_only = read_arr()
        bb_call_vs_sb_only = read_arr()

        btn_push_ev = read_arr()
        sb_call_ev_vs_btn_push = read_arr()
        sb_push_ev_vs_btn_fold = read_arr()
        bb_call_ev_3way = read_arr()
        bb_call_ev_vs_btn_only = read_arr()
        bb_call_ev_vs_sb_only = read_arr()

        btn_fold_ev, sb_fold_ev_vs_btn_push, sb_fold_ev_vs_btn_fold, bb_fold_ev = struct.unpack_from(
            "<dddd", data, cur
        )
        cur += 32

        btn_ev, sb_ev, bb_ev = struct.unpack_from("<ddd", data, cur)
        cur += 24

        (expl,) = struct.unpack_from("<d", data, cur)
        cur += 8
        (iterations,) = struct.unpack_from("<Q", data, cur)
        cur += 8

        if cur != len(data):
            raise ValueError(f"trailing bytes in 3P strategy file ({len(data) - cur} bytes)")

        result = cls(
            params=GameParams(sb_blind=sb_blind, bb_blind=bb_blind, stack=stack),
            btn_push=btn_push,
            sb_call_vs_btn_push=sb_call_vs_btn_push,
            sb_push_vs_btn_fold=sb_push_vs_btn_fold,
            bb_call_3way=bb_call_3way,
            bb_call_vs_btn_only=bb_call_vs_btn_only,
            bb_call_vs_sb_only=bb_call_vs_sb_only,
            btn_push_ev=btn_push_ev,
            sb_call_ev_vs_btn_push=sb_call_ev_vs_btn_push,
            sb_push_ev_vs_btn_fold=sb_push_ev_vs_btn_fold,
            bb_call_ev_3way=bb_call_ev_3way,
            bb_call_ev_vs_btn_only=bb_call_ev_vs_btn_only,
            bb_call_ev_vs_sb_only=bb_call_ev_vs_sb_only,
            btn_fold_ev=btn_fold_ev,
            sb_fold_ev_vs_btn_push=sb_fold_ev_vs_btn_push,
            sb_fold_ev_vs_btn_fold=sb_fold_ev_vs_btn_fold,
            bb_fold_ev=bb_fold_ev,
            btn_ev=btn_ev,
            sb_ev=sb_ev,
            bb_ev=bb_ev,
            exploitability_bb=expl,
            iterations=iterations,
        )
        validate_legacy(result)
        return result


def validate_legacy(strategy) -> None:
    frequency_names = {
        "sb_push",
        "bb_call",
        "btn_push",
        "sb_call_vs_btn_push",
        "sb_push_vs_btn_fold",
        "bb_call_3way",
        "bb_call_vs_btn_only",
        "bb_call_vs_sb_only",
    }
    for item in fields(strategy):
        value = getattr(strategy, item.name)
        if isinstance(value, tuple):
            if len(value) != NUM_HAND_CLASSES or not all(math.isfinite(v) for v in value):
                raise ValueError("策略包含损坏的手牌数据")
            if item.name in frequency_names and any(not 0 <= v <= 1 for v in value):
                raise ValueError("策略频率不在 0–1 范围内")
        elif isinstance(value, float) and not math.isfinite(value):
            raise ValueError("策略包含无效数值")


def positions(players: int) -> tuple[str, ...]:
    if players not in (2, 3, 4):
        raise ValueError("人数必须为 2、3 或 4")
    return ("CO", "BTN", "SB", "BB")[-players:]


def decision_layout(players: int) -> list[tuple[int, int]]:
    positions(players)
    return [
        (seat, mask)
        for seat in range(players)
        for mask in range(1 << seat)
        if not (seat == players - 1 and mask == 0)
    ]


@dataclass
class RangeRecord:
    seat: int
    mask: int
    frequency: tuple[float, ...]
    action_ev: tuple[float, ...]
    fold_ev: tuple[float, ...]
    reach: tuple[float, ...] | None = None
    effective_samples: tuple[float, ...] | None = None
    action_ev_std_error: tuple[float, ...] | None = None
    advantage_lower: tuple[float, ...] | None = None
    advantage_upper: tuple[float, ...] | None = None

    def title(self, players: int) -> str:
        seats = positions(players)
        action = "跟注全下" if self.mask else "率先全下"
        history = " / ".join(
            f"{seats[i]}{'全下' if self.mask & (1 << i) else '弃牌'}" for i in range(self.seat)
        )
        return f"{seats[self.seat]} {action}" + (f" · {history}" if history else "")

    def weighted_frequency(self) -> float:
        return sum(v * HandClass.from_index(i).num_combos() for i, v in enumerate(self.frequency)) / 1326


@dataclass
class StrategyDocument:
    players: int
    params: GameParams
    nodes: list[RangeRecord]
    iterations: int
    source: Path
    engine: str
    ev: tuple[float, ...]
    metric: float
    seed: int | None = None
    evaluation_samples: int = 0
    ev_std_error: tuple[float, ...] = ()
    expected_rake: float = 0.0
    training_method: int = 0
    training_sweeps: int = 0
    audit_samples: int = 0
    confidence: float = 0.0
    deviation_upper: float = 0.0
    conditional_ev: tuple[float, ...] = ()
    conditional_ev_std_error: tuple[float, ...] = ()
    content_hash: str = ""

    def label(self) -> str:
        p = self.params
        cap = f"封顶 {p.rake_cap:g} BB" if p.rake_cap else "不封顶"
        drop = "仅摊牌抽水" if p.no_flop_no_drop else "含无人跟注"
        if p.rake_mode == "fixed":
            return f"{self.players}人 · {p.stack:g} BB · 固定抽水 {p.rake_fixed:g} BB · {drop}"
        return f"{self.players}人 · {p.stack:g} BB · 抽水 {p.rake_rate * 100:g}% · {cap} · {drop}"

    def export_csv(self, path: Path) -> None:
        with path.open("w", newline="", encoding="utf-8-sig") as f:
            writer = csv.writer(f)
            writer.writerow(
                (
                    "players",
                    "stack_bb",
                    "sb_blind",
                    "bb_blind",
                    "rake_percent",
                    "rake_cap_bb",
                    "no_flop_no_drop",
                    "position",
                    "prior_mask",
                    "hand",
                    "all_in_frequency",
                    "action_ev_bb",
                    "fold_ev_bb",
                    "history_reach",
                    "effective_samples",
                    "action_ev_std_error",
                    "advantage_lower",
                    "advantage_upper",
                    "simultaneous_confidence",
                    "training_method",
                    "training_samples",
                    "audit_samples",
                    "deviation_upper",
                    "rake_mode",
                    "rake_fixed_bb",
                )
            )
            p = self.params
            for node in self.nodes:
                for i, freq in enumerate(node.frequency):
                    supported = node.effective_samples is None or node.effective_samples[i] > 0
                    writer.writerow(
                        (
                            self.players,
                            p.stack,
                            p.sb_blind,
                            p.bb_blind,
                            p.rake_rate * 100,
                            p.rake_cap,
                            int(p.no_flop_no_drop),
                            positions(self.players)[node.seat],
                            node.mask,
                            HandClass.from_index(i).label(),
                            freq,
                            node.action_ev[i] if supported else "",
                            node.fold_ev[i] if supported else "",
                            node.reach[i] if node.reach is not None else "",
                            node.effective_samples[i] if node.effective_samples is not None else "",
                            node.action_ev_std_error[i] if self.audit_samples and supported else "",
                            node.advantage_lower[i] if self.audit_samples and supported else "",
                            node.advantage_upper[i] if self.audit_samples and supported else "",
                            self.confidence,
                            self.training_method,
                            self.iterations,
                            self.audit_samples,
                            self.deviation_upper,
                            p.rake_mode,
                            p.rake_fixed,
                        )
                    )


def legacy_document(strategy: Strategy2P | Strategy3P, path: Path) -> StrategyDocument:
    def node(seat, mask, frequency, action_ev, fold_ev):
        return RangeRecord(
            seat,
            mask,
            tuple(frequency),
            tuple(action_ev),
            (fold_ev,) * NUM_HAND_CLASSES,
        )

    if isinstance(strategy, Strategy2P):
        nodes = [
            node(0, 0, strategy.sb_push, strategy.sb_push_ev, strategy.sb_fold_ev),
            node(1, 1, strategy.bb_call, strategy.bb_call_ev, strategy.bb_fold_ev),
        ]
        ev = (strategy.sb_ev, -strategy.sb_ev)
        players = 2
    else:
        s = strategy
        nodes = [
            node(0, 0, s.btn_push, s.btn_push_ev, s.btn_fold_ev),
            node(
                1,
                0,
                s.sb_push_vs_btn_fold,
                s.sb_push_ev_vs_btn_fold,
                s.sb_fold_ev_vs_btn_fold,
            ),
            node(
                1,
                1,
                s.sb_call_vs_btn_push,
                s.sb_call_ev_vs_btn_push,
                s.sb_fold_ev_vs_btn_push,
            ),
            node(2, 1, s.bb_call_vs_btn_only, s.bb_call_ev_vs_btn_only, s.bb_fold_ev),
            node(2, 2, s.bb_call_vs_sb_only, s.bb_call_ev_vs_sb_only, s.bb_fold_ev),
            node(2, 3, s.bb_call_3way, s.bb_call_ev_3way, s.bb_fold_ev),
        ]
        ev = (s.btn_ev, s.sb_ev, s.bb_ev)
        players = 3
    return StrategyDocument(
        players,
        strategy.params,
        nodes,
        strategy.iterations,
        path,
        "旧版表格 CFR+",
        ev,
        strategy.exploitability_bb,
    )


def load_strategy(path: Path) -> StrategyDocument:
    if path.stat().st_size > 1_000_000:
        raise ValueError("策略文件过大，可能选择了胜率缓存文件")
    data = path.read_bytes()
    if data[:8] == STRAT2_MAGIC:
        return legacy_document(Strategy2P.load(path), path)
    if data[:8] == STRAT3_MAGIC:
        return legacy_document(Strategy3P.load(path), path)
    if data[:8] != b"AOFMSTR1":
        raise ValueError("无法识别的策略文件")
    cur = 8

    def read(fmt):
        nonlocal cur
        values = struct.unpack_from("<" + fmt, data, cur)
        cur += struct.calcsize("<" + fmt)
        return values

    version, players, hands, count = read("IIII")
    if version not in (1, 2, 3, 4) or hands != NUM_HAND_CLASSES or count != len(decision_layout(players)):
        raise ValueError("不支持的策略版本、人数或决策数量")
    source_hash = hashlib.sha256(data).hexdigest()
    if version >= 3:
        if len(data) < 28 or zlib.crc32(data[:-4]) != struct.unpack_from("<I", data, len(data) - 4)[0]:
            raise ValueError("策略文件校验失败，内容可能损坏")
        data = data[:-4]
    sb, bb, stack, rate, cap = read("5d")
    (flag,) = read("I")
    if flag not in (0, 1):
        raise ValueError("无效的抽水规则标志")
    mode, fixed = read("Id") if version >= 4 else (0, 0.0)
    if mode not in (0, 1):
        raise ValueError("无效的抽水模式")
    params = GameParams(sb, bb, stack, rate, cap, bool(flag), ("percentage", "fixed")[mode], fixed)
    iterations, seed, evaluation_samples = read("QQQ")
    if iterations > 1000010000000 or evaluation_samples > 1000010000000:
        raise ValueError("策略采样数量无效")
    ev = read("4d")
    errors = read("4d")
    expected_rake, metric = read("dd")
    if not all(math.isfinite(v) for v in (*ev, *errors, expected_rake, metric)):
        raise ValueError("策略包含无效统计数据")
    if min(*errors, expected_rake, metric) < 0:
        raise ValueError("策略包含负数误差或抽水")
    method, sweeps, audit_samples, confidence, upper = 0, 0, 0, 0.0, 0.0
    conditional_ev, conditional_errors = (), ()
    if version >= 2:
        method, sweeps, audit_samples, confidence, upper = read("IQQdd")
        conditional_ev, conditional_errors = read("4d"), read("4d")
        if audit_samples > 1000010000000 or sweeps > iterations or (method and not sweeps):
            raise ValueError("策略训练计数无效")
        if (
            method not in (0, 1, 2, 3, 4)
            or not 0 <= confidence < 1
            or upper < 0
            or (audit_samples and not confidence)
        ):
            raise ValueError("策略精度元数据无效")
        if not all(math.isfinite(v) for v in (confidence, upper, *conditional_ev, *conditional_errors)):
            raise ValueError("策略精度元数据包含非有限数值")
        if any(v < 0 for v in conditional_errors):
            raise ValueError("策略精度元数据包含负数误差")
    nodes = []
    for expected in decision_layout(players):
        seat, mask = read("II")
        if (seat, mask) != expected:
            raise ValueError("策略行动历史损坏")
        rows = [read(f"{NUM_HAND_CLASSES}d") for _ in range(8 if version >= 2 else 5)]
        if not all(math.isfinite(v) for row in rows for v in row):
            raise ValueError("策略包含非有限数值")
        if any(not 0 <= v <= 1 for v in rows[0]) or any(not 0 <= v <= 1.000000001 for v in rows[3]):
            raise ValueError("策略包含无效概率")
        if any(not 0 <= v <= max(evaluation_samples, audit_samples) + 1 for v in rows[4]):
            raise ValueError("策略有效样本数量损坏")
        if version >= 2 and (any(v < 0 for v in rows[5]) or any(a > b for a, b in zip(rows[6], rows[7]))):
            raise ValueError("策略误差区间无效")
        nodes.append(RangeRecord(seat, mask, *rows))
    if cur != len(data):
        raise ValueError("策略文件末尾存在多余数据")
    return StrategyDocument(
        players,
        params,
        nodes,
        iterations,
        path,
        ("采样 CFR", "分层 CFR", "分层线性 CFR", "分层 DCFR（实验）", "分层 HS-DCFR（实验）")[method],
        ev[:players],
        metric,
        seed,
        evaluation_samples,
        errors[:players],
        expected_rake,
        method,
        sweeps,
        audit_samples,
        confidence,
        upper,
        conditional_ev[:players],
        conditional_errors[:players],
        source_hash,
    )
