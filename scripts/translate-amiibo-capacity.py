import xml.etree.ElementTree as ET

TS = r"E:\Opencode_project\Proxmark3GUI-master\i18n\zh_CN.ts"
MAP = {
    "The tag holds %1 bytes but the image is %2 bytes.":
        "标签容量为 %1 字节，但镜像为 %2 字节。",
    "NTAG213 image (180 bytes)": "NTAG213 镜像（180 字节）",
    "NTAG216 image (924 bytes)": "NTAG216 镜像（924 字节）",
    "The tag does not match the written data (read %1 bytes, expected at least %2).":
        "标签与写入数据不一致（读到 %1 字节，至少需要 %2 字节）。",
}

tree = ET.parse(TS)
applied = 0
for ctx in tree.getroot().findall("context"):
    if ctx.findtext("name") != "AmiiboPage":
        continue
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
