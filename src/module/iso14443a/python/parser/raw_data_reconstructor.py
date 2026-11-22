#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
原始数据还原器 (Raw Data Reconstructor)

根据 readme.md 的要求，对接收到的原始数据进行拼接还原：
  1. 接收到的数据为原始数据，MSB 即为最先收到的数据（位流按 MSB 优先打包进字节）；
  2. 最后一个数据的有效位数为 1-8（8 表示整个字节有效，其他表示对应位数有效）；
  3. 最后一个数据有效位数不足 8 位的，应左移 (8 - valid_bits) 位，
     使得接收的数据由最高位开始往后排列；
  4. 如果第一个字节只有 7 位，则为 7bits 命令，按 7bits 命令解析；
  5. 拼接后，将数据和校验位按上下两行对应排列；
  6. 检查校验位是否正确；
  7. 检查 CRC 是否正确；
  8. 对于 CRC 校验正确而校验位不正确的数据，应视为 Mifare 1 数据
     （v3 起改为分层判定：卡片级 SAK + 会话级 + 帧级辅证，见 _classify_mifare）

ISO 14443A 帧结构（位级）:
  [SoC] [8 data bits][1 parity bit] [8 data bits][1 parity bit] ... [EoC]
  - 数据位在线上以 LSB 优先传输；
  - 校验位为奇校验（ISO/IEC 14443-3 Type A 字符格式，两个方向规则相同）；
  - 本还原器接收的 raw bytes 已将位流按 MSB 优先打包（bit7 of byte0 = 最先收到的位）。
