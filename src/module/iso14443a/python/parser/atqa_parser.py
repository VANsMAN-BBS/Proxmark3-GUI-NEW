#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
ATQA (Answer To Request Type A) 解析器
依据 ISO/IEC 14443-3 及 NXP AN10833 规范

ATQA 为 2 字节 (16 bit)，ISO 位编号 b1=LSB .. b16=MSB。
字节1 (先收到) 在大端 16 位值中占高字节 (b16..b9)；
字节2 (后收到) 占低字节 (b8..b1)。

ATQA 位定义 (ISO 14443-3 Table 7.2 / NXP AN10833 Table 4):
  b16..b13 : RFU (保留，应为 0)
  b12..b8  : Bit Frame Anticollision (位帧防冲突，5 个独立位)
  b7       : UID 大小 — 1=Double (7 字节)
  b8       : UID 大小 — 1=Triple (10 字节) [与 BFA 最高位重叠]
  b6..b1   : RFU / 平台特定

UID 大小编码:
  b8=0, b7=0 → Single (4 字节)
  b8=0, b7=1 → Double (7 字节)
  b8=1, b7=0 → Triple (10 字节)

常见值:
  0x0004 : MIFARE Classic 1K (4B UID)
  0x0002 : MIFARE Classic 4K (4B UID)
  0x0044 : MIFARE Ultralight / NTAG (7B UID)
  0x0084 : 10B UID 卡片 (罕见)
"""

import sys
import os

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from ui.language import Language


class ATQAParser:
    """ATQA 解析器 (ISO/IEC 14443-3)"""

    # FSDI/FSDI → 最大帧大小 映射表 (ISO 14443-4 Table 6)
    # 同时用于 RATS 的 FSDI 和 ATS 的 FSCI
    FSD_TABLE = {
        0x0: 16, 0x1: 24, 0x2: 32, 0x3: 40,
        0x4: 48, 0x5: 64, 0x6: 96, 0x7: 128,
        0x8: 256,
        0xF: 256,  # 0xF = 不限制 (>=256)
    }

    @staticmethod
    def get_frame_size(fsi):
        """FSDI/FSCI → 最大帧大小 (bytes)，未知值返回 None"""
        return ATQAParser.FSD_TABLE.get(fsi)

    @staticmethod
    def parse(atqa, extra_info):
        """解析 16 位 ATQA 值"""
        extra_info.append(f"  - {Language.get('atqa_parse')}: 0x{atqa:04X} ({atqa:d})")

        # ---------- 逐位提取 (ISO b1..b16) ----------
        bits = [(atqa >> i) & 0x01 for i in range(16)]  # bits[0]=b1 .. bits[15]=b16

        b16, b15, b14, b13 = bits[15], bits[14], bits[13], bits[12]
        b12, b11, b10, b9, b8 = bits[11], bits[10], bits[9], bits[8], bits[7]
        b7, b6, b5, b4, b3, b2, b1 = bits[6], bits[5], bits[4], bits[3], bits[2], bits[1], bits[0]

        # ---------- 字节分解 ----------
        # 空口字节序: 低字节 (b8..b1) 先发, 高字节 (b16..b9) 后发。
        byte0 = atqa & 0xFF          # 空口先发 (b8..b1)
        byte1 = (atqa >> 8) & 0xFF   # 空口后发 (b16..b9)
        extra_info.append(f"    - {Language.get('atqa_bytes')}: "
                          f"先发=0x{byte0:02X} (b8..b1), 后发=0x{byte1:02X} (b16..b9)")

        # ---------- UID 大小 (b8, b7) ----------
        # b8=1 → Triple(10B), b7=1 → Double(7B), 都0 → Single(4B)
        uid_size_code = (b8 << 1) | b7

        if uid_size_code == 0:
            uid_size_desc = Language.get("uid_size_4bytes")
            uid_size_bytes = 4
        elif uid_size_code == 1:
            uid_size_desc = Language.get("uid_size_7bytes")
            uid_size_bytes = 7
        elif uid_size_code == 2:
            uid_size_desc = Language.get("uid_size_10bytes")
            uid_size_bytes = 10
        else:
            uid_size_desc = Language.get("uid_size_reserved")
            uid_size_bytes = 0

        extra_info.append(f"    - {Language.get('atqa_uid_size_code')}: {uid_size_code} (b8={b8}, b7={b7})")
        extra_info.append(f"      - {Language.get('uid_size_atqa')}: {uid_size_desc}")

        # ---------- Bit Frame Anticollision (b12..b8, 5 bits) ----------
        bfa_code = (b12 << 4) | (b11 << 3) | (b10 << 2) | (b9 << 1) | b8
        extra_info.append(f"    - {Language.get('atqa_bfa_value')}: 0x{bfa_code:02X} ({bfa_code:d})")
        extra_info.append(f"      - {Language.get('atqa_bfa_desc')}: b12..b8 = {b12}{b11}{b10}{b9}{b8}")

        if bfa_code != 0:
            extra_info.append(f"      - {Language.get('bit_frame_anticollision')}: Supported (value=0x{bfa_code:02X})")
        else:
            extra_info.append(f"      - {Language.get('bit_frame_anticollision')}: Not supported / standard Type A")

        # ---------- 逐位详情 ----------
        extra_info.append(f"    - {Language.get('atqa_bit16')}: {b16}")
        extra_info.append(f"    - {Language.get('atqa_bit15')}: {b15}")
        extra_info.append(f"    - {Language.get('atqa_bit14')}: {b14} (RFU)")
        extra_info.append(f"    - {Language.get('atqa_bit13')}: {b13} (RFU)")
        extra_info.append(f"    - {Language.get('atqa_bit12')}: {b12}")
        extra_info.append(f"    - {Language.get('atqa_bit11')}: {b11}")
        extra_info.append(f"    - {Language.get('atqa_bit10')}: {b10}")
        extra_info.append(f"    - {Language.get('atqa_bit9')}: {b9}")
        extra_info.append(f"    - {Language.get('atqa_bit8')}: {b8}")
        extra_info.append(f"    - {Language.get('atqa_bit7')}: {b7}")
        extra_info.append(f"    - {Language.get('atqa_bit6')}: {b6} (RFU)")
        extra_info.append(f"    - {Language.get('atqa_bit5')}: {b5} (RFU)")
        extra_info.append(f"    - {Language.get('atqa_bit4')}: {b4} (RFU)")
        extra_info.append(f"    - {Language.get('atqa_bit3')}: {b3} (RFU)")
        extra_info.append(f"    - {Language.get('atqa_bit2')}: {b2} (RFU)")
        extra_info.append(f"    - {Language.get('atqa_bit1')}: {b1} (RFU)")

        extra_info.append(f"    - {Language.get('atqa_valid')}: Yes")
