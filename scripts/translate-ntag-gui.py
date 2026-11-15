"""zh_CN translations for the NTAG memory browser / NDEF view."""

import xml.etree.ElementTree as ET

TS = r"E:\Opencode_project\Proxmark3GUI-master\i18n\zh_CN.ts"

MAP = {
    "Model:": "型号：",
    "GET_VERSION:": "GET_VERSION：",
    "Capability Container:": "能力容器（CC）：",
    "Capacity:": "容量：",
    "Region": "区域",
    "Status": "状态",
    "NDEF": "NDEF",
    "Read the tag to parse CC / TLV / NDEF records.": "读取标签后可解析 CC / TLV / NDEF 记录。",
    "Memory": "内存",
    "Read": "已读取",
    "Read failed": "读取失败",
    "Unavailable": "不可用",
    "Not read": "未读取",
    "Manufacturer / UID0-2 / BCC0": "厂商 / UID0-2 / BCC0",
    "UID3-6": "UID3-6",
    "BCC1 / Internal / Lock": "BCC1 / 内部 / 锁定位",
    "Capability Container": "能力容器（CC）",
    "Dynamic lock": "动态锁定位",
    "Configuration 0": "配置页 0",
    "Configuration 1": "配置页 1",
    "PWD": "PWD",
    "PACK": "PACK",
    "User data": "用户数据",
    "Vendor / reserved": "厂商 / 保留",
    "Out of range": "超出范围",
    "(not supported)": "（不支持）",
    "%1 pages (0..%2), user %3..%4, %5 B": "%1 页（0..%2），用户区 %3..%4，%5 字节",
    "Reading pages %1..%2 of %3...": "正在读取第 %1..%2 页（共 %3 页）…",
    "Dump read %1 of %2 pages.": "已读取 %1 / %2 页。",
    "Capability Container: %1": "能力容器（CC）：%1",
    "  NDEF capacity: %1 bytes": "  NDEF 容量：%1 字节",
    "  Access: %1": "  访问权限：%1",
    "read/write": "可读写",
    "read-only": "只读",
    "  Multiple block read: %1": "  多块读：%1",
    "supported": "支持",
    "not supported": "不支持",
    "No valid Capability Container was read.": "未读取到有效的 CC。",
    "NDEF: no user data was read.": "NDEF：未读取到用户数据。",
    "NDEF: some user pages are missing; parsing may be incomplete.":
        "NDEF：部分用户页缺失，解析可能不完整。",
    "NDEF message: %1 bytes, %2 record(s)": "NDEF 消息：%1 字节，%2 条记录",
    "NDEF parse failed: %1": "NDEF 解析失败：%1",
    "Raw user data: %1": "原始用户数据：%1",
    "Reading %1 pages with `hf mfu dump`...": "正在用 `hf mfu dump` 读取 %1 页…",
    "Dump failed; falling back to per-page reads (limited by the client).":
        "dump 失败，改为逐页读取（受客户端上限限制）。",
    "Read %1 of %2 pages (per-page; the client caps rdbl at page 41).":
        "已读取 %1 / %2 页（逐页读取；客户端 rdbl 上限为第 41 页）。",
}


def main() -> int:
    tree = ET.parse(TS)
    applied = 0
    missing = []
    for ctx in tree.getroot().findall("context"):
        if ctx.findtext("name") != "NtagTab":
            continue
        for msg in ctx.findall("message"):
            source = msg.findtext("source") or ""
            target = MAP.get(source)
            tr = msg.find("translation")
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
