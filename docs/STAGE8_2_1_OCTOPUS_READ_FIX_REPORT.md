# Stage 8.2.1 — Octopus 读取失败修复

## 1. `-7` 的定义

`include/pm3_cmd.h`（v4.23346）：

```text
#define PM3_ERFTRANS -7
```

即射频传输失败/无响应，不是服务不存在或认证失败。

## 2. 34 字节响应的含义

```text
01 <IDm bytes>   Polling 响应码 0x01 + IDm
01 <IDm bytes>   IDm 重复
03 32 42 82 82 47 AA FF   PMm
03 32 42 82 82 47 AA FF   PMm 重复
00 00                     request data
```

这是 `-s` 的 Active Select/Polling 阶段响应，不是余额读取结果。随后真正的 Read Without Encryption 没有得到应答。

## 3. 根因

实测同一张八达通：

| 命令 | 结果 |
|---|---|
| `hf felica raw -s -c 0601<IDm>011701018000` | Polling 成功，Read 返回 -7 |
| `hf felica raw -a -c 0601<IDm>011701018000` | 成功返回余额帧 |

`-a` 返回：

```text
B2 4D 1D 07 <IDm> 00 00 01
00 00 02 A3 00 00 00 00 00 00 00 00 00 00 00 2B
```

Block 0 与已验证数据一致（example balance decoded (value redacted)）。

结论：该八达通在 Active Select 后不响应立即发送的 Read Without Encryption；改用“激活射频但不执行 Select”的 `-a` 后读取成功。命令格式、Service 顺序、Block List、IDm 均正确。

## 4. 最小修改

`src/module/felica/felica_reader.cpp`：

```text
Octopus (0x8008): hf felica raw -a -c <frame>
CJRC (0x0003):    hf felica raw -s -c <frame>   （保持原路径）
```

未修改解析器、余额公式、UI、CommandQueue、ClientSession、固件。

## 5. 验证

- 实机：实体八达通（System 0x8008）用 `-a` 成功读取 Block 0，返回 `00 00 02 A3 ...`。
- 构建：Release 0 warnings / 0 errors，2,362,368 bytes。
- 回归：FeliCa 22、NTAG 67、Amiibo 22、LF 59、LF Stage2 126、eMRTD 47、Git audit 17，全部通过。

## 6. 未验证

- 未在 GUI 中再次截图确认余额显示；GUI 使用与实机成功命令相同的 `-a` 路径。
- 旧版八达通押金偏移 350 仍不支持。
- 多 System 卡优先级未实测。

## 7. Git

未提交、未推送；`HEAD=7dbd45f`，`v0.4.1` Tag 与 Release 未改变。
