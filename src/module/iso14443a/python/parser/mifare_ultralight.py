#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Mifare Ultralight / NTAG 卡片解析器

NTAG/Ultralight 命令集 (兼容 ISO 14443-3, 不进入 14443-4 T=CL):
  0x30 READ         : 读 4 个连续页 (16B 响应)
  0x3A FAST_READ    : 快速读 (起始页~结束页, 最多 8 页/32B)
  0xA2 WRITE        : 写 1 个页 (4B 数据, 单帧含数据)
  0xA0 COMPAT_WRITE : 兼容写 (5B 数据, NTAG)
  0x60 GET_VERSION  : 取版本 (8B 响应, NTAG EV1+)
  0x1B PWD_AUTH     : 密码认证 (1B 命令 + 4B 明文密码 + CRC → 2B PACK)
  0x1A 3DES_AUTH    : Ultralight C 3DES 认证 (第 1 步)
  0xAF 3DES_AUTH2   : Ultralight C 3DES 认证 (第 2 步, 附加认证)
  0x39 READ_CNT     : 读计数器 (5B 输入)
  0x3C READ_SIG     : 读签名 (2B 输入, 32B 响应)
  0x3E CHECK_TEAR    : 检查撕裂事件
  0x4B VCSL         : VCSL (虚拟卡选择)
  0xBB SECT_READ    : 扇区读 (NTAG 21x)

