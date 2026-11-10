# Stage 5 Report — LF 可选波形显示

**日期：** 2026-11-10  
**分支：** `upgrade/iceman-v4.23346`  
**目标：** 在不破坏 LF 文本输出和原有操作方式的前提下，增加可选的 GUI 原始采样波形显示。

---

## 1. 数据源审计

### 1.1 当前 Client 的可行数据路径

`lf read` / `lf sniff` 把采样存入 Client 的 GraphBuffer。  
当前 Windows Client 没有 Qt GUI，`data plot` 不能显示图形；但 Client 提供了本地导出命令：

```text
data save -f <name>
```

实测输出：

```text
[+] Saved 200 bytes to PM3 file `.../pm3gui_lf_wave.pm3`
```

`.pm3` 文件是**文本文件，一行一个采样值**，范围 -127..127。  
也验证了反向路径：

```text
data load -f pm3gui_test
[+] loaded 200 samples
```

因此 GUI 可以：

1. 通过 `data save` 导出 GraphBuffer；
2. 读取 `.pm3` 文本；
3. 用 QPainter 绘制原始采样；
4. 不依赖 Client 的 Qt GUI，也不引入 Qt Charts。

### 1.2 数据限制

`.pm3` 文件只包含采样值，**没有采样率/时间戳元数据**。  
因此：

- 横轴使用 **sample index**；
- 不伪造 “ms” 或 “kHz” 时间轴；
- 界面明确提示 “rate not provided by data save”；
- 不把卡号、解码结果或普通文本转换成波形。

当前连接设备 firmware 为 256K `PM3 GENERIC`，没有 LF：

```text
lf read -v
[!] This command is not available in this mode
```

所以 **真实 LF 采样未能验证**。波形功能使用 `data load` 加载的 200 点合成 GraphBuffer 做 GUI 集成验证。

---

## 2. 实际完成内容

### 2.1 WaveformWidget

新增 `src/ui/waveformwidget.h/.cpp`：

- 纯 `QPainter` 绘制；
- 固定 y 轴 -127..+127；
- 绘制零线、水平网格、边框；
- 自动/手动缩放；
- 鼠标滚轮缩放（围绕光标）；
- 左键拖拽平移；
- 双击 Fit；
- 样本不足时明确提示；
- 无数据时提示运行 LF Read/Sniff 或 `data load`；
- 不生成任何“看起来像波形”的假数据。

### 2.2 WaveformDialog

新增 `src/ui/waveformdialog.h/.cpp`：

- 非模态对话框，不阻塞主窗口；
- 两个页签：
  - **Waveform**：原始采样波形；
  - **Sample Text**：同一批采样值的文本列表；
- Fit / Zoom In / Zoom Out；
- 信息栏显示：
  - 样本数；
  - 最小/最大值；
  - “rate not provided” 限制；
  - 明确说明解码文本仍在 RawCommand。
- 超过 20000 个样本时文本页只显示前 20000 个并注明剩余数量；波形页仍使用全部样本。

### 2.3 LF 页面入口

在 LF Operation 组底部增加：

```text
Show Waveform
```

点击后：

1. 执行 `data save -f pm3gui_lf_wave`；
2. 从 Client 输出中的 `Saved ... to PM3 file` 解析 `.pm3` 路径；
3. 回退到 Client 工作目录/当前目录的候选路径；
4. 读取一行一个整数；
5. 样本数 < 2 时提示数据不足；
6. 打开 `WaveformDialog`。

原有：

- LF Search / Read / Tune / Sniff 按钮；
- 文本 RawCommand 输出；
- `data plot` 调用路径（LF::read/sniff 中保留）均未删除。

> 注意：在当前无 LF 固件上，`lf read`/`lf sniff` 本身不可用；波形按钮只导出 Client 当前 GraphBuffer，不会凭空产生采样。

---

## 3. 修改文件列表

| 文件 | 修改 |
|---|---|
| `src/ui/waveformwidget.h` | 新增 |
| `src/ui/waveformwidget.cpp` | 新增 |
| `src/ui/waveformdialog.h` | 新增 |
| `src/ui/waveformdialog.cpp` | 新增 |
| `src/module/lf.h` | 增加 `showWaveform`、工作目录 |
| `src/module/lf.cpp` | 增加 Show Waveform 按钮、`data save` 导出、`.pm3` 解析、对话框 |
| `src/ui/mainwindow.cpp` | 连接后把 Client 工作目录交给 LF |
| `src/Proxmark3GUI.pro` | 加入波形文件 |
| `docs/STAGE5_REPORT.md` | 本报告 |

---

## 4. 编译结果

```text
qmake-qt5 ..\src\Proxmark3GUI.pro -spec win32-g++ CONFIG+=release
mingw32-make -j4
```

结果：**编译、链接成功**。

新增一个 Qt 弃用警告：`QWheelEvent::pos()`（Qt 5.15 建议 `position()`）。功能正常，Stage 6 可清理。

---

## 5. 测试结果

### 5.1 数据格式验证（本地，无设备）

创建 200 点合成 GraphBuffer：

```text
data load -f pm3gui_test
[+] loaded 200 samples
data save -f pm3gui_test_out
[+] Saved 200 bytes to PM3 file `.../pm3gui_test_out.pm3`
```

输出文件为一行一个整数，符合预期。

### 5.2 GUI 集成验证

1. 启动 GUI，连接真实 v4.23346 Client/设备；
2. 在 RawCommand 发送：
   ```text
   data load -f pm3gui_test
   ```
3. 切换到 LF 页；
4. 点击 `Show Waveform`；
5. GUI 执行 `data save -f pm3gui_lf_wave`，解析到：
   ```text
   Saved 200 bytes to PM3 file `.../pm3gui_lf_wave.pm3`
   ```
6. 弹出波形窗口；
7. 波形页显示完整正弦采样，y 轴 -127..+127，x 轴 sample index 0..199；
8. 信息栏显示 `Samples: 200, range: -127..127`；
9. `Sample Text` 页可查看同一批整数。

结论：**GUI 波形渲染、数据导出、路径解析、缩放/平移控件均通过合成数据验证**。

### 5.3 真实 LF 采样

**Not verified on physical hardware**：

- 当前 firmware 无 LF；
- `lf read` / `lf sniff` 返回 not available；
- 没有真实 LF 波形数据；
- 未伪造任何 LF 波形或时间信息。

---

## 6. 已知问题

1. 真实 LF 采样未验证（firmware 限制）。
2. `.pm3` 文件没有采样率，横轴只能显示 sample index；这是数据源限制，不是 GUI 简化。
3. 波形仅显示当前 GraphBuffer；不是实时流式绘制。
4. `data save` 的路径解析依赖 Client 输出文案；已提供工作目录/当前目录回退。
5. 文本页最多显示前 20000 个样本；波形页使用全部样本。
6. 当前没有“数字时序图”与“解码文本”的独立视图；界面明确区分原始采样与 RawCommand 解码文本，未把两者混为一种数据。

---

## 7. 下一阶段计划

Stage 6：整体回归与整理。

- 清理 Qt 5.15 弃用警告；
- 完成独立运行依赖打包（Qt DLL + MSYS2 传递 DLL）；
- 回归所有页面、连接/断开、中文路径、配置；
- 更新 README / BUILD_GUIDE / CHANGELOG；
- 生成最终构建与测试报告，明确硬件未验证项。

---

**Stage 5 结论：通过（波形功能以合成 GraphBuffer 验证；真实 LF 采样因固件无 LF 未验证）。**
