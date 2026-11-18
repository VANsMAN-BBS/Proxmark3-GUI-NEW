from pathlib import Path
import xml.etree.ElementTree as ET

TS = str(Path(__file__).resolve().parent.parent / "i18n" / "zh_CN.ts")
MAP = {
    "Model: %1\nUID: %2\n\nPages to clear: %3 (%4 page(s) currently holding data; %5 blank page(s) will be skipped)\nPreserved: %6\n\n%7\n\nOnly 00 00 00 00 is written to confirmed writable user pages. UID, CC, OTP, lock, configuration and PWD/PACK pages are never modified. This cannot be undone and the original data cannot be recovered. A locked tag cannot be restored to factory state by software.":
        "型号：%1\nUID：%2\n\n待清空页：%3（%4 页当前有数据；跳过 %5 个空白页）\n保留：%6\n\n%7\n\n只会向确认可写的用户页写入 00 00 00 00。UID、CC、OTP、锁定、配置和 PWD/PACK 页不会被修改。此操作不可撤销，原始数据无法恢复。被锁定的标签无法通过软件恢复出厂状态。",
    "Target pages: %1 (%2)": "目标页：%1（%2 页）",
}

tree = ET.parse(TS)
applied = 0
for ctx in tree.getroot().findall("context"):
    for msg in ctx.findall("message"):
        source = msg.findtext("source") or ""
        if source in MAP:
            tr = msg.find("translation")
            if tr is None:
                tr = ET.SubElement(msg, "translation")
            tr.text = MAP[source]
            tr.attrib.pop("type", None)
            applied += 1
tree.write(TS, encoding="utf-8", xml_declaration=True)
print("applied", applied)
