#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Mifare Classic 卡片解析器
"""

class MifareClassicParser:
    """Mifare Classic 解析器"""
    
    CMD_AUTH_A = 0x60
    CMD_AUTH_B = 0x61
    CMD_READ = 0x30
    CMD_WRITE = 0xA0
    CMD_INCREMENT = 0xC1
    CMD_DECREMENT = 0xC0
    CMD_RESTORE = 0xC2
    CMD_TRANSFER = 0xB0
    
    @staticmethod
    def parse(cmd, payload, data_len, result):
        cmd_desc = ""
        
        if cmd == MifareClassicParser.CMD_AUTH_A:
            if data_len >= 2:
                block = payload[1]
                cmd_desc = f"Mifare Classic Auth A (block={block})"
            else:
                cmd_desc = "Mifare Classic Auth A"
        elif cmd == MifareClassicParser.CMD_AUTH_B:
            if data_len >= 2:
                block = payload[1]
                cmd_desc = f"Mifare Classic Auth B (block={block})"
            else:
                cmd_desc = "Mifare Classic Auth B"
        elif cmd == MifareClassicParser.CMD_READ:
            if data_len >= 2:
                block = payload[1]
                cmd_desc = f"Mifare Classic Read (block={block})"
            else:
                cmd_desc = "Mifare Classic Read"
        elif cmd == MifareClassicParser.CMD_WRITE:
            if data_len >= 2:
                block = payload[1]
                cmd_desc = f"Mifare Classic Write (block={block})"
            else:
                cmd_desc = "Mifare Classic Write"
        elif cmd == MifareClassicParser.CMD_INCREMENT:
            if data_len >= 2:
                block = payload[1]
                cmd_desc = f"Mifare Classic Increment (block={block})"
            else:
                cmd_desc = "Mifare Classic Increment"
        elif cmd == MifareClassicParser.CMD_DECREMENT:
            if data_len >= 2:
                block = payload[1]
                cmd_desc = f"Mifare Classic Decrement (block={block})"
            else:
                cmd_desc = "Mifare Classic Decrement"
        elif cmd == MifareClassicParser.CMD_RESTORE:
            if data_len >= 2:
                block = payload[1]
                cmd_desc = f"Mifare Classic Restore (block={block})"
            else:
                cmd_desc = "Mifare Classic Restore"
        elif cmd == MifareClassicParser.CMD_TRANSFER:
            if data_len >= 2:
                block = payload[1]
                cmd_desc = f"Mifare Classic Transfer (block={block})"
            else:
                cmd_desc = "Mifare Classic Transfer"
        
        return cmd_desc