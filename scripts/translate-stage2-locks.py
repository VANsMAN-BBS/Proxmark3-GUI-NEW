from pathlib import Path
import xml.etree.ElementTree as ET

TS = str(Path(__file__).resolve().parent.parent / "i18n" / "zh_CN.ts")
MAP = {
    "(not read)": "（未读取）",
    "Unverified: %1 targeted page(s) were cleared but %2 page(s) could not be read back.":
        "未经确认：已清除 %1 个目标页，但 %2 个页面无法重新读取。",
    "Partially erased: %1 of %2 targeted page(s) were cleared, %3 still hold data and %4 could not be read.":
        "部分清空：已清除 %1/%2 个目标页，%3 个页仍有数据，%4 个页无法读取。",
    "The Capability Container read back from the tag is missing or read-only. Nothing was written.":
        "从标签读回的 Capability Container 缺失或为只读，未写入任何内容。",
    "The static/dynamic lock pages could not be read, so write protection cannot be checked. Nothing was written.":
        "无法读取静态/动态锁定页，不能检查写保护，未写入任何内容。",
    "Lock bits protect the target user pages (static %1, dynamic %2). The write was refused instead of bypassing the lock. No page was modified.":
        "锁定位保护了目标用户页（静态 %1，动态 %2）。已拒绝写入，不会绕过锁定，未修改任何页。",
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
