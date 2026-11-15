import xml.etree.ElementTree as ET

TS = r"E:\Opencode_project\Proxmark3GUI-master\i18n\zh_CN.ts"

MAP = {
    "The file is empty.": "文件为空。",
    "Length %1 bytes is not a multiple of 4 (or is shorter than 48 bytes). The client will probably reject it; the file is passed through unchanged.":
        "长度 %1 字节不是 4 的倍数（或小于 48 字节）。Client 很可能拒绝该文件；文件会原样传递。",
    "Local check: dump file, 56-byte header + %1 pages (%2 bytes). It is passed through unchanged; the client detects new/old/plain format itself.":
        "本地检查：dump 文件，56 字节头 + %1 页（%2 字节）。文件原样传递；由 Client 自行识别 new/old/plain 格式。",
    "Local check: headerless page image, %1 pages (%2 bytes). It is passed through unchanged; the client detects plain/new/old format itself.":
        "本地检查：无头页镜像，%1 页（%2 字节）。文件原样传递；由 Client 自行识别 plain/new/old 格式。",
    "Local check: no known layout matched (%1 bytes). No conversion is attempted; the client decides whether it can load the file.":
        "本地检查：未匹配已知布局（%1 字节）。不进行任何转换，由 Client 决定能否加载。",
    "Amiibo raw BIN": "Amiibo 原始 BIN",
    "Import raw BIN...": "导入原始 BIN…",
    "(no file)": "（无文件）",
    "Path:": "路径：",
    "Input check:": "输入检查：",
    "This page only imports a file and starts the client simulator. Tag detection, read, write and verify were removed: use the NTAG page for tag operations. The file is not parsed, re-headed, trimmed or padded.":
        "本页只负责导入文件并启动 Client 模拟。标签检测、读取、写入和校验已移除：请在 NTAG 页面进行标签操作。文件不会被解析、加头、截断或补零。",
    "Runs `hf mfu eload -f <the imported file>` followed by `hf mfu sim -t 7`.":
        "依次执行 `hf mfu eload -f <导入文件>` 和 `hf mfu sim -t 7`。",
    "No file loaded.": "未加载文件。",
    "Client output from `hf mfu eload` and the simulation command.":
        "`hf mfu eload` 与模拟命令的 Client 输出。",
    "Raw passthrough: the imported bytes are handed to the client unchanged. The client's own format detection decides whether the file is a plain image, a current dump or an old dump. If the client rejects the file, its original error is shown here.":
        "原始直通：导入的字节原样交给 Client。由 Client 自行判断文件是 plain 镜像、当前 dump 还是旧 dump。若 Client 拒绝，会在这里显示其原始错误。",
    "The device disconnected; emulation state is unknown.":
        "设备已断开；模拟状态未知。",
    "Import raw Amiibo BIN": "导入原始 Amiibo BIN",
    "Amiibo BIN (*.bin);;All files (*)": "Amiibo BIN (*.bin);;所有文件 (*)",
    "File loaded. It will be passed to the client unchanged.":
        "文件已加载，将原样传递给 Client。",
    "The client returned no output.": "Client 没有返回输出。",
    "The client reported an error.": "Client 报告错误。",
    "The client did not confirm that the file was loaded.":
        "Client 未确认文件已加载。",
    "Cannot write the temporary client file:\n%1": "无法写入临时 Client 文件：\n%1",
    "Cannot verify the temporary client file.": "无法校验临时 Client 文件。",
    "The temporary client file does not match the imported bytes; emulation was not started.":
        "临时 Client 文件与导入字节不一致，未启动模拟。",
    "Connect the Proxmark3 first.": "请先连接 Proxmark3。",
    "Import a raw BIN file first.": "请先导入原始 BIN 文件。",
    "The imported file is no longer readable: %1": "导入的文件已无法读取：%1",
    "The file changed after import (size %1, SHA-256 %2); emulation was not started.":
        "文件在导入后发生变化（大小 %1，SHA-256 %2），未启动模拟。",
    "Loading the file into emulator memory...": "正在加载到模拟器内存…",
    "Client file: %1\nSize: %2 bytes\nSHA-256: %3\n\n%4":
        "Client 文件：%1\n大小：%2 字节\nSHA-256：%3\n\n%4",
    "The file was not modified; see the client output below.":
        "文件未被修改；请查看下方 Client 输出。",
    "The client accepted the file but refused to start the simulation.":
        "Client 已接受文件，但拒绝启动模拟。",
    "Simulation command started. The client is running `hf mfu sim -t 7`; press Stop to end it.":
        "模拟命令已启动。Client 正在运行 `hf mfu sim -t 7`，点击“停止”结束。",
    "Stopping simulation...": "正在停止模拟…",
    "Simulation stopped.": "模拟已停止。",

    "Dump failed; falling back to per-page reads with --force...":
        "dump 失败；改用带 --force 的逐页读取…",
    "Reading page %1 of %2...": "正在读取第 %1 / %2 页…",
    "Read %1 of %2 pages (per-page).": "已读取 %1 / %2 页（逐页）。",
    "Page %1 has uncommitted edits. Re-read it and discard those edits?":
        "第 %1 页有未提交的修改。是否重新读取并放弃这些修改？",
    "page data must be 8 hex digits": "页数据必须是 8 个十六进制字符",
    "page %1 is not writable under the detected layout":
        "第 %1 页在当前布局下不可写",
    "key format is invalid": "密钥格式无效",
    "client write failed for page %1": "第 %1 页 Client 写入失败",
    "page %1 was written but could not be read back":
        "第 %1 页已写入但无法复读",
    "wrote %1, read %2": "写入 %1，读回 %2",
    "verification differs": "校验不一致",
    "Read page %1 before writing it.": "写入前请先读取第 %1 页。",
    "Write selected pages": "写入选中页",
    "There are no uncommitted edits to write.": "没有未提交的修改可写入。",
    "Write dirty pages": "写入修改页",
    "Page %1 has not been read; write cancelled.":
        "第 %1 页尚未读取，已取消写入。",
    "Page %1 is read-only; write cancelled.": "第 %1 页为只读，已取消写入。",
    "The selected pages have no uncommitted changes.":
        "选中的页面没有未提交的修改。",
    "Write %1 page(s)?\n%2\n\nOnly these user pages are written; UID, CC, lock, OTP, configuration and PWD/PACK are never modified. The result is verified by reading each page back.":
        "写入 %1 页？\n%2\n\n只会写入这些用户页；UID、CC、锁定、OTP、配置和 PWD/PACK 永远不会被修改。结果会通过逐页复读验证。",
    "Write cancelled.": "已取消写入。",
    "Write stopped at page %1 after one retry: %2\n%3 page(s) before it were written and verified. No later page was attempted.":
        "第 %1 页在重试一次后停止：%2\n之前的 %3 页已写入并验证，后续页面未再尝试。",
    "Write stopped at page %1: %2": "写入在第 %1 页停止：%2",
    "No written or edited pages to verify.": "没有可校验的已写入或已修改页面。",
    "Verify written pages": "校验已写入页",
    "Read page %1 before verifying it.": "校验前请先读取第 %1 页。",
    "Only the selected page is compared with its edited value.":
        "只把选中页与其编辑值比较。",
    "Erase reads the tag again and replaces the current view.":
        "清空操作会重新读取标签并替换当前视图。",
    "Quick Launch reads the tag again before writing.":
        "快捷启动写入前会重新读取标签。",
    "ATQA:": "ATQA：",
    "SAK:": "SAK：",
    "Pages read:": "已读取页：",
    "Writable pages:": "可写页：",
    "Lock state:": "锁定状态：",
    "B0": "B0",
    "B1": "B1",
    "B2": "B2",
    "B3": "B3",
    "Page type": "页面类型",
    "State": "状态",
    "Selected page": "选中页",
    "Page:": "页：",
    "Original:": "原始：",
    "Edited:": "编辑后：",
    "Write access:": "写权限：",
    "Last write:": "最近写入：",
    "Last read:": "最近读取：",
    "Show PWD/PACK data": "显示 PWD/PACK 数据",
    "Refresh": "刷新",
    "Undo": "撤销",
    "Redo": "重做",
    "Discard Edits": "放弃修改",
    "Export...": "导出…",
    "Import...": "导入…",
    "Write Dirty": "写入修改页",
    "Verify Written": "校验已写入",
    "States: Read / Modified / Written / Verified / Read failed / Not read. Double-click a byte to edit; Ctrl+C/V copies and pastes hex; Ctrl+Z/Y undoes. Only writable user pages can be changed.":
        "状态：已读取 / 已修改 / 已写入 / 已验证 / 读取失败 / 未读取。双击字节编辑；Ctrl+C/V 复制粘贴十六进制；Ctrl+Z/Y 撤销。只有可写用户页可以修改。",
    "Quick Launch (NDEF URI) - Experimental": "快捷启动（NDEF URI）— 实验性",
    "Experimental feature: real-phone compatibility has not been fully verified. It is not a stable feature. No new NDEF types are being added; only fixes that affect crashes or other pages are accepted.":
        "实验性功能：尚未充分验证真实手机兼容性，不是稳定功能。不再新增 NDEF 类型，只接受影响崩溃或其他页面的修复。",
    "%1 (4 bytes/page)": "%1 页（每页 4 字节）",
    "Modified": "已修改",
    "Verified": "已验证",
    "Write failed": "写入失败",
    "Written": "已写入",
    "page data is empty": "页数据为空",
    "%1 / %2": "%1 / %2",
    "static %1": "静态 %1",
    "static unknown": "静态未知",
    "dynamic %1": "动态 %1",
    "dynamic unknown": "动态未知",
    "Uncommitted edits": "未提交的修改",
    "%1\n%2 page(s) have uncommitted edits. Discard them?":
        "%1\n%2 页有未提交的修改。是否放弃？",
    "Undone.": "已撤销。",
    "Redone.": "已重做。",
    "Discard all uncommitted edits?": "放弃所有未提交的修改？",
    "Uncommitted edits discarded.": "已放弃未提交的修改。",
    "lock bytes (read-only)": "锁定字节（只读）",
    "not a lock page": "非锁定页",
    "written": "已写入",
    "failed: %1": "失败：%1",
    "Manufacturer / UID page: never editable, never written.":
        "制造商 / UID 页：永远不可编辑、不会写入。",
    "UID page: never editable, never written.":
        "UID 页：永远不可编辑、不会写入。",
    "Lock page: protected; erase/write never touches it.":
        "锁定页：受保护；清空/写入不会触碰。",
    "Capability Container / configuration: read-only here.":
        "Capability Container / 配置：此处只读。",
    "PWD/PACK: masked by default and never written.":
        "PWD/PACK：默认遮蔽，永不写入。",
    "Writable user page. Edits stay local until Write Selected/Dirty.":
        "可写用户页。修改在点击“写入选中/修改页”前只保留在本地。",
    "Not applicable or unknown for the detected layout.":
        "对当前布局不适用或未知。",
    "A byte must be exactly two hex digits.":
        "一个字节必须正好是两位十六进制。",
    "Page %1 is not editable.": "第 %1 页不可编辑。",
    "Page %1 edited (not written).": "第 %1 页已修改（尚未写入）。",
    "Select hex byte cells to copy.": "请选择十六进制字节单元格进行复制。",
    "Copied %1 byte(s).": "已复制 %1 字节。",
    "Clipboard is not hex data: %1": "剪贴板不是十六进制数据：%1",
    "Select a starting page first.": "请先选择起始页。",
    "Paste refused: %1": "拒绝粘贴：%1",
    "Pasted %1 byte(s). Not written yet.": "已粘贴 %1 字节，尚未写入。",
    "Import failed: not a dump file and not a multiple of 4 bytes.":
        "导入失败：不是 dump 文件，也不是 4 字节的倍数。",
    "Import failed: no writable, already-read pages matched.":
        "导入失败：没有匹配的可写且已读取页面。",
    "Imported %1 page(s); skipped %2 read-only/out-of-range page(s). Nothing was written to the tag.":
        "已导入 %1 页；跳过 %2 个只读/越界页。没有向标签写入任何内容。",
    "Detect and read the tag before importing a memory image.":
        "导入内存镜像前请先检测并读取标签。",
    "Import memory image": "导入内存镜像",
    "Importing replaces the edited values of matching pages. Continue with the current edits?":
        "导入会替换匹配页的编辑值。是否在保留当前修改的情况下继续？",
    "Dump files (*.bin *.dump *.json);;All files (*)":
        "转储文件 (*.bin *.dump *.json);;所有文件 (*)",
    "Cannot open %1": "无法打开 %1",
    "Read all pages before exporting an image.":
        "导出镜像前请先读取全部页面。",
    "Export NTAG memory image": "导出 NTAG 内存镜像",
    "PM3 dump (*.bin)": "PM3 转储 (*.bin)",
    "Cannot build the dump file.": "无法生成 dump 文件。",
    "Cannot write %1": "无法写入 %1",
    "Exported %1 bytes to %2 (edited values, not read back).":
        "已导出 %1 字节到 %2（编辑值，尚未复读验证）。",
    "The tag UID changed between identification steps; nothing was read or written.":
        "标签 UID 在识别步骤之间发生变化；未读取或写入任何内容。",
    "read failed": "读取失败",
    "A full read replaces the current memory view.":
        "全量读取会替换当前内存视图。",
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