退出条件: REQA(0x26) / WUPA(0x52) / HLTA(0x50) / RATS(0xE0)
"""


class MifareUltralightParser:
    """Mifare Ultralight / NTAG 解析器"""

    # 命令码 (与 M1 Classic 部分共用, 但语义不同)
    CMD_READ = 0x30
    CMD_WRITE = 0xA2
    CMD_COMPAT_WRITE = 0xA0
    CMD_GET_VERSION = 0x60       # 注意: 与 M1 AUTH_A(0x60) 共用, 通过 data_len 区分
    CMD_PWD_AUTH = 0x1B          # 密码认证 (PWD 为 4 字节明文)
    CMD_3DES_AUTH = 0x1A         # Ultralight C 3DES 认证 (第 1 步)
    CMD_3DES_AUTH2 = 0xAF        # Ultralight C 3DES 认证 (第 2 步)
    CMD_READ_CNT = 0x39
    CMD_READ_SIG = 0x3C
    CMD_CHECK_TEAR = 0x3E
    CMD_VCSEL = 0x4B
    CMD_FAST_READ = 0x3A
    CMD_SECT_READ = 0xBB
    CMD_SECT_WRITE = 0xBC

    @staticmethod
    def is_native_command(cmd, data_len):
        """判断是否为 NTAG/Ultralight 原生命令 (用于进入原生卡模式)

        参数:
          cmd      : 命令码 (payload[0])
          data_len : 整个 payload 长度

        返回: True=是原生命令, False=否
        """
        # 0x60: 单字节 → NTAG GET_VERSION; 带 CRC 时是 3 字节 (60 xx xx)。
        # M1 AUTH_A 的最小长度是 4 字节 (60 <block> CRC), 因此 1/3 字节可安全区分。
        if cmd == MifareUltralightParser.CMD_GET_VERSION and data_len in (1, 3):
            return True
        # 0xA0 五字节 → NTAG COMPATIBLE_WRITE (M1 WRITE 是2字节: cmd+block)
        if cmd == MifareUltralightParser.CMD_COMPAT_WRITE and data_len >= 5:
            return True
        # NTAG 专有命令 (无 M1 冲突)
        if cmd in (MifareUltralightParser.CMD_WRITE,        # 0xA2
                   MifareUltralightParser.CMD_PWD_AUTH,      # 0x1B
                   MifareUltralightParser.CMD_3DES_AUTH,     # 0x1A
                   MifareUltralightParser.CMD_3DES_AUTH2,    # 0xAF
                   MifareUltralightParser.CMD_READ_CNT,      # 0x39
                   MifareUltralightParser.CMD_READ_SIG,      # 0x3C
                   MifareUltralightParser.CMD_CHECK_TEAR,    # 0x3E
                   MifareUltralightParser.CMD_VCSEL,         # 0x4B
                   MifareUltralightParser.CMD_FAST_READ,     # 0x3A
                   MifareUltralightParser.CMD_SECT_READ,      # 0xBB
                   MifareUltralightParser.CMD_SECT_WRITE):    # 0xBC
            return True
        # 0x30 READ: M1/NTAG 共用, 但 M1 需先 AUTH, NTAG 可直接读
        # 若未进入 Mifare 状态时收到 0x30 → 视为 NTAG READ
        if cmd == MifareUltralightParser.CMD_READ and data_len >= 2:
            return True
        return False

    @staticmethod
    def parse(cmd, payload, data_len, result):
        """解析 NTAG/Ultralight 命令 (PCD→PICC)"""
        cmd_desc = ""

        if cmd == MifareUltralightParser.CMD_GET_VERSION:
            cmd_desc = "NTAG/Ultralight GET_VERSION (取版本)"

        elif cmd == MifareUltralightParser.CMD_PWD_AUTH:
            if data_len >= 5:
                pwd = payload[1:5]
                pwd_hex = " ".join(f"{b:02X}" for b in pwd)
                result["native_pwd_hex"] = pwd_hex
                result["native_pwd_value"] = f"0x{pwd.hex().upper()}"
                cmd_desc = f"NTAG PWD_AUTH (明文密码: {pwd_hex})"
                result["extra_info"].append(f"  - NTAG 密码认证 (PWD_AUTH, 0x1B):")
                result["extra_info"].append(f"    - 明文密码 (4B): {pwd_hex}")
                result["extra_info"].append(f"    - 密码数值: 0x{pwd.hex().upper()} (线上明文传输, 无加密)")
            else:
                cmd_desc = "NTAG PWD_AUTH (incomplete)"

        elif cmd == MifareUltralightParser.CMD_3DES_AUTH:
            cmd_desc = "Ultralight C 3DES 认证 (第 1 步, 0x1A)"
            result["extra_info"].append(f"  - Ultralight C 3DES 认证 (第 1 步): {payload[1:].hex().upper()}")

        elif cmd == MifareUltralightParser.CMD_3DES_AUTH2:
            cmd_desc = "Ultralight C 3DES 认证 (第 2 步, 0xAF)"
            result["extra_info"].append(f"  - Ultralight C 3DES 认证 (第 2 步): {payload[1:].hex().upper()}")

        elif cmd == MifareUltralightParser.CMD_CHECK_TEAR:
            cmd_desc = "NTAG CHECK_TEARING_EVENT (0x3E, 检查撕裂事件)"

        elif cmd == MifareUltralightParser.CMD_READ:
            if data_len >= 2:
                page = payload[1]
                cmd_desc = f"NTAG/Ultralight READ (起始页={page}, 返回4页=16B)"
                result["extra_info"].append(f"  - NTAG 读页:")
                result["extra_info"].append(f"    - 起始页号: {page}")
                result["extra_info"].append(f"    - 返回 4 个连续页 (16B)")

        elif cmd == MifareUltralightParser.CMD_WRITE:
            if data_len >= 6:
                page = payload[1]
                data = payload[2:6]
                cmd_desc = f"NTAG/Ultralight WRITE (页={page}, 数据={data.hex().upper()})"
                result["extra_info"].append(f"  - NTAG 写页:")
                result["extra_info"].append(f"    - 页号: {page}")
                result["extra_info"].append(f"    - 数据 (4B): {data.hex().upper()}")
            else:
                cmd_desc = "NTAG/Ultralight WRITE (incomplete)"

        elif cmd == MifareUltralightParser.CMD_COMPAT_WRITE:
            if data_len >= 6:
                page = payload[1]
                data = payload[2:6]
                cmd_desc = f"NTAG COMPATIBLE_WRITE (页={page}, 数据={data.hex().upper()})"
                result["extra_info"].append(f"  - NTAG 兼容写:")
                result["extra_info"].append(f"    - 页号: {page}")
                result["extra_info"].append(f"    - 数据 (4B): {data.hex().upper()}")

        elif cmd == MifareUltralightParser.CMD_READ_CNT:
            if data_len >= 2:
                cnt = payload[1]
                cmd_desc = f"NTAG READ_CNT (计数器={cnt})"
                result["extra_info"].append(f"  - NTAG 读计数器: 计数器号 {cnt}")

        elif cmd == MifareUltralightParser.CMD_READ_SIG:
            cmd_desc = "NTAG READ_SIG (读签名, 32B)"
            result["extra_info"].append(f"  - NTAG 读签名: 返回 32B 签名数据")

        elif cmd == MifareUltralightParser.CMD_VCSEL:
            cmd_desc = "VCSL (虚拟卡选择, 0x4B)"

        elif cmd == MifareUltralightParser.CMD_FAST_READ:
            if data_len >= 3:
                start = payload[1]
                end = payload[2]
                cmd_desc = f"NTAG FAST_READ (起={start}, 止={end})"
                result["extra_info"].append(f"  - NTAG 快速读: 起始页 {start}, 结束页 {end}")

        elif cmd == MifareUltralightParser.CMD_SECT_READ:
            cmd_desc = "NTAG SECTOR_READ (扇区读)"

        elif cmd == MifareUltralightParser.CMD_SECT_WRITE:
            cmd_desc = "NTAG SECTOR_WRITE (扇区写)"

        return cmd_desc

    @staticmethod
    def parse_response(payload, data_len, last_cmd, last_page, result):
        """解析 NTAG/Ultralight 响应 (PICC→PCD)

        参数:
          payload   : 响应载荷
          data_len  : 载荷长度
          last_cmd  : 最后一条 NTAG 命令码 (来自 _native_last_cmd)
          last_page : 最后操作的页号 (来自 _native_last_page, 可为 None)
          result    : 结果字典
        """
        if data_len == 0:
            result["commands"].append("NTAG 空响应")
            return

        if data_len == 1:
            ack = payload[0]
            if ack == 0x0A:
                result["extra_info"].append(f"  - NTAG 响应: ACK (0x0A, 成功)")
                result["commands"].append("NTAG ACK (成功)")
            else:
                result["extra_info"].append(f"  - NTAG 响应: NAK (0x{ack:02X})")
                result["commands"].append(f"NTAG NAK (0x{ack:02X})")
            return

        # 多字节响应: 按最后命令分类
        if last_cmd == MifareUltralightParser.CMD_GET_VERSION:
            # GET_VERSION 响应: 8B 固定结构
            result["extra_info"].append(f"  - NTAG GET_VERSION 响应 ({data_len}B):")
            if data_len >= 8:
                # Header(0x60) + VendorID + ProdType + SubType + Major + Minor + ProdVer + ConfigSize
                hdr = payload[0]
                vendor = payload[1]
                prod_type = payload[2]
                sub_type = payload[3]
                major = payload[4]
                minor = payload[5]
                prod_ver = payload[6]
                cfg_size = payload[7]
                result["extra_info"].append(f"    - 头: 0x{hdr:02X}")
                result["extra_info"].append(f"    - 厂商ID: 0x{vendor:02X} (0x04=NXP)")
                result["extra_info"].append(f"    - 产品类型: 0x{prod_type:02X}")
                result["extra_info"].append(f"    - 子类型: 0x{sub_type:02X}")
                result["extra_info"].append(f"    - 主版本: {major}")
                result["extra_info"].append(f"    - 次版本: {minor}")
                result["extra_info"].append(f"    - 产品版本: 0x{prod_ver:02X}")
                result["extra_info"].append(f"    - 配置大小: 0x{cfg_size:02X}")
                result["commands"].append("NTAG GET_VERSION 响应 (8B)")
            else:
                result["extra_info"].append(f"    - 数据: {payload.hex().upper()}")
                result["commands"].append(f"NTAG GET_VERSION 响应 ({data_len}B)")

        elif last_cmd == MifareUltralightParser.CMD_READ:
            # READ 响应: 16B (4 页 × 4B)
            result["extra_info"].append(f"  - NTAG READ 响应 ({data_len}B, 起始页={last_page}):")
            if data_len >= 16 and last_page is not None:
                for i in range(4):
                    page = last_page + i
                    page_data = payload[i*4:(i+1)*4]
                    result["extra_info"].append(f"    - 页 {page}: {page_data.hex().upper()}")
            else:
                result["extra_info"].append(f"    - 数据: {payload.hex().upper()}")
            result["commands"].append(f"NTAG READ 响应 (起始页={last_page}, {data_len}B)")

        elif last_cmd == MifareUltralightParser.CMD_PWD_AUTH:
            # PWD_AUTH 响应: 2B PACK (明文密码认证应答)
            result["extra_info"].append(f"  - NTAG PWD_AUTH 响应 ({data_len}B):")
            if data_len >= 2:
                pack_hex = payload[:2].hex().upper()
                result["extra_info"].append(f"    - PACK (2B): {pack_hex} (认证成功应答, 明文)")
                result["commands"].append(f"NTAG PWD_AUTH 响应 (PACK={pack_hex})")
            else:
                result["extra_info"].append(f"    - 数据: {payload.hex().upper()}")
                result["commands"].append(f"NTAG PWD_AUTH 响应 ({data_len}B)")

        elif last_cmd == MifareUltralightParser.CMD_READ_SIG:
            # READ_SIG 响应: 32B 签名
            result["extra_info"].append(f"  - NTAG READ_SIG 响应 ({data_len}B):")
            result["extra_info"].append(f"    - 签名: {payload.hex().upper()}")
            result["commands"].append(f"NTAG READ_SIG 响应 ({data_len}B)")

        else:
            result["extra_info"].append(f"  - NTAG 响应 ({data_len}B): {payload.hex().upper()}")
            result["commands"].append(f"NTAG 响应 ({data_len}B)")
