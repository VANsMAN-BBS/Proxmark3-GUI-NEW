# Amiibo / NTAG215 兼容性说明

**版本：** v0.4.1

**Client：** Iceman v4.23346（256 KB 固件）

---

## 1. 结论速览

| 项目 | 状态 |
|---|---|
| NTAG215 识别 | 支持（`hf mfu info`） |
| NTAG215 读取 | 支持（`hf mfu dump`）——已在 NTAG215 测试标签验证 |
| NTAG215 写入 | 支持（`hf mfu restore`）——已在 NTAG215 测试标签验证 |
| 写入后校验 | 支持（复读比对）——已在 NTAG215 测试标签验证 |
| NTAG215 模拟 | Client 支持（`hf mfu eload` + `hf mfu sim -t 7`）——**真实标签/游戏机未验证** |
| Amiibo 解密 | Client 支持（`hf mfu amiibo --dec`）——界面未集成 |
| UID 冒充 | **未使用**（不是完整模拟，绝不以此冒充） |

---

## 2. 为什么 Amiibo 不是 JTAG

Amiibo 使用 NFC Forum Type 2（NTAG215）数据结构，通过 13.56 MHz ISO 14443-A 交互，与 JTAG 无关。  
自 v0.4.1 起，Amiibo 与 NTAG 一起放在 **NTAG / Ultralight** 标签页下；JTAG 占位页已移除。

---

## 3. 命令映射

| 界面操作 | Client 命令 | 说明 |
|---|---|---|
| Detect tag | `hf mfu info` | 自动识别卡型 |
| Read tag | `hf mfu dump -f <file>` | 生成 `.bin` 后由 GUI 载入，临时文件随即删除 |
| Write tag | `hf mfu restore -f <file>` | 需要 540 字节镜像；写入前确认 |
| Verify tag | `hf mfu dump -f <file>` | 与所选数据逐字节比较 |
| Start emulation | `hf mfu eload -f <file>` + `hf mfu sim -t 7` | `-t 7` = MFU EV1 / NTAG 215 Amiibo |
| Stop emulation | Client 重启 | 终止 detached 模拟并恢复队列 |

未使用 `hf 14a sim` 之类的 UID 模拟来代替 Amiibo 模拟。

---

## 4. 数据格式

- 标准 NTAG215 镜像：**540 字节**（135 页 × 4 字节）。
- 页面接受 540 / 1024（NTAG213 尺寸）/ 2048（NTAG216 尺寸）字节。
- 写入与模拟要求 **540 字节**。
- 本程序不解析 Amiibo 名称/系列（原始 `.bin` 不包含可靠的公开名称字段；名称来自用户文件名或在线目录元数据）。

---

## 5. 写入流程与风险

1. 选择数据（内置示例 / 导入文件）。
2. `Write tag...` → 确认对话框（显示名称与字节数，默认按钮为“否”）。
3. 写入临时文件 → `hf mfu restore`。
4. 自动复读 → 与写入数据比较。
5. 仅当完全一致时提示“写入成功并已验证”。

注意：

- 正版 Amiibo 标签通常已锁定，**不可写入**；实际写入需要可重写（“magic”）NTAG215。
- 不执行 Lock/OTP/PWD 修改，避免不可逆损坏。
- 写入失败或复读失败会明确报告，不会标记成功。

---

## 6. 模拟说明

- `hf mfu eload` 将 540 字节加载到 Client 的 8 KB 模拟内存。
- `hf mfu sim -t 7` 以 MFU EV1 / NTAG215 类型模拟。
- 模拟是 **detached** 长命令：界面显示“长命令执行中”，Stop 会重启 Client。
- 是否被特定游戏机接受**未验证**；模拟能力取决于 Client/固件，不是 GUI 实现。

---

## 7. 版权与数据来源

- 不内置任何商业 Amiibo 二进制数据。
- 内置示例为程序生成的合成数据，仅用于测试。
- 在线目录仅提供**元数据**浏览；是否提供二进制由目录运营方决定，程序不内置、不再分发。
- 用户导入的数据仅保存在本机内存/用户选择的文件中，不会上传。

---

## 8. 未集成项

- Amiibo 解密/加密（`hf mfu amiibo --dec/--enc`）。
- NTAG215 密码认证（`hf mfu dump -k`）。
- 批量读写、UID 修改、锁定/OTP 操作。
