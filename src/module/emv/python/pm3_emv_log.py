#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
pm3_emv_log.py — 通过 Proxmark3 `emv reader -v` 读取闪付卡内公开交易日志的解析模块。

来源：根据用户提供的 3 张代码截图（pm3_emv_reader_silent.py）还原并加固。
用途：供 GUI 集成时直接 import，也可当命令行工具用。

设计要点
--------
1. 与 PM3 客户端解耦：只依赖子进程 + 文本输出，不绑定任何 GUI 框架。
2. 流式：边跑边解析，每读到一条记录立即回调，便于 GUI 实时上屏。
3. 可取消：提供 cancel()，GUI 的"停止"按钮可直接调用。
4. 无卡可测：配合 mock_pm3.py 可在没有卡片的情况下跑通全流程。

命令行示例
----------
    py pm3_emv_log.py                        # 自动找端口、找 exe
    py pm3_emv_log.py --port COM14 --csv tx.csv
    py pm3_emv_log.py --mock                 # 用 mock 客户端跑，无需硬件
"""

from __future__ import annotations

import argparse
import csv
import os
import queue
import re
import shutil
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from typing import Callable, Iterable, Optional

# --------------------------------------------------------------------------
# 常量
# --------------------------------------------------------------------------

#: 客户端候选路径（GUI 通过 --exe 传入实际路径；这里只做通用兜底）
PM3_EXE_CANDIDATES = [
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "proxmark3.exe"),
    r"C:\proxmark3\client\proxmark3.exe",
]

#: PM3 USB CDC 的 USB VID/PID（iceman 固件），注册表里可反查 COM 号
PM3_VID_PID = "VID_9AC4&PID_4B8F"

#: `emv reader` 是"直到按 Enter 或按下设备按钮"才停的循环命令
READER_CMD = "emv reader -v"

ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")

# READ RECORD: 00 B2 <rec_no> <p2> 00   (p2 = (SFI<<3)|4，0x5C => SFI=11)
RE_REQ = re.compile(r">>>\s+00\s+B2\s+([0-9A-Fa-f]{2})\s+5C\s+00", re.I)
RE_HEX2 = re.compile(r"^[0-9A-Fa-f]{2}$")

#: 记录最小长度（含 2 字节状态字 90 00）
MIN_RECORD_LEN = 47

# 原始截图里的固定字段偏移（单位：字节，基于去掉 SW 后的记录体）
OFF_DATE = slice(0, 3)     # YYMMDD (BCD)
OFF_TIME = slice(3, 6)     # HHMMSS (BCD)
OFF_AMOUNT = slice(6, 12)  # 金额，BCD，最小货币单位（分）
OFF_MERCHANT = slice(22, 42)  # 9F4E 商户名/位置，ASCII 20 字节

WIN = os.name == "nt"
CREATE_NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0) if WIN else 0


# --------------------------------------------------------------------------
# 数据结构
# --------------------------------------------------------------------------

@dataclass
class Txn:
    """一条交易记录。"""

    date: str          # "2026-06-15"
    time: str          # "09:32:41"
    merchant: str      # "PSBC-ATM"，空则 "-"
    amount: str        # "500.00"（字符串，保留两位，便于直接上屏）
    record_no: int = 0  # 卡片上的记录号（APDU P1）
    raw: str = ""       # 原始 hex，排障用

    @property
    def amount_float(self) -> float:
        try:
            return float(self.amount)
        except ValueError:
            return 0.0

    def as_row(self) -> tuple:
        return (self.date, self.time, self.merchant, self.amount)

    def as_dict(self) -> dict:
        d = {
            "date": self.date,
            "time": self.time,
            "merchant": self.merchant,
            "amount": self.amount,
        }
        if self.record_no:
            d["record_no"] = self.record_no
        if self.raw:
            d["raw"] = self.raw
        return d


@dataclass
class ReadResult:
    """一次读取的完整结果，GUI 可用它区分"空结果"和"失败"。"""

    txns: list = field(default_factory=list)
    card_detected: bool = False
    stop_reason: str = ""      # done / timeout / cancelled / no_card / error
    returncode: Optional[int] = None
    raw_text: str = ""
    warnings: list = field(default_factory=list)


# --------------------------------------------------------------------------
# 端口 / 客户端自动发现
# --------------------------------------------------------------------------

def find_pm3_ports() -> list:
    """从注册表枚举 PM3 的 COM 口，按插入顺序返回，如 ['COM14']。"""
    if not WIN:
        return []
    try:
        import winreg
    except ImportError:  # pragma: no cover
        return []

    ports = []
    base = r"SYSTEM\CurrentControlSet\Enum\USB" + "\\" + PM3_VID_PID
    try:
        key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, base)
    except OSError:
        return []

    try:
        i = 0
        while True:
            try:
                sub = winreg.EnumKey(key, i)
            except OSError:
                break
            i += 1
            try:
                dk = winreg.OpenKey(key, sub + r"\Device Parameters")
            except OSError:
                continue
            try:
                val, _ = winreg.QueryValueEx(dk, "PortName")
            except OSError:
                continue
            finally:
                dk.Close()
            if val and val not in ports:
                ports.append(val)
    finally:
        key.Close()
    return ports


def find_pm3_exe() -> Optional[str]:
    """按环境变量 / 已知路径 / PATH 顺序查找 proxmark3 客户端。"""
    env = os.environ.get("PM3_EXE")
    if env and os.path.isfile(env):
        return env
    for p in PM3_EXE_CANDIDATES:
        if os.path.isfile(p):
            return p
    return shutil.which("proxmark3")


# --------------------------------------------------------------------------
# 文本解析
# --------------------------------------------------------------------------

def strip_ansi(text: str) -> str:
    return ANSI_RE.sub("", text)


def hex_after_chevrons(line: str) -> Optional[bytes]:
    """
    取 `<<<<` 之后的十六进制字节。

    兼容客户端可能出现的三种排版：
        [+] <<<< 70 2d ... 90 00
        [+] <<<< 70 2d ... | 90 00
        [+] <<<< 70 2d ... - Success
    """
    i = line.find("<<<<")
    start = i + 4 if i >= 0 else None
    if start is None:
        i = line.find("<<<")
        if i < 0:
            return None
        start = i + 3

    out = []
    seen_bar = False
    for tok in line[start:].split():
        if tok.startswith("|"):
            seen_bar = True
            tok = tok.lstrip("|")
            if not tok:
                continue
        if RE_HEX2.match(tok):
            out.append(tok)
        elif re.fullmatch(r"[0-9A-Fa-f]+", tok) and len(tok) % 2 == 0:
            # 容错：客户端若把字节连写（如 4320），按 2 字符切开
            out.extend(tok[i:i + 2] for i in range(0, len(tok), 2))
        elif seen_bar:
            break          # 状态字之后是描述文本，收工
        elif out:
            break          # 数据之后遇到非 hex token（描述文字），收工
        else:
            return None    # 还没读到任何数据就出现非 hex，放弃
    if not out:
        return None
    try:
        return bytes.fromhex("".join(out))
    except ValueError:
        return None


def _bcd(data: bytes) -> Optional[str]:
    """把 BCD 字节转成十进制数字串；含非法半字节则返回 None。"""
    s = data.hex()
    if not s.isdigit():
        return None
    return s


def _plausible_datetime(yymmdd: str, hhmmss: str) -> bool:
    def n(s):
        return int(s)

    if len(yymmdd) != 6 or len(hhmmss) != 6:
        return False
    mo, dd = n(yymmdd[2:4]), n(yymmdd[4:6])
    hh, mi, ss = n(hhmmss[0:2]), n(hhmmss[2:4]), n(hhmmss[4:6])
    return 1 <= mo <= 12 and 1 <= dd <= 31 and hh < 24 and mi < 60 and ss < 60


def decode_record_fixed(rec: bytes, record_no: int = 0) -> tuple:
    """
    按截图里的固定布局解析记录体（不含 SW）。

    返回 (Txn | None, warning | None)
    """
    if len(rec) < OFF_MERCHANT.stop:
        return None, "记录长度 %d 不足 42 字节，无法按固定布局解析" % len(rec)

    d = _bcd(rec[OFF_DATE])
    t = _bcd(rec[OFF_TIME])
    amt = _bcd(rec[OFF_AMOUNT])

    if d is None or t is None or amt is None or not _plausible_datetime(d, t):
        return None, (
            "记录不符合「日期/时间/金额 BCD」固定布局（可能是另一张卡的 9F4F 日志格式）: "
            + rec.hex()
        )

    merchant = "".join(chr(c) for c in rec[OFF_MERCHANT] if 0x20 <= c <= 0x7E).strip()
    date = "%04d-%s-%s" % (2000 + int(d[0:2]), d[2:4], d[4:6])
    tm = "%s:%s:%s" % (t[0:2], t[2:4], t[4:6])
    return (
        Txn(
            date=date,
            time=tm,
            merchant=merchant or "-",
            amount="%.2f" % (int(amt) / 100.0),
            record_no=record_no,
            raw=rec.hex(),
        ),
        None,
    )


def parse_log_format(log_format: bytes) -> list:
    """解析 9F4F(Log Format) 里的 (tag, len) 定义序列。"""
    fields = []
    i = 0
    while i < len(log_format):
        b = log_format[i]
        if b in (0x9F, 0x5F, 0xBF, 0xDF) and i + 1 < len(log_format):
            tag = log_format[i:i + 2].hex().upper()
            i += 2
        else:
            tag = log_format[i:i + 1].hex().upper()
            i += 1
        if i >= len(log_format):
            break
        ln = log_format[i]
        i += 1
        fields.append((tag, ln))
    return fields


def decode_record_9f4f(rec: bytes, log_format: bytes, record_no: int = 0) -> tuple:
    """
    按卡片自带的 9F4F 日志格式动态解析记录（可选升级路径，需真卡验证）。

    典型 9F4F: 9A 03 9F21 03 9F02 06 ... 9F4E 14
    返回 (Txn | None, warning | None)
    """
    fields = parse_log_format(log_format)
    if not fields:
        return None, "9F4F 为空或无法解析"

    pos = 0
    got = {}
    for tag, ln in fields:
        if pos + ln > len(rec):
            break
        got[tag] = rec[pos:pos + ln]
        pos += ln

    d = _bcd(got.get("9A", b""))
    t = _bcd(got.get("9F21", b""))
    amt = _bcd(got.get("9F02", b""))
    if d is None or t is None or amt is None or not _plausible_datetime(d, t):
        return None, "9F4F 动态解析失败: " + rec.hex()

    raw_merchant = got.get("9F4E", b"")
    merchant = "".join(chr(c) for c in raw_merchant if 0x20 <= c <= 0x7E).strip()
    return (
        Txn(
            date="%04d-%s-%s" % (2000 + int(d[0:2]), d[2:4], d[4:6]),
            time="%s:%s:%s" % (t[0:2], t[2:4], t[4:6]),
            merchant=merchant or "-",
            amount="%.2f" % (int(amt) / 100.0),
            record_no=record_no,
            raw=rec.hex(),
        ),
        None,
    )


class TxnParser:
    """
    流式解析器：喂入客户端的每一行输出，吐出交易记录。

    状态机：
        空闲 --(看到 READ RECORD 请求)--> 等待响应
        等待响应 --(看到 <<<< 数据)--> 校验 SW/长度 --> 出记录 --> 空闲
    兼容响应数据与状态字分两行打印的情况（先暂存，等下一行状态）。
    """

    def __init__(self, log_format: Optional[bytes] = None):
        self.log_format = log_format
        self.records: list = []
        self.warnings: list = []
        self.card_detected = False
        self._pending = False        # 已发出 READ RECORD 请求，等响应
        self._record_no = 0
        self._cand = None            # 缺 SW 的候选数据

    # -- 内部 ---------------------------------------------------------
    def _emit(self, data: bytes, record_no: int) -> Optional[Txn]:
        rec = data[:-2]
        if self.log_format:
            txn, warn = decode_record_9f4f(rec, self.log_format, record_no)
        else:
            txn, warn = decode_record_fixed(rec, record_no)
        if warn:
            self.warnings.append(warn)
            return None
        self.records.append(txn)
        return txn

    def _resolve_candidate(self, sw_ok: bool) -> Optional[Txn]:
        cand, self._cand = self._cand, None
        if cand is None:
            return None
        if sw_ok:
            return self._emit(cand, self._record_no)
        self.warnings.append("丢弃无 90 00 状态字的响应: " + cand.hex())
        return None

    # -- 主入口 -------------------------------------------------------
    def feed(self, raw_line: str) -> Optional[Txn]:
        """喂一行原始输出，若本行产出一条记录则返回它。"""
        line = strip_ansi(raw_line.rstrip("\r\n"))

        # 1) 先看这一行是否是上一条候选数据的"状态字行"
        if self._cand is not None:
            if re.search(r"status:\s*90\s*00\b", line, re.I) or re.search(r"\b90\s*00\b", line):
                txn = self._resolve_candidate(True)
                if txn:
                    return txn
            elif RE_REQ.search(line) or ">>>>" in line:
                self._resolve_candidate(False)

        # 2) READ RECORD 请求？
        m = RE_REQ.search(line)
        if m:
            self._pending = True
            self._record_no = int(m.group(1), 16)
            return None

        # 3) 响应？
        if not self._pending:
            return None
        data = hex_after_chevrons(line)
        if data is None:
            if "No EMV application" in line or "Card in field" in line:
                self._pending = False
            return None

        self._pending = False
        if len(data) >= 3 and data[-2:] == b"\x90\x00":
            self.card_detected = True
            if len(data) < MIN_RECORD_LEN:
                self.warnings.append(
                    "记录长度 %d 小于预期 %d，已跳过: %s" % (len(data), MIN_RECORD_LEN, data.hex())
                )
                return None
            return self._emit(data, self._record_no)

        if len(data) >= 43:
            # 数据行没带 SW，等下一行状态字再决定
            self._cand = data
            return None
        return None

    def feed_all(self, text: str) -> list:
        for ln in text.splitlines():
            self.feed(ln)
        if self._cand is not None:
            self._resolve_candidate(False)
        return self.records


def extract_txns(text: str, log_format: Optional[bytes] = None) -> list:
    """离线解析一段完整输出（截图版本的等价函数，便于回放日志/单测）。"""
    return TxnParser(log_format).feed_all(text)


# --------------------------------------------------------------------------
# 运行 PM3 客户端（流式、可取消）
# --------------------------------------------------------------------------

class EmvLogReader:
    """
    GUI 集成入口。

    用法（工作线程里）：
        reader = EmvLogReader()                 # 自动找 exe 和端口
        res = reader.read(on_record=..., on_status=...)
        # 用户在 UI 点"停止" → reader.cancel()
    """

    def __init__(
        self,
        exe: Optional[str] = None,
        port: Optional[str] = None,
        *,
        timeout: float = 45.0,        # 单次读取总超时（秒）
        idle_after_card: float = 3.0,  # 有卡时静默多久判定读完
        no_card_timeout: float = 12.0,  # 无卡时多久判定失败（实测约 6.5s 自动退出）
        log_format: Optional[bytes] = None,
    ):
        self.exe = exe or find_pm3_exe()
        self.port = port or (find_pm3_ports() or [None])[0]
        self.timeout = timeout
        self.idle_after_card = idle_after_card
        self.no_card_timeout = no_card_timeout
        self.log_format = log_format
        self._cancel = threading.Event()
        self._proc = None
        self._lock = threading.Lock()

    # -- 控制 ---------------------------------------------------------
    def cancel(self) -> None:
        """GUI 停止按钮调用；线程安全。"""
        self._cancel.set()
        with self._lock:
            proc = self._proc
        if proc and proc.poll() is None:
            try:
                proc.terminate()
            except OSError:
                pass

    # -- 主流程 -------------------------------------------------------
    def read(
        self,
        on_record: Optional[Callable[[Txn], None]] = None,
        on_status: Optional[Callable[[str], None]] = None,
    ) -> ReadResult:
        res = ReadResult()

        def status(msg):
            if on_status:
                on_status(msg)

        if not self.exe:
            res.stop_reason = "error"
            res.warnings.append("找不到 proxmark3.exe（可用 PM3_EXE 环境变量指定）")
            status(res.warnings[-1])
            return res

        if not self.port:
            res.stop_reason = "error"
            res.warnings.append("未发现 Proxmark3 串口（VID_9AC4&PID_4B8F）")
            status(res.warnings[-1])
            return res

        status("使用 %s @ %s，正在读卡…" % (os.path.basename(self.exe), self.port))
        cmd = [self.exe, "-p", str(self.port), "-c", READER_CMD]
        # 客户端会把会话日志写到 CWD/.proxmark3，固定到 exe 所在目录，避免污染 GUI 工作目录
        workdir = os.path.dirname(os.path.abspath(self.exe)) or None
        try:
            proc = subprocess.Popen(
                cmd,
                stdin=subprocess.PIPE,     # 保持打开：命令是"直到 Enter 才停"的循环
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,  # 客户端有部分提示走 stderr，必须合并
                bufsize=0,
                cwd=workdir,
                creationflags=CREATE_NO_WINDOW,
            )
        except OSError as e:
            res.stop_reason = "error"
            res.warnings.append("启动客户端失败: %s" % e)
            status(res.warnings[-1])
            return res

        with self._lock:
            self._proc = proc

        parser = TxnParser(self.log_format)
        lines: "queue.Queue[Optional[bytes]]" = queue.Queue()
        buf = []

        def pump():  # 读线程：阻塞读，行入队
            try:
                while True:
                    try:
                        raw = proc.stdout.readline()
                    except (ValueError, OSError):
                        break
                    if not raw:
                        break
                    lines.put(raw)
            finally:
                lines.put(None)

        th = threading.Thread(target=pump, name="pm3-out", daemon=True)
        th.start()

        t0 = time.time()
        last_activity = t0
        timed_out = False
        hard_timeout = self.timeout

        try:
            while True:
                if self._cancel.is_set():
                    res.stop_reason = "cancelled"
                    break

                now = time.time()
                elapsed = now - t0
                if elapsed > hard_timeout:
                    res.stop_reason = "timeout"
                    timed_out = True
                    break

                if parser.card_detected:
                    # 有卡：静默一段时间即判定读完（卡片读完后客户端只在等 Enter）
                    if (now - last_activity) > self.idle_after_card and elapsed > 1.0:
                        res.stop_reason = "done"
                        break
                elif elapsed > self.no_card_timeout:
                    res.stop_reason = "no_card"
                    break

                try:
                    raw = lines.get(timeout=0.2)
                except queue.Empty:
                    continue

                if raw is None:          # 客户端自己退出了
                    res.stop_reason = res.stop_reason or (
                        "done" if parser.card_detected else "no_card"
                    )
                    break

                last_activity = time.time()
                text = raw.decode("utf-8", "replace")
                buf.append(text)
                for ln in text.rstrip("\r\n").splitlines():
                    txn = parser.feed(ln)
                    if txn and on_record:
                        on_record(txn)
        finally:
            # 先按 Enter 让客户端优雅退出，再兜底 kill，确保端口释放
            try:
                if proc.poll() is None:
                    try:
                        proc.stdin.write(b"\n")
                        proc.stdin.flush()
                    except (OSError, ValueError):
                        pass
                    try:
                        proc.wait(timeout=1.5)
                    except subprocess.TimeoutExpired:
                        proc.terminate()
                        try:
                            proc.wait(timeout=2.0)
                        except subprocess.TimeoutExpired:
                            proc.kill()
                            proc.wait(timeout=2.0)
            except OSError:
                pass
            try:
                th.join(timeout=2.0)
            except RuntimeError:
                pass
            for stream in (proc.stdin, proc.stdout):
                try:
                    if stream:
                        stream.close()
                except OSError:
                    pass
            with self._lock:
                self._proc = None

        res.txns = parser.records
        res.warnings = parser.warnings
        res.card_detected = parser.card_detected
        res.returncode = proc.returncode
        res.raw_text = "".join(buf)

        if res.stop_reason == "no_card" and not res.warnings:
            res.warnings.append("未检测到卡片/EMV 应用（请把卡贴在 PM3 天线上）")
        elif res.stop_reason == "done" and not res.txns:
            res.warnings.append("卡片已识别，但未解析到交易记录（可能是无日志卡或 9F4F 布局不同）")
        elif timed_out:
            res.warnings.append("读取超时（%.0fs），返回已解析到的数据" % hard_timeout)

        status("停止原因: %s，记录 %d 条" % (res.stop_reason, len(res.txns)))
        return res


def read_transactions(
    exe: Optional[str] = None,
    port: Optional[str] = None,
    **kw,
) -> ReadResult:
    """最简调用：一次读完，返回 ReadResult（同步阻塞，调用方负责放线程里）。"""
    reader = EmvLogReader(exe=exe, port=port, **{k: v for k, v in kw.items() if k in (
        "timeout", "idle_after_card", "no_card_timeout", "log_format")})
    return reader.read()


# --------------------------------------------------------------------------
# 输出（原截图版的三种打印方式，保留给命令行用）
# --------------------------------------------------------------------------

def print_plain(txns: Iterable[Txn]) -> None:
    txns = list(txns)
    print("%-12s%-10s%-14s%10s" % ("日期", "时间", "交易方式", "金额"))
    print("-" * 46)
    for t in txns:
        print("%-12s%-10s%-14s%10s" % (t.date, t.time, t.merchant, t.amount))


def print_rich(txns: Iterable[Txn]) -> bool:
    try:
        from rich.console import Console
        from rich.table import Table
    except ImportError:
        return False
    table = Table(title="交易记录", show_lines=False)
    table.add_column("日期", style="cyan")
    table.add_column("时间")
    table.add_column("交易方式", style="magenta")
    table.add_column("金额", justify="right", style="green")
    for t in txns:
        table.add_row(t.date, t.time, t.merchant, t.amount)
    Console().print(table)
    return True


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Proxmark3 闪付卡交易日志读取")
    ap.add_argument("--exe", default=None, help="proxmark3.exe 路径（默认自动查找）")
    ap.add_argument("--port", default=None, help="串口，如 COM14（默认按 VID/PID 自动识别）")
    ap.add_argument("--csv", help="导出 CSV")
    ap.add_argument("--json", help="导出 JSON")
    ap.add_argument("--timeout", type=float, default=45.0)
    ap.add_argument("--mock", action="store_true", help="用 mock_pm3 假客户端跑（无需硬件）")
    args = ap.parse_args(argv)

    exe = args.exe
    if args.mock:
        here = os.path.dirname(os.path.abspath(__file__))
        exe = os.path.join(here, "mock_pm3.bat")
        port = args.port or "COM14"

    reader = EmvLogReader(exe=exe, port=args.port, timeout=args.timeout)
    print("[*] 读取卡片 (port=%s) …" % (reader.port,), flush=True)
    res = reader.read()

    if not res.txns:
        print("[!] 未找到交易记录 (%s)" % res.stop_reason)
        for w in res.warnings:
            print("    " + w)
        return 2

    if not print_rich(res.txns):
        print_plain(res.txns)

    if args.csv:
        with open(args.csv, "w", newline="", encoding="utf-8-sig") as f:
            w = csv.writer(f)
            w.writerow(["date", "time", "merchant", "amount"])
            w.writerows(t.as_row() for t in res.txns)
        print("\n[*] 已导出: %s" % args.csv)

    if args.json:
        import json
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump([t.as_dict() for t in res.txns], f, ensure_ascii=False, indent=2)
        print("[*] 已导出: %s" % args.json)

    return 0


if __name__ == "__main__":
    sys.exit(main())
