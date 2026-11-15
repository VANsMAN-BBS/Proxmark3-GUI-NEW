import xml.etree.ElementTree as ET

TS = r"E:\Opencode_project\Proxmark3GUI-master\i18n\zh_CN.ts"
MAP = {
    "Import Amiibo folder": "导入 Amiibo 文件夹",
    "Scanning folder...": "正在扫描文件夹…",
    "Scanning folder... %1 file(s)": "正在扫描文件夹… %1 个文件",
    "No BIN files were found in this folder.": "此文件夹中没有找到 BIN 文件。",
    "Found %1 BIN file(s). Importing...": "找到 %1 个 BIN 文件，正在导入…",
    "Importing %1 of %2: %3": "正在导入 %1/%2：%3",
    "SKIP  %1  (already in library)": "跳过  %1  （已在库中）",
    "FAIL  %1  (cannot open)": "失败  %1  （无法打开）",
    "FAIL  %1  (empty file)": "失败  %1  （空文件）",
    "OK    %1  (%2 bytes, %3)": "成功  %1  （%2 字节，%3）",
    "      note: same SHA-256 as %1": "      注意：与 %1 的 SHA-256 相同",
    "Amiibo import": "Amiibo 导入",
    "Import complete": "导入完成",
    "Success: %1\nSkipped: %2\nFailed: %3": "成功：%1\n跳过：%2\n失败：%3",
    "Imported %1 file(s), skipped %2, failed %3.":
        "已导入 %1 个文件，跳过 %2 个，失败 %3 个。",
    "Import folder...": "导入文件夹…",
    "Auto compatibility": "自动兼容",
    "Standard MFU load": "标准 MFU 加载",
    "Direct page load (compatibility)": "直接页加载（兼容）",
    "Standard uses the client's MFU dump detection. Direct page load writes the 540-byte image into emulator memory without changing the BIN; it is used when the client rejects the dump header.":
        "标准模式使用 Client 的 MFU dump 识别。直接页加载只把 540 字节镜像写入模拟器内存，不修改 BIN；用于 Client 拒绝 dump 头的情况。",
    "Load mode:": "加载方式：",
    "Loading the selected mode...": "正在按所选方式加载…",
    "File: %1\nSize: %2 bytes\nSHA-256: %3\n":
        "文件：%1\n大小：%2 字节\nSHA-256：%3\n",
    "Client file: %1\n": "Client 文件：%1\n",
    "Could not prepare the client file.": "无法准备 Client 文件。",
    "\nStandard load failed; trying direct page load.\n":
        "\n标准加载失败，正在尝试直接页加载。\n",
    "Standard MFU: %1\nDirect page load: %2":
        "标准 MFU：%1\n直接页加载：%2",
    "Unknown load mode.": "未知加载方式。",
    "Loading failed.": "加载失败。",
    "The client accepted the data but refused to start the simulation.":
        "Client 接受了数据，但拒绝启动模拟。",
    "Simulation running (%1).": "模拟运行中（%1）。",
    "The client could not detect or convert the MFU dump format.":
        "Client 无法识别或转换 MFU dump 格式。",
    "The client did not confirm MFU dump format detection.":
        "Client 未确认识别到 MFU dump 格式。",
    "Direct page load supports 540-byte images only.":
        "直接页加载仅支持 540 字节镜像。",
    "The image has no valid NTAG Capability Container (E1 10 12/3E/6D 00).":
        "镜像没有有效的 NTAG Capability Container（E1 10 12/3E/6D 00）。",
    "The image has an empty UID; it is not a usable NTAG image.":
        "镜像的 UID 为空，不是可用的 NTAG 镜像。",
    "[Standard MFU] %1": "[标准 MFU] %1",
    "Could not clear emulator memory.": "无法清空模拟器内存。",
    "Could not set the emulator header.": "无法设置模拟器头。",
    "The emulator header was not accepted (page count mismatch).":
        "模拟器头未被接受（页数不匹配）。",
    "Could not load emulator pages %1..%2.":
        "无法加载模拟器第 %1..%2 页。",
}

tree = ET.parse(TS)
applied = set()
for ctx in tree.getroot().findall("context"):
    for msg in ctx.findall("message"):
        source = msg.findtext("source") or ""
        if source in MAP:
            tr = msg.find("translation")
            if tr is None:
                tr = ET.SubElement(msg, "translation")
            tr.text = MAP[source]
            tr.attrib.pop("type", None)
            applied.add(source)
missing = [s for s in MAP if s not in applied]
tree.write(TS, encoding="utf-8", xml_declaration=True)
print("mapped", len(MAP), "applied", len(applied), "missing", len(missing))
for source in missing:
    print("MISSING:", source[:100])
