#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
SAK (Select Acknowledge) 解析器
依据 ISO/IEC 14443-3 及 NXP AN10833 规范

SAK 为 1 字节，ISO 位编号 b1=LSB .. b8=MSB。

SAK 位定义 (ISO 14443-3 / NXP AN10833):
  b1 (0x01) : RFU
  b2 (0x02) : RFU (NXP: 保留，为1时改变其他位含义)
  b3 (0x04) : Cascade bit — 0=UID完整, 1=UID不完整(需继续下一级联)
  b4 (0x08) : MIFARE Classic / CRYPTO1 支持
  b5 (0x10) : (平台特定)
  b6 (0x20) : ISO 14443-4 (T=CL) 协议支持
  b7 (0x40) : (平台特定)
  b8 (0x80) : (平台特定)

常见 SAK 值 (NXP AN10833 表):
  0x00 : MIFARE Ultralight / NTAG
  0x01 : MIFARE DESFire (obsolete, 老式)
  0x08 : MIFARE Classic 1K (M1 1K)
  0x09 : MIFARE Mini (M1 320b)
  0x10 : MIFARE Plus SL1 (SL1)
  0x11 : MIFARE Plus SL1 (4K)
  0x18 : MIFARE Classic 4K (M1 4K)
  0x20 : ISO 14443-4 CPU卡 (FM1208 / DESFire / CPU卡)
  0x28 : 兼容M1的CPU卡 (FM1208 / Mifare Plus SL3)
  0x38 : MIFARE DESFire EV1 / EV2 / EV3
  0x40 : MIFARE Plus SL3
  0x44 : MIFARE Ultralight EV1 / NTAG I2C
  0x48 : MIFARE Plus SL2

SAK 多位组合语义:
  b3=1 only        : UID 不完整, 需继续下一级联 (CL2/CL3)
  b4=1 (CRYPTO1)   : M1 兼容 (使用 CRYPTO1 加密)
  b6=1 (T=CL)      : 支持 ISO 14443-4 协议 (RATS/ATS 流程)
  b6=1 & b4=0      : 纯 CPU 卡 / DESFire / FM1208 (无 M1)
  b6=1 & b4=1      : M1+CPU 复合卡 (Mifare Plus SL3 等)
"""

import sys
import os

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from ui.language import Language


class SAKParser:
    """SAK 解析器 (ISO/IEC 14443-3)"""

    # SAK 位掩码
    BIT_RFU_B1 = 0x01       # b1 RFU
    BIT_RFU_B2 = 0x02       # b2 RFU (NXP: 保留)
    BIT_CASCADE = 0x04      # b3 Cascade bit
    BIT_CRYPTO1 = 0x08      # b4 MIFARE Classic / CRYPTO1
    BIT_PLATFORM_B5 = 0x10  # b5
    BIT_TCL = 0x20          # b6 ISO 14443-4 (T=CL)
    BIT_PLATFORM_B7 = 0x40  # b7
    BIT_PLATFORM_B8 = 0x80  # b8

    @staticmethod
    def parse(sak, extra_info):
        extra_info.append(f"  - {Language.get('sak_response')}: 0x{sak:02X}")

        # ---------- 逐位提取 ----------
        b1 = sak & 0x01
        b2 = (sak >> 1) & 0x01
        b3 = (sak >> 2) & 0x01
        b4 = (sak >> 3) & 0x01
        b5 = (sak >> 4) & 0x01
        b6 = (sak >> 5) & 0x01
        b7 = (sak >> 6) & 0x01
        b8 = (sak >> 7) & 0x01

        # ---------- 关键标志 ----------
        # b3=0 → UID完整; b3=1 → UID不完整
        uid_complete = (sak & SAKParser.BIT_CASCADE) == 0
        supports_14443_4 = (sak & SAKParser.BIT_TCL) != 0
        supports_crypto1 = (sak & SAKParser.BIT_CRYPTO1) != 0

        extra_info.append(f"    - {Language.get('atqa_bit3')}: {b3} ({Language.get('sak_cascade_bit')})")
        extra_info.append(f"    - {Language.get('atqa_bit4')}: {b4} ({Language.get('sak_crypto1')})")
        extra_info.append(f"    - {Language.get('atqa_bit6')}: {b6} ({Language.get('sak_tcl_support')})")

        extra_info.append(f"    - {Language.get('uid_complete')}: {uid_complete}")
        extra_info.append(f"    - {Language.get('support_14443_4')}: {supports_14443_4}")

        if not uid_complete:
            extra_info.append(f"    - {Language.get('sak_uid_not_complete')}")
        if supports_14443_4:
            extra_info.append(f"    - {Language.get('support_14443_4_true')}")
        if supports_crypto1:
            extra_info.append(f"    - {Language.get('sak_crypto1_support')}")

        # ---------- 卡片类型 ----------
        card_type = SAKParser.get_card_type(sak)
        extra_info.append(f"    - {Language.get('card_type')}: {card_type}")

        if supports_14443_4:
            extra_info.append(f"    - {Language.get('protocol_14443_4_detected')}")

        # ---------- 逐位详情 ----------
        extra_info.append(f"    - b1: {b1} (RFU)")
        extra_info.append(f"    - b2: {b2} (RFU)")
        extra_info.append(f"    - b3: {b3} (Cascade bit, 0=UID完整)")
        extra_info.append(f"    - b4: {b4} (MIFARE Classic/CRYPTO1)")
        extra_info.append(f"    - b5: {b5}")
        extra_info.append(f"    - b6: {b6} (ISO 14443-4 T=CL)")
        extra_info.append(f"    - b7: {b7}")
        extra_info.append(f"    - b8: {b8}")

        return card_type

    @staticmethod
    def get_card_type(sak):
        # 如果 cascade bit 置位，说明 UID 未完整，不应据此判断卡片类型
        if sak & 0x04:
            return Language.get("sak_cascade_level")

        if sak == 0x00:
            return "Mifare Ultralight / NTAG"
        elif sak == 0x01:
            return "Mifare DESFire (老式/废弃)"
        elif sak == 0x08:
            return "Mifare Classic 1K (M1)"
        elif sak == 0x09:
            return "Mifare Mini (M1 320b)"
        elif sak == 0x10:
            return "Mifare Plus SL1"
        elif sak == 0x11:
            return "Mifare Plus SL1 (4K)"
        elif sak == 0x18:
            return "Mifare Classic 4K (M1)"
        elif sak == 0x20:
            return Language.get("card_type_standard_cpu")
        elif sak == 0x28:
            return Language.get("card_type_compatible_m1")
        elif sak == 0x38:
            return "Mifare DESFire EV1/EV2/EV3"
        elif sak == 0x40:
            return "Mifare Plus SL3"
        elif sak == 0x44:
            return "Mifare Ultralight EV1 / NTAG I2C"
        elif sak == 0x48:
            return "Mifare Plus SL2"
        else:
            # b6 置位 → 14443-4 兼容
            if sak & 0x20:
                return f"ISO 14443-4 兼容卡 (SAK=0x{sak:02X})"
            # b4 置位 → CRYPTO1 兼容
            if sak & 0x08:
                return f"MIFARE Classic 兼容 (SAK=0x{sak:02X})"
            return Language.get("card_type_unknown")