"""


class RawDataReconstructor:
    """原始数据还原器：位拼接、校验位检查、CRC16-A 检查、M1 识别"""

    # 7-bit 命令码 (ISO 14443-3)
    CMD_REQA_7BIT = 0x26   # REQA (7 bits)
    CMD_WUPA_7BIT = 0x52   # WUPA (7 bits)

    @staticmethod
    def crc16_a(data):
        """计算 CRC16-A (ISO 14443A)，返回 16 位值（低字节在前的交换格式）。

        移植自 readme.md 中的 C 代码:
          crc 初值 0x6363, 多项式 0x8408 (即 0x1021 反向),
          返回时交换高低字节。
        """
        crc = 0x6363
        for b in data:
            bt = b & 0xFF
            bt = bt ^ (crc & 0xFF)
            bt = bt ^ ((bt << 4) & 0xFF)
            crc = (crc >> 8) ^ ((bt << 8) & 0xFFFF) ^ ((bt << 3) & 0xFFFF) ^ (bt >> 4)
            crc &= 0xFFFF
        # 交换高低字节，与 C 代码返回一致
        return ((crc & 0x00FF) << 8) | ((crc >> 8) & 0x00FF)

    @staticmethod
    def has_valid_crc_a(data):
        """检查数据末尾 2 字节是否为正确的 CRC16-A。"""
        if len(data) < 2:
            return False
        crc = RawDataReconstructor.crc16_a(data[:-2])
        crc_hi = (crc >> 8) & 0xFF
        crc_lo = crc & 0xFF
        return data[-2] == crc_hi and data[-1] == crc_lo

    @staticmethod
    def append_crc_a(data):
        """在数据末尾追加 2 字节 CRC16-A，返回新的 bytes。"""
        crc = RawDataReconstructor.crc16_a(data)
        return data + bytes([(crc >> 8) & 0xFF, crc & 0xFF])

    @staticmethod
    def _should_check_crc(data_bytes, direction_byte, is_7bit_cmd, tcl_session=False):
        """判断该帧按 ISO 14443 协议是否应含 CRC16-A。

        明知不含 CRC 的命令及返回数据 (ISO 14443-3):
          - 7-bit 命令 (REQA/WUPA)
          - ATQA 响应 ('<' 方向, 2 字节)
          - ANTICOLLISION 命令 ('>' 方向, SEL + NVB=0x2X)
          - ANTICOLLISION 响应 ('<' 方向, 5 字节 = 4 字节 UID + 1 字节 BCC)

        进入 T=CL 会话后 (已发 RATS / 已收 ATS), 所有 I/R/S-block 与 ATS
        都必须带 CRC16-A, 因此不再按帧长度跳过 —— 否则 5 字节 I-block
        (PCB + 2B APDU + 2B CRC) 会被误当成防冲突响应。

        返回: (should_check: bool, skip_reason: str|None)
        """
        if is_7bit_cmd:
            return False, "7-bit 命令 (REQA/WUPA) 按协议不含 CRC"
        n = len(data_bytes)
        # T=CL 会话: 一律校验 (ATS / I-block / R-block / S-block 均含 CRC)
        if tcl_session:
            return True, None
        if direction_byte == ord('>'):
            # NVB 高半字节为 2 (0x2X) 表示 ANTICOLLISION, 不含 CRC;
            # NVB=0x70 才是 SELECT, 需要 CRC。
            if (n >= 2 and data_bytes[0] in (0x93, 0x95, 0x97)
                    and (data_bytes[1] & 0xF0) == 0x20):
                return False, "ANTICOLLISION 命令 (SEL+NVB=0x2X) 按协议不含 CRC"
        else:
            if n == 2:
                return False, "ATQA 响应 (2 字节) 按协议不含 CRC"
            if n == 5:
                return False, "ANTICOLLISION 响应 (4 字节 UID + BCC, 共 5 字节) 按协议不含 CRC"
        return True, None

    # ---- 卡片级协议提示 (由解析器依据 SAK 计算后传入) ----
    HINT_MIFARE = "mifare"    # SAK b4=1, 支持 CRYPTO1
    HINT_TCL = "tcl"          # SAK b4=0 且 b6=1, 纯 T=CL
    HINT_NATIVE = "native"    # SAK=0x00/0x44, Ultralight/NTAG
    HINT_UNKNOWN = "unknown"  # 尚未获取 SAK

    # ---- Mifare 判定结果 ----
    M1_PLAINTEXT = "mifare_plaintext"
    M1_CIPHERTEXT = "mifare_ciphertext"

    @staticmethod
    def _classify_mifare(data_bytes, parity_ok, crc_ok, is_7bit_cmd,
                         card_hint, mifare_session, tcl_session):
        """分层判定 Mifare 1 数据帧，返回 (kind, reason)。

        判定优先级 (高 → 低):
          1. 会话级: 已进入 AUTH 会话且 CRC 不成立 → M1 加密帧
             (Crypto1 加密帧的 CRC 在明文上计算后随明文一起加密，
              线上数据无法通过 CRC16-A 校验)
          2. 会话级: 已进入 T=CL 会话且未进入 M1 会话 → 非 M1
          3. 卡片级: SAK b4=1 (支持 CRYPTO1) 且 CRC 成立 → M1 明文帧
          4. 卡片级: SAK 为纯 T=CL (b4=0) → 非 M1
          5. 帧级辅证: CRC 成立但校验位错误 (readme 第 8 条)

        参数:
          card_hint     : HINT_MIFARE / HINT_TCL / HINT_NATIVE / HINT_UNKNOWN
          mifare_session: 是否已进入 AUTH 会话 (_mifare_active)
          tcl_session   : 是否已进入 14443-4 T=CL 会话 (_tcl_active)

        返回:
          kind   : None | 'mifare_plaintext' | 'mifare_ciphertext'
          reason : str|None, 判定依据说明
        """
        if is_7bit_cmd:
            return None, None

        heuristic = (crc_ok is True and not parity_ok)

        # 1. 会话级: AUTH 之后且 CRC 不成立 → M1 加密帧
        if mifare_session and crc_ok is False:
            return (RawDataReconstructor.M1_CIPHERTEXT,
                    "已进入 AUTH 会话，加密帧 CRC16-A 不成立 (Crypto1 密文特征)")

        # 2. 会话级: 已进入 T=CL 会话且非 M1 会话 → 非 M1
        if tcl_session and not mifare_session:
            return None, None

        # 3. 卡片级: M1 兼容卡 + CRC 成立 → 明文帧
        if card_hint == RawDataReconstructor.HINT_MIFARE and crc_ok is True:
            return (RawDataReconstructor.M1_PLAINTEXT,
                    "SAK b4=1 支持 CRYPTO1，且 CRC16-A 校验通过")

        # 4. 卡片级: 纯 T=CL 卡 → 非 M1
        if card_hint == RawDataReconstructor.HINT_TCL:
            if heuristic:
                return None, "SAK 显示为纯 T=CL 卡 (b4=0)，不判为 Mifare"
            return None, None

        # 5. 帧级辅证: readme 第 8 条 (未获卡片级信息时的兜底)
        if heuristic:
            return (RawDataReconstructor.M1_PLAINTEXT,
                    "CRC 正确但校验位错误，按 readme 第 8 条辅证判为 M1 (无卡片级信息)")

        return None, None

    @staticmethod
    def _popcount(b):
        """计算字节中 1 的个数。"""
        return bin(b & 0xFF).count('1')

    @staticmethod
    def _check_parity(data_byte, parity_bit):
        """检查单个字节的校验位是否正确。

        ISO/IEC 14443-3 Type A 字符格式: 每个字节后附加 1 个奇校验位，
        PCD→PICC 与 PICC→PCD 两个方向均为奇校验
        (8 个数据位中 1 的个数 + 校验位 = 奇数)。
        """
        ones = RawDataReconstructor._popcount(data_byte)
        total = ones + (1 if parity_bit else 0)
        return (total % 2) == 1   # 奇校验

    @staticmethod
    def extract_bits(raw_bytes):
        """将原始字节按 MSB 优先展开为位列表（bit7 在前）。"""
        bits = []
        for b in raw_bytes:
            for i in range(7, -1, -1):
                bits.append((b >> i) & 0x01)
        return bits

    @staticmethod
    def reconstruct(raw_bytes, valid_bits, direction_byte,
                    card_hint=HINT_UNKNOWN, mifare_session=False, tcl_session=False):
        """还原原始数据为 (data_bytes, parity_bits, info)。

        参数:
          raw_bytes     : 原始数据字节（含数据位与校验位交织的位流，MSB 优先打包）
          valid_bits    : 最后一个数据字节的有效位数 (1-8)
          direction_byte: 方向字节 ord('>') 或 ord('<')
          card_hint     : 卡片级协议提示 (由 SAK 推导), 见 HINT_*
          mifare_session: 是否已进入 Mifare AUTH 会话
          tcl_session   : 是否已进入 ISO 14443-4 T=CL 会话

        返回: dict
          {
            "data_bytes"      : bytes, 还原后的数据字节（去校验位，末字节已左移对齐）
            "parity_bits"     : list[int], 每个数据字节对应的校验位 (0/1)
            "parity_ok"       : bool, 所有校验位是否正确
            "parity_errors"   : list[int], 校验错误的字节索引
            "is_7bit_cmd"     : bool, 是否为 7-bit 命令 (首字节仅 7 位)
            "crc_checked"     : bool, 是否执行了 CRC 检查 (明知不含 CRC 的帧不检查)
            "crc_ok"          : bool|None, CRC16-A 是否正确 (末尾 2 字节), 未检查为 None
            "mifare_kind"     : None|'mifare_plaintext'|'mifare_ciphertext', 分层判定结果
            "mifare_reason"   : str|None, 判定依据说明
            "mifare_heuristic": bool, 是否命中 readme 第 8 条帧级辅证
            "card_hint"       : str, 本次判定所用的卡片级提示
            "is_mifare1"      : bool, mifare_kind 是否非空
            "raw_bit_count"   : int, 实际使用的位数
            "info"            : list[str], 过程说明
          }
        """
        info = []
        if valid_bits < 1 or valid_bits > 8:
            # 容错：非法值按 8 处理
            info.append(f"  - 警告: valid_bits={valid_bits} 超出 1-8 范围，按 8 处理")
            valid_bits = 8

        # ------------------------------------------------------------------
        # 步骤 1: 末字节先左移对齐
        # 最后一个原始字节的有效位不足 8 位时，先左移 (8 - valid_bits) 位，
        # 使有效数据从字节最高位开始往后排列，然后再参与 MSB 优先的位展开。
        # ------------------------------------------------------------------
        raw_list = list(raw_bytes)
        last_byte_original = None  # 末字节左移前的原始值（调试用）
        if raw_list and valid_bits < 8:
            last_byte_original = raw_list[-1]
            shift = 8 - valid_bits
            raw_list[-1] = (raw_list[-1] << shift) & 0xFF
            info.append(f"  - 末字节有效位={valid_bits}, 原始值=0x{last_byte_original:02X}, 先左移 {shift} 位: 0x{raw_list[-1]:02X}")

        # ------------------------------------------------------------------
        # 步骤 2: 左移后的字节序列按 MSB 优先展开为位流
        # 有效位总数 = 前 (N-1) 字节全部有效 + 末字节 valid_bits 位，
        # 末尾 (8 - valid_bits) 个展开位为左移补入的 0，不参与还原。
        # ------------------------------------------------------------------
        bits = RawDataReconstructor.extract_bits(bytes(raw_list))
        total_bits_available = len(bits)
        total_effective_bits = (len(raw_list) - 1) * 8 + valid_bits if raw_list else 0
        info.append(f"  - 原始数据: {len(raw_list)} 字节 = {total_bits_available} 位展开, 有效位数={total_effective_bits} (MSB 优先)")

        # ------------------------------------------------------------------
        # 步骤 3: 按位流结构还原: [8 data][1 parity] [8 data][1 parity] ... [last partial data]
        # 每个完整数据字节占 9 位（8 数据 + 1 校验），末尾剩余不足 9 位的为
        # 最后一个不完整数据字节（无校验位，如 7-bit 命令）。
        # 位流顺序 = 线上 LSB 优先，即 bits[0] 是最先收到的位 = 数据字节的 bit0 (LSB)。
        # 因此数据字节值 = sum(bits[i] << i)，无需反序。
        # ------------------------------------------------------------------
        data_bytes = []
        parity_bits = []
        bit_idx = 0

        # 先尽可能还原完整的 8 位数据字节（含校验位）
        while bit_idx + 9 <= total_effective_bits:
            data_8 = bits[bit_idx:bit_idx + 8]
            # bits 已是 LSB 优先顺序，直接累加
            byte_val = 0
            for i in range(8):
                if data_8[i]:
                    byte_val |= (1 << i)
            bit_idx += 8

            # 取 1 位校验位
            parity = bits[bit_idx]
            bit_idx += 1

            data_bytes.append(byte_val)
            parity_bits.append(parity)

        # 末尾剩余不足 9 位的部分：最后一个不完整数据字节（无校验位）
        # 典型场景: 7-bit 命令 (REQA/WUPA)，仅 7 位数据
        remaining_bits = total_effective_bits - bit_idx
        if remaining_bits > 0:
            byte_val = 0
            for i in range(remaining_bits):
                if bits[bit_idx + i]:
                    byte_val |= (1 << i)
            data_bytes.append(byte_val)
            parity_bits.append(None)
            info.append(f"  - 末字节数据: 仅 {remaining_bits} 位, 重组值=0x{byte_val:02X}")
            bit_idx += remaining_bits

        data_bytes = bytes(data_bytes)
        raw_bit_count = bit_idx

        # ------------------------------------------------------------------
        # 7-bit 命令检测：首字节有效位为 7 且无校验位
        # 使用还原后的 7-bit 重组值识别命令 (REQA=0x26, WUPA=0x52)
        # ------------------------------------------------------------------
        is_7bit_cmd = False
        cmd_7bit_value = None
        if valid_bits == 7 and len(data_bytes) == 1 and (len(parity_bits) == 0 or parity_bits[0] is None):
            is_7bit_cmd = True
            cmd_7bit_value = data_bytes[0]
            cmd_name = {
                RawDataReconstructor.CMD_REQA_7BIT: "REQA (Request All)",
                RawDataReconstructor.CMD_WUPA_7BIT: "WUPA (Wake Up All)",
            }.get(cmd_7bit_value, f"未知 7-bit 命令 (0x{cmd_7bit_value:02X})")
            info.append(f"  - 7-bit 命令: {cmd_name} (值=0x{cmd_7bit_value:02X})")

        # ------------------------------------------------------------------
        # 校验位检查
        # ------------------------------------------------------------------
        parity_ok = True
        parity_errors = []
        if parity_bits and not is_7bit_cmd:
            for i, p in enumerate(parity_bits):
                if p is None:
                    continue
                if i >= len(data_bytes):
                    break
                ok = RawDataReconstructor._check_parity(data_bytes[i], p)
                if not ok:
                    parity_ok = False
                    parity_errors.append(i)
            info.append(f"  - 校验位检查: {'通过' if parity_ok else '失败'}")
            if parity_errors:
                info.append(f"    - 校验错误字节索引: {parity_errors}")
        else:
            info.append(f"  - 校验位检查: 跳过（无校验位或 7-bit 命令）")

        # ------------------------------------------------------------------
        # CRC16-A 检查（末尾 2 字节为 CRC）
        # 明知不含 CRC 的命令及返回数据 (REQA/WUPA/ATQA/ANTICOLLISION) 不做检查
        # ------------------------------------------------------------------
        crc_ok = None
        check_crc, skip_reason = RawDataReconstructor._should_check_crc(
            data_bytes, direction_byte, is_7bit_cmd, tcl_session=tcl_session)
        if not check_crc:
            info.append(f"  - CRC16-A 检查: 跳过（{skip_reason}）")
        elif len(data_bytes) >= 2:
            crc_ok = RawDataReconstructor.has_valid_crc_a(data_bytes)
            info.append(f"  - CRC16-A 检查: {'通过' if crc_ok else '失败'}")
            if not crc_ok:
                calc = RawDataReconstructor.crc16_a(data_bytes[:-2])
                info.append(f"    - 计算 CRC: 0x{calc:04X}, 实际: 0x{data_bytes[-2]:02X}{data_bytes[-1]:02X}")
        else:
            info.append(f"  - CRC16-A 检查: 跳过（数据不足 2 字节）")

        # ------------------------------------------------------------------
        # Mifare 1 数据帧判定 (分层: 会话级 → 卡片级 → 帧级辅证)
        # ------------------------------------------------------------------
        mifare_kind, mifare_reason = RawDataReconstructor._classify_mifare(
            data_bytes, parity_ok, crc_ok, is_7bit_cmd,
            card_hint, mifare_session, tcl_session)
        mifare_heuristic = (crc_ok is True and not parity_ok)

        if mifare_kind == RawDataReconstructor.M1_PLAINTEXT:
            info.append(f"  - 判定: Mifare 1 明文帧（依据: {mifare_reason}）")
        elif mifare_kind == RawDataReconstructor.M1_CIPHERTEXT:
            info.append(f"  - 判定: Mifare 1 加密帧（依据: {mifare_reason}）")
        elif mifare_reason:
            info.append(f"  - 判定: 非 Mifare 1（{mifare_reason}）")
        else:
            info.append("  - 判定: 非 Mifare 1")

        return {
            "data_bytes": data_bytes,
            "parity_bits": parity_bits,
            "parity_ok": parity_ok,
            "parity_errors": parity_errors,
            "is_7bit_cmd": is_7bit_cmd,
            "cmd_7bit_value": cmd_7bit_value,
            "crc_checked": check_crc and len(data_bytes) >= 2,
            "crc_ok": crc_ok,
            "mifare_kind": mifare_kind,
            "mifare_reason": mifare_reason,
            "mifare_heuristic": mifare_heuristic,
            "card_hint": card_hint,
            "is_mifare1": mifare_kind is not None,
            "raw_bit_count": raw_bit_count,
            "info": info,
        }

    @staticmethod
    def format_data_parity_rows(data_bytes, parity_bits):
        """将数据字节和校验位按上下两行对应排列，返回 (data_row, parity_row) 字符串。

        用于 readme.md 第 5 条要求：拼接后将数据和校验位按上下两行对应排列。
        """
        data_row_parts = []
        parity_row_parts = []
        for i, b in enumerate(data_bytes):
            data_row_parts.append(f"{b:08b}")
            p = parity_bits[i] if i < len(parity_bits) else None
            parity_row_parts.append("-" if p is None else str(p))
        return " ".join(data_row_parts), " ".join(parity_row_parts)
