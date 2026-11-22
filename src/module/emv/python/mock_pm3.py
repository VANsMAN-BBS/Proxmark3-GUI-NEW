#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
mock_pm3.py — 假的 proxmark3 客户端，用来在没有卡片（甚至没有 PM3）时调通 GUI 全流程。

它接受和真客户端一样的参数：
    python mock_pm3.py -p COM14 -c "emv reader -v"

输出格式与 Iceman v4.23346 的 `emv reader -v` 实测输出一致：
    [+] >>>> 00 B2 01 5C 00
    [+] <<<< <记录hex> | 90 00

环境变量：
    MOCK_PM3_NO_CARD=1     → 模拟"没有卡片"，只打印失败分支
    MOCK_PM3_SLOW=1        → 每行延迟 0.15s，用来测试 GUI 的忙碌/取消状态
    MOCK_PM3_BAD_LAYOUT=1  → 输出一条"长度正确但日期/金额非 BCD"的记录，
                             用来验证布局不符告警（不会产生交易行）
    MOCK_PM3_STDERR=1      → 额外向 stderr 写探针行，验证客户端 stderr 被合并

本文件是 Qt GUI 的离线测试夹具，复制自 emv_log_reader/mock_pm3.py 并加了
上面三个仅用于自动化测试的开关。它不包含任何真实卡片或银行数据。
"""

import os
import sys
import time

Y = "\x1b[33m"
G = "\x1b[32m"
R = "\x1b[31m"
N = "\x1b[0m"

# (YYMMDD, HHMMSS, 金额（分）, 商户名)
CARDS = [
    (b"\x26\x06\x15", b"\x09\x32\x41", 50000, "PSBC-ATM"),
    (b"\x26\x06\x14", b"\x18\x02\x10", 2350, "12345678"),
    (b"\x26\x06\x12", b"\x07\x45\x03", 100000, "PSBC-ATM"),
    (b"\x26\x06\x10", b"\x12\x00\x59", 1580, "ATM"),
]


def build_record(d, t, cents, merchant):
    """构造 45 字节记录体：日期3 + 时间3 + 金额6(BCD) + 保留10 + 商户20 + 尾3"""
    amt = ("%012d" % cents)
    body = d + t + bytes.fromhex(amt)
    body += b"\x00" * 10
    body += merchant.encode("ascii").ljust(20, b" ")  # 9F4E：20 字节
    body += b"\x00" * 3
    return body


def build_bad_layout_record():
    """45 字节、SW 正确，但日期/时间/金额都不是 BCD → 解析器应告警并跳过。"""
    body = b"\xff\xff\xff" + b"\x99\x99\x99" + b"\xfa\xfb\xfc\xfd\xfe\xff"
    body += b"\x00" * 10
    body += b"MOCK-BAD-LAYOUT".ljust(20, b" ")
    body += b"\x00" * 3
    return body


def hx(b):
    return " ".join("%02x" % c for c in b)


def out(line, color="", slow=False):
    sys.stdout.write("%s%s%s\n" % (color, line, N if color else ""))
    sys.stdout.flush()
    if slow:
        time.sleep(0.15)


def err_out(line, slow=False):
    """写 stderr；客户端里部分输出确实走 stderr，读取端必须合并。"""
    sys.stderr.write(line + "\n")
    sys.stderr.flush()
    if slow:
        time.sleep(0.15)


def main():
    argv = sys.argv[1:]
    cmd = ""
    port = "COM14"
    for i, a in enumerate(argv):
        if a == "-c" and i + 1 < len(argv):
            cmd = argv[i + 1]
        if a == "-p" and i + 1 < len(argv):
            port = argv[i + 1]

    slow = os.environ.get("MOCK_PM3_SLOW") == "1"
    no_card = os.environ.get("MOCK_PM3_NO_CARD") == "1"
    bad_layout = os.environ.get("MOCK_PM3_BAD_LAYOUT") == "1"
    stderr_probe = os.environ.get("MOCK_PM3_STDERR") == "1"

    if stderr_probe:
        # 客户端有一部分提示走 stderr；GUI 侧必须是 stderr=STDOUT 才能看到。
        sys.stderr.write("[+] mock-stderr-probe: status quiet\n")
        sys.stderr.flush()

    out("[+] Using UART port %s" % port, slow=slow)
    out("[+] Communicating with PM3 over USB-CDC", slow=slow)
    out("[+] Max frame size: 624 bytes", slow=slow)
    out("[usb|script] pm3 --> %s" % cmd, slow=slow)

    if "hw version" in cmd:
        out(" [ Proxmark3 ]", slow=slow)
        out("  Iceman/master/v4.23346-suspect (mock)", slow=slow)
        return 0

    if no_card:
        out("[+] Setting ISODEP -> inactive", Y, slow)
        out("[+] >>>> 00 A4 04 00 0E 32 50 41 59 2E 53 59 53 2E 44 44 46 30 31 00 ", slow=slow)
        out("[!] No ISO14443-A Card in field", R, slow)
        out("[=] Testing ISO14443-B... ( fail )", slow=slow)
        out("[-] No EMV application found (no PSE, no PPSE, no known AID)", R, slow)
        return 1

    out("[+] Setting ISODEP -> inactive", Y, slow)
    out("[+] >>>> 00 A4 04 00 0E 32 50 41 59 2E 53 59 53 2E 44 44 46 30 31 00 ", slow=slow)
    out("[+] <<<< 6f 23 84 0e 32 50 41 59 2e 53 59 53 2e 44 44 46 30 31 "
        "a5 11 bf 0c 0e 61 0c 4f 07 a0 00 00 00 03 05 07 87 01 01 | 90 00", slow=slow)
    out("[+] >>>> 00 A4 04 00 09 A0 00 00 00 03 05 07 60 10 00 ", slow=slow)
    out("[+] <<<< 6f 1c 84 09 a0 00 00 00 03 05 07 60 a5 0f 9f 38 0c 9f 33 03 "
        "9f 35 01 04 9f 40 02 | 90 00", slow=slow)
    out("[+] >>>> 80 A8 00 00 02 83 00 00 ", slow=slow)
    out("[+] <<<< 77 0a 82 02 19 80 94 04 08 01 01 00 | 90 00", slow=slow)
    out("[+] >>>> 00 B2 01 0C 00 ", slow=slow)          # 9F4F 日志格式所在记录
    out("[+] <<<< 9f 4f 0a 9a 03 9f 21 03 9f 02 06 00 00 | 90 00", slow=slow)

    if bad_layout:
        rec = build_bad_layout_record()
        out("[+] >>>> 00 B2 01 5c 00 ", slow=slow)
        out("[+] <<<< %s | 90 00" % hx(rec), slow=slow)
        out("[+] Setting ISODEP -> inactive", Y, slow=slow)
        return 0

    for idx, (d, t, cents, merchant) in enumerate(CARDS, start=1):
        rec = build_record(d, t, cents, merchant)
        req = "[+] >>>> 00 B2 %02x 5c 00 " % idx     # READ RECORD, SFI=11
        resp = "[+] <<<< %s | 90 00" % hx(rec)
        if stderr_probe and idx == 2:
            # 第 2 条完整记录走 stderr：只有 stderr=STDOUT 才能读全 4 条。
            err_out(req, slow=slow)
            err_out(resp, slow=slow)
        else:
            out(req, slow=slow)
            out(resp, G, slow)

    out("[+] Setting ISODEP -> inactive", Y, slow)
    return 0


if __name__ == "__main__":
    sys.exit(main())
