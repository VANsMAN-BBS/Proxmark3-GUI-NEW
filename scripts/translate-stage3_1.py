import xml.etree.ElementTree as ET

TS = r"E:\Opencode_project\Proxmark3GUI-master\i18n\zh_CN.ts"
MAP = {
    "Manage Amiibo files and simulate them through the Proxmark3.":
        "管理 Amiibo 数据文件并通过 Proxmark3 模拟。",
    "Amiibo library": "Amiibo 库",
    "Import BIN...": "导入 BIN…",
    "Remove": "移除",
    "Selected Amiibo": "选中的 Amiibo",
    "No image": "无图片",
    "(none)": "（无）",
    "Name:": "名称：",
    "Source:": "来源：",
    "Format:": "格式：",
    "Simulation": "模拟",
    "No Amiibo selected.": "未选择 Amiibo。",
    "Client output": "Client 输出",
    "Online catalog": "在线目录",
    "Catalog URL:": "目录 URL：",
    "https://example.org/amiibo.json": "https://example.org/amiibo.json",
    "Search name": "搜索名称",
    "Search": "搜索",
    "Download BIN": "下载 BIN",
    "No catalog request yet.": "尚未请求在线目录。",
    "Device disconnected; emulation state is unknown.":
        "设备已断开；模拟状态未知。",
    "Source: %1": "来源：%1",
    "Ready.": "已就绪。",
    "The selected file is empty.": "选中的文件为空。",
    "Empty": "空文件",
    "Dump file (%1 pages, %2 bytes)": "Dump 文件（%1 页，%2 字节）",
    "Raw image (%1 pages, %2 bytes)": "原始镜像（%1 页，%2 字节）",
    "Unrecognised (%1 bytes)": "无法识别（%1 字节）",
    "Image: %1": "图片：%1",
    "Loading image...": "正在加载图片…",
    "Import Amiibo BIN": "导入 Amiibo BIN",
    "file": "本地文件",
    "The temporary client file does not match the imported bytes.":
        "临时 Client 文件与导入字节不一致。",
    "Select an Amiibo BIN first.": "请先选择一个 Amiibo BIN。",
    "The selected file is no longer readable: %1": "选中的文件已无法读取：%1",
    "The file changed after import; emulation was not started.":
        "文件在导入后发生变化，未启动模拟。",
    "Simulation running (hf mfu sim -t 7).":
        "模拟运行中（hf mfu sim -t 7）。",
    "Set a catalog URL first.": "请先设置目录 URL。",
    "The catalog URL is not valid.": "目录 URL 无效。",
    "Searching...": "正在搜索…",
    "Catalog image loaded": "目录图片已加载",
    "Catalog request failed: %1": "目录请求失败：%1",
    "The catalog returned an empty BIN.": "目录返回了空 BIN。",
    "Cannot save the downloaded BIN.": "无法保存下载的 BIN。",
    "Catalog Amiibo": "目录 Amiibo",
    "online": "在线",
    "Downloaded %1 bytes.": "已下载 %1 字节。",
    "The catalog returned invalid JSON.": "目录返回了无效 JSON。",
    "%1 catalog entries.": "%1 个目录条目。",
    "Downloading %1...": "正在下载 %1…",
    "Cancel Read": "取消读取",
    "Experimental: phone compatibility has not been fully verified. No new NDEF types planned; critical fixes only.":
        "实验性功能：手机兼容性尚未充分验证。暂不新增 NDEF 类型，仅修复严重问题。",
    "A read is already running.": "已有读取任务正在运行。",
    "No client session available for asynchronous reads.":
        "没有可用于异步读取的 Client 会话。",
    "Reading %1 pages...": "正在读取 %1 页…",
    "Reading %1 selected page(s)...": "正在读取 %1 个选中页…",
    "Reading pages %1..%2 of %3 (async)...": "正在异步读取第 %1..%2 / %3 页…",
    "Reading page %1 (%2 of %3)...": "正在读取第 %1 页（%2/%3）…",
    "Read cancelled: %1 of %2 pages read, %3 failed.":
        "读取已取消：已读 %1/%2 页，失败 %3 页。",
    "Read finished: %1 of %2 pages read, %3 failed.":
        "读取结束：已读 %1/%2 页，失败 %3 页。",
    "Read finished: %1 of %2 pages.": "读取结束：已读 %1/%2 页。",
    "Cancelling after the current command...": "将在当前命令结束后取消…",
    "Write %1 to page %2?": "将 %1 写入第 %2 页？",
    "A read is running; wait for it to finish before writing.":
        "读取任务正在运行，请等待结束再写入。",
    "A read is running; wait for it to finish before verifying.":
        "读取任务正在运行，请等待结束再校验。",
    "A read is running; wait for it to finish before erasing.":
        "读取任务正在运行，请等待结束再清空。",
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
