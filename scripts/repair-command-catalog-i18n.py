#!/usr/bin/env python3
"""Repair the generated CommandCatalog translation context.

Why this exists
---------------
``CommandCatalogModel`` looks descriptions up at run time with
``QCoreApplication::translate("CommandCatalog", "<stable id>")``.  Because the
source string is a variable, ``lupdate`` cannot see the use site: on its next
run it marks *every* CommandCatalog message as ``type="vanished"``.  lrelease
then drops those messages from the .qm file and the GUI silently falls back to
the English description shipped in ``commands.json``.

This tool only touches the CommandCatalog context:

* removes ``type="vanished"`` so lrelease keeps the translations,
* keeps the existing translations (no mass re-translation),
* applies a small reviewed override table for poor machine translations and
  the project terminology (Authentication=身份认证, Block=块,
  Nonce=随机数（Nonce）, CRC=循环冗余校验, Sniffer=嗅探器, ...),
* verifies that every catalog id has a non-empty translation, that zh_CN has
  Han characters, and that en_US is identical to the catalog description.

Usage (after any lupdate run)::

    python scripts/repair-command-catalog-i18n.py
    lrelease-qt5 i18n/zh_CN.ts i18n/en_US.ts -qm ...

``--check`` performs the verification without writing anything.
"""

from __future__ import annotations

import argparse
import html
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TS_DIR = ROOT / "i18n"
CATALOG = ROOT / "resources" / "commands" / "commands.json"

# Reviewed translations for entries whose generated text was wrong, mixed
# English/Chinese, or used the wrong word class (BLOCK as a verb, AUTH as a
# bare acronym, "challenges found" left untranslated, ...).
OVERRIDES_ZH = {
    "cmd.emv_intauth": "内部身份认证",
    "cmd.emv_smart2nfc": "作为 NFC 智能卡完成交易，使用 ISO7816 接口进行身份认证",
    "cmd.hf_cipurse_auth": "对 CIPURSE 标签进行身份认证",
    "cmd.hf_felica_rdbl": "从无需身份认证的服务读取块数据。",
    "cmd.hf_felica_wrbl": "向无需身份认证的服务写入块数据。",
    "cmd.hf_felica_seacauth1": "FeliCa SEAC 身份认证 1",
    "cmd.hf_felica_auth1": "对卡片进行身份认证，并使用 Auth1 开始相互身份认证",
    "cmd.hf_felica_auth2": "允许卡片对读写器进行身份认证，完成相互身份认证",
    "cmd.hf_felica_liteauth": "对卡片进行身份认证。",
    "cmd.hf_fido_auth": "FIDO U2F 身份认证消息。",
    "cmd.hf_fmcos_authexternal": "使用 DES/3DES 密钥进行外部身份认证",
    "cmd.hf_fmcos_authinternal": "内部身份认证（卡证明密钥知识）",
    "cmd.hf_fmcos_tidsetauth": "设置内部身份认证密钥",
    "cmd.hf_fmcos_block": "锁定卡或应用",
    "cmd.hf_mf_auth4": "ISO14443-4 AES 身份认证",
    "cmd.hf_mfp_auth": "身份认证",
    "cmd.hf_mfu_chk": "Ultralight C/AES - 身份认证字典检查",
    "cmd.hf_mfu_cauth": "Ultralight-C - 身份认证",
    "cmd.hf_mfu_aesauth": "Ultralight-AES - 身份认证",
    "cmd.hf_mfdes_auth": "MIFARE DesFire 身份认证",
    "cmd.hf_mfdes_intauth": "ISO 内部身份认证（ECDSA 挑战-响应）",
    "cmd.hf_ntag424_auth": "使用密钥测试身份认证",
    "cmd.hf_iclass_lookup": "使用身份认证跟踪在字典文件中检查密钥",
    "cmd.lf_hitag_lookup": "使用身份认证跟踪在字典文件中检查密钥",
    "cmd.lf_hitag_ta": "Hitag 2：测试所有记录的身份认证",
    "cmd.lf_em_4x70_auth": "对 EM4x70 进行身份认证",
    "cmd.piv_authsign": "使用卡片进行身份认证",
    "cmd.trace_extract": "提取跟踪中记录的身份认证挑战",
    "cmd.hf_sniff": "通用 HF 嗅探器",
    # Nonce = 随机数（Nonce）; keep the surrounding CJK spacing natural.
    "cmd.hf_epa_cnonces": "获取指定长度的加密 PACE 随机数（Nonce）",
    "cmd.hf_mf_staticnested": "针对静态随机数（Nonce）MIFARE Classic 卡的嵌套攻击",
    "cmd.hf_mf_sen": "FM11RF08S 静态加密随机数（Nonce）攻击",
    "cmd.hf_mf_isen": "静态加密随机数（Nonce）信息",
    # UID means the card's UID in these rows; reader UID stays "读卡器 UID".
    "cmd.analyse_nuid": "由 7 字节 UID 生成 NUID",
    "cmd.hf_fmcos_tidsetuid": "设置卡片 UID",
    "cmd.hf_mf_csetuid": "设置卡片 UID",
    "cmd.hf_mf_cwipe": "将卡擦除为默认卡片 UID/扇区/密钥",
    "cmd.hf_mf_gen3uid": "在不变更厂商块的情况下设置卡片 UID",
    "cmd.hf_mf_gdmsetuid": "设置 GDM 卡片 UID",
    "cmd.hf_mf_personalize": "个性化卡片 UID（仅 MIFARE Classic EV1）",
    "cmd.hf_mfdes_getuid": "获取卡片 UID",
    "cmd.hf_mfu_setuid": "设置卡片 UID - 仅限魔术标签",
    "cmd.hf_mfu_aesgetuid": "Ultralight-AES - 在 RID 使用时获取卡片 UID",
    "cmd.hf_14a_cuids": "一次连续采集 n>0 个 ISO14443-A 卡片 UID",
    "cmd.hf_14b_setuid": "设置魔术卡 UID",
    "cmd.hf_15_csetuid": "设置魔术卡 UID",
    # CRC is spelled out once, with the acronym kept for searchability.
    "cmd.analyse_crc": "用于循环冗余校验（CRC）评估的占位方法",
    "cmd.hf_legic_crc": "计算给定字节的 LEGIC 循环冗余校验（CRC）",
}

