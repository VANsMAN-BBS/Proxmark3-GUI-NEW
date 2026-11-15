import xml.etree.ElementTree as ET

TS = r"E:\Opencode_project\Proxmark3GUI-master\i18n\zh_CN.ts"

MAP = {
    # MainWindow / T55xx leftovers
    "NTAG/Ultralight": "NTAG / Ultralight",
    "T55xx": "T55xx",

    # AmiiboPage
    "Data mode:": "数据模式：",
    "SHA-256:": "SHA-256：",
    "%1 bytes of page data (file %2 bytes)": "页数据 %1 字节（文件 %2 字节）",
    "(not applicable)": "（不适用）",
    "SHA-256 of the imported file bytes": "导入文件原始字节的 SHA-256",
    "Raw BIN (dump file: 56-byte header + pages, original bytes kept)":
        "原始 BIN（dump 文件：56 字节头 + 页数据，保留原始字节）",
    "Raw image (headerless page data)": "原始镜像（无头的页数据）",
    "Synthetic / in-memory image": "合成 / 内存镜像",
    "Unsupported data length: %1 bytes.\nExpected a 540-byte NTAG215 image, an 180/924-byte image, or a dump file (56-byte header + pages).":
        "不支持的数据长度：%1 字节。\n需要 540 字节 NTAG215 镜像、180/924 字节镜像，或 dump 文件（56 字节头 + 页数据）。",
    "Loaded %1 (%2 bytes, SHA-256 %3).": "已载入 %1（%2 字节，SHA-256 %3）。",
    "Saved %1 (%2 bytes, unchanged).": "已保存 %1（%2 字节，内容未改变）。",
    "Emulation requires a 180/540/924-byte NTAG image.":
        "模拟需要 180/540/924 字节的 NTAG 镜像。",
    "The simulation input does not preserve the source payload; emulation was not started.":
        "模拟输入未能保持源数据负载，已取消模拟。",
    "The imported file changed before emulation; emulation was not started.":
        "导入文件在模拟前发生变化，已取消模拟。",
    "Emulating %1 bytes from %2 data. Press Stop to end.":
        "正在用%2数据模拟 %1 字节，点击“停止”结束。",
    "Downloaded %1 bytes (SHA-256 %2) and added it to the library.":
        "已下载 %1 字节（SHA-256 %2）并加入列表。",
    "The data could not be converted to the dump format without changing the payload.":
        "无法在不改变负载的情况下转换为 dump 格式。",

    # NtagTab - erase UI
    "Erase Tag...": "清空标签…",
    "Clear writable user data pages only; UID, CC, lock, OTP, configuration and password pages are never modified.":
        "只清除可写的用户数据页；UID、CC、锁定、OTP、配置和密码页不会被修改。",
    "Quick Launch (NDEF URI)": "快捷启动（NDEF URI）",
    "Web link (https://...)": "网页链接（https://…）",
    "Phone number (tel:)": "电话号码（tel:）",
    "SMS (sms:)": "短信（sms:）",
    "E-mail (mailto:)": "邮箱（mailto:）",
    "Custom URI": "自定义 URI",
    "Wi-Fi (not supported)": "Wi-Fi（暂不支持）",
    "Launch type:": "启动类型：",
    "URL:": "网址：",
    "Subject:": "主题：",
    "Body:": "正文：",
    "Detect a tag to see the NDEF capacity.": "检测标签后显示 NDEF 容量。",
    "The generated NDEF message and TLV appear here.": "生成的 NDEF 消息和 TLV 显示在这里。",
    "Preview and Write...": "预览并写入…",
    "Quick Launch": "快捷启动",
    "manufacturer (read-only)": "制造商（只读）",
    "UID (read-only)": "UID（只读）",
    "lock (read-only)": "锁定（只读）",
    "config / CC (read-only)": "配置 / CC（只读）",
    "security (read-only)": "安全区（只读）",
    "reserved / not applicable": "保留 / 不适用",
    "Write verification": "写入校验",
    "Wrote %1 of %2 pages; checking them against a fresh read...":
        "已写入 %1/%2 页，正在重新读取校验…",
    "Page verification": "页校验",
    "Only the selected page is compared.": "只比较选中的页面。",
    "Refusing to erase: the model or memory layout is not known.":
        "拒绝清空：型号或内存布局未知。",
    "Erase NTAG": "清空 NTAG",
    "The tag model or its memory layout is not known, so no page range can be assumed. Detect the tag again or use manual page operations.":
        "标签型号或内存布局未知，无法推断页范围。请重新检测标签，或使用手动页操作。",
    "Refusing to erase: the Capability Container reports read-only access.":
        "拒绝清空：Capability Container 报告为只读。",
    "The Capability Container marks the tag read-only. Nothing was written.":
        "Capability Container 将该标签标记为只读，未写入任何内容。",
    "Reading every page before building the erase plan...": "正在读取全部页面以生成清空计划…",
    "Refusing to erase: the tag could not be read completely.":
        "拒绝清空：无法完整读取标签。",
    "The tag could not be read completely, so the pages that must be preserved cannot be confirmed. Nothing was written.":
        "无法完整读取标签，不能确认必须保留的页面，未写入任何内容。",
    "Refusing to erase: %1.": "拒绝清空：%1。",
    "No safe erase plan could be built: %1\nNothing was written.":
        "无法生成安全的清空计划：%1\n未写入任何内容。",
    "All writable user pages already contain 00 00 00 00.":
        "所有可写用户页已经是 00 00 00 00。",
    "All %1 writable user pages (4..%2) are already blank. Nothing was written.":
        "全部 %1 个可写用户页（4..%2）已为空，未写入任何内容。",
    " (UID / static lock / CC)": "（UID / 静态锁定 / CC）",
    "dynamic lock page %1": "动态锁定页 %1",
    "configuration pages %1": "配置页 %1",
    "PWD/PACK pages %1": "PWD/PACK 页 %1",
    "Lock bits are already set. Some user pages may be permanently write-protected; they will be reported as not cleared and will not be bypassed.":
        "已有锁定位被设置。部分用户页可能被永久写保护；它们会报告为“未清除”，程序不会绕过锁定。",
    "No lock bits are set in the current read.": "当前读取结果中没有锁定位被设置。",
    "Erase writable user data from this tag?": "清空此标签上的可写用户数据？",
    "Model: %1\nUID: %2\n\nPages to clear: %3..%4 (%5 page(s) currently holding data; %6 blank page(s) will be skipped)\nPreserved: %7\n\n%8\n\nOnly 00 00 00 00 is written to confirmed writable user pages. UID, CC, OTP, lock, configuration and PWD/PACK pages are never modified. This cannot be undone and the original data cannot be recovered. A locked tag cannot be restored to factory state by software.":
        "型号：%1\nUID：%2\n\n待清空页：%3..%4（%5 页当前有数据；跳过 %6 个空白页）\n保留：%7\n\n%8\n\n只会向确认可写的用户页写入 00 00 00 00。UID、CC、OTP、锁定、配置和 PWD/PACK 页不会被修改。此操作不可撤销，原始数据无法恢复。被锁定的标签无法通过软件恢复出厂状态。",
    "Erase cancelled.": "已取消清空。",
    "Clearing page %1 (%2 of %3)...": "正在清除第 %1 页（%2/%3）…",
    "Re-reading the tag to check the erase result...": "正在重新读取标签以确认清空结果…",
    "page %1: %2": "第 %1 页：%2",
    "Unknown: the tag could not be read back, so the erase result cannot be confirmed.":
        "无法确认：标签无法重新读取，清空结果不能确定。",
    "Fully erased: all %1 targeted user page(s) now read 00 00 00 00.":
        "完全清空：全部 %1 个目标用户页现在均为 00 00 00 00。",
    "Erase failed: none of the %1 targeted user page(s) could be cleared.":
        "清空失败：%1 个目标用户页均未能清除。",
    "Partially erased: %1 of %2 targeted page(s) were cleared.":
        "部分清空：已清除 %1/%2 个目标页。",
    "Model: %1   UID: %2": "型号：%1   UID：%2",
    "Target pages: %1..%2 (%3)": "目标页：%1..%2（%3 页）",
    "Cleared: %1   Still holding data: %2   Not readable: %3":
        "已清除：%1   仍有数据：%2   无法读取：%3",
    "Write commands failed on: %1": "写入命令失败页：%1",
    "Not cleared (locked or write-protected):\n  %1":
        "未清除（已锁定或写保护）：\n  %1",
    "Preserved pages: %1": "保留页：%1",
    "The Capability Container was not modified; an NDEF TLV is no longer present because the user pages are blank.":
        "Capability Container 未被修改；由于用户页已清空，不再存在 NDEF TLV。",

    # NtagTab - quick launch inputs
    "Phone number:": "电话号码：",
    "+1 555 0100": "+86 138 0000 0000",
    "E-mail address:": "邮箱地址：",
    "name@example.com": "name@example.com",
    "URI:": "URI：",
    "https:// or another scheme:": "https:// 或其他协议：",
    "Wi-Fi:": "Wi-Fi：",
    "https://example.com": "https://example.com",
    "Writes a tel: URI. Most phones open the dialer; the call is not placed automatically.":
        "写入 tel: URI。多数手机会打开拨号界面，但不会自动拨出。",
    "Writes an sms: URI with an optional body. Phones usually open the messaging app without sending.":
        "写入带可选正文的 sms: URI。手机通常打开短信编辑界面，不会自动发送。",
    "Writes a mailto: URI. The mail app opens; the message is not sent automatically.":
        "写入 mailto: URI。会打开邮件应用，不会自动发送。",
    "Wi-Fi NDEF is not implemented in this build: no verified record format is available, so writing is disabled instead of emitting a guess.":
        "此版本未实现 Wi-Fi NDEF：没有经过验证的记录格式，因此禁用写入，而不是生成猜测数据。",
    "Any URI with a scheme is accepted and kept as entered. Observed behavior depends on the phone.":
        "接受任意带协议的 URI 并原样保留。实际表现取决于手机。",
    "Writes an https/http URI record. Whether a browser opens automatically depends on the phone and its settings.":
        "写入 https/http URI 记录。是否自动打开浏览器取决于手机及其设置。",
    "Cannot build the NDEF data: %1": "无法生成 NDEF 数据：%1",
    "URI: %1": "URI：%1",
    "NDEF message: %1 bytes": "NDEF 消息：%1 字节",
    "Required space (TLV + terminator): %1 bytes": "所需空间（TLV + 终止符）：%1 字节",
    "Available NDEF space: %1 bytes": "可用 NDEF 空间：%1 字节",
    "Capacity used: %1%": "容量占用：%1%",
    "Fits: %1": "是否可容纳：%1",
    "yes": "是",
    "no": "否",
    "NDEF message (hex):": "NDEF 消息（十六进制）：",
    "TLV + terminator, padded to pages:": "TLV + 终止符（按页补齐）：",
    "Model: %1, user pages 4..%2, NDEF capacity %3 bytes (Capability Container %4)":
        "型号：%1，用户页 4..%2，NDEF 容量 %3 字节（Capability Container %4）",
    "read": "已读取",
    "not read": "未读取",
    "Detect and read a tag to calculate the capacity.": "检测并读取标签以计算容量。",
    "Ready to write: the data fits the tag.": "可以写入：数据未超出容量。",
    "Over capacity: writing is disabled.": "超出容量：已禁用写入。",
    "Cannot build the payload: writing is disabled.": "无法生成数据：已禁用写入。",
    "Detect and read the tag first; the model and memory layout must be known.":
        "请先检测并读取标签；必须已知型号和内存布局。",
    "No valid Capability Container was read. Writing is refused because the NDEF area is unknown.":
        "未读到有效的 Capability Container，无法确定 NDEF 区域，已拒绝写入。",
    "The Capability Container marks the tag read-only.":
        "Capability Container 将该标签标记为只读。",
    "The NDEF data needs %1 bytes but only %2 are available.":
        "NDEF 数据需要 %1 字节，但只有 %2 字节可用。",
    "The tag could not be read before writing.": "写入前无法读取标签。",
    "Lock bits are set on this tag (static %1, dynamic %2). Because the locked ranges cannot be confirmed safely, the write was refused. No page was modified.":
        "该标签已设置锁定位（静态 %1，动态 %2）。无法安全确认锁定范围，已拒绝写入，未修改任何页。",
    "The data needs pages %1..%2 but the tag only allows user pages up to %3.":
        "数据需要第 %1..%2 页，但标签只允许用户页到 %3。",
    "Write this NDEF URI to the tag?": "将此 NDEF URI 写入标签？",
    "Model: %1\nUID: %2\nURI: %3\nPages: %4..%5 (%6 bytes)\n\nOnly user data pages are written. The Capability Container, UID, lock, OTP, configuration and PWD/PACK pages are not modified. Phones may open the dialer/messaging/mail app instead of acting automatically, and browser behavior depends on the phone.":
        "型号：%1\nUID：%2\nURI：%3\n页：%4..%5（%6 字节）\n\n只写入用户数据页。Capability Container、UID、锁定、OTP、配置和 PWD/PACK 页不会被修改。手机可能打开拨号/短信/邮件应用而不是自动执行，浏览器行为取决于手机。",
    "Quick Launch write cancelled.": "已取消快捷启动写入。",
    "Writing page %1 (%2 of %3)...": "正在写入第 %1 页（%2/%3）…",
    "Incomplete: the written pages could not be read back, so the result is unverified.":
        "不完整：无法重新读取已写入的页面，结果未经验证。",
    "Write verified: the tag parses back as the expected URI.":
        "写入已验证：标签读回并解析为预期 URI。",
    "Verification FAILED: the NDEF data read back differs from the data written.":
        "校验失败：读回的 NDEF 数据与写入数据不一致。",
    "Expected URI: %1": "预期 URI：%1",
    "Read back URI: %1": "读回 URI：%1",
    "Read back: %1": "读回：%1",
    "no NDEF record": "没有 NDEF 记录",
    "Pages written: %1   Pages already correct: %2":
        "已写入页：%1   原本已正确的页：%2",
    "Write failed on page(s): %1": "写入失败页：%1",
    "Capability Container and system pages were not modified.":
        "Capability Container 和系统页未被修改。",
    "Quick Launch written and verified: %1": "快捷启动已写入并验证：%1",

    # NtagTab - verification report
    "Writable": "可写",
    "ReadOnly": "只读",
    "Manufacturer": "制造商",
    "UID": "UID",
    "Lock": "锁定",
    "Config": "配置",
    "Security": "安全区",
    "NotApplicable": "不适用",
    "Unknown": "未知",
    "match": "一致",
    "MISMATCH": "不一致",
    "preserved": "已保留",
    "not checkable": "不可校验",
    "not applicable": "不适用",
    "The tag could not be read back, so the write result is unknown.":
        "无法重新读取标签，写入结果未知。",
    "The tag could not be read back. The write result cannot be verified.":
        "无法重新读取标签，写入结果无法验证。",
    "Only pages targeted by the write are compared. Manufacturer, UID, lock, configuration and security pages cannot cause a failure.":
        "只比较写入涉及的目标页。制造商、UID、锁定、配置和安全页不会导致校验失败。",
    "Passed: every writable target page matches.": "通过：所有目标可写页均一致。",
    "Passed with Exceptions: every writable target page matches, but some pages were read-only or could not be checked.":
        "带例外通过：所有目标可写页均一致，但部分页面为只读或不可校验。",
    "Failed: at least one writable target page differs.": "失败：至少一个目标可写页不一致。",
    "Incomplete: not enough pages were read to decide.": "不完整：读取的页面不足以得出结论。",
    "Writable targets: %1, passed: %2, failed: %3, not checkable: %4.":
        "目标可写页：%1，通过：%2，失败：%3，不可校验：%4。",
    "Page\tClass\tExpected\tActual\tResult": "页\t类型\t预期数据\t实际数据\t结果",
    "(not readable)": "（不可读取）",
    "Verification: %1 (passed %2/%3 writable pages).":
        "校验：%1（通过 %2/%3 个可写页）。",
}

tree = ET.parse(TS)
root = tree.getroot()
applied = set()
for ctx in root.findall("context"):
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
for s in missing:
    print("MISSING:", s[:80])
