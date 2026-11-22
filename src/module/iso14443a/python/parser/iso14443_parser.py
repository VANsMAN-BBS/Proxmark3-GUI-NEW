#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
ISO 14443A 核心协议解析器
包含 14443-3 和 14443-4 基础解析，支持RATS/ATS详细解析和APDU解析
支持FM1208 (复旦微CPU卡) 专用命令解析

v2 (20261025): 接入原始数据还原 (RawDataReconstructor)
  - 接收的 payload 为含校验位的原始位流数据，需先还原为纯数据字节
  - 支持 7-bit 命令、校验位检查、CRC16-A 检查、Mifare 1 数据识别
"""

import struct
import sys
import os
import traceback

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from ui.language import Language
from parser.raw_data_reconstructor import RawDataReconstructor
from parser.atqa_parser import ATQAParser
from parser.sak_parser import SAKParser
from parser.mifare_classic import MifareClassicParser
from parser.mifare_ultralight import MifareUltralightParser
from parser.fm1208 import FM1208Parser
from parser.desfire import DESFireParser
from parser.parser_state import ParserState


class _ParserStateMeta(type):
    """Route historical underscore attributes to the active ParserState.

    Stage-1 code and its tests read/write ``ISO14443AParser._last_sak`` etc.
    directly.  The facade keeps those names working while the actual storage
    belongs to one ``ParserState`` instance, so a sidecar can parse two traces
    without sharing state.
    """

    def __getattr__(cls, name):
        if name in ParserState.FIELDS:
            return getattr(cls._state, name)
        raise AttributeError(name)

    def __setattr__(cls, name, value):
        if name in ParserState.FIELDS:
            setattr(cls._state, name, value)
        else:
            super(_ParserStateMeta, cls).__setattr__(name, value)


class ISO14443AParser(metaclass=_ParserStateMeta):
    """14443A 协议解析器（支持 -3、-4 及各类卡片）"""

    CMD_REQA = 0x26
    CMD_WUPA = 0x52
    CMD_HLTA = 0x50
    CMD_ANTICOLLISION_CL1 = 0x93
    CMD_ANTICOLLISION_CL2 = 0x95
    CMD_ANTICOLLISION_CL3 = 0x97
    CMD_SELECT_CL1 = 0x93
    CMD_SELECT_CL2 = 0x95
    CMD_SELECT_CL3 = 0x97

    CMD_RATS = 0xE0
    CMD_PPS = 0xD1

    # 协议分层状态机 (由 ParserState 实例持有; 类级同名属性仅作兼容门面)
    # 14443-3 阶段: REQA/WUPA/HLTA/ANTICOLLISION/SELECT + ATQA/SAK 响应
    # 14443-4 阶段: RATS/ATS/PPS 之后, I-block + APDU
    # Mifare 阶段: AUTH(60/61) 之后, 仅按长度判断数据/认证过程
    # 原生卡阶段: NTAG/Ultralight 命令 (READ/WRITE/GET_VERSION/PWD_AUTH 等)
    #
    # 每个抓包应创建独立状态:
    #   state = ISO14443AParser.new_state()
    #   with ISO14443AParser.parser_state(state):
    #       ... parse_packet(...) ...
    _state = ParserState()

    @classmethod
    def new_state(cls):
        """Create a fresh, independent parser state."""
        return ParserState()

    @classmethod
    def use_state(cls, state=None):
        """Install state as the active parser state (None = fresh state)."""
        cls._state = state if state is not None else ParserState()
        return cls._state

    @classmethod
    def current_state(cls):
        return cls._state

    @classmethod
    def parser_state(cls, state=None):
        """Context manager: bind one ParserState to a capture.

        On exit the previous state is restored, so an embedding application
        (the Qt sidecar) can parse a second trace with a new instance without
        inheriting the first trace's T=CL / Mifare / native session.
        """
        class _StateScope(object):
            def __init__(self, parser_cls, new_state):
                self._parser_cls = parser_cls
                self._new_state = new_state
                self._previous = None

            def __enter__(self):
                self._previous = self._parser_cls._state
                self._parser_cls._state = (self._new_state
                                           if self._new_state is not None
                                           else ParserState())
                return self._parser_cls._state

            def __exit__(self, exc_type, exc, tb):
                self._parser_cls._state = self._previous
                return False

        return _StateScope(cls, state)


    @staticmethod
    def reset_protocol_state():
        """重置所有协议状态 (新文件/新捕获开始时应调用)"""
        ISO14443AParser._tcl_active = False
        ISO14443AParser._waiting_for_ats = False
        ISO14443AParser._last_atqa = None
        ISO14443AParser._last_sak = None
        ISO14443AParser._last_card_type = None
        ISO14443AParser._last_ats = None
        # UID 级联状态
        ISO14443AParser._last_sel_code = None
        ISO14443AParser._uid_cl1 = None
        ISO14443AParser._uid_cl2 = None
        ISO14443AParser._uid_cl3 = None
        ISO14443AParser._uid_full = None
        ISO14443AParser.reset_mifare_state()
        ISO14443AParser.reset_native_state()

    @staticmethod
    def reset_native_state():
        """重置原生卡 (NTAG/Ultralight) 状态机"""
        ISO14443AParser._native_active = False
        ISO14443AParser._native_last_cmd = None
        ISO14443AParser._native_last_page = None

    @staticmethod
    def reset_mifare_state():
        """重置 Mifare 状态机"""
        ISO14443AParser._mifare_active = False
        ISO14443AParser._mifare_auth_step = 0
        ISO14443AParser._mifare_last_cmd = None
        ISO14443AParser._mifare_last_block = None
        ISO14443AParser._mifare_pending_data = False
        ISO14443AParser._mifare_data_sent = False
        ISO14443AParser._mifare_probe_pending = False

    @staticmethod
    def identify_card_type(atqa=None, sak=None):
        """根据 ATQA + SAK 粗略识别卡片类型 (NXP AN10833)

        参数:
          atqa: 16-bit ATQA 值 (可为 None)
          sak : 8-bit SAK 值 (可为 None, cascade bit=1 时忽略)

        返回: (card_type_str, next_protocol_hint)
          next_protocol_hint: 'mifare' | 'tcl' | 'native' | 'unknown'
            - 'mifare' : 后续以 AUTH(60/61) 进入 Mifare 模式
            - 'tcl'    : 后续以 RATS/ATS/PPS 进入 14443-4 T=CL (CPU卡/FM1208)
            - 'native' : 后续以 READ/WRITE/GET_VERSION 等 NTAG/Ultralight 命令进入原生卡模式
            - 'unknown': 未知/级联中
        """
        # SAK cascade bit 置位 → UID 不完整, 不能判断类型
        if sak is not None and (sak & 0x04):
            return "级联中 (UID 未完整)", "unknown"

        # 已知 ATQA + SAK 组合 (最准确, NXP AN10833 表)
        if atqa is not None and sak is not None:
            combo = (atqa, sak)
            known = {
                (0x0004, 0x08): ("Mifare Classic 1K", "mifare"),
                (0x0002, 0x18): ("Mifare Classic 4K", "mifare"),
                (0x0004, 0x09): ("Mifare Mini", "mifare"),
                (0x0044, 0x00): ("Mifare Ultralight / NTAG", "native"),
                (0x0044, 0x20): ("Mifare Ultralight C / NTAG I2C", "native"),
                (0x0200, 0x00): ("Mifare Ultralight / NTAG (7B UID)", "native"),
                (0x0004, 0x01): ("Mifare DESFire (老式)", "tcl"),
                (0x0004, 0x10): ("Mifare Plus SL1", "mifare"),
                (0x0002, 0x10): ("Mifare Plus SL1 (4K)", "mifare"),
                (0x0004, 0x11): ("Mifare Plus SL1 (4K)", "mifare"),
                (0x0002, 0x11): ("Mifare Plus SL1 (4K)", "mifare"),
                # SAK=0x20 (T=CL only, 无 CRYPTO1) → CPU卡/Mifare Plus SL3
                (0x0004, 0x20): ("ISO 14443-4 CPU卡 (FM1208/DESFire/Mifare Plus SL3)", "tcl"),
                (0x0002, 0x20): ("ISO 14443-4 CPU卡 (FM1208/DESFire/Mifare Plus SL3)", "tcl"),
                # SAK=0x28 (T=CL + CRYPTO1) → Mifare Plus SL3 兼容 M1 / 兼容 M1 的 CPU卡
                (0x0004, 0x28): ("Mifare Plus SL3 (兼容M1) / FM1208", "mifare"),
                (0x0002, 0x28): ("Mifare Plus SL3 4K (兼容M1) / FM1208", "mifare"),
                (0x0004, 0x38): ("Mifare DESFire EV1", "tcl"),
                (0x0002, 0x38): ("Mifare DESFire EV1 4K", "tcl"),
                (0x0004, 0x40): ("Mifare Plus SL3", "tcl"),
                (0x0002, 0x40): ("Mifare Plus SL3", "tcl"),
                (0x0004, 0x48): ("Mifare Plus SL2", "tcl"),
                (0x0002, 0x48): ("Mifare Plus SL2", "tcl"),
            }
            if combo in known:
                return known[combo]

        # 仅根据 SAK 判断
        if sak is not None:
            if sak == 0x00:
                return "Mifare Ultralight / NTAG", "native"
            elif sak == 0x01:
                return "Mifare DESFire (老式/废弃)", "tcl"
            elif sak == 0x08:
                return "Mifare Classic 1K", "mifare"
            elif sak == 0x09:
                return "Mifare Mini", "mifare"
            elif sak == 0x18:
                return "Mifare Classic 4K", "mifare"
            elif sak == 0x10:
                return "Mifare Plus SL1", "mifare"
            elif sak == 0x11:
                return "Mifare Plus SL1 (4K)", "mifare"
            elif sak == 0x20:
                return "ISO 14443-4 CPU卡 (FM1208/DESFire等)", "tcl"
            elif sak == 0x28:
                return "兼容M1的CPU卡 (FM1208)", "mifare"
            elif sak == 0x38:
                return "Mifare DESFire EV1/EV2/EV3", "tcl"
            elif sak == 0x40:
                return "Mifare Plus SL3", "tcl"
            elif sak == 0x44:
                return "Mifare Ultralight EV1 / NTAG I2C", "native"
            elif sak == 0x48:
                return "Mifare Plus SL2", "tcl"
            else:
                if sak & 0x20:
                    return f"ISO 14443-4 兼容卡 (SAK=0x{sak:02X})", "tcl"
                if sak & 0x08:
                    return f"MIFARE Classic 兼容 (SAK=0x{sak:02X})", "mifare"
                return f"未知卡 (SAK=0x{sak:02X})", "unknown"

        # 仅根据 ATQA 判断 (UID 大小)
        if atqa is not None:
            b7 = (atqa >> 6) & 0x01
            b8 = (atqa >> 7) & 0x01
            uid_size = (b8 << 1) | b7
            if uid_size == 0:
                return "4字节UID卡 (可能 Mifare Classic)", "unknown"
            elif uid_size == 1:
                return "7字节UID卡 (可能 Ultralight/NTAG)", "native"
            elif uid_size == 2:
                return "10字节UID卡", "unknown"

        return "未知卡", "unknown"

    @staticmethod
    def _get_mifare_nak_desc(code):
        """Mifare NAK 错误码描述"""
        nak_codes = {
            0x00: "操作不允许",
            0x01: "CRC/奇偶校验错误",
            0x04: "奇偶校验错误",
            0x05: "认证失败/未认证",
        }
        return nak_codes.get(code, "未知错误")

    @staticmethod
    def _parse_mifare_response(payload, data_len, result):
        """解析 Mifare 响应 (PICC→PCD, 处于 Mifare 状态时调用)

        根据 _mifare_auth_step 和 _mifare_last_cmd 解释响应含义:
        - step=1: Nonce (卡片随机数, 4B)
        - step=3: 加密响应 (4B)
        - step=0: 认证后数据响应 (READ=16B数据, WRITE/INC/DEC/RESTORE=ACK后期待数据, TRANSFER=ACK)
        """
        if data_len == 0:
            result["commands"].append("Mifare 空响应")
            return

        step = ISO14443AParser._mifare_auth_step
        last_cmd = ISO14443AParser._mifare_last_cmd

        if step == 1:
            # AUTH 后的 Nonce 响应 (4字节随机数)
            result["extra_info"].append(f"  - Mifare 认证: 卡片随机数 (Nonce, {data_len}B)")
            result["extra_info"].append(f"    - 数据: {payload.hex().upper()}")
            result["commands"].append("Mifare Nonce (卡片随机数)")

        elif step == 3:
            # 加密挑战后的加密响应 (4字节)
            result["extra_info"].append(f"  - Mifare 认证: 加密响应 ({data_len}B)")
            result["extra_info"].append(f"    - 数据: {payload.hex().upper()}")
            result["commands"].append("Mifare 加密响应 (认证完成)")

        elif step == 0:
            # 认证完成后的数据命令响应
            if data_len == 1:
                # ACK / NAK
                ack = payload[0]
                if ack == 0x0A:
                    result["extra_info"].append(f"  - Mifare 响应: ACK (0x0A, 成功)")
                    result["commands"].append("Mifare ACK (成功)")
                    # WRITE/INCREMENT/DECREMENT/RESTORE 有数据阶段:
                    # - pre-data ACK (data_sent=False): 期待数据, set pending_data=True
                    # - post-data ACK (data_sent=True): 数据已发, 不设 pending_data, reset data_sent
                    if last_cmd in (MifareClassicParser.CMD_WRITE,
                                    MifareClassicParser.CMD_INCREMENT,
                                    MifareClassicParser.CMD_DECREMENT,
                                    MifareClassicParser.CMD_RESTORE):
                        if not ISO14443AParser._mifare_data_sent:
                            ISO14443AParser._mifare_pending_data = True
                        else:
                            ISO14443AParser._mifare_data_sent = False
                else:
                    nak_desc = ISO14443AParser._get_mifare_nak_desc(ack)
                    result["extra_info"].append(f"  - Mifare 响应: NAK (0x{ack:02X} - {nak_desc})")
                    result["commands"].append(f"Mifare NAK (0x{ack:02X} {nak_desc})")
            elif last_cmd == MifareClassicParser.CMD_READ:
                # READ 响应: 16字节块数据
                block_disp = (ISO14443AParser._mifare_last_block
                              if ISO14443AParser._mifare_last_block is not None
                              else "未知(密文)")
                result["extra_info"].append(f"  - Mifare Read 响应: 块数据 ({data_len}B)")
                result["extra_info"].append(f"    - 块号: {block_disp}")
                result["extra_info"].append(f"    - 数据: {payload.hex().upper()}")
                result["commands"].append(f"Mifare Read 响应 (block={block_disp}, {data_len}B)")
            else:
                # 其他数据响应
                result["extra_info"].append(f"  - Mifare 响应 ({data_len}B): {payload.hex().upper()}")
                result["commands"].append(f"Mifare 响应 ({data_len}B)")

        else:
            result["extra_info"].append(f"  - Mifare 响应 ({data_len}B): {payload.hex().upper()}")
            result["commands"].append(f"Mifare 响应 ({data_len}B)")

    @staticmethod
    def parse_timestamp(timestamp_raw):
        ts_value = timestamp_raw & 0x00FFFFFF
        if ts_value == 0:
            return f"0x{ts_value:06X}", ts_value, True
        elif ts_value == 0x00FFFFFF:
            return f"0x{ts_value:06X}", ts_value, False
        else:
            return f"0x{ts_value:06X}", ts_value, False

    @staticmethod
    def calculate_time_diff(prev_ts, curr_ts):
        if curr_ts <= prev_ts:
            diff = prev_ts - curr_ts
            overflow = False
        else:
            diff = (prev_ts + 0x01000000) - curr_ts
            overflow = True
        return diff, overflow

    @staticmethod
    def parse_i_block(payload, extra_info, direction_byte=ord('>')):
        """解析14443-4 T=CL协议的I-block

        I-block PCB (ISO 14443-4):
          b8-b7 (0xC0): 00 = I-block
          b6   (0x20): Chaining (1=链式, 更多数据将跟随)
          b5   (0x10): CID present
          b4   (0x08): NAD present
          b3   (0x04): RFU
          b2   (0x02): RFU
          b1   (0x01): 序列号 (0/1)
        """
        if not payload or len(payload) < 3:
            return {"error": Language.get("i_block_too_short")}

        pcb = payload[0]
        if (pcb & 0xC0) != 0x00:
            return {"error": Language.get("not_valid_i_block")}

        result = {}
        has_cid = (pcb & 0x10) != 0
        seq_num = pcb & 0x01
        chaining = (pcb & 0x20) != 0

        offset = 1
        if has_cid:
            cid = payload[offset]
            offset += 1
            result["cid"] = cid
        else:
            result["cid"] = None

        has_nad = (pcb & 0x08) != 0
        if has_nad and offset < len(payload):
            nad = payload[offset]
            offset += 1
            result["nad"] = nad
        else:
            result["nad"] = None

        # I-block 数据区 = payload[offset:-2] (去掉末尾2字节CRC)
        if len(payload) > offset + 2:
            apdu = payload[offset:-2]
            result["apdu_hex"] = apdu.hex().upper()
            # 根据方向解析为命令APDU或响应APDU
            ISO14443AParser.parse_apdu(apdu, extra_info, direction_byte)
        elif len(payload) > offset:
            # 可能不含CRC (某些硬件已剥离CRC)
            apdu = payload[offset:]
            if len(apdu) >= 4:
                result["apdu_hex"] = apdu.hex().upper()
                ISO14443AParser.parse_apdu(apdu, extra_info, direction_byte)
            else:
                result["apdu_hex"] = ""
        else:
            result["apdu_hex"] = ""

        result["pcb"] = pcb
        result["seq_num"] = seq_num
        result["has_cid"] = has_cid
        result["chaining"] = chaining
        result["block_type"] = "I-block (Information Block)"

        extra_info.append(f"  - I-block解析:")
        extra_info.append(f"    - PCB: 0x{pcb:02X}")
        extra_info.append(f"    - 块类型: I-block (b8..b7=00)")
        extra_info.append(f"    - b6 Chaining: {chaining}")
        extra_info.append(f"    - b1 序列号: {seq_num}")
        if has_cid:
            extra_info.append(f"    - CID: {cid}")
        if has_nad:
            extra_info.append(f"    - NAD: {nad}")
        if result.get("apdu_hex"):
            extra_info.append(f"    - APDU数据 ({len(apdu)}B): {result['apdu_hex']}")

        return result

    @staticmethod
    def parse_apdu(apdu_data, extra_info, direction_byte=ord('>')):
        """解析APDU (Application Protocol Data Unit)

        根据 direction_byte 区分命令APDU (PCD→PICC) 和响应APDU (PICC→PCD)。

        命令APDU (ISO 7816-4):
          Case 1: CLA INS P1 P2                           (4B, 无数据)
          Case 2: CLA INS P1 P2 Le                       (5B, 期望响应)
          Case 3: CLA INS P1 P2 Lc Data                  (5+Lc B, 发送数据)
          Case 4: CLA INS P1 P2 Lc Data Le               (6+Lc B, 发送+期望响应)

        响应APDU:
          [Data] SW1 SW2                                 (末尾2字节为状态字)
        """
        if not apdu_data or len(apdu_data) < 2:
            return

        is_response = (direction_byte == ord('<'))

        if is_response:
            ISO14443AParser.parse_response_apdu(apdu_data, extra_info)
        else:
            ISO14443AParser.parse_command_apdu(apdu_data, extra_info)

    @staticmethod
    def parse_command_apdu(apdu_data, extra_info):
        """解析命令APDU (PCD→PICC, ISO 7816-4)"""
        if not apdu_data or len(apdu_data) < 4:
            return

        cla = apdu_data[0]
        ins = apdu_data[1]
        p1 = apdu_data[2]
        p2 = apdu_data[3]
        apdu_len = len(apdu_data)

        # ---------- 检测 APDU Case (ISO 7816-4) ----------
        # 短 APDU : CLA INS P1 P2 [Lc Data] [Le]
        # 扩展 APDU: CLA INS P1 P2 00 LcHi LcLo Data [LeHi LeLo]
        # 不能再用 CLA & 0x10 判断扩展长度, 也不能对 2 字节切片做 '>I' 解包。
        case = 0
        lc = 0
        le = 0
        data = b''
        warnings = []

        if apdu_len == 4:
            case = 1
        elif apdu_len == 5:
            case = 2
            le = apdu_data[4]          # Le=0 表示 256
        elif apdu_data[4] != 0x00:
            # 短 APDU: byte4 = Lc
            lc = apdu_data[4]
            remaining = apdu_len - 5
            if remaining < lc:
                warnings.append(f"短APDU Lc={lc} 超出剩余长度({remaining}), 截断")
                case = 3
                data = apdu_data[5:5 + max(remaining, 0)]
            elif remaining == lc:
                case = 3
                data = apdu_data[5:5 + lc]
            elif remaining == lc + 1:
                case = 4
                data = apdu_data[5:5 + lc]
                le = apdu_data[5 + lc]
            else:
                case = 4
                data = apdu_data[5:5 + lc]
                le = apdu_data[5 + lc]
                warnings.append(
                    f"短APDU 长度多余: 期望 {5 + lc} 或 {6 + lc}, 实际 {apdu_len}")
        else:
            # byte4 == 0x00: 短 Case 2 (Le=0) 或扩展 APDU
            if apdu_len == 6:
                warnings.append(f"APDU 长度 {apdu_len} 不完整, 按 Case 2 处理")
                case = 2
                le = apdu_data[5]
            else:  # apdu_len >= 7
                ext = (apdu_data[5] << 8) | apdu_data[6]
                if apdu_len == 7:
                    # 00 LeHi LeLo -> 扩展 Case 2
                    case = 2
                    le = ext
                elif ext == 0:
                    warnings.append("扩展APDU Lc=0 且带数据, 非法, 按 Case 2 处理")
                    case = 2
                    le = 0
                else:
                    lc = ext
                    data_start = 7
                    remaining = apdu_len - data_start
                    if remaining < lc:
                        warnings.append(
                            f"扩展APDU Lc={lc} 超出剩余长度({remaining}), 截断")
                        case = 3
                        data = apdu_data[data_start:data_start + max(remaining, 0)]
                    elif remaining == lc:
                        case = 3
                        data = apdu_data[data_start:data_start + lc]
                    elif remaining == lc + 2:
                        case = 4
                        data = apdu_data[data_start:data_start + lc]
                        le = ((apdu_data[data_start + lc] << 8)
                              | apdu_data[data_start + lc + 1])
                    else:
                        case = 4
                        data = apdu_data[data_start:data_start + lc]
                        if remaining >= lc + 2:
                            le = ((apdu_data[data_start + lc] << 8)
                                  | apdu_data[data_start + lc + 1])
                        warnings.append(
                            f"扩展APDU 长度多余: Lc={lc}, 剩余={remaining}")

        case_names = {
            1: "Case 1 (无数据)",
            2: "Case 2 (Le, 期望响应)",
            3: "Case 3 (Lc+Data, 发送数据)",
            4: "Case 4 (Lc+Data+Le, 发送+期望响应)",
        }
        for warn in warnings:
            extra_info.append(f"      - 警告: {warn}")

        # ---------- 解析CLA ----------
        secure_messaging = (cla & 0x0C) == 0x0C
        cla_desc = []
        if cla == 0x00 or (cla & 0x80):
            cla_desc.append("ISO 7816-4")
        else:
            cla_desc.append("厂商自定义")
        if cla & 0x04:
            cla_desc.append("安全消息(加密)")
        if cla & 0x08:
            cla_desc.append("安全消息(MAC)")
        if cla & 0x10:
            cla_desc.append("b5置位")
        if cla & 0x20:
            cla_desc.append("逻辑通道2")
        if cla & 0x40:
            cla_desc.append("逻辑通道3")
        if not any([cla & 0x04, cla & 0x08, cla & 0x10, cla & 0x20, cla & 0x40, cla & 0x80]):
            cla_desc.append("标准")
        channel = cla & 0x03
        if channel > 0:
            cla_desc.append(f"通道={channel}")

        extra_info.append(f"    - 命令APDU ({case_names.get(case, '未知')}):")
        extra_info.append(f"      - CLA: 0x{cla:02X} ({', '.join(cla_desc)})")
        extra_info.append(f"      - 安全消息: {secure_messaging}")
        extra_info.append(f"      - INS: 0x{ins:02X} ({ISO14443AParser.get_ins_description(ins)})")
        extra_info.append(f"      - P1: 0x{p1:02X}")
        extra_info.append(f"      - P2: 0x{p2:02X}")

        if lc > 0:
            extra_info.append(f"      - Lc: {lc}")
            extra_info.append(f"      - Data ({lc}B): {data.hex().upper()}")
        if case in (2, 4):
            le_disp = le if le > 0 else 256
            extra_info.append(f"      - Le: {le} (期望 {le_disp} 字节)")

        # ---------- FM1208 专用命令解析 ----------
        FM1208Parser.parse_command(cla, ins, p1, p2, lc, data, le, case, apdu_data, extra_info)

    @staticmethod
    def parse_response_apdu(apdu_data, extra_info):
        """解析响应APDU (PICC→PCD)"""
        if not apdu_data or len(apdu_data) < 2:
            return

        sw1 = apdu_data[-2]
        sw2 = apdu_data[-1]
        response_data = apdu_data[:-2] if len(apdu_data) > 2 else b''
        sw_code = (sw1 << 8) | sw2

        extra_info.append(f"    - 响应APDU:")
        if response_data:
            extra_info.append(f"      - 响应数据 ({len(response_data)}B): {response_data.hex().upper()}")
        extra_info.append(f"      - SW1: 0x{sw1:02X}")
        extra_info.append(f"      - SW2: 0x{sw2:02X}")
        extra_info.append(f"      - 状态字: 0x{sw1:02X}{sw2:02X} ({ISO14443AParser.get_sw_description(sw1, sw2)})")

        # ---------- FM1208 响应解析 ----------
        FM1208Parser.parse_response(sw1, sw2, response_data, extra_info)

    @staticmethod
    def get_ins_description(ins):
        """获取INS指令描述"""
        ins_names = {
            0x00: "SELECT FILE (选择文件)",
            0x0A: "GET DATA (获取数据)",
            0x0C: "GET CHALLENGE (获取挑战)",
            0x10: "INTERNAL AUTHENTICATE (内部认证)",
            0x12: "EXTERNAL AUTHENTICATE (外部认证)",
            0x20: "VERIFY (验证)",
            0x22: "MANAGE SECURITY ENV (管理安全环境)",
            0x30: "PERFORM SCQL OPERATION",
            0x50: "GENERATE APPLICATION CRYPTOGRAM",
            0x70: "MANAGE CHANNEL (管理通道)",
            0x80: "READ RECORD (读记录)",
            0x82: "EXTERNAL AUTHENTICATE (外部认证)",
            0x84: "GET CHALLENGE (获取挑战)",
            0x88: "INTERNAL AUTHENTICATE (内部认证)",
            0xA0: "UPDATE RECORD (更新记录)",
            0xA4: "SELECT FILE (选择文件)",
            0xA8: "READ BINARY (读二进制)",
            0xB0: "READ BINARY (读二进制)",
            0xB2: "READ RECORD (读记录)",
            0xC0: "GET RESPONSE (获取响应)",
            0xC2: "ENVELOPE (信封)",
            0xC4: "GET DATA (获取数据)",
            0xCA: "GET DATA (获取数据)",
            0xD0: "WRITE BINARY (写二进制)",
            0xD2: "WRITE RECORD (写记录)",
            0xD6: "UPDATE BINARY (更新二进制)",
            0xDA: "PUT DATA (放入数据)",
            0xE0: "EXTERNAL AUTHENTICATE (外部认证)",
            0xE2: "APPEND RECORD (追加记录)",
            0xF0: "SELECT FILE (选择文件)",
        }
        return ins_names.get(ins, f"未知指令 (0x{ins:02X})")

    @staticmethod
    def get_sw_description(sw1, sw2):
        """获取状态字描述"""
        sw_code = (sw1 << 8) | sw2
        sw_names = {
            0x9000: "成功 (Success)",
            0x6100: "更多数据 (More Data)",
            0x6200: "警告 (Warning)",
            0x6300: "错误 (Error)",
            0x6400: "执行错误 (Execution Error)",
            0x6500: "内存错误 (Memory Error)",
            0x6600: "安全错误 (Security Error)",
            0x6700: "密钥错误 (Key Error)",
            0x6C00: "长度错误 (Wrong Length)",
            0x6D00: "CLA错误 (Wrong CLA)",
            0x6E00: "INS错误 (Wrong INS)",
            0x6F00: "技术错误 (Technical Error)",
            0x6A80: "数据字段错误 (Wrong Data Field)",
            0x6A82: "文件未找到 (File Not Found)",
            0x6A86: "P1/P2错误 (Incorrect P1/P2)",
            0x6A88: "安全消息错误 (Secure Messaging Error)",
            0x6A89: "引用数据错误 (Referenced Data Error)",
            0x6A8A: "参数错误 (Wrong Parameter)",
            0x6A81: "功能不支持 (Function Not Supported)",
            0x6981: "命令与文件结构不兼容",
            0x6982: "安全状态不满足",
            0x6983: "认证方法锁定",
            0x6984: "引用数据无效",
            0x6985: "条件不满足",
            0x6986: "命令不允许",
        }
        if sw1 == 0x61:
            return f"更多数据, Le={sw2} (More Data)"
        elif sw1 == 0x6C:
            return f"长度错误, Le={sw2} (Wrong Length)"
        return sw_names.get(sw_code, f"未知状态字 (0x{sw1:02X}{sw2:02X})")

    @staticmethod
    def parse_r_block(payload, extra_info):
        """解析14443-4 T=CL协议的R-block

        R-block PCB (ISO 14443-4):
          b8b7 = 10
          b5   = 0 (R-block)
          b4   = 0: R-ACK, 1: R-NAK
          b1   = 序列号
        返回: "R-ACK" | "R-NAK" | None
        """
        if not payload or len(payload) < 1:
            return None

        pcb = payload[0]
        if (pcb & 0xC0) != 0x80:
            return None

        is_nak = (pcb & 0x10) != 0
        seq = pcb & 0x01
        r_type = "R-NAK" if is_nak else "R-ACK"

        extra_info.append(f"  - R-block解析:")
        extra_info.append(f"    - PCB: 0x{pcb:02X}")
        extra_info.append(f"    - 类型: {r_type}"
                          + (" (未确认/重传请求)" if is_nak else " (确认)"))
        extra_info.append(f"    - 序列号: {seq}")
        return r_type

    @staticmethod
    def parse_s_block(payload, extra_info):
        """解析14443-4 T=CL协议的S-block"""
        if not payload or len(payload) < 1:
            return
        
        pcb = payload[0]
        if (pcb & 0xC0) != 0xC0:
            return
        
        extra_info.append(f"  - S-block解析:")
        extra_info.append(f"    - PCB: 0x{pcb:02X}")
        
        if pcb & 0x20:
            extra_info.append(f"    - 类型: WTX (等待时间扩展)")
            if len(payload) >= 2:
                extra_info.append(f"    - 等待时间: {payload[1]}")
        else:
            extra_info.append(f"    - 类型: DESELECT")
        
        if pcb & 0x10:
            cid = pcb & 0x0F
            extra_info.append(f"    - CID: {cid}")

    @staticmethod
    def parse_anticollision_response(payload, sel_code, extra_info, commands=None):
        """解析 PICC 防冲突响应 (UID片段 + BCC, 5字节)

        参数:
          payload  : PICC 响应载荷 (NVB格式或纯UID+BCC, 兼容旧sniffer格式)
          sel_code : 0x93/0x95/0x97 (CL1/CL2/CL3), 由 PCD 的 _last_sel_code 决定
          extra_info: 输出列表
          commands : 可选, 命令列表 (若提供则添加命令描述)

        副作用: 将 UID 片段 (4B) 存入 _uid_cl1/_uid_cl2/_uid_cl3
                CT(0x88) 头会被保留在片段中, 完整UID拼接时再剥离
        """
        if len(payload) < 1:
            return

        cl_desc = ""
        cl_short = ""
        uid_byte_range = ""
        is_final_cl = False
        if sel_code == 0x93:
            cl_desc = "级联级别1 (CL1, SEL=0x93)"
            cl_short = "CL1"
            # CL1 可能是 4B UID (单一CL), 或 7B/10B UID 的第一段
            uid_byte_range = "UID 字节 0-2 (若 7B/10B UID) 或 0-3 (若 4B UID)"
            is_final_cl = ISO14443AParser._uid_cl2 is None  # 没有 CL2 则 CL1 即最终级
        elif sel_code == 0x95:
            cl_desc = "级联级别2 (CL2, SEL=0x95)"
            cl_short = "CL2"
            # CL2 是 7B UID 的最后一段 (字节 3-6), 或 10B UID 的第二段 (字节 3-5)
            uid_byte_range = "UID 字节 3-6 (若 7B UID) 或 3-5 (若 10B UID)"
            is_final_cl = ISO14443AParser._uid_cl3 is None  # 没有 CL3 则 CL2 即最终级
        elif sel_code == 0x97:
            cl_desc = "级联级别3 (CL3, SEL=0x97)"
            cl_short = "CL3"
            uid_byte_range = "UID 字节 6-9 (10B UID 的最后一段)"
            is_final_cl = True  # CL3 一定是最终级
        else:
            cl_desc = f"未知级联 (SEL=0x{sel_code:02X})"
            cl_short = f"CL?(0x{sel_code:02X})"

        extra_info.append(f"  - UID级联: {cl_desc}")
        extra_info.append(f"    - 此片段对应 {uid_byte_range}")
        if is_final_cl:
            extra_info.append(f"    - 此为最终级联级别 (后续 SELECT 后 SAK cascade bit 应清零)")
        else:
            extra_info.append(f"    - 此为中间级联级别 (后续 SAK cascade bit 应置位, 继续下一级)")

        # 兼容两种sniffer格式:
        # 格式A (PCD命令+PICC响应合并): payload = [NVB][UID片段4B][BCC]
        # 格式B (纯PICC响应):           payload = [UID片段4B][BCC]
        # 通过 payload[0] 是否为合法 NVB (高4位=0010/0111 即 0x20/0x70) 来区分
        first = payload[0]
        if first in (0x20, 0x70) and len(payload) >= 6:
            # 格式A: NVB + UID片段 + BCC
            nvb = first
            valid_bits = nvb & 0x0F
            extra_info.append(f"  - NVB: 0x{nvb:02X} (有效比特数: {valid_bits})")
            uid_data = payload[1:5]  # UID片段4字节
            bcc = payload[5] if len(payload) > 5 else None
        else:
            # 格式B: UID片段 + BCC (无NVB)
            extra_info.append(f"  - 防冲突响应 (无NVB前缀, {len(payload)}B)")
            uid_data = payload[0:4]
            bcc = payload[4] if len(payload) > 4 else None

        if len(uid_data) >= 4:
            has_ct = (uid_data[0] == 0x88)
            if has_ct:
                extra_info.append(f"    - CT=0x88 (级联标签, 仅CL1/CL2片段首字节)")
                extra_info.append(f"    - UID片段 (剥离CT后): {uid_data[1:].hex().upper()}")
            else:
                extra_info.append(f"    - UID片段: {uid_data.hex().upper()}")
            if bcc is not None:
                extra_info.append(f"    - BCC: 0x{bcc:02X}")
                # BCC 校验: 异或所有UID片段字节
                calc_bcc = 0
                for b in uid_data:
                    calc_bcc ^= b
                extra_info.append(f"    - BCC校验: {'通过' if calc_bcc == bcc else '失败(0x'+format(calc_bcc, '02X')+')'}")

            # 存储 UID 片段 (含 CT 字节, 拼接时再剥离)
            if sel_code == 0x93:
                ISO14443AParser._uid_cl1 = bytes(uid_data)
            elif sel_code == 0x95:
                ISO14443AParser._uid_cl2 = bytes(uid_data)
            elif sel_code == 0x97:
                ISO14443AParser._uid_cl3 = bytes(uid_data)

            # 添加命令描述 (供 commands 列表使用)
            if commands is not None:
                ct_tag = " (含CT=0x88)" if has_ct else ""
                commands.append(
                    f"{cl_short} 防冲突响应 (UID片段: {uid_data.hex().upper()}{ct_tag}, BCC: 0x{bcc:02X})"
                    if bcc is not None else
                    f"{cl_short} 防冲突响应 (UID片段: {uid_data.hex().upper()}{ct_tag})"
                )

    @staticmethod
    def _concatenate_full_uid():
        """根据已存储的 _uid_cl1/cl2/cl3 拼接完整 UID (剥离 CT 字节)

        返回: 完整 UID (bytes) 或 None
        """
        parts = []
        # CL1 片段: 若以 0x88 开头, 剥离首字节 (CT 级联标签)
        if ISO14443AParser._uid_cl1 is not None:
            cl1 = ISO14443AParser._uid_cl1
            if len(cl1) == 4 and cl1[0] == 0x88:
                parts.append(cl1[1:])
            else:
                parts.append(cl1)
        # CL2/CL3 片段: 同样剥离可能的 CT 字节
        for frag in (ISO14443AParser._uid_cl2, ISO14443AParser._uid_cl3):
            if frag is not None:
                if len(frag) == 4 and frag[0] == 0x88:
                    parts.append(frag[1:])
                else:
                    parts.append(frag)
        if not parts:
            return None
        return b''.join(parts)

    @staticmethod
    def parse_valid_bits_info(direction_byte, valid_bits, extra_info):
        if direction_byte == ord('<'):
            if valid_bits == 0:
                desc = "8 bits (完整字节)"
            else:
                desc = f"{valid_bits} bits (部分字节)"
            extra_info.append(f"  - 有效位: {valid_bits} ({desc})")
        elif direction_byte == ord('>'):
            if valid_bits == 0:
                desc = "8 bits + 校验位"
            else:
                desc = f"{valid_bits} bits (无校验位)"
            extra_info.append(f"  - 有效位: {valid_bits} ({desc})")
        else:
            extra_info.append(f"  - 有效位: {valid_bits}")

    @staticmethod
    def parse_14443_3_command(cmd, payload, data_len, result):
        cmd_desc = ""
        sel_code = cmd

        if cmd == ISO14443AParser.CMD_REQA:
            cmd_desc = "REQA (Request All Cards)"
        elif cmd == ISO14443AParser.CMD_WUPA:
            cmd_desc = "WUPA (Wake Up All Cards)"
        elif cmd == ISO14443AParser.CMD_HLTA:
            cmd_desc = "HLTA (Halt)"
        elif cmd in (ISO14443AParser.CMD_ANTICOLLISION_CL1,
                     ISO14443AParser.CMD_ANTICOLLISION_CL2,
                     ISO14443AParser.CMD_ANTICOLLISION_CL3):
            # ANTICOLLISION 和 SELECT 共用 0x93/0x95/0x97, 通过 NVB 区分:
            #   NVB=0x70 -> SELECT (完整选择)
            #   其他     -> ANTICOLLISION
            # 记录 SEL 码, 用于后续 PICC 防冲突响应的级联级别判定
            ISO14443AParser._last_sel_code = cmd
            if data_len >= 2:
                nvb = payload[1]
                bits = nvb & 0x0F
                uid_part = payload[2:] if data_len > 2 else b''

                cl_desc = ""
                if cmd == 0x93:
                    cl_desc = "级联级别1 (CL1)"
                elif cmd == 0x95:
                    cl_desc = "级联级别2 (CL2)"
                elif cmd == 0x97:
                    cl_desc = "级联级别3 (CL3)"

                if nvb == 0x70:
                    # SELECT: 发送完整 UID 进行选择
                    uid = payload[2:6] if data_len >= 6 else b''
                    cmd_desc = f"SELECT (SEL=0x{cmd:02X} {cl_desc}, UID: {uid.hex().upper()})"
                    if len(uid) == 4:
                        result["extra_info"].append(f"  - UID片段 (SELECT): {uid.hex().upper()}")
                        if uid[0] == 0x88:
                            result["extra_info"].append(f"    - CT=0x88 (级联标签, 剥离后 UID片段: {uid[1:4].hex().upper()})")
                else:
                    # ANTICOLLISION: 防冲突循环
                    cmd_desc = f"ANTICOLLISION (SEL=0x{cmd:02X} {cl_desc}, NVB=0x{nvb:02X}, bits={bits})"
                    if bits > 0 and len(uid_part) > 0:
                        result["extra_info"].append(f"  - UID片段 (部分): {uid_part.hex().upper()}")
                        if uid_part[0] == 0x88:
                            result["extra_info"].append(f"    - CT=0x88 (级联标签)")
            else:
                cmd_desc = "ANTICOLLISION/SELECT (incomplete)"
        else:
            return None, sel_code

        return cmd_desc, sel_code

    @staticmethod
    def parse_other_commands(cmd, payload, data_len, result):
        cmd_desc = ""
        
        # 尝试解析14443-4命令 (RATS, PPS)
        if cmd == ISO14443AParser.CMD_RATS:
            if data_len >= 2:
                param = payload[1]
                fsdi = (param >> 4) & 0x0F
                cid = param & 0x0F
                fsd = ATQAParser.get_frame_size(fsdi)
                fsd_str = f"{fsd}" if fsd else "RFU/未知"
                cmd_desc = f"RATS (FSDI={fsdi} -> FSD={fsd_str} bytes, CID={cid})"
                result["extra_info"].append(f"  - RATS (Request for Answer To Select):")
                result["extra_info"].append(f"    - FSDI={fsdi} (b8..b5 of param byte)")
                result["extra_info"].append(f"      - FSD (最大帧大小): {fsd_str} bytes")
                result["extra_info"].append(f"    - CID={cid} (b4..b1 of param byte)")
                if cid == 0x0F:
                    result["extra_info"].append(f"      - CID=0xF: 保留值")
                result["extra_info"].append(f"    - 14443-4: RATS 请求, 期望 PICC 返回 ATS")
            else:
                cmd_desc = "RATS (incomplete)"
            return cmd_desc
        
        elif cmd == ISO14443AParser.CMD_PPS:
            if data_len >= 2:
                pps0 = payload[1]
                cmd_desc = f"PPS (PPS0=0x{pps0:02X})"
                if pps0 & 0x01:
                    result["extra_info"].append("  - PPS1 present")
                if pps0 & 0x0E:
                    rate = (pps0 >> 1) & 0x07
                    rate_desc = ["106 kbps", "212 kbps", "424 kbps", "848 kbps"]
                    rate_str = rate_desc[rate] if rate < len(rate_desc) else f"Reserved ({rate})"
                    result["extra_info"].append(f"  - 请求速率: D={rate} ({rate_str})")
                result["extra_info"].append(f"  - 14443-4: PPS Negotiation")
            else:
                cmd_desc = "PPS (incomplete)"
            return cmd_desc
        
        # 尝试Mifare Classic
        cmd_desc = MifareClassicParser.parse(cmd, payload, data_len, result)
        if cmd_desc:
            return cmd_desc
        
        # 尝试Mifare Ultralight
        cmd_desc = MifareUltralightParser.parse(cmd, payload, data_len, result)
        if cmd_desc:
            return cmd_desc
        
        # 尝试FM1208
        cmd_desc = FM1208Parser.parse(cmd, payload, data_len, result)
        if cmd_desc:
            return cmd_desc
        
        # 尝试DESFire
        cmd_desc = DESFireParser.parse(cmd, payload, data_len, result)
        if cmd_desc:
            return cmd_desc
        
        return f"未知命令 (0x{cmd:02X})"

    @staticmethod
    def parse_ats(payload, data_len, result):
        """解析ATS (Answer To Select, RATS响应)
        ATS 结构 (ISO 14443-4):
          TL (1B, 必选) : ATS 总长度 (含 TL)
          T0 (1B)       : b8=TA1存在, b7=TB1存在, b6=TC1存在, b5=RFU, b4..b1=FSCI
          TA1(1B, 可选) : 速率支持 (DS/DR 位掩码)
          TB1(1B, 可选) : 帧等待时间 (FWI + SFGI)
          TC1(1B, 可选) : 协议参数 (CID/NAD 支持)
          历史字节(可选) : 卡片厂商/状态信息 (可能含 Compact TLV)
        """
        if not payload or data_len < 2:
            result["extra_info"].append("  - ATS (Answer To Select):")
            result["extra_info"].append("    - ATS 太短, 无 T0, 放弃解析")
            return False

        tl = payload[0]
        result["extra_info"].append(f"  - ATS (Answer To Select):")
        result["extra_info"].append(f"    - TL={tl} (ATS总长度, 含TL本身)")

        # TL 至少为 2 (TL + T0)。任何越界/零长度都必须在访问 payload 前拒绝,
        # 返回 False 由调用方保持 14443-3 状态, 避免畸形 ATS 激活 T=CL。
        if tl < 2:
            result["extra_info"].append(f"    - 警告: TL={tl} 非法 (至少为 2), 放弃解析")
            return False
        if tl > data_len:
            result["extra_info"].append(
                f"    - 警告: TL({tl}) 超出缓冲区实际长度({data_len}), 放弃解析")
            return False

        # TL 应等于 data_len (无CRC) 或 data_len-2 (含CRC);
        # 其他情况按 TL 截断解析并记录警告 (绝不越过 payload)。
        ats_end = tl  # 有效 ATS 数据结束位置 (payload[0..tl-1])
        if tl == data_len:
            has_crc = False
        elif tl == data_len - 2:
            has_crc = True
            result["extra_info"].append(f"    - CRC: 2字节 (末尾)")
        else:
            has_crc = False
            result["extra_info"].append(
                f"    - 警告: TL({tl}) != 实际长度({data_len}) 或 {data_len-2}(含CRC), 使用TL 截断")

        # ---------- T0 ----------
        t0 = payload[1]
        fsci = t0 & 0x0F
        fsc = ATQAParser.get_frame_size(fsci)
        fsc_str = f"{fsc}" if fsc else "RFU/未知"

        # T0 位掩码: b8=TA1, b7=TB1, b6=TC1, b5=RFU
        ta_present = (t0 & 0x80) != 0
        tb_present = (t0 & 0x40) != 0
        tc_present = (t0 & 0x20) != 0

        result["extra_info"].append(f"    - T0: 0x{t0:02X}")
        result["extra_info"].append(f"      - FSCI={fsci} -> FSC (最大帧大小)={fsc_str} bytes")
        result["extra_info"].append(f"      - b8 (0x80): {'1 → TA1存在' if ta_present else '0 → 无TA1'}")
        result["extra_info"].append(f"      - b7 (0x40): {'1 → TB1存在' if tb_present else '0 → 无TB1'}")
        result["extra_info"].append(f"      - b6 (0x20): {'1 → TC1存在' if tc_present else '0 → 无TC1'}")

        ats_info = {
            "hex": bytes(payload[:ats_end]).hex().upper(),
            "tl": tl,
            "t0": t0,
            "fsci": fsci,
            "fsc": fsc,
            "ta1": None,
            "tb1": None,
            "tc1": None,
            "history_hex": "",
        }
        result["ats"] = ats_info


        pos = 2  # 当前解析位置 (跳过 TL, T0)

        # ---------- TA1: 速率支持 ----------
        if ta_present and pos < ats_end:
            ta1 = payload[pos]; pos += 1
            ats_info["ta1"] = ta1
            result["extra_info"].append(f"    - TA1: 0x{ta1:02X} (速率支持)")
            # DS (Divisor Send, PCD→PICC): b7..b4
            ds_rates = []
            if ta1 & 0x10: ds_rates.append("106 kbit/s")
            if ta1 & 0x20: ds_rates.append("212 kbit/s")
            if ta1 & 0x40: ds_rates.append("424 kbit/s")
            if ta1 & 0x80: ds_rates.append("848 kbit/s")
            # DR (Divisor Receive, PICC→PCD): b3..b0
            dr_rates = []
            if ta1 & 0x01: dr_rates.append("106 kbit/s")
            if ta1 & 0x02: dr_rates.append("212 kbit/s")
            if ta1 & 0x04: dr_rates.append("424 kbit/s")
            if ta1 & 0x08: dr_rates.append("848 kbit/s")
            result["extra_info"].append(f"      - DS (PCD→PICC): {', '.join(ds_rates) if ds_rates else '仅106 kbit/s (默认)'}")
            result["extra_info"].append(f"      - DR (PICC→PCD): {', '.join(dr_rates) if dr_rates else '仅106 kbit/s (默认)'}")

        # ---------- TB1: 帧等待时间 ----------
        if tb_present and pos < ats_end:
            tb1 = payload[pos]; pos += 1
            ats_info["tb1"] = tb1
            fwi = tb1 & 0x0F
            sfgi = (tb1 >> 4) & 0x0F
            # FWT = 256 * 16 * (2^FWI) / fc, fc=13.56MHz
            #     = 4096 * (2^FWI) / 13560000 秒 = (2^FWI) * 302.06 μs
            fc = 13560000
            fwt_us = (4096 * (1 << fwi)) / fc * 1e6 if fwi <= 14 else 0
            sfgt_us = (4096 * (1 << sfgi)) / fc * 1e6 if sfgi <= 14 else 0

            result["extra_info"].append(f"    - TB1: 0x{tb1:02X} (帧等待时间)")
            result["extra_info"].append(f"      - FWI={fwi} (b4..b1)")
            if fwt_us > 0:
                result["extra_info"].append(f"        - FWT (帧等待时间) = {fwt_us:.1f} μs ≈ {fwt_us/1000:.3f} ms")
            else:
                result["extra_info"].append(f"        - FWT: RFU")
            result["extra_info"].append(f"      - SFGI={sfgi} (b8..b5)")
            if sfgi == 0:
                result["extra_info"].append(f"        - SFGT (启动帧保护时间) = 0 (最小)")
            elif sfgt_us > 0:
                result["extra_info"].append(f"        - SFGT (启动帧保护时间) = {sfgt_us:.1f} μs ≈ {sfgt_us/1000:.3f} ms")

        # ---------- TC1: 协议参数 ----------
        if tc_present and pos < ats_end:
            tc1 = payload[pos]; pos += 1
            ats_info["tc1"] = tc1
            # CID: b3 (0x04), NAD: b2 (0x02)
            cid_support = (tc1 & 0x04) != 0
            nad_support = (tc1 & 0x02) != 0
            result["extra_info"].append(f"    - TC1: 0x{tc1:02X} (协议参数)")
            result["extra_info"].append(f"      - b3 (0x04): {'1 → 支持CID' if cid_support else '0 → 不支持CID'}")
            result["extra_info"].append(f"      - b2 (0x02): {'1 → 支持NAD' if nad_support else '0 → 不支持NAD'}")

        # ---------- 历史字节 ----------
        if pos < ats_end:
            hist_bytes = payload[pos:ats_end]
            ats_info["history_hex"] = bytes(hist_bytes).hex().upper()
            result["extra_info"].append(f"    - 历史字节 ({len(hist_bytes)}B): {hist_bytes.hex().upper()}")
            result["extra_info"].append(f"      - 历史字节中可能包含卡片厂商信息")
            # Compact TLV 解析 (ISO 7816-4)
            # 首字节 0x80 = 类别指示符, 后续为 Compact TLV
            if len(hist_bytes) >= 2 and hist_bytes[0] == 0x80:
                result["extra_info"].append(f"      - 类别指示符: 0x80 (Compact TLV 格式)")
                idx = 1
                while idx < len(hist_bytes):
                    tag = (hist_bytes[idx] >> 4) & 0x0F
                    length = hist_bytes[idx] & 0x0F
                    tag_names = {
                        0x0: "未知",
                        0x1: "国家代码",
                        0x2: "发行者标识",
                        0x3: "卡片服务数据",
                        0x4: "初始访问数据",
                        0x5: "卡片发行者数据",
                        0x6: "序列号",
                        0x7: "ROM / 内存容量",
                        0x8: "芯片信息",
                    }
                    if length == 0 and tag == 0:
                        break
                    tag_desc = tag_names.get(tag, f"Tag={tag}")
                    if idx + 1 + length <= len(hist_bytes):
                        value = hist_bytes[idx+1:idx+1+length]
                        result["extra_info"].append(f"        - T={tag}({tag_desc}), L={length}, V={value.hex().upper()}")
                    else:
                        result["extra_info"].append(f"        - T={tag}({tag_desc}), L={length} (超出范围)")
                        break
                    idx += 1 + length

                # FM1208/CPU卡检测
                result["extra_info"].append(f"      - 检测到CPU卡标识 (0x80 类别指示符)")
                result["extra_info"].append(f"      - 可能是FM1208或其他ISO 14443-4 CPU卡")

        result["extra_info"].append(f"    - 检测到14443-4协议激活")
        result["extra_info"].append(f"    - 后续数据交换需使用I-block封装APDU")
        result["extra_info"].append(f"    - 帧结构: [PCB][CID][NAD][APDU数据][CRC]")
        ISO14443AParser._last_ats = ats_info
        return True

    @staticmethod
    def _looks_like_ats(payload, data_len):
        """快速判断载荷是否为 ATS (而非 I-block 响应)

        ATS 结构特征:
          - TL (首字节) = ATS 总长度 (含 TL), 合理范围 4~32
          - TL 应与 data_len 一致 (无CRC) 或 data_len-2 一致 (含CRC)
          - T0 (第二字节) 的 b5 (0x10) 应为 0 (RFU)
          - T0 的 FSCI (b4..b1) 应在 0~8 或 0xF
        """
        if data_len < 2:
            return False
        tl = payload[0]
        if tl < 4 or tl > 32:
            return False
        # TL 应与 data_len 一致 (无CRC) 或 data_len-2 一致 (含CRC)
        if tl != data_len and tl != data_len - 2:
            return False
        t0 = payload[1]
        # T0 b5 (0x10) 应为 0 (RFU)
        if (t0 & 0x10) != 0:
            return False
        # FSCI 应在 0~8 或 0xF
        fsci = t0 & 0x0F
        if fsci > 0x08 and fsci != 0x0F:
            return False
        return True

    @staticmethod
    def get_card_protocol_hint():
        """依据最近一次 SAK 推导卡片级协议提示 (供 Mifare 1 分层判定使用)。

        - SAK b4 (0x08) = 1 : 支持 CRYPTO1 → 'mifare'
        - SAK b4=0 且 b6 (0x20) = 1 : 纯 T=CL → 'tcl'
        - SAK = 0x00 / 0x44 : Ultralight / NTAG → 'native'
        - 其余 (含尚未收到 SAK) : 'unknown'
        """
        sak = ISO14443AParser._last_sak
        if sak is None:
            return RawDataReconstructor.HINT_UNKNOWN
        if sak & 0x08:
            return RawDataReconstructor.HINT_MIFARE
        if sak & 0x20:
            return RawDataReconstructor.HINT_TCL
        if sak in (0x00, 0x44):
            return RawDataReconstructor.HINT_NATIVE
        return RawDataReconstructor.HINT_UNKNOWN

    @staticmethod
    def parse_packet(packet_bytes):
        """外部入口: 单个数据包的异常隔离。

        解析器的所有边界错误已在内部修复; 此包装用于保证任何未预料的
        畸形输入都不会向串口/UI 线程抛出异常。失败结果包含 error_type /
        error_detail (traceback), 便于记录, 而不是静默忽略。
        """
        try:
            return ISO14443AParser._parse_packet_impl(packet_bytes)
        except Exception as exc:
            try:
                pkt_hex = bytes(packet_bytes).hex().upper()
            except Exception:
                pkt_hex = repr(packet_bytes)
            return {
                "error": f"解析异常: {type(exc).__name__}: {exc}",
                "error_type": type(exc).__name__,
                "error_detail": traceback.format_exc(limit=8),
                "packet_hex": pkt_hex,
                "commands": [],
                "extra_info": ["包解析异常, 已隔离; 后续数据可继续解析"],
                "payload_len": 0,
                "payload_hex": "",
                "direction": "未知方向",
                "direction_short": "?",
                "raw_payload_hex": "",
            }

    @staticmethod
    def _parse_packet_impl(packet_bytes):
        """
        解析完整数据包。
        数据格式：[方向1][时间戳4][长度2][有效位1][数据N]
        """
        if len(packet_bytes) < 8:
            return {"error": "数据包太短，至少需要8字节头部（方向1+时间戳4+长度2+有效位1）"}

        direction_byte = packet_bytes[0]
        timestamp_raw = struct.unpack('>I', packet_bytes[1:5])[0]
        data_len = struct.unpack('>H', packet_bytes[5:7])[0]
        valid_bits = packet_bytes[7]
        raw_payload = packet_bytes[8:8+data_len]

        if len(raw_payload) != data_len:
            return {"error": f"实际载荷长度({len(raw_payload)})与声明长度({data_len})不符"}

        # 解析方向
        if direction_byte == ord('>'):
            direction_str = "PCD -> PICC (阅读器->卡片)"
            direction_short = ">"
        elif direction_byte == ord('<'):
            direction_str = "PICC -> PCD (卡片->阅读器)"
            direction_short = "<"
        else:
            direction_str = f"未知方向 (0x{direction_byte:02X})"
            direction_short = "?"

        # 解析时间戳
        ts_display, ts_value, ts_zero = ISO14443AParser.parse_timestamp(timestamp_raw)

        result = {
            "direction_short": direction_short,
            "direction": direction_str,
            "direction_byte": direction_byte,
            "timestamp_raw": timestamp_raw,
            "timestamp_display": ts_display,
            "timestamp_value": ts_value,
            "timestamp_zero": ts_zero,
            "valid_bits": valid_bits,
            "raw_payload_hex": raw_payload.hex().upper(),
            "payload_hex": "",          # 还原后的数据 (赋值于下方)
            "payload_len": 0,           # 还原后的数据长度
            "commands": [],
            "extra_info": [],
            "card_type": None,
            # ---- 原始数据还原相关字段 (v2) ----
            "reconstruct_parity_ok": None,
            "reconstruct_crc_ok": None,
            "reconstruct_is_7bit": False,
            "reconstruct_is_mifare1": False,
            "reconstruct_mifare_kind": None,
            "reconstruct_mifare_reason": None,
            "reconstruct_parity_bits": [],
            "reconstruct_info": [],
        }

        # ====================================================================
        # 原始数据还原 (v2 新增)
        #   - 将含校验位的原始位流还原为纯数据字节
        #   - 检查校验位、CRC16-A
        #   - 识别 7-bit 命令、Mifare 1 数据
        # ====================================================================
        result["extra_info"].append("=== 原始数据还原 ===")
        result["extra_info"].append(f"  - 原始载荷 ({data_len}B): {raw_payload.hex().upper()}")
        result["extra_info"].append(f"  - 末字节有效位: {valid_bits} (1-8, 8=完整字节)")

        if data_len == 0:
            result["commands"].append("空数据")
            result["payload_len"] = 0
            result["payload_hex"] = ""
            return result

        recon = RawDataReconstructor.reconstruct(
            raw_payload, valid_bits, direction_byte,
            card_hint=ISO14443AParser.get_card_protocol_hint(),
            mifare_session=ISO14443AParser._mifare_active,
            tcl_session=(ISO14443AParser._tcl_active
                         or ISO14443AParser._waiting_for_ats))
        payload = recon["data_bytes"]
        data_len = len(payload)
        result["payload_hex"] = payload.hex().upper()
        result["payload_len"] = data_len
        result["reconstruct_parity_ok"] = recon["parity_ok"]
        result["reconstruct_crc_ok"] = recon["crc_ok"]
        result["reconstruct_is_7bit"] = recon["is_7bit_cmd"]
        result["reconstruct_is_mifare1"] = recon["is_mifare1"]
        result["reconstruct_mifare_kind"] = recon["mifare_kind"]
        result["reconstruct_mifare_reason"] = recon["mifare_reason"]
        result["reconstruct_parity_bits"] = recon["parity_bits"]
        result["reconstruct_info"] = recon["info"]

        # 输出还原过程信息
        for line in recon["info"]:
            result["extra_info"].append(line)

        # 数据与校验位上下两行排列 (readme 第 5 条)
        data_row, parity_row = RawDataReconstructor.format_data_parity_rows(
            payload, recon["parity_bits"])
        result["extra_info"].append(f"  - 数据位: {data_row}")
        result["extra_info"].append(f"  - 校验位: {parity_row}")
        result["extra_info"].append(f"  - 还原后数据 ({data_len}B): {payload.hex().upper()}")

        # 7-bit 命令: 直接解析，不走后续状态机
        # 使用左移前的原始 7-bit 值 (recon["cmd_7bit_value"]) 识别命令
        if recon["is_7bit_cmd"]:
            cmd_7 = recon.get("cmd_7bit_value")
            if cmd_7 is None:
                cmd_7 = payload[0] >> 1 if data_len >= 1 else 0
            if cmd_7 == 0x26:
                result["commands"].append("REQA (7-bit, Request All Cards)")
                ISO14443AParser.reset_protocol_state()
            elif cmd_7 == 0x52:
                result["commands"].append("WUPA (7-bit, Wake Up All Cards)")
                ISO14443AParser.reset_protocol_state()
            else:
                result["commands"].append(f"7-bit 命令 (0x{cmd_7:02X})")
            return result

        if data_len == 0:
            result["commands"].append("空数据")
            return result

        cmd = payload[0]

        # ====================================================================
        # 协议分层状态机
        #   14443-3 阶段: REQA/WUPA/HLTA/ANTICOLLISION/SELECT + ATQA/SAK
        #                 -> 不解析 T=CL block, 不解析 APDU
        #   14443-4 阶段: 收到 ATS 响应后 (_tcl_active=True)
        #                 -> 解析 I/R/S-block + APDU/FM1208
        #   Mifare 阶段:  收到 AUTH(60/61) 后 (_mifare_active=True)
        #                 -> 不解析命令内容, 仅按长度判断数据/认证过程
        # ====================================================================

        # ---------- 状态退出: REQA/WUPA/HLTA 重置所有协议状态 ----------
        if direction_byte == ord('>') and cmd in (ISO14443AParser.CMD_REQA,
                                                   ISO14443AParser.CMD_WUPA,
                                                   ISO14443AParser.CMD_HLTA):
            ISO14443AParser.reset_protocol_state()

        # ====================================================================
        # PCD → PICC (阅读器 → 卡片)
        # ====================================================================
        if direction_byte == ord('>'):

            # 新的 PCD 帧到来: 之前的"未认证 READ 探针"已超时/被覆盖
            ISO14443AParser._mifare_probe_pending = False

            # ---- RATS: 允许从 Mifare / 原生模式切回 ISO 14443-4 ----
            # 必须先于 Mifare/Native 的"吞掉所有命令"分支处理。
            if cmd == ISO14443AParser.CMD_RATS:
                ISO14443AParser.reset_mifare_state()
                ISO14443AParser.reset_native_state()
                ISO14443AParser._tcl_active = False
                ISO14443AParser._waiting_for_ats = True
                cmd_desc = ISO14443AParser.parse_other_commands(cmd, payload, data_len, result)
                result["commands"].append(cmd_desc)
                return result

            # ---- Mifare 数据阶段 (WRITE/INC/DEC/RESTORE ACK 后的数据) ----
            if ISO14443AParser._mifare_active and ISO14443AParser._mifare_pending_data:
                ISO14443AParser._mifare_pending_data = False
                ISO14443AParser._mifare_data_sent = True
                last_cmd = ISO14443AParser._mifare_last_cmd
                last_block = ISO14443AParser._mifare_last_block
                block_disp = (last_block if last_block is not None
                              else "未知(密文)")
                result["extra_info"].append(f"  - Mifare 数据 ({data_len}B): {payload.hex().upper()}")
                cmd_name = {MifareClassicParser.CMD_WRITE: "Write",
                            MifareClassicParser.CMD_INCREMENT: "Increment",
                            MifareClassicParser.CMD_DECREMENT: "Decrement",
                            MifareClassicParser.CMD_RESTORE: "Restore"}.get(last_cmd, "")
                result["commands"].append(f"Mifare {cmd_name} 数据 (block={block_disp}, {data_len}B)"
                                          if cmd_name else f"Mifare 数据 ({data_len}B)")
                return result

            # ---- Mifare AUTH (0x60/0x61): 激活 Mifare 状态 ----
            # 仅在 14443-3 阶段 (非 T=CL) 才触发 Mifare 分支。
            # 0x60 的歧义:
            #   - GET_VERSION: 1 字节 (无 CRC) 或 3 字节 (60 + CRC)
            #   - AUTH_A     : 2 字节 (无 CRC) 或 4 字节 (60 + block + CRC)
            # 因此 1/3 字节的 0x60 必须留给原生卡分支, 不能按 AUTH 处理。
            if (not ISO14443AParser._tcl_active
                    and not ISO14443AParser._native_active
                    and data_len >= 2
                    and not (cmd == MifareClassicParser.CMD_AUTH_A
                             and data_len in (1, 3))
                    and cmd in (MifareClassicParser.CMD_AUTH_A, MifareClassicParser.CMD_AUTH_B)):
                ISO14443AParser._mifare_active = True
                ISO14443AParser._mifare_auth_step = 1
                ISO14443AParser._mifare_last_cmd = cmd
                ISO14443AParser._mifare_last_block = payload[1] if data_len >= 2 else None
                ISO14443AParser._mifare_pending_data = False
                ISO14443AParser._mifare_data_sent = False
                auth_name = "Auth A" if cmd == MifareClassicParser.CMD_AUTH_A else "Auth B"
                result["commands"].append(f"Mifare {auth_name} (block={ISO14443AParser._mifare_last_block})")
                return result

            # ---- Mifare 活动中: 不解析命令内容, 仅按状态/长度判断 ----
            if ISO14443AParser._mifare_active:
                if ISO14443AParser._mifare_auth_step == 2:
                    # 加密挑战 (AUTH 第3步)
                    ISO14443AParser._mifare_auth_step = 3
                    ISO14443AParser._mifare_last_cmd = None
                    result["extra_info"].append(f"  - Mifare 加密挑战 ({data_len}B): {payload.hex().upper()}")
                    result["commands"].append(f"Mifare 认证挑战 (加密数据, {data_len}B)")
                elif ISO14443AParser._mifare_auth_step == 0:
                    # 认证后数据命令: 命令码/块号/数据均已被 Crypto1 加密,
                    # 不能把 payload[1] 当作明文块号, 明确标记为未知。
                    ISO14443AParser._mifare_last_cmd = cmd
                    ISO14443AParser._mifare_last_block = None
                    ISO14443AParser._mifare_data_sent = False
                    result["extra_info"].append(f"  - Mifare 命令 (0x{cmd:02X}, {data_len}B): {payload.hex().upper()}")
                    result["extra_info"].append("  - 块号: 未知 (Crypto1 密文)")
                    result["commands"].append(f"Mifare 命令 (0x{cmd:02X}, {data_len}B, block=未知/密文)")
                return result

            # ---- 14443-3 命令解析 (仅在 14443-3 阶段) ----
            # 注意: REQA/WUPA/HLTA 已在最前面处理并重置状态
            # 在 T=CL 阶段, 非退出命令不应被解析为 14443-3 (如 0x93 应按 R-block 处理)
            if not ISO14443AParser._tcl_active and not ISO14443AParser._native_active:
                cmd_desc, sel_code = ISO14443AParser.parse_14443_3_command(cmd, payload, data_len, result)
                if cmd_desc is not None:
                    result["commands"].append(cmd_desc)
                    return result

            # ---- 原生卡 (NTAG/Ultralight) 模式: 第3类分支 ----
            # 进入条件: 14443-3 阶段 (非 T=CL/Mifare) 收到 NTAG 命令且无前置 AUTH
            #   0x60(单字节=GET_VERSION) / 0x30(READ) / 0xA2(WRITE) / 0xA0(5字节=COMPAT_WRITE)
            #   0x1B(PWD_AUTH) / 0x1A(Ultralight C 3DES 认证) / 0x39(READ_CNT) / 0x3C(READ_SIG) / 0x4B(VCSL) 等
            # 退出条件: REQA/WUPA/HLTA (已在最前面处理) 或 RATS
            if ISO14443AParser._native_active:
                # 已在原生卡模式: 用 MifareUltralightParser 解析
                ISO14443AParser._native_last_cmd = cmd
                ISO14443AParser._native_last_page = payload[1] if data_len >= 2 else None
                cmd_desc = MifareUltralightParser.parse(cmd, payload, data_len, result)
                result["commands"].append(cmd_desc if cmd_desc else f"NTAG 命令 (0x{cmd:02X})")
                return result

            # ---- 未认证 MIFARE READ 探针 (0x30) ----
            # 0x30 同时是 NTAG READ 与 M1 READ。M1 在未认证时会回 NAK 0x04,
            # 之后仍可正常 AUTH; 因此不能仅凭 0x30 就进入原生卡模式。
            # 只有 SAK 明确提示 native (0x00/0x44) 时才交给下面的原生卡分支。
            if (not ISO14443AParser._tcl_active
                    and not ISO14443AParser._native_active
                    and cmd == MifareClassicParser.CMD_READ
                    and data_len >= 2):
                hint = ISO14443AParser.get_card_protocol_hint()
                if hint != RawDataReconstructor.HINT_NATIVE:
                    ISO14443AParser._mifare_probe_pending = True
                    ISO14443AParser._mifare_last_cmd = cmd
                    ISO14443AParser._mifare_last_block = payload[1]
                    result["extra_info"].append(
                        f"  - MIFARE READ 探针 (未认证): block={payload[1]}")
                    result["extra_info"].append(
                        f"  - 当前卡片提示: {hint}; 等待 NAK/ACK, 不切换原生卡模式")
                    result["commands"].append(
                        f"Mifare Read (block={payload[1]}, 未认证, 等待响应)")
                    return result

            if (not ISO14443AParser._tcl_active
                    and MifareUltralightParser.is_native_command(cmd, data_len)):
                # 进入原生卡模式
                ISO14443AParser._native_active = True
                ISO14443AParser._native_last_cmd = cmd
                ISO14443AParser._native_last_page = payload[1] if data_len >= 2 else None
                result["extra_info"].append(f"  - 进入原生卡 (NTAG/Ultralight) 模式")
                cmd_desc = MifareUltralightParser.parse(cmd, payload, data_len, result)
                result["commands"].append(cmd_desc if cmd_desc else f"NTAG 命令 (0x{cmd:02X})")
                return result

            # ---- 14443-4 过渡命令: PPS ----
            # RATS 已在上方优先处理 (允许从 Mifare/Native 切回 T=CL)。
            # PPS 可在 ATS 前后出现 (14443-4 速率协商)
            if cmd == ISO14443AParser.CMD_PPS:
                cmd_desc = ISO14443AParser.parse_other_commands(cmd, payload, data_len, result)
                result["commands"].append(cmd_desc)
                return result

            # ---- 14443-4 T=CL 阶段: 解析 I/R/S-block + APDU ----
            if ISO14443AParser._tcl_active:
                pcb = payload[0]
                if (pcb & 0xC0) == 0x00:
                    i_block_result = ISO14443AParser.parse_i_block(payload, result["extra_info"], direction_byte)
                    if "error" not in i_block_result:
                        result["commands"].append("I-block (信息块, 命令)")
                elif (pcb & 0xC0) == 0x80:
                    r_type = ISO14443AParser.parse_r_block(payload, result["extra_info"])
                    result["commands"].append(f"R-block ({r_type or '未知'})")
                elif (pcb & 0xC0) == 0xC0:
                    ISO14443AParser.parse_s_block(payload, result["extra_info"])
                    result["commands"].append("S-block (控制块)")
                else:
                    result["commands"].append(f"未知 T=CL 命令 (0x{cmd:02X})")
                return result

            # ---- 14443-3 阶段: 非标准命令, 不解析 block ----
            cmd_desc = ISO14443AParser.parse_other_commands(cmd, payload, data_len, result)
            result["commands"].append(cmd_desc)
            return result

        # ====================================================================
        # PICC → PCD (卡片 → 阅读器)
        # ====================================================================
        else:
            # ---- 未认证 MIFARE READ 的响应 (探针) ----
            # 关键: 1 字节 NAK 不能被当成 SAK, 也不能切换到原生卡模式。
            if ISO14443AParser._mifare_probe_pending:
                ISO14443AParser._mifare_probe_pending = False
                last_block = ISO14443AParser._mifare_last_block
                if data_len == 1:
                    code = payload[0]
                    if code == 0x0A:
                        result["extra_info"].append("  - Mifare READ 响应: ACK (0x0A)")
                        result["commands"].append("Mifare READ ACK (成功)")
                    else:
                        desc = ISO14443AParser._get_mifare_nak_desc(code)
                        result["extra_info"].append(
                            f"  - Mifare READ 响应: NAK (0x{code:02X} - {desc})")
                        result["commands"].append(
                            f"Mifare READ NAK (0x{code:02X} {desc})")
                elif data_len >= 16:
                    result["extra_info"].append(
                        f"  - Mifare READ 响应: 块数据 (block={last_block}, "
                        f"{data_len}B): {payload.hex().upper()}")
                    result["commands"].append(
                        f"Mifare READ 响应 (block={last_block}, {data_len}B)")
                else:
                    result["extra_info"].append(
                        f"  - Mifare READ 响应 ({data_len}B): {payload.hex().upper()}")
                    result["commands"].append(f"Mifare READ 响应 ({data_len}B)")
                return result

            # ---- 原生卡 (NTAG/Ultralight) 响应: 按命令解析 ----
            if ISO14443AParser._native_active:
                MifareUltralightParser.parse_response(
                    payload, data_len,
                    ISO14443AParser._native_last_cmd,
                    ISO14443AParser._native_last_page,
                    result)
                return result

            # ---- Mifare 响应: 按状态/长度判断, 不解析 T=CL ----
            if ISO14443AParser._mifare_active:
                # 先按当前步骤解析, 再推进状态机 (否则 step 1/3 分支不可达)
                step = ISO14443AParser._mifare_auth_step
                ISO14443AParser._parse_mifare_response(payload, data_len, result)
                if step == 1:
                    ISO14443AParser._mifare_auth_step = 2  # Nonce 已收
                elif step == 3:
                    ISO14443AParser._mifare_auth_step = 0  # 认证完成
                return result

            # ---- ATS 响应 (RATS 的响应): 进入 14443-4 阶段 ----
            if ISO14443AParser._waiting_for_ats:
                ISO14443AParser._waiting_for_ats = False
                ok = ISO14443AParser.parse_ats(payload, data_len, result)
                # 仅当 ATS 声明了 CRC (data_len - TL == 2) 且校验失败时拒绝;
                # 若 TL == data_len (嗅探器已剥离 CRC), 不因 CRC 失败而拒绝。
                ats_tl = payload[0] if payload else 0
                if (ok and data_len - ats_tl == 2
                        and result.get("reconstruct_crc_ok") is False):
                    result["extra_info"].append(
                        "  - 警告: ATS CRC16-A 校验失败, 不进入 T=CL")
                    ok = False
                if ok:
                    ISO14443AParser._tcl_active = True
                    result["commands"].append("ATS (Answer To Select, 进入 14443-4)")
                else:
                    ISO14443AParser._tcl_active = False
                    result["commands"].append("ATS 解析失败 (保持 14443-3)")
                return result

            # ---- 14443-4 T=CL 响应: 解析 I/R/S-block ----
            if ISO14443AParser._tcl_active:
                pcb = payload[0] if data_len >= 1 else 0
                if (pcb & 0xC0) == 0xC0:
                    ISO14443AParser.parse_s_block(payload, result["extra_info"])
                    result["commands"].append("S-block (控制块, 响应)")
                elif (pcb & 0xC0) == 0x80:
                    r_type = ISO14443AParser.parse_r_block(payload, result["extra_info"])
                    result["commands"].append(f"R-block ({r_type or '未知'}, 响应)")
                elif (pcb & 0xC0) == 0x00:
                    i_block_result = ISO14443AParser.parse_i_block(payload, result["extra_info"], ord('<'))
                    if "error" not in i_block_result:
                        result["commands"].append("I-block (信息块, 响应)")
                else:
                    result["commands"].append(f"未知 T=CL 响应 ({data_len}B)")
                return result

            # ---- 14443-3 响应: ATQA / SAK / 防冲突响应 ----
            # 兼容两种sniffer格式: 不含CRC (data_len=N) 和 含2B CRC (data_len=N+2)
            # ATQA: 2B (无CRC) 或 4B (含CRC)
            # SAK : 1B (无CRC) 或 3B (含CRC)
            # 防冲突响应: 5B+ (无CRC) 或 7B+ (含CRC), 已由 data_len >= 5 覆盖
            if data_len == 2 or data_len == 4:
                # ATQA 在空口上低字节先发 (LSB first)。payload[0] 是先收到的
                # b8..b1, payload[1] 是后收到的 b16..b9, 因此数值应为:
                #   atqa = payload[1] << 8 | payload[0]
                # 对 M1 1K 的空口字节 04 00 → 0x0004; NTAG 的 44 00 → 0x0044。
                atqa = (payload[1] << 8) | payload[0]
                ATQAParser.parse(atqa, result["extra_info"])
                ISO14443AParser._last_atqa = atqa
                # 仅 ATQA 时粗略识别
                card_type, hint = ISO14443AParser.identify_card_type(atqa=atqa)
                result["card_type"] = card_type
                ISO14443AParser._last_card_type = card_type
                result["extra_info"].append(f"  - 卡片类型 (ATQA): {card_type}")
                if data_len == 4:
                    result["extra_info"].append(f"  - 含2B CRC: {payload[2:4].hex().upper()}")
                result["commands"].append("ATQA (应答请求)")
            elif data_len == 1 or data_len == 3:
                sak = payload[0]
                SAKParser.parse(sak, result["extra_info"])
                ISO14443AParser._last_sak = sak
                if data_len == 3:
                    result["extra_info"].append(f"  - 含2B CRC: {payload[1:3].hex().upper()}")
                # 根据 _last_sel_code 标注本次 SAK 来自哪个 SELECT 级联
                sel = ISO14443AParser._last_sel_code
                if sel == 0x93:
                    cl_label = "CL1 SELECT 响应"
                elif sel == 0x95:
                    cl_label = "CL2 SELECT 响应"
                elif sel == 0x97:
                    cl_label = "CL3 SELECT 响应"
                else:
                    cl_label = "SELECT 响应"
                result["extra_info"].append(f"  - SAK 来源: {cl_label} (SEL=0x{sel:02X})" if sel else f"  - SAK 来源: {cl_label}")
                # 结合 ATQA + SAK 综合识别卡片类型
                card_type, hint = ISO14443AParser.identify_card_type(
                    atqa=ISO14443AParser._last_atqa, sak=sak)
                result["card_type"] = card_type
                ISO14443AParser._last_card_type = card_type
                result["extra_info"].append(f"  - 卡片类型 (ATQA+SAK): {card_type}")
                # 提示下一步协议分支 (mifare / tcl / native)
                if hint == "mifare":
                    result["extra_info"].append(f"  - 协议预测: 后续 AUTH(0x60/0x61) -> Mifare 模式")
                elif hint == "tcl":
                    result["extra_info"].append(f"  - 协议预测: 后续 RATS -> 14443-4 T=CL / FM1208 / DESFire")
                elif hint == "native":
                    result["extra_info"].append(f"  - 协议预测: 后续 READ/WRITE/GET_VERSION -> 原生卡 (NTAG/Ultralight) 模式")
                # SAK cascade bit 处理: 1=UID不完整需继续下一级联, 0=UID完整
                if sak & 0x04:
                    if sel == 0x93:
                        result["extra_info"].append(f"  - cascade bit=1: CL1 UID 不完整, 需继续 CL2 (SEL=0x95) 级联")
                    elif sel == 0x95:
                        result["extra_info"].append(f"  - cascade bit=1: CL2 UID 不完整, 需继续 CL3 (SEL=0x97) 级联")
                    else:
                        result["extra_info"].append(f"  - cascade bit=1: UID 不完整, 需继续下一级联")
                else:
                    # cascade bit 清零 → UID 完整, 拼接完整 UID 并显示
                    if sel == 0x95:
                        result["extra_info"].append(f"  - cascade bit=0: CL2 SELECT 完成, UID 级联结束 (7B UID)")
                    elif sel == 0x97:
                        result["extra_info"].append(f"  - cascade bit=0: CL3 SELECT 完成, UID 级联结束 (10B UID)")
                    elif sel == 0x93:
                        result["extra_info"].append(f"  - cascade bit=0: CL1 SELECT 完成, UID 完整 (4B UID, 无级联)")
                    full_uid = ISO14443AParser._concatenate_full_uid()
                    if full_uid:
                        ISO14443AParser._uid_full = full_uid
                        # 显示完整 UID 并按 CL 级别分段
                        result["extra_info"].append(
                            f"  - 完整 UID ({len(full_uid)}B): {full_uid.hex().upper()}")
                        # 按 CL 片段分段显示
                        if ISO14443AParser._uid_cl1 is not None:
                            cl1_disp = ISO14443AParser._uid_cl1
                            if len(cl1_disp) == 4 and cl1_disp[0] == 0x88:
                                result["extra_info"].append(
                                    f"    - CL1 片段: {cl1_disp.hex().upper()} (CT={cl1_disp[0]:02X} + UID[0..2]={cl1_disp[1:].hex().upper()})")
                            else:
                                result["extra_info"].append(
                                    f"    - CL1 片段: {cl1_disp.hex().upper()} (=UID[0..3], 4B UID 全部)")
                        if ISO14443AParser._uid_cl2 is not None:
                            cl2_disp = ISO14443AParser._uid_cl2
                            result["extra_info"].append(
                                f"    - CL2 片段: {cl2_disp.hex().upper()} (=UID[3..6], 7B UID 后4字节)")
                        if ISO14443AParser._uid_cl3 is not None:
                            cl3_disp = ISO14443AParser._uid_cl3
                            result["extra_info"].append(
                                f"    - CL3 片段: {cl3_disp.hex().upper()} (=UID[6..9], 10B UID 后4字节)")
                result["commands"].append(f"SAK ({cl_label})")
            elif data_len >= 5:
                result["extra_info"].append(f"  - 防冲突响应 ({data_len}B):")
                sel_code = ISO14443AParser._last_sel_code or 0x93
                ISO14443AParser.parse_anticollision_response(
                    payload, sel_code, result["extra_info"], result["commands"])
            else:
                result["commands"].append(f"未知 14443-3 响应 ({data_len}B)")
            return result