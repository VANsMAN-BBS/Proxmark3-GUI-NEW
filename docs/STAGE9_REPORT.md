# Stage 9 Report — JTAG 页面下的 Amiibo 子页面

**日期：** 2026-11-12  
**版本：** Proxmark3GUI v0.4.0 开发中（Stage 9）  
**分支：** `upgrade/v0.4.0-emrtd-amiibo`

---

## 1. 技术验证（先验证后实现）

### 1.1 现有页面

v0.3.0 **没有 JTAG 页面**（标签页为 Mifare / LF / T55xx / NTAG / RawCommand / Settings）。  
本次按要求新增一个 JTAG 容器，其中 JTAG 子页明确标注“未实现”，Amiibo 子页为实际功能。**没有把 Amiibo 说成 JTAG 设备。**

### 1.2 Client / 固件能力（实测 `hf mfu --help` 等）

Client：`Iceman/master/v4.23346-suspect`，固件 256 KB。设备报告：

```text
Emulator memory: 8192 bytes
fpga_pm3_hf.ncd / fpga_pm3_felica.ncd
```

| 能力 | 命令 | 结论 |
|---|---|---|
| 标签识别 | `hf mfu info` | 支持（自动识别 NTAG 213/215/216 等） |
| 整卡读取 | `hf mfu dump -f <file>` | 支持，自动检测卡型 |
| 页读写 | `hf mfu rdbl` / `wrbl` | 支持 |
| 回写整卡 | `hf mfu restore -f <file>` | 支持 |
| 模拟准备 | `hf mfu eload -f <file>` | 支持，加载到 8 KB 模拟内存 |
| 模拟 | `hf mfu sim -t 7` | 支持，`-t 7` 即 “MFU EV1 / NTAG 215 Amiibo” |
| Amiibo 专用 | `hf mfu amiibo --dec/-i/-o` | 支持（读取并解密 Amiibo 内存） |

因此 **NTAG215 读写、校验与模拟在本 Client/固件上具备实现基础**；但本环境**没有 NTAG215 标签**，真实读写/模拟未验证。

---

## 2. 实现

### 2.1 页面结构

`MainWindow` 新增顶层标签 **JTAG**，内含两个子标签：

- **JTAG**：占位说明（本版本未实现 JTAG 功能，仅用于归组）；
- **Amiibo**：`AmiiboPage`（`src/module/amiibopage.h/.cpp`）。

Amiibo 页面分三区：

1. **Amiibo 数据（左侧列表）**：内置示例、用户导入、在线下载的数据；来源标注。
2. **已选数据**：名称/来源/大小/格式、十六进制预览、图片预览。
3. **标签操作 + 在线目录**：Detect / Read / Write / Verify / Start Emulation / Stop Emulation；在线目录 URL、搜索、结果、下载。

### 2.2 数据来源

| 来源 | 说明 |
|---|---|
| 内置示例 | 运行时生成的 540 字节**合成** NTAG215 镜像（明确标注“非真实 Amiibo”，不含任何商业数据） |
| 用户文件 | 导入 `.bin`/`.dump`；接受 540 / 1024 / 2048 字节；其它长度给出明确错误 |
| 在线目录 | **可配置 URL** 的 JSON 元数据目录（`items[]`），可选 `image_url` / `bin_url`；默认未配置 → 不联网；二进制数据默认不内置、不分发 |

内置示例包含 UID、BCC、CC 与确定性填充，仅用于文件加载、解析、显示、写入流程与模拟流程的测试。

### 2.3 操作与安全

| 操作 | 实现 | 安全措施 |
|---|---|---|
| Detect | `hf mfu info` | 只读 |
| Read | `hf mfu dump -f <临时文件>` → 载入 → 删除临时文件 | 临时文件位于客户端工作目录 |
| Write | `hf mfu restore -f <临时文件>` | **必须先确认**；仅接受 540 字节；写入后**自动复读比对** |
| Verify | 复读并与所选数据逐字节比较 | 报告首个差异偏移 |
| Emulation | `hf mfu eload -f` + `hf mfu sim -t 7`（detached） | 不模拟 UID 冒充；Stop 会重启 Client 结束模拟 |
| 不可逆操作 | 未实现（无 Lock / OTP / PWD 修改） | — |

写入不会被当作成功：只有复读结果与写入数据完全一致时才报告“校验通过”。

### 2.4 在线目录

- 请求只由用户点击 “Search” / “Download data” 触发。
- 目录格式：`{"items":[{"name","series","image_url","bin_url"}]}`；也接受顶层数组。
- 下载的 `bin_url` 必须是 540/1024/2048 字节，否则拒绝并提示。
- 断网或 URL 为空时，本地功能完全可用。
- **未内置任何商业 Amiibo 数据**，以避免再分发许可问题。

---

## 3. 验证

| 测试 | 结果 |
|---|---|
| JTAG 标签页与 JTAG/Amiibo 子标签出现 | 通过 |
| JTAG 子页说明文字 | 通过 |
| 内置示例：名称/来源/540 字节/NTAG215 格式 | 通过 |
| 导入 540 字节文件 | 通过（名称、大小、格式、状态） |
| 另存为 `.bin` | 通过（540 字节） |
| 十六进制预览 | 通过（显示 512 字节 + 偏移/ASCII） |
| 中文界面 | 通过（98 条新翻译已编译进 `zh_CN.qm`） |
| 在线目录未配置时的提示 | 通过（不联网） |
| 真实 NTAG215 读取/写入/校验 | **未验证**（无标签） |
| 真实 NTAG215 模拟 | **未验证**（无标签、无游戏设备） |
| 数据长度错误的提示 | 代码路径已实现；自动化未捕获到该对话框（文件对话框焦点问题） |

---

## 4. 已知限制

1. 本环境没有 NTAG215，**读写/校验/模拟只完成了命令与流程验证**，不能声称在真实标签上成功。
2. Amiibo 标签通常已锁定，普通 NTAG215 无法写入；实际写入需要可重写（“magic”）NTAG215，页面已在确认框说明。
3. 模拟依赖 Client 的 8 KB 模拟内存与 `hf mfu sim -t 7`；游戏机兼容性未验证。
4. 未实现 NTAG215 密码认证（`-k`）与 Amiibo 解密（`hf mfu amiibo --dec`）的界面入口；只做原始镜像读写。
5. 在线目录没有可验证的公共 API（构建时 `amiiboapi.com` 返回 404），因此提供可配置目录而不是写死地址。
6. 文件对话框在自动化环境中的焦点不稳定，导致“错误长度”弹窗未被自动化捕获。

---

## 5. 结论

**Stage 9 页面与本地/设备流程完成；真实 NTAG215 读写与模拟待硬件验证。**  
页面明确区分“读取 / 解析 / 写入 / 校验 / 模拟”，写入有确认与复读校验，未实现任何不可逆操作。
