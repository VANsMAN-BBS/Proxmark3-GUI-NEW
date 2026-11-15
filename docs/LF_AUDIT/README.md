# LF 参考文档

本目录保留低频（LF）实现所需的参考材料。原始审计与移植决策文档已移除，只保留长期有用的部分。

| 文档 | 内容 |
|---|---|
| [LF_CARD_SUPPORT_MATRIX.md](LF_CARD_SUPPORT_MATRIX.md) | LF 卡型与协议覆盖矩阵、证据等级 |
| [LF_COMMAND_MAPPING.md](LF_COMMAND_MAPPING.md) | Phosphor ↔ Iceman Client 命令映射、参数与版本差异 |
| [T55XX_ANALYSIS.md](T55XX_ANALYSIS.md) | T55xx 功能面、数据结构、写入流程 |

说明：

- 解码（Manchester / FSK / PSK / 时钟恢复 / CRC）由 Client 和固件完成，GUI 只解析文本输出。
- Client 基线为 Iceman/RRG v4.23346；其他版本可能缺少表中列出的命令或输出字段。
- 表中的“Client 是否支持”来自帮助文本与二进制字符串的静态确认，不代表已在设备上运行验证。
- 当前 256 KB 测试固件不含 LF，LF 射频路径未在该固件上验证。

更多 LF 解析器内容见 [LF Parser core](../LF_CORE/README.md)。
