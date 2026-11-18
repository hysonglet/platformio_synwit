# platform-synwit

[English](README.en.md) | 中文

[PlatformIO](https://platformio.org/) 的 **华芯微特（Synwit）SWM 系列** ARM Cortex-M 开发平台。

平台设计得非常薄且**数据驱动**：`platform-synwit` 只负责板级定义、编译/烧录脚本和导入工具；所有芯片细节（内核、Flash/RAM、头文件与源码目录、链接脚本、SVD、可选库、时钟 preset）都放在同级的 `framework-synwit/mcu.json` 里。新增型号只需要增加数据，不需要改平台代码。

- 10 个系列 / 15 款芯片，全部经过实编译验证
- 厂商标准外设库（CSL）+ CMSIS-Core，GCC / newlib 开箱即用
- 时钟树、可选库（CMSIS-DSP、USB-Host）、链接脚本均可在 `platformio.ini` 配置
- 每个系列都有 blink 示例，另有 RTT、LVGL、离线地图等进阶示例

## 关于华芯微特

[华芯微特（Synwit）](https://www.synwit.cn/) 是一家由留美归国资深技术团队创立的中国芯片设计公司、国家高新技术企业，2014 年进入 MCU 产业，专注于 **32 位 MCU** 的研发设计，并面向细分市场提供参考解决方案。其官网为 <https://www.synwit.cn/>。

核心团队拥有十年以上芯片设计经验，在 CPU、存储、低功耗处理器、AD、POWER、ESD 等方面积累深厚，拥有自主知识产权超过 100 件。芯片基于 ARM® Cortex®-M0 / M4 / M33 等内核（含安谋科技“星辰”STAR-MC1），主要面向 **TFT-LCD 屏驱控制、电机控制、白色家电、智能控制与工控仪表** 等领域，强调可靠性与稳定性。公司愿景是“成为屏驱和电控领域产品及服务领先的半导体公司”，秉持“中国心·世界芯”的理念。

本平台支持的 SWM 系列即其产品线的 32 位 MCU；其中 SWM341（Cortex-M33，带 TFT-LCD 控制）是屏驱方向的代表，配套 800×480 RGB 屏的 LVGL 示例见 [`swm341-lvgl-rtt`](examples/swm341-lvgl-rtt/) 与 [`swm341-lvgl9.5-map`](examples/swm341-lvgl9.5-map/)。

## 目录结构

```
platform-synwit/
├── platform.json               # 平台元数据：框架、工具链、包依赖
├── platform.py                 # 自动选用同级 framework-synwit，解析 SVD 路径
├── boards/                     # 15 个板级 JSON（由 tools/import_csl.py 生成）
├── builder/
│   ├── main.py                 # 编译 / .bin / .hex / 烧录目标
│   └── frameworks/
│       ├── _bare.py            # 裸机公共编译选项
│       └── synwit.py           # 读取 mcu.json，构建 CSL 静态库
├── tools/import_csl.py         # 从厂商 SDK 导入 CSL 并生成上述数据
└── examples/                   # 示例工程（详见 examples/README.md）
```

配套的 `framework-synwit/` 与 `platform-synwit/` 同级，包含 `csl/`（厂商外设库）、`mcu.json`、`ldscripts/` 和 `svd/`。

## 安装

### 前置条件

- [PlatformIO Core 6](https://docs.platformio.org/en/latest/core/index.html)（`pio --version` 检查）
- Python 3（`platformio` 自带，平台脚本会用到）
- GCC ARM 工具链由平台自动下载

### 从 Git 仓库安装

```bash
pio pkg install --global --platform https://github.com/hysonglet/platformio_synwit.git
```

这种方式会同时从 GitHub 拉取配套的 `framework-synwit` 框架包，无需单独安装。

### 验证安装

```bash
pio pkg list --global          # 应看到 synwit 平台
pio boards synwit              # 列出 15 个板子
pio platform show synwit
```

## 支持的型号

| 系列   | 内核       | Board ID（`board = ...`）        | Flash             | RAM         | 默认时钟 preset            |
| ------ | ---------- | -------------------------------- | ----------------- | ----------- | -------------------------- |
| SWM166 | Cortex-M0  | `swm166x8`                       | 64 KB             | 8 KB        | `sys_clk_12mhz`            |
| SWM181 | Cortex-M0  | `swm181x9` `swm181xb` `swm181xc` | 96 / 120 / 248 KB | 16 KB       | `vendor_default`           |
| SWM190 | Cortex-M0  | `swm190x9` `swm190xb`            | 96 / 120 KB       | 20 KB       | `vendor_default`           |
| SWM201 | Cortex-M0  | `swm201x6`                       | 32 KB             | 8 KB        | `vendor_default`           |
| SWM211 | Cortex-M0  | `swm211x6` `swm211x8`            | 32 / 64 KB        | 8 KB        | `vendor_default`           |
| SWM221 | Cortex-M0  | `swm221xb`                       | 128 KB            | 8 KB        | `vendor_default`           |
| SWM241 | Cortex-M0  | `swm241xb`                       | 128 KB            | 8 KB        | `vendor_default`           |
| SWM260 | Cortex-M0  | `swm260xb`                       | 128 KB            | 8 KB        | `vendor_default`           |
| SWM320 | Cortex-M4  | `swm320xc` `swm320xe`            | 256 / 512 KB      | 64 / 128 KB | `vendor_default`           |
| SWM341 | Cortex-M33 | `swm341xe`                       | 512 KB            | 64 KB       | `sys_clk_20mhz`（内部 RC） |

全部 15 个 board 均已实编译通过，产出 `.elf` / `.bin`；每个系列都带 SVD，`pio debug` 可以直接用。SWM201 与 SWM211 共用一份 `csl/swm2x1`，靠 `CHIP_SWM201` / `CHIP_SWM211` 区分。

## 快速开始

```bash
cp -r examples/swm341-blink /tmp/my-swm341 && cd /tmp/my-swm341
pio run                       # 编译本工程全部 env
pio run -e swm341xe           # 只编译一个芯片
pio device monitor            # 看串口输出，57600 8N1
```

最小 `platformio.ini`：

```ini
[env:swm341xe]
platform = synwit
framework = synwit
board = swm341xe
monitor_speed = 57600
```

blink 示例使用厂商评估板的引脚（LED=PA5，UART0 引脚见 `examples/README.md`），自己的板子按 `main.c` 顶部注释修改。

## 工程配置

都写在 `platformio.ini`，对应 board JSON 的 `build.*`：

| 选项                                 | 说明                                                         |
| ------------------------------------ | ------------------------------------------------------------ |
| `board_build.synwit_clock`           | 时钟树：`mcu.json` 中的 preset 名，或任意 `NAME=VALUE` 列表  |
| `board_build.f_cpu`                  | 送给 `F_CPU` 的主频，**换 preset 后需手动同步**              |
| `board_build.synwit_features`        | 追加可选库：`dsp`（SWM320/341）、`usb_host`（SWM341）        |
| `board_build.float_abi`              | `soft`（默认，与厂商 Makefile 一致）/ `softfp` / `hard`      |
| `board_build.ldscript`               | 覆盖默认链接脚本                                             |
| `board_build.synwit_<库短名>_filter` | 覆盖某个库的 `src_filter` 来裁剪驱动，如 `synwit_csp_filter` |

每个器件的 preset 从 `system_<芯片>.c` 的 `SYS_CLK_xxx` 常量自动派生，外加 `vendor_default`。列出某个芯片的全部 preset：

```bash
python3 -c "import json;print(*sorted(json.load(open('../framework-synwit/mcu.json'))['families']['swm181xc']['clock_presets']),sep='\n')"
```

SWM341 另外提供了 `xtal12m` 和 `pll_xtal12m_120m`（12 MHz 晶振 → PLL 120 MHz）：

```ini
board_build.synwit_clock = pll_xtal12m_120m
board_build.f_cpu        = 120000000L
board_build.synwit_features = dsp
```

## 示例

每个系列一个工程，工程里每个芯片一个 env。完整说明（引脚、RTT、SDRAM 布局、地图库安装等）见 [`examples/README.md`](examples/README.md)。

| 示例                                    | 覆盖的芯片（`pio run -e ...`）   | 说明                                              |
| --------------------------------------- | -------------------------------- | ------------------------------------------------- |
| `swm166-blink/`                         | `swm166x8`                       | LED 闪烁 + UART0 printf                           |
| `swm181-blink/`                         | `swm181x9` `swm181xb` `swm181xc` | 同上                                              |
| `swm190-blink/`                         | `swm190x9` `swm190xb`            | 同上                                              |
| `swm2x1-blink/`                         | `swm201x6` `swm211x6` `swm211x8` | 同上，SWM201/211 共用一份 CSL                     |
| `swm221-blink/`                         | `swm221xb`                       | 同上                                              |
| `swm241-blink/`                         | `swm241xb`                       | 同上                                              |
| `swm260-blink/`                         | `swm260xb`                       | 同上                                              |
| `swm320-blink/`                         | `swm320xc` `swm320xe`            | 同上                                              |
| `swm341-blink/`                         | `swm341xe`                       | 同上                                              |
| `swm341-rtt/`                           | `swm341xe`                       | SEGGER RTT 控制台，不占串口                       |
| `swm341-lvgl-rtt/`                      | `swm341xe`                       | 800×480 RGB 屏 + LVGL 8.4，日志走 RTT（需 SDRAM） |
| `swm341-lvgl9.5-map/`                   | `swm341xe`                       | LVGL 9.5 + microSD（FatFs）+ 离线地图预览         |

```bash
cd examples/swm341-blink
pio run -e swm341xe          # 只编译一个芯片
pio run                      # 编译本系列全部芯片
pio device monitor           # 串口 57600 8N1
```

## 烧录与调试

`pio run` 只做编译，烧录链路尚未逐项做硬件实测，接口已留好：

1. `upload_protocol = custom` + `upload_command=...` 调厂商工具（SWMTool / SynwitPRG / J-Flash）。上面几个 SWM341 进阶示例用的是 `probe-rs run --chip swm341`。
2. `upload_protocol = jlink`：需先给 J-Link 安装器件支持，厂商在 `01.开发环境搭建(入门必备)/04.Segger J-Link 套件/Synwit_JFLASH-241126.zip` 提供了各系列 `.FLM`，放到 `Devices/Synwit/` 并在 `JLinkDevices.xml` 中登记。`platform.json` 默认声明了 tool-jlink，但没人用 jlink 时会自动去掉，不白下载约 40 MB。

调试用的 SVD 随框架包发布，`platform.py::_add_series_debug()` 会把 `debug.svd_path` 解析成本地绝对路径。

## 许可

本项目采用 [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0)，这是宽松许可证：可以商用、修改和闭源分发，**无需公开你的源码**；仅需保留版权与许可声明、标明改动，并附带许可证副本。
