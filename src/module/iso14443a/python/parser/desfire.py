#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Mifare DESFire 卡片解析器
"""

import sys
import os

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from ui.language import Language


class DESFireParser:
    """Mifare DESFire 解析器"""
    
    CMD_DESFIRE_CLA = 0x90
    
    @staticmethod
    def parse(cmd, payload, data_len, result):
        cmd_desc = ""
        
        if cmd == DESFireParser.CMD_DESFIRE_CLA:
            if data_len >= 4:
                ins = payload[1] if data_len >= 2 else 0
                p1 = payload[2] if data_len >= 3 else 0
                p2 = payload[3] if data_len >= 4 else 0
                cmd_desc = f"DESFire APDU (CLA=0x{cmd:02X}, INS=0x{ins:02X}, P1=0x{p1:02X}, P2=0x{p2:02X})"
                result["extra_info"].append(f"  - DESFire command: INS=0x{ins:02X}")
            else:
                cmd_desc = "DESFire APDU"
        
        return cmd_desc