# Whole-word replacements applied to every catalog translation.
TERM_REPLACEMENTS_ZH = (
    # Terminology: Block = 块 (not 区块).
    (re.compile("区块"), "块"),
    # Nonce = 随机数（Nonce）.
    (re.compile(r"(?i)\bnonces?\b"), "随机数（Nonce）"),
)

MESSAGE_RE = re.compile(
    r"(<message>\s*<source>(?P<source>.*?)</source>\s*"
    r"<translation)(?P<attrs>[^>]*)(?P<gt>>)(?P<text>.*?)(?P<close></translation>)",
    re.S,
)


def escape_xml(value: str) -> str:
    return (value.replace("&", "&amp;").replace("<", "&lt;")
            .replace(">", "&gt;"))


def has_han(value: str) -> bool:
    return bool(re.search(r"[\u4e00-\u9fff]", value))


def normalize_zh(text: str) -> str:
    for pattern, replacement in TERM_REPLACEMENTS_ZH:
        text = pattern.sub(replacement, text)
    return text


def load_catalog():
    data = json.loads(CATALOG.read_text(encoding="utf-8"))
    return data["commands"]


def context_bounds(text: str):
    marker = "<name>CommandCatalog</name>"
    pos = text.find(marker)
    if pos < 0:
        raise SystemExit("CommandCatalog context not found")
    start = text.rfind("<context>", 0, pos)
    end = text.find("</context>", pos)
    if start < 0 or end < 0:
        raise SystemExit("CommandCatalog context is malformed")
    return start, end + len("</context>")


def repair_language(language: str, text: str, commands, by_id) -> tuple[str, list[str]]:
    start, end = context_bounds(text)
    block = text[start:end]
    seen = set()
    problems: list[str] = []

    def repl(match: re.Match) -> str:
        source = match.group("source").strip()
        attrs = match.group("attrs")
        value = match.group("text")
        seen.add(source)

        attrs = re.sub(r'\s*type="vanished"', "", attrs)

        if language == "zh_CN":
            override = OVERRIDES_ZH.get(source)
            value = escape_xml(override) if override is not None else normalize_zh(value)
        else:
            row = by_id.get(source, {})
            english = (row.get("description") or "").strip() or "No description provided"
            if html.unescape(value).strip() != english:
                problems.append(f"{language}: {source} differs from catalog description")
                value = escape_xml(english)

        return f"{match.group(1)}{attrs}{match.group('gt')}{value}{match.group('close')}"

    new_block = MESSAGE_RE.sub(repl, block)

    missing = [row["id"] for row in commands if row["id"] not in seen]
    extra = [sid for sid in seen if sid not in by_id]
    for sid in missing:
        problems.append(f"{language}: missing message {sid}")
    for sid in extra:
        problems.append(f"{language}: stale message {sid}")

    for match in MESSAGE_RE.finditer(new_block):
        source = match.group("source").strip()
        value = match.group("text")
        if not value.strip():
            problems.append(f"{language}: blank translation {source}")
        elif language == "zh_CN" and not has_han(value):
            problems.append(f"{language}: no Chinese text {source}")

    return text[:start] + new_block + text[end:], problems


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="verify only, do not write files")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    commands = load_catalog()
    by_id = {row["id"]: row for row in commands}

    all_problems: list[str] = []
    for language in ("zh_CN", "en_US"):
        path = TS_DIR / f"{language}.ts"
        text = path.read_text(encoding="utf-8")
        updated, problems = repair_language(language, text, commands, by_id)
        all_problems.extend(problems)
        if not args.check:
            # newline="" keeps the file's LF endings; the default text mode
            # would rewrite every line as CRLF on Windows and bury the real
            # translation change in a whole-file diff.
            with path.open("w", encoding="utf-8", newline="") as handle:
                handle.write(updated)
            print(f"repaired {path}")
        else:
            print(f"checked  {path}")

    if all_problems:
        print(f"{len(all_problems)} problem(s):", file=sys.stderr)
        for problem in all_problems:
            print("  " + problem, file=sys.stderr)
        return 1
    print(f"CommandCatalog: {len(commands)} ids, translations complete")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
