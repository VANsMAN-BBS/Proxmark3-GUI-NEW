# NTAG216 实机测试记录（Stage 3）

日期：2026-11-14  
设备：Proxmark3，COM14  
Client：`E:\Opencode_project\pm3-256-iceman-v4.23346\proxmark3.exe`  
测试目录：`E:\Opencode_project\PM3-HW-Test-Stage3\`

## 标签身份

| 字段 | 值 |
|---|---|
| UID | `53 24 60 F3 53 00 01` |
| ATQA | `00 44` |
| SAK | `00` |
| TYPE（Client） | `NTAG UNKNOWN`（兼容芯片） |
| dump 头版本 | `00 53 04 02 01 00 13 03` |
| CC | `E1 10 6D 00`（872 B NDEF） |
| 型号判定 | NTAG216（dump 头 storage 0x13 + CC，Probable） |
| 总页数 / 用户页 | 231 / 4..225（222 页，888 B） |
| 静态锁 / 动态锁 | `00 00` / `00 00 00` |

GET_VERSION（`hf 14a apdu -k -d 6000000000`）在该芯片上无返回。

## 基准镜像

- 文件：`baseline216.bin`
- 大小：980 字节
- SHA-256：`c8c489b4230ea499699547e6452d50305f87af4df864ce4deb514e341d350283`
- JSON：`baseline216.json`

## 失败根因证据

```text
hf mfu rdbl -b 42
[!] block number to large. Max block is 41/0x29

hf mfu rdbl -b 42 --force
[=] 42/0x2A | F3 B0 09 C1
```

Client 自动识别为 41 页上限；42..225 在 GUI 旧实现中被 Client 地址检查拦截。  
GUI 现在对 >41 的页追加 `--force`（仅在已识别布局且页在 userLast 内）。

## 写入与恢复

| 阶段 | 写入页数 | 结果 |
|---|---:|---|
| GUI 编辑页 4/42/100/225 + Write Dirty | 4 | 校验 Passed 4/4 |
| 全用户区 4..225 全零 | 222 | 成功；dump 全零 |
| 16 个代表页 × 5 种模式 × 3 轮 | 240 | 成功；dump 与最后一轮一致 |
| 恢复基准用户区 | 222 | 成功 |
| 恢复后全片 dump | — | 与基准逐字节一致 |

模式：全 FF、AA/55 交替、递增字节、页标记、固定种子随机。  
所有阶段 226..230（动态锁/CFG0/CFG1/PWD/PACK）保持不变。

证据文件：

- `stage-zero.bin` / `.json`
- `stage-patterns.bin` / `.json`
- `stage-restored.bin` / `.json`
- `fulltest.log`（684 条 wrbl 的原始 Client 输出）
- `after-write1.bin`、`after-gui-write.bin`（中间恢复校验）

## GUI 读取/编辑证据

- Detect：`NTAG216 (Probable)`，ATQA `00 44`，SAK `00`，231 页，222 可写
- Read All：`231 / 231`，锁状态 `static 00 00; dynamic 00 00 00`
- 编辑 4/42/100/225 后 Write Dirty：`Passed 4/4`
- 恢复后基准哈希一致

## Amiibo

- 有效 plain 540 镜像：Client 输出 `Detected plain mfu dump format`，`hf mfu sim -t 7` 启动/停止成功。
- 无效无头镜像：Client 输出 `Failed to detected mfu dump format`，GUI 原样显示，未启动模拟。
- 中文/空格路径：Windows Client 窄字符串打开失败；GUI 复制完全相同的字节到 ASCII 临时文件（单元测试覆盖字节/哈希一致）。该分支端到端 GUI 自动化未完成，因为文件对话框无法自动输入中文路径。
