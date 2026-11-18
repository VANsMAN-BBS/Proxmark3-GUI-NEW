"""zh_CN translations for the LF multi-protocol parsing page."""

from pathlib import Path
import xml.etree.ElementTree as ET

TS = str(Path(__file__).resolve().parent.parent / "i18n" / "zh_CN.ts")

LF = {
    "Not provided": "未提供",
    "Client text input": "客户端文本输入",
    "Paste Proxmark3 LF output (lf search / protocol readers) or import a text file. Parsing is pure text and works without LF-capable firmware.":
        "粘贴 Proxmark3 低频输出（lf search / 各协议读取命令），或导入文本文件。解析完全基于文本，无需支持低频的固件。",
    "[+] EM 410x ID 0F0368568B\n[+] Valid HID Prox ID found!":
        "[+] EM 410x ID 0F0368568B\n[+] Valid HID Prox ID found!",
    "Parse": "解析",
    "Clear": "清空",
    "Import text...": "导入文本…",
    "Use RawCommand output": "使用原始命令输出",
    "Source: manual input": "来源：手动输入",
    "Parsed card": "解析结果",
    "No input parsed yet.": "尚未解析任何输入。",
    "Parsed fields": "解析字段",
    "Field": "字段",
    "Value": "值",
    "Raw output": "原始输出",
    "Warnings and unparsed fields": "警告与未解析字段",
    "T55xx (read-only)": "T55xx（只读）",
    "manual input": "手动输入",
    "Source: %1": "来源：%1",
    "RawCommand output": "原始命令输出",
    "Import LF output text": "导入低频输出文本",
    "Text files (*.txt *.log *.md);;All files (*)": "文本文件 (*.txt *.log *.md);;所有文件 (*)",
    "Cannot open the file.": "无法打开文件。",
    "text file": "文本文件",
    "Country code": "国家代码",
    "Animal bit": "动物位",
    "Extended data": "扩展数据",
    "Extended value": "扩展值",
    "CRC": "CRC",
    "CRC state": "CRC 状态",
    "RUDI bit": "RUDI 位",
    "Reserved": "保留",
    "Unique tag ID": "唯一标签 ID",
    "RF divisor": "射频分频",
    "Facility code (decimal)": "设施码（十进制）",
    "Facility code (hex)": "设施码（十六进制）",
    "Card (hex)": "卡号（十六进制）",
    "Card ID (hex)": "卡 ID（十六进制）",
    "Region code": "区域代码",
    "OEM code": "OEM 代码",
    "Customer code": "客户代码",
    "Customer code (hex)": "客户代码（十六进制）",
    "Issue level": "签发级别",
    "Keri type": "Keri 类型",
    "Wiegand format": "Wiegand 格式",
    "Wiegand": "Wiegand",
    "Parity": "校验位",
    "Checksum": "校验和",
    "Checksum state": "校验和状态",
    "Raw ID": "原始 ID",
    "Raw payload": "原始载荷",
    "Full code": "完整编码",
    "Frame": "帧",
    "Frame data": "帧数据",
    "Subtype": "子类型",
    "Variant": "变体",
    "Version": "版本",
    "Year": "年份",
    "Mode": "模式",
    "Decimal (8 digits)": "十进制（8 位）",
    "Decimal (14 digits)": "十进制（14 位）",
    "Decimal (15 digits)": "十进制（15 位）",
    "Cansec 37 facility code": "Cansec 37 设施码",
    "Cansec 37 card number": "Cansec 37 卡号",
    "Keyscan 40010 facility code": "Keyscan 40010 设施码",
    "Keyscan 40010 card number": "Keyscan 40010 卡号",
    "CDVI pattern facility code": "CDVI 模板设施码",
    "CDVI pattern card number": "CDVI 模板卡号",
    "RBH card number": "RBH 卡号",
    "HCC40 ID": "HCC40 ID",
    "PAC card (hex)": "PAC 卡号（十六进制）",
    "ID88 (hex)": "ID88（十六进制）",
    "Internal ID": "内部 ID",
    "Displayed value": "显示值",
    "Printed value": "打印值",
    "Printed value (hex)": "打印值（十六进制）",
    "Second card": "第二卡号",
    "Fingerprint": "指纹",
    "Electra": "Electra",
    "Heden 2L": "Heden 2L",
    "XL high bits": "XL 高位",
    "XL low bits": "XL 低位",
    "XOR": "异或",
    "Input is empty.": "输入为空。",
    "Paste Proxmark3 LF output or import a text file.": "请粘贴 Proxmark3 低频输出或导入文本文件。",
    "Warnings:": "警告：",
    "Unparsed fields:": "未解析字段：",
    "Format candidates: %1": "候选格式：%1",
    "No warnings.": "无警告。",
    "Identified": "已识别",
    "Partial parse": "部分解析",
    "No supported protocol found": "未找到受支持的协议",
    "Invalid input": "输入无效",
    "Format: %1": "格式：%1",
    "%1 bit": "%1 位",
    "Confidence: %1": "置信度：%1",
    "Ambiguous format: %1 (the parser deliberately does not pick one).":
        "格式存在歧义：%1（解析器不会擅自选择其一）。",
    "Protocol recognised, but required fields are incomplete.":
        "已识别协议，但必需字段不完整。",
    "No supported LF protocol signature was found in this text.":
        "该文本中未找到受支持的低频协议特征。",
    "The client reports an unsupported or missing command: the connected firmware may not include LF support. Text parsing still works.":
        "客户端报告命令不受支持或缺失：当前固件可能不含低频功能。文本解析仍然可用。",
    "Protocol": "协议",
    "Format": "格式",
    "Bit length": "位长",
    "Facility code": "设施码",
    "Card number": "卡号",
    "Hex ID": "十六进制 ID",
    "Decimal ID": "十进制 ID",
    "Raw data": "原始数据",
    "No T55xx detect/info data was found in this text.":
        "该文本中没有 T55xx detect/info 数据。",
    "T55xx parsing is read-only: this page never writes, locks or erases a tag.":
        "T55xx 解析为只读：本页面不会写入、锁定或擦除标签。",
    "Detected: %1": "检测结果：%1",
    "yes": "是",
    "no": "否",
    "Chip: %1": "芯片：%1",
    "Password set: %1": "已设置密码：%1",
    "Block 0: %1": "块 0：%1",
    "Modulation: %1": "调制方式：%1",
    "Bit rate: %1": "比特率：%1",
    "Offset: %1": "偏移：%1",
    "Inverted: %1": "反相：%1",
    "Block 0 decoded (read-only):": "块 0 解码（只读）：",
    "  Modulation: %1": "  调制方式：%1",
    "  Extended mode: %1": "  扩展模式：%1",
    "  Bit rate index: %1": "  比特率索引：%1",
    "  RF divisor: RF/%1": "  射频分频：RF/%1",
    "  Max block: %1": "  最大块：%1",
    "  Master key: %1": "  主密钥：%1",
    "  Warning: %1": "  警告：%1",
    "This page is read-only for T55xx: no write, lock, OTP or password operation is offered.":
        "本页面对 T55xx 仅做只读展示：不提供写入、锁定、OTP 或密码操作。",
}

WINDOW = {
    "Low frequency functions": "低频功能",
    "LF multi-protocol parsing": "低频多协议解析",
}

MAPS = {"LfParserTab": LF, "MainWindow": WINDOW}


def main() -> int:
    tree = ET.parse(TS)
    root = tree.getroot()
    applied = 0
    missing = []
    for ctx in root.findall("context"):
        mapping = MAPS.get(ctx.findtext("name"))
        if not mapping:
            continue
        for msg in ctx.findall("message"):
            source = msg.findtext("source") or ""
            tr = msg.find("translation")
            target = mapping.get(source)
            if target is None:
                if tr is not None and (tr.get("type") == "unfinished"
                                       or not (tr.text or "").strip()):
                    missing.append(source)
                continue
            if tr is None:
                tr = ET.SubElement(msg, "translation")
            tr.text = target
            tr.attrib.pop("type", None)
            applied += 1
    tree.write(TS, encoding="utf-8", xml_declaration=True)
    print("applied", applied)
    for item in missing:
        print("  missing:", item.replace("\n", "\\n"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
