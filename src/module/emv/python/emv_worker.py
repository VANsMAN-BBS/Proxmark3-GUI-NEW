#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
emv_worker.py — Qt GUI 与 pm3_emv_log.EmvLogReader 之间的 JSON-lines 桥。

GUI 以子进程方式运行本文件：

    python -W ignore -u emv_worker.py --port COM14 [--exe proxmark3.exe]
    python -W ignore -u emv_worker.py --mock

stdout 每行一个 JSON 对象（UTF-8）：
    {"event": "started", "port": "COM14", "exe": "..."}
    {"event": "record", "txn": {"date": ..., "time": ..., "merchant": ...,
                                "amount": "12.50", "record_no": 1, "raw": "..."}}
    {"event": "status", "message": "..."}
    {"event": "result", "stop_reason": "done", "card_detected": true,
     "returncode": 0, "count": 4, "warnings": ["..."]}
    {"event": "fatal", "message": "..."}

stdin：单独一行 `cancel` 会调用 EmvLogReader.cancel()（线程安全）。
stderr：只用于诊断信息，GUI 会收集但不会当作交易记录。

这里所有记录都来自 pm3_emv_log.py 的解析器；本文件不做 APDU 解析，
也不改写任何记录字段。--mock 时使用同目录的 mock_pm3.py（合成数据），
不会访问真实硬件。
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import pm3_emv_log as core  # noqa: E402


def emit(obj: dict) -> None:
    sys.stdout.write(json.dumps(obj, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def log(message: str) -> None:
    try:
        sys.stderr.write(message + "\n")
        sys.stderr.flush()
    except Exception:
        pass


def ensure_mock_shim() -> str:
    """生成调用 mock_pm3.py 的启动器，返回其路径。

    EmvLogReader 只接受一个可执行文件路径；Windows 上不能直接执行 .py，
    所以这里生成一个同目录的小启动器。它不是 emv_log_reader 里的
    mock_pm3.bat，也不包含任何本地路径（使用运行时的 sys.executable）。
    """
    script = os.path.join(HERE, "mock_pm3.py")
    if os.name == "nt":
        shim = os.path.join(HERE, "mock_pm3_runtime.bat")
        content = '@echo off\r\n"%s" "%s" %%*\r\n' % (sys.executable, script)
    else:
        shim = os.path.join(HERE, "mock_pm3_runtime.sh")
        content = '#!/bin/sh\nexec "%s" "%s" "$@"\n' % (sys.executable, script)
    try:
        old = None
        if os.path.isfile(shim):
            with open(shim, "r", encoding="utf-8", newline="") as f:
                old = f.read()
        if old != content:
            with open(shim, "w", encoding="utf-8", newline="") as f:
                f.write(content)
            if os.name != "nt":
                os.chmod(shim, 0o755)
    except OSError as e:
        log("cannot write mock launcher: %s" % e)
        return script
    return shim


def watch_stdin(reader: core.EmvLogReader) -> None:
    """后台读取 stdin；收到 cancel 立即取消阻塞中的 read()。"""
    try:
        for line in sys.stdin:
            if line.strip().lower() in ("cancel", "stop"):
                log("cancel requested")
                reader.cancel()
                return
    except (OSError, ValueError):
        pass


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="PM3 EMV transaction log bridge")
    ap.add_argument("--exe", default=None)
    ap.add_argument("--port", default=None)
    ap.add_argument("--timeout", type=float, default=45.0)
    ap.add_argument("--idle-after-card", type=float, default=3.0)
    ap.add_argument("--no-card-timeout", type=float, default=12.0)
    ap.add_argument("--mock", action="store_true",
                    help="use the bundled mock client (synthetic records)")
    args = ap.parse_args(argv)

    exe = args.exe
    if args.mock:
        exe = ensure_mock_shim()
        port = args.port or "COM14"
    else:
        port = args.port

    try:
        reader = core.EmvLogReader(
            exe=exe,
            port=port,
            timeout=args.timeout,
            idle_after_card=args.idle_after_card,
            no_card_timeout=args.no_card_timeout,
        )
    except Exception as e:  # noqa: BLE001 - report instead of crashing
        emit({"event": "fatal", "message": "reader init failed: %s" % e})
        return 1

    emit({"event": "started",
          "port": reader.port or "",
          "exe": reader.exe or "",
          "mock": bool(args.mock)})

    threading.Thread(target=watch_stdin, args=(reader,),
                     name="emv-stdin", daemon=True).start()

    def on_record(txn) -> None:
        emit({"event": "record", "txn": txn.as_dict()})

    def on_status(message: str) -> None:
        # pm3_emv_log 的状态文案是中文；GUI 自己本地化最终状态，
        # 这里只把它留在 stderr 供排障。
        log("[pm3_emv_log] %s" % message)

    try:
        res = reader.read(on_record=on_record, on_status=on_status)
    except Exception as e:  # noqa: BLE001
        emit({"event": "fatal", "message": "reader failed: %s" % e})
        return 1

    emit({
        "event": "result",
        "stop_reason": res.stop_reason,
        "card_detected": bool(res.card_detected),
        "returncode": res.returncode,
        "count": len(res.txns),
        "warnings": list(res.warnings),
    })
    return 0


if __name__ == "__main__":
    sys.exit(main())
