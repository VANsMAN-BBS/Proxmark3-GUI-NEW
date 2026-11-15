import xml.etree.ElementTree as ET

TS = r"E:\Opencode_project\Proxmark3GUI-master\i18n\zh_CN.ts"
MAP = {
    "Writable pages checked: %1, passed: %2, failed: %3, not checkable: %4":
        "已校验可写页：%1，通过：%2，失败：%3，不可校验：%4",
    "Page %1: expected %2, read %3": "第 %1 页：预期 %2，读回 %3",
    "(not readable)": "（不可读取）",
    "Write verified: writable user data matches.": "写入已验证：可写用户数据一致。",
    "Write succeeded and the writable user data was verified by reading the tag back.":
        "写入成功，并已通过重新读取验证可写用户数据。",
    "Verification OK: writable user data matches.": "校验通过：可写用户数据一致。",
    "The writable user data matches the selected data.": "可写用户数据与所选数据一致。",
    "The tag does not match the written data.": "标签与写入数据不一致。",
    "Read %1 bytes; checked %2 writable pages, %3 failed.":
        "读取 %1 字节；校验 %2 个可写页，%3 个失败。",
    "Verification incomplete: some user pages could not be read.":
        "校验不完整：部分用户页无法读取。",
    "Verification FAILED (first mismatch at page %1).":
        "校验失败（首个不一致在第 %1 页）。",
    "Loading the emulator memory failed. Emulation was not started.":
        "加载模拟器内存失败，未启动模拟。",
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
