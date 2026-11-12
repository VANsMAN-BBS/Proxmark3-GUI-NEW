# 应用图标设计说明

**版本：** v0.4.1  
**源文件：** `resources/icons/app-icon.svg`  
**生成脚本：** `scripts/make-icons.py`

---

## 1. 设计概念

主体是 **Proxmark3 硬件设备**的简化轮廓，而不是通用的 RFID 波纹或抽象芯片：

| 元素 | 作用 |
|---|---|
| 圆角深色底板 | 现代工具类应用的统一底色，保证浅色/深色任务栏上都可辨识 |
| 绿色 PCB 外形 | 硬件属性；与深色底板形成高对比 |
| 左侧银色 USB 接口 | 最独特的识别元素（Proxmark3 通过 USB 连接） |
| 右侧橙色天线线圈（同心环） | HF 天线，第二识别元素 |
| 两枚深色芯片 + 金色引脚 | 少量元件细节，不堆砌 |
| 铜色走线、指示灯、丝印条 | 提升专业感，缩小后自动弱化 |

不包含任何第三方 Logo、真实设备照片或商业图形。

---

## 2. 配色

| 名称 | 色值 | 用途 |
|---|---|---|
| 背景 | `#24303F → #0C1016` | 渐变底板 |
| 边框 | `#3A4A5E` | 底板描边 |
| PCB 绿 | `#1A5C42` / 高亮 `#2E8B57` | 板身 |
| 铜色 | `#D69236` / 深铜 `#A86C24` | 天线线圈、走线 |
| 银灰 | `#C8D0DA` | USB 接口 |
| 石墨黑 | `#0C1016` | 芯片 |
| 丝印 | `#D0E0D8` | 指示灯、标记 |

---

## 3. 尺寸与简化变体

`scripts/make-icons.py` 用**同一套几何定义**同时生成 SVG 与所有 PNG，避免二者不一致。

- **≥48 px**：完整细节（三个线圈、芯片引脚、走线、丝印）。
- **≤32 px**：简化变体（更粗的 USB、单个粗线圈、实心芯片），保证 16×16 仍可辨识。

输出：

```text
resources/icons/
├─ app-icon.svg          (矢量源，1024 单位画布)
├─ app-icon.png          (512×512)
├─ app-icon-256.png
├─ app-icon-128.png
├─ app-icon-64.png
├─ app-icon-48.png
├─ app-icon-32.png
├─ app-icon-24.png
├─ app-icon-16.png
└─ app-icon.ico          (包含 16/24/32/48/64/128/256 七个真实尺寸)
```

ICO 由脚本按 PNG 帧直接封装（每个尺寸是独立图像帧，不是把单张图改名）。

---

## 4. Windows 集成

| 位置 | 实现 |
|---|---|
| Qt 应用图标 | `QApplication::setWindowIcon()`，按 16/24/32/48/64/128/256 添加多尺寸 |
| 主窗口图标 | `MainWindow::setWindowIcon(QApplication::windowIcon())` |
| 任务栏 | 使用窗口图标（运行时） |
| EXE 文件图标 | `.pro` 中 `win32: RC_ICONS = ../resources/icons/app-icon.ico`，windres 嵌入 |
| 快捷方式 | 继承 EXE 图标 |
| 发布目录 EXE | 构建产物直接带图标（已从 EXE 提取验证） |

资源清单：`resources/icons/icons.qrc`（prefix `/icons`），已加入 `RESOURCES`。

---

## 5. 验证

| 检查 | 结果 |
|---|---|
| ICO 帧列表 | `16, 24, 32, 48, 64, 128, 256`（7 个真实尺寸） |
| 构建日志包含 windres 处理资源 | 通过 |
| 从构建 EXE 提取图标 | 32×32；颜色统计 green=196 / orange=88（与设计一致） |
| 图标缓存注意 | 同一路径重复提取可能命中 Windows 图标缓存；复制到新路径后验证通过 |
| 16×16 可辨识性 | 目视检查通过（深色底板 + 绿色 PCB + 银色 USB + 橙色线圈） |

---

## 6. 维护

修改 `scripts/make-icons.py` 中的几何或配色后运行：

```powershell
python scripts\make-icons.py
```

再重新构建即可同时更新 SVG、PNG、ICO 与 EXE 图标。
