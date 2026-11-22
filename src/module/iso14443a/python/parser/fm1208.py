#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
复旦微 FM1208 CPU卡 解析器
支持ISO 14443-4协议和FM1208专用APDU命令
"""

import struct
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from ui.language import Language


class FM1208Parser:
    """FM1208 CPU卡解析器"""
    
    CMD_RATS = 0xE0
    CMD_PPS = 0xD1
    
    # FM1208 专用APDU指令 (INS)
    INS_SELECT = 0xA4       # 选择文件
    INS_READ_BINARY = 0xB0  # 读二进制
    INS_WRITE_BINARY = 0xD0 # 写二进制
    INS_GET_RESPONSE = 0xC0 # 获取响应
    INS_VERIFY = 0x20       # 验证PIN
    INS_INTERNAL_AUTH = 0x88 # 内部认证
    INS_EXTERNAL_AUTH = 0x82 # 外部认证
    INS_GET_CHALLENGE = 0x84 # 获取挑战
    INS_UPDATE_BINARY = 0xD6 # 更新二进制
    INS_READ_RECORD = 0xB2  # 读记录
    INS_WRITE_RECORD = 0xD2 # 写记录
    INS_APPEND_RECORD = 0xE2 # 追加记录
    INS_GET_DATA = 0xCA     # 获取数据
    INS_PUT_DATA = 0xDA     # 放入数据
    INS_MANAGE_CHANNEL = 0x70 # 管理通道
    
    # FM1208 专用文件ID
    FILE_ID_MF = 0x3F00     # 主文件 (MF)
    FILE_ID_DF = 0xDF01     # 目录文件
    FILE_ID_EF = 0x0001     # 基本文件
    
    # FM1208 响应状态字
    SW_SUCCESS = 0x9000     # 成功
    SW_CHANGE = 0x6100      # 有更多数据
    SW_WARNING = 0x6200     # 警告
    SW_ERROR = 0x6300       # 错误
    SW_EXEC_ERROR = 0x6400  # 执行错误
    SW_MEMORY_ERROR = 0x6500 # 内存错误
    SW_SECURITY_ERROR = 0x6600 # 安全错误
    SW_KEY_ERROR = 0x6700   # 密钥错误
    SW_WRONG_LENGTH = 0x6C00 # 长度错误
    SW_WRONG_CLA = 0x6D00   # CLA错误
    SW_WRONG_INS = 0x6E00   # INS错误
    SW_TECHNICAL_ERROR = 0x6F00 # 技术错误
    SW_FILE_NOT_FOUND = 0x6A82 # 文件未找到
    SW_INCORRECT_P1P2 = 0x6A86 # P1/P2错误
    SW_SECURE_MESSAGING = 0x6A88 # 安全消息错误
    SW_REFERENCED_DATA = 0x6A89 # 引用数据错误
    SW_WRONG_PARAM = 0x6A8A # 参数错误

    @staticmethod
    def parse(cmd, payload, data_len, result):
        """解析FM1208相关命令"""
        cmd_desc = ""
        
        # RATS 已在14443-3中处理，但这里可以补充FM1208特定信息
        if cmd == FM1208Parser.CMD_RATS:
            if data_len >= 2:
                param = payload[1]
                fsdi = (param >> 4) & 0x0F
                cid = param & 0x0F
                cmd_desc = f"RATS (FSDI={fsdi} -> Max Frame Size={2**(fsdi+1)} bytes, CID={cid})"
                result["extra_info"].append(f"  - FSDI={fsdi}, CID={cid}")
                result["extra_info"].append(f"  - FM1208: RATS request")
            else:
                cmd_desc = "RATS (incomplete)"
        elif cmd == FM1208Parser.CMD_PPS:
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
                result["extra_info"].append(f"  - FM1208: PPS negotiation")
            else:
                cmd_desc = "PPS (incomplete)"
        
        return cmd_desc
    
    @staticmethod
    def parse_ats(payload, data_len, result):
        """解析FM1208的ATS响应"""
        if data_len >= 2:
            t0 = payload[1]
            fsci = t0 & 0x0F
            if fsci >= 4 and fsci <= 6:
                result["extra_info"].append(f"  - FM1208: FSCI={fsci} matches typical CPU card")
            # 检查历史字节中的FM1208标识
            if data_len >= 4:
                hist_bytes = payload[3:] if data_len > 3 else b''
                if len(hist_bytes) >= 2:
                    # FM1208 历史字节通常以 0x80 开头
                    if hist_bytes[0] == 0x80:
                        result["extra_info"].append(f"  - FM1208: 检测到CPU卡标识 (0x80)")
                        if len(hist_bytes) >= 3:
                            # 解析历史字节中的TLV
                            tag = hist_bytes[0]
                            length = hist_bytes[1]
                            if length > 0 and len(hist_bytes) >= 2 + length:
                                value = hist_bytes[2:2+length]
                                result["extra_info"].append(f"  - FM1208: 历史TLV Tag=0x{tag:02X}, Len={length}, Value={value.hex().upper()}")

    @staticmethod
    def parse_apdu(apdu_data, extra_info):
        """
        解析FM1208 APDU命令 (旧接口, 保留兼容)
        新代码请使用 parse_command() / parse_response()
        """
        if not apdu_data or len(apdu_data) < 4:
            return

        cla = apdu_data[0]
        ins = apdu_data[1] if len(apdu_data) > 1 else 0
        p1 = apdu_data[2] if len(apdu_data) > 2 else 0
        p2 = apdu_data[3] if len(apdu_data) > 3 else 0
        FM1208Parser.parse_command(cla, ins, p1, p2, 0, b'', 0, 0, apdu_data, extra_info)

    @staticmethod
    def parse_command(cla, ins, p1, p2, lc, data, le, case, apdu_data, extra_info):
        """FM1208 命令APDU专用解析

        由 ISO14443AParser.parse_command_apdu() 在通用 CLA/INS/P1/P2/Lc/Data/Le 解析完成后调用,
        补充 FM1208 / ISO 7816-4 特定命令的 P1/P2/Data 语义解释。

        参数:
          cla, ins, p1, p2 : 已解析的 APDU 头字段
          lc               : 命令数据长度 (Case 3/4), 0 表示无
          data             : 命令数据 (bytes), 可为 b''
          le               : 期望响应长度 (Case 2/4), 0 表示无
          case             : APDU Case (1~4)
          apdu_data        : 原始 APDU 字节 (供旧解析器使用)
          extra_info       : 输出列表, 追加解析详情
        """
        # 仅处理标准 ISO 7816-4 CLA (0x00/0x0C/0x80 系列)
        # 跳过厂商自定义 CLA (如 DESFire 0x90)
        if (cla & 0xF0) not in (0x00, 0x80):
            return

        # ---------- 路由到对应的命令解析器 ----------
        if ins == FM1208Parser.INS_SELECT:
            FM1208Parser.parse_select_file(p1, p2, apdu_data, extra_info)
        elif ins == FM1208Parser.INS_READ_BINARY:
            FM1208Parser.parse_read_binary(p1, p2, apdu_data, extra_info)
        elif ins == FM1208Parser.INS_WRITE_BINARY:
            FM1208Parser.parse_write_binary(p1, p2, apdu_data, extra_info)
        elif ins == FM1208Parser.INS_UPDATE_BINARY:
            # UPDATE BINARY (0xD6): 与 WRITE BINARY 类似, 偏移在 P1/P2, 数据在 Data
            extra_info.append(f"        - FM1208 UPDATE BINARY:")
            offset = (p1 << 8) | p2
            extra_info.append(f"          - 偏移: {offset} (0x{offset:04X})")
            if lc > 0 and data:
                extra_info.append(f"          - 更新数据 ({lc}B): {data.hex().upper()}")
        elif ins == FM1208Parser.INS_READ_RECORD:
            FM1208Parser.parse_read_record(p1, p2, apdu_data, extra_info)
        elif ins == FM1208Parser.INS_WRITE_RECORD:
            FM1208Parser.parse_write_record(p1, p2, apdu_data, extra_info)
        elif ins == FM1208Parser.INS_APPEND_RECORD:
            FM1208Parser.parse_append_record(p1, p2, apdu_data, extra_info)
        elif ins == FM1208Parser.INS_GET_RESPONSE:
            FM1208Parser.parse_get_response(p1, p2, apdu_data, extra_info)
        elif ins == FM1208Parser.INS_VERIFY:
            FM1208Parser.parse_verify(p1, p2, apdu_data, extra_info)
        elif ins == FM1208Parser.INS_INTERNAL_AUTH:
            FM1208Parser.parse_internal_auth(p1, p2, apdu_data, extra_info)
        elif ins == FM1208Parser.INS_EXTERNAL_AUTH:
            FM1208Parser.parse_external_auth(p1, p2, apdu_data, extra_info)
        elif ins == FM1208Parser.INS_GET_CHALLENGE:
            FM1208Parser.parse_get_challenge(p1, p2, apdu_data, extra_info)
        elif ins == FM1208Parser.INS_GET_DATA:
            FM1208Parser.parse_get_data(p1, p2, apdu_data, extra_info)
        elif ins == FM1208Parser.INS_PUT_DATA:
            FM1208Parser.parse_put_data(p1, p2, apdu_data, extra_info)
        elif ins == FM1208Parser.INS_MANAGE_CHANNEL:
            FM1208Parser.parse_manage_channel(p1, p2, apdu_data, extra_info)

    @staticmethod
    def parse_response(sw1, sw2, response_data, extra_info):
        """FM1208 响应APDU专用解析

        由 ISO14443AParser.parse_response_apdu() 在通用 SW1/SW2 解析完成后调用,
        补充 FM1208 特定响应数据的语义解释 (如 FCI、GET RESPONSE 提示等)。

        参数:
          sw1, sw2      : 状态字
          response_data : 响应数据 (bytes), 不含 SW1/SW2, 可为 b''
          extra_info    : 输出列表
        """
        if not response_data:
            # 无响应数据的常见情况
            if sw1 == 0x90 and sw2 == 0x00:
                pass  # 成功, 无额外数据
            elif sw1 == 0x61:
                extra_info.append(f"      - FM1208: 还有 {sw2} 字节数据待读取 (需 GET RESPONSE)")
            elif sw1 == 0x6C:
                extra_info.append(f"      - FM1208: Le 错误, 正确长度={sw2}")
            return

        # 有响应数据的情况
        if sw1 == 0x90 and sw2 == 0x00:
            # 成功响应 + 有数据: 可能是 SELECT 响应 (FCI) 或 READ 响应
            if response_data[0] == 0x6F:
                # FCI 模板 (ISO 7816-4 / EMV)
                extra_info.append(f"      - FM1208: FCI模板 (Tag=0x6F, {len(response_data)}B)")
                FM1208Parser._parse_tlv(response_data, extra_info, indent=8)
            elif response_data[0] == 0x6A:
                # FCI 专有模板
                extra_info.append(f"      - FM1208: FCI专有模板 (Tag=0x6A, {len(response_data)}B)")
                FM1208Parser._parse_tlv(response_data, extra_info, indent=8)
            elif response_data[0] in (0x62, 0x63, 0x64, 0x65, 0x66):
                # FCP / FMD 等模板
                extra_info.append(f"      - FM1208: 文件控制模板 (Tag=0x{response_data[0]:02X}, {len(response_data)}B)")
                FM1208Parser._parse_tlv(response_data, extra_info, indent=8)
        elif sw1 == 0x61:
            extra_info.append(f"      - FM1208: 还有 {sw2} 字节数据待读取 (需 GET RESPONSE)")
        elif sw1 == 0x6C:
            extra_info.append(f"      - FM1208: Le 错误, 正确长度={sw2}")

    @staticmethod
    def _parse_tlv(data, extra_info, indent=8):
        """简单 BER-TLV 解析 (用于 FCI / FCP 等)

        支持 1~2 字节 Tag, 1~3 字节 Length。
        """
        tag_names = {
            0x6F: "FCI模板",
            0x6A: "FCI专有模板",
            0x62: "FCP模板",
            0x64: "FMD模板",
            0x84: "DF名称 (AID)",
            0x85: "专有信息",
            0xA5: "FCI专有",
            0x8C: "发行者自由数据",
            0x87: "SM数据对象",
            0x8E: "SM相关",
            0x80: "文件大小/数据",
            0x82: "文件描述符",
            0x83: "文件标识符",
            0x86: "安全属性",
            0x88: "短文件标识符 (SFI)",
            0x8A: "生命周期状态",
            0x8B: "安全属性 (扩展)",
            0x8D: "安全环境",
            0x89: "权限",
            0xC6: "PIN使用计数器",
        }

        idx = 0
        prefix = " " * indent
        while idx < len(data):
            # ---------- Tag (1 或 2 字节) ----------
            if idx >= len(data):
                break
            tag = data[idx]
            idx += 1
            # 多字节 Tag: 若低 5 位全 1 (0x1F), 后续还有 Tag 字节
            if (tag & 0x1F) == 0x1F and idx < len(data):
                tag = (tag << 8) | data[idx]
                idx += 1

            if idx >= len(data):
                break

            # ---------- Length (1, 2 或 3 字节) ----------
            length = data[idx]
            idx += 1
            if length == 0x81:
                if idx >= len(data):
                    break
                length = data[idx]
                idx += 1
            elif length == 0x82:
                if idx + 1 >= len(data):
                    break
                length = (data[idx] << 8) | data[idx + 1]
                idx += 2

            if idx + length > len(data):
                extra_info.append(f"{prefix}- Tag=0x{tag:02X}, Len={length} (数据截断)")
                break

            value = data[idx:idx + length]
            tag_desc = tag_names.get(tag, f"Tag=0x{tag:02X}" if tag < 0x100 else f"Tag=0x{tag:04X}")
            if length == 0:
                extra_info.append(f"{prefix}- {tag_desc}: (空)")
            else:
                extra_info.append(f"{prefix}- {tag_desc}: {value.hex().upper()}")
            idx += length

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
            0x82: "EXTERNAL AUTHENTICATE (外部认证)",
            0x84: "GET CHALLENGE (获取挑战)",
            0x88: "INTERNAL AUTHENTICATE (内部认证)",
            0xA0: "UPDATE RECORD (更新记录)",
            0xA4: "SELECT FILE (选择文件)",
            0xB0: "READ BINARY (读二进制)",
            0xB2: "READ RECORD (读记录)",
            0xC0: "GET RESPONSE (获取响应)",
            0xC2: "ENVELOPE (信封)",
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
            0x6A82: "文件未找到 (File Not Found)",
            0x6A86: "P1/P2错误 (Incorrect P1/P2)",
            0x6A88: "安全消息错误 (Secure Messaging Error)",
            0x6A89: "引用数据错误 (Referenced Data Error)",
            0x6A8A: "参数错误 (Wrong Parameter)",
            0x6A80: "数据字段错误 (Wrong Data Field)",
        }
        if sw1 == 0x61:
            return f"更多数据, Le={sw2} (More Data)"
        elif sw1 == 0x6C:
            return f"长度错误, Le={sw2} (Wrong Length)"
        return sw_names.get(sw_code, f"未知状态字 (0x{sw1:02X}{sw2:02X})")

    @staticmethod
    def parse_length_fields(apdu_data, extra_info):
        """解析APDU长度字段"""
        if len(apdu_data) < 5:
            return
        
        cla = apdu_data[0]
        if cla & 0x10:  # 扩展APDU
            if len(apdu_data) >= 8:
                lc = struct.unpack('>I', b'\x00' + apdu_data[4:7])[0]
                extra_info.append(f"      - Lc: {lc} (扩展)")
                if lc > 0 and len(apdu_data) > 7 + lc:
                    data = apdu_data[7:7+lc]
                    extra_info.append(f"      - 数据: {data.hex().upper()}")
                if len(apdu_data) > 7 + lc + 3:
                    le = struct.unpack('>I', b'\x00' + apdu_data[4+lc:7+lc])[0] if len(apdu_data) > 7+lc else -1
                    extra_info.append(f"      - Le: {le} (扩展)")
        else:  # 短APDU
            lc = apdu_data[4] if len(apdu_data) > 4 else 0
            if lc > 0 and len(apdu_data) > 5:
                data = apdu_data[5:5+lc]
                extra_info.append(f"      - Lc: {lc}")
                extra_info.append(f"      - 数据: {data.hex().upper()}")
            if len(apdu_data) > 5 + lc:
                le = apdu_data[4 + lc + 1] if len(apdu_data) > 4 + lc + 1 else -1
                if le >= 0:
                    extra_info.append(f"      - Le: {le}")

    @staticmethod
    def parse_select_file(p1, p2, apdu_data, extra_info):
        """解析SELECT FILE命令"""
        extra_info.append(f"        - FM1208 SELECT FILE:")
        # P1解析
        p1_desc = {
            0x00: "通过DF名称选择",
            0x01: "通过DF标识符选择",
            0x02: "通过路径选择",
            0x03: "通过DF名称选择 (P2=0x0C)",
        }
        extra_info.append(f"          - P1: 0x{p1:02X} ({p1_desc.get(p1, '保留')})")
        
        # P2解析
        p2_desc = []
        if p2 & 0x08:
            p2_desc.append("返回FCI")
        else:
            p2_desc.append("不返回FCI")
        if p2 & 0x04:
            p2_desc.append("选择子DF")
        if p2 & 0x02:
            p2_desc.append("选择文件")
        if p2 & 0x01:
            p2_desc.append("选择MF")
        extra_info.append(f"          - P2: 0x{p2:02X} ({', '.join(p2_desc)})")
        
        # 解析文件ID或DF名称
        if len(apdu_data) >= 6:
            # 短APDU: CLA INS P1 P2 Lc Data
            data_start = 5
            lc = apdu_data[4] if len(apdu_data) > 4 else 0
            if lc > 0 and len(apdu_data) >= data_start + lc:
                data = apdu_data[data_start:data_start+lc]
                if lc == 2:
                    # 文件标识符 (2字节)
                    file_id = struct.unpack('>H', data[:2])[0]
                    if file_id == 0x3F00:
                        extra_info.append(f"          - MF (主文件): 0x{file_id:04X}")
                    elif file_id == 0xDF01:
                        extra_info.append(f"          - DF (目录文件): 0x{file_id:04X}")
                    else:
                        extra_info.append(f"          - 文件ID: 0x{file_id:04X}")
                elif lc == 4 and data[0] == 0x00:
                    # 可能是DF名称
                    extra_info.append(f"          - DF名称: {data.hex().upper()}")
                else:
                    extra_info.append(f"          - 数据: {data.hex().upper()}")

    @staticmethod
    def parse_read_binary(p1, p2, apdu_data, extra_info):
        """解析READ BINARY命令"""
        extra_info.append(f"        - FM1208 READ BINARY:")
        offset = (p1 << 8) | p2
        extra_info.append(f"          - 偏移: {offset} (0x{offset:04X})")
        # 解析Le
        if len(apdu_data) >= 5:
            le = apdu_data[4]
            extra_info.append(f"          - Le: {le} 字节")

    @staticmethod
    def parse_write_binary(p1, p2, apdu_data, extra_info):
        """解析WRITE BINARY命令"""
        extra_info.append(f"        - FM1208 WRITE BINARY:")
        offset = (p1 << 8) | p2
        extra_info.append(f"          - 偏移: {offset} (0x{offset:04X})")
        # 解析数据
        if len(apdu_data) >= 6:
            lc = apdu_data[4] if len(apdu_data) > 4 else 0
            if lc > 0 and len(apdu_data) >= 5 + lc:
                data = apdu_data[5:5+lc]
                extra_info.append(f"          - 数据: {data.hex().upper()}")

    @staticmethod
    def parse_get_response(p1, p2, apdu_data, extra_info):
        """解析GET RESPONSE命令"""
        extra_info.append(f"        - FM1208 GET RESPONSE:")
        le = p1 if p1 > 0 else p2
        if le == 0:
            le = 256  # Le=0表示256字节
        extra_info.append(f"          - 期望长度: {le} 字节")

    @staticmethod
    def parse_verify(p1, p2, apdu_data, extra_info):
        """解析VERIFY命令 (PIN验证)"""
        extra_info.append(f"        - FM1208 VERIFY:")
        if p1 == 0x00:
            extra_info.append(f"          - PIN验证")
        elif p1 == 0x01:
            extra_info.append(f"          - PIN验证 (重试)")
        else:
            extra_info.append(f"          - P1: 0x{p1:02X}")
        if len(apdu_data) >= 6:
            lc = apdu_data[4] if len(apdu_data) > 4 else 0
            if lc > 0 and len(apdu_data) >= 5 + lc:
                data = apdu_data[5:5+lc]
                extra_info.append(f"          - PIN数据: {data.hex().upper()}")

    @staticmethod
    def parse_internal_auth(p1, p2, apdu_data, extra_info):
        """解析INTERNAL AUTHENTICATE命令"""
        extra_info.append(f"        - FM1208 INTERNAL AUTHENTICATE:")
        if len(apdu_data) >= 6:
            lc = apdu_data[4] if len(apdu_data) > 4 else 0
            if lc > 0 and len(apdu_data) >= 5 + lc:
                data = apdu_data[5:5+lc]
                extra_info.append(f"          - 挑战数据: {data.hex().upper()}")

    @staticmethod
    def parse_external_auth(p1, p2, apdu_data, extra_info):
        """解析EXTERNAL AUTHENTICATE命令"""
        extra_info.append(f"        - FM1208 EXTERNAL AUTHENTICATE:")
        if len(apdu_data) >= 6:
            lc = apdu_data[4] if len(apdu_data) > 4 else 0
            if lc > 0 and len(apdu_data) >= 5 + lc:
                data = apdu_data[5:5+lc]
                extra_info.append(f"          - 认证数据: {data.hex().upper()}")

    @staticmethod
    def parse_get_challenge(p1, p2, apdu_data, extra_info):
        """解析GET CHALLENGE命令"""
        extra_info.append(f"        - FM1208 GET CHALLENGE:")
        if len(apdu_data) >= 5:
            le = apdu_data[4]
            extra_info.append(f"          - 请求长度: {le} 字节")

    @staticmethod
    def parse_get_data(p1, p2, apdu_data, extra_info):
        """解析GET DATA命令"""
        extra_info.append(f"        - FM1208 GET DATA:")
        tag = (p1 << 8) | p2
        extra_info.append(f"          - 标签: 0x{tag:04X}")

    @staticmethod
    def parse_put_data(p1, p2, apdu_data, extra_info):
        """解析PUT DATA命令"""
        extra_info.append(f"        - FM1208 PUT DATA:")
        tag = (p1 << 8) | p2
        extra_info.append(f"          - 标签: 0x{tag:04X}")

    @staticmethod
    def parse_manage_channel(p1, p2, apdu_data, extra_info):
        """解析MANAGE CHANNEL命令"""
        extra_info.append(f"        - FM1208 MANAGE CHANNEL:")
        if p1 == 0x00:
            extra_info.append(f"          - 打开通道")
        elif p1 == 0x80:
            extra_info.append(f"          - 关闭通道 (通道号: {p2})")
        else:
            extra_info.append(f"          - P1: 0x{p1:02X}, P2: 0x{p2:02X}")

    @staticmethod
    def parse_read_record(p1, p2, apdu_data, extra_info):
        """解析READ RECORD命令 (INS=0xB2, ISO 7816-4)

        P1: 记录号 (1~255, 0=RFU)
        P2: b5..b1 = SFI (短文件标识符), b8..b6 = 读取模式
            0x00=首条, 0x20=末条, 0x40=下一条, 0x60=上一条, 0x80=指定记录(P1)
        Le: 期望字节数
        """
        extra_info.append(f"        - FM1208 READ RECORD:")
        if p1 == 0x00:
            extra_info.append(f"          - 记录号: 0x00 (RFU/首条)")
        else:
            extra_info.append(f"          - 记录号: {p1}")

        sfi = p2 & 0x1F
        mode = p2 & 0xE0
        mode_desc = {
            0x00: "首条记录" if p1 == 0 else f"记录 {p1}",
            0x20: "末条记录",
            0x40: "下一条记录",
            0x60: "上一条记录",
            0x80: f"指定记录 (P1={p1})",
        }
        extra_info.append(f"          - 读取模式: {mode_desc.get(mode, f'未知 (0x{mode:02X})')}")
        if sfi > 0:
            extra_info.append(f"          - SFI (短文件标识符): {sfi}")
        # Le
        if len(apdu_data) >= 5:
            le = apdu_data[4]
            le_disp = le if le > 0 else 256
            extra_info.append(f"          - Le: {le} (期望 {le_disp} 字节)")

    @staticmethod
    def parse_write_record(p1, p2, apdu_data, extra_info):
        """解析WRITE RECORD命令 (INS=0xD2, ISO 7816-4)"""
        extra_info.append(f"        - FM1208 WRITE RECORD:")
        extra_info.append(f"          - 记录号: {p1}")
        sfi = p2 & 0x1F
        if sfi > 0:
            extra_info.append(f"          - SFI: {sfi}")
        if len(apdu_data) >= 6:
            lc = apdu_data[4]
            if lc > 0 and len(apdu_data) >= 5 + lc:
                data = apdu_data[5:5+lc]
                extra_info.append(f"          - 记录数据 ({lc}B): {data.hex().upper()}")

    @staticmethod
    def parse_append_record(p1, p2, apdu_data, extra_info):
        """解析APPEND RECORD命令 (INS=0xE2, ISO 7816-4)"""
        extra_info.append(f"        - FM1208 APPEND RECORD:")
        if p1 > 0:
            extra_info.append(f"          - 记录号: {p1}")
        else:
            extra_info.append(f"          - 记录号: 0 (追加到末尾)")
        sfi = p2 & 0x1F
        if sfi > 0:
            extra_info.append(f"          - SFI: {sfi}")
        if len(apdu_data) >= 6:
            lc = apdu_data[4]
            if lc > 0 and len(apdu_data) >= 5 + lc:
                data = apdu_data[5:5+lc]
                extra_info.append(f"          - 记录数据 ({lc}B): {data.hex().upper()}")