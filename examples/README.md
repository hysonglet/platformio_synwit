# 示例

每个系列一个工程，工程里**每个芯片一个 env**，所以 15 个器件都有可直接编译的例子。

| 示例 | 覆盖的芯片（`pio run -e ...`） | LED | UART0 |
| --- | --- | --- | --- |
| `swm166-blink/` | `swm166x8` | PA5 | TX=PA1  RX=PA0 |
| `swm181-blink/` | `swm181x9` `swm181xb` `swm181xc` | PA5 | TX=PA1  RX=PA0 |
| `swm190-blink/` | `swm190x9` `swm190xb` | PA5 | TX=PA1  RX=PA0 |
| `swm2x1-blink/` | `swm201x6` `swm211x6` `swm211x8` | PA5 | TX=PA1  RX=PA0 |
| `swm221-blink/` | `swm221xb` | PA5 | TX=PA1  RX=PA0 |
| `swm241-blink/` | `swm241xb` | PA5 | TX=PD14  RX=PD13 |
| `swm260-blink/` | `swm260xb` | PA5 | TX=PC13  RX=PC14 |
| `swm320-blink/` | `swm320xc` `swm320xe` | PA5 | TX=PA3  RX=PA2 |
| `swm341-blink/` | `swm341xe` | PA5 | TX=PM1  RX=PM0 |
| `swm341-rtt/` | `swm341xe` | PA5 | 不走串口，输出经 SWD 调试口走 SEGGER RTT |
| `swm341-lvgl-rtt/` | `swm341xe` | 无 | 800x480 RGB 屏 + LVGL，日志走 RTT（需 SDRAM） |

SWM201 与 SWM211 共用一份 CSL，所以合在 `swm2x1-blink/` 里（靠
`CHIP_SWM201` / `CHIP_SWM211` 区分，导入脚本会自动加）。

## 用法

```bash
cd swm181-blink
pio run -e swm181xc          # 只编译一个芯片
pio run                      # 编译本系列全部芯片
pio device monitor           # 看串口输出，57600 8N1
```

## 程序做了什么

`SystemInit()` → 初始化 UART0（57600 8N1）→ 翻转 PA5 上的 LED，同时
`printf("Hi from <芯片>! SYSCLK = %u Hz\n", SystemCoreClock)`。

引脚和初始化代码都来自厂商 SDK 自带的 `UART/SimplUART` 与 `GPIO/KeyLED`
参考示例，所以拿官方评估板直接就能跑；自己的板子引脚不同时改 `main.c`
顶部的注释处即可。

## printf 是怎么接到串口的

平台链接了 `-specs=nosys.specs`，默认的 `_write()` 只是个弱符号空桩，
printf 的输出会被吞掉。所以每个示例都实现了：

```c
int _write(int fd, char *ptr, int len)   /* overrides the libnosys stub */
```

另外调用了 `setvbuf(stdout, NULL, _IONBF, 0);`，否则 newlib 会缓冲 stdout，
短字符串可能一直不出。

## swm341-rtt：不占用串口的控制台

`swm341-rtt/` 用 SEGGER RTT 输出，什么都不接，靠 SWD 调试口读写芯片 RAM 里的
RTT 控制块。库不是本地拷贝的，而是 `platformio.ini` 里的：

```ini
lib_deps = git@github.com:SEGGERMicro/RTT.git
```

`pio run` 时 PlatformIO 自己 clone 到 `.pio/libdeps/<env>/RTT/`。两个坑已经填了：

- 该仓库根目录没有 `library.json`，PIO 只解包 `RTT/` 子目录，于是官方那份
  `Config/SEGGER_RTT_Conf.h` 不会被拉下来。它本来就该由应用提供，所以工程
  自带一份 `include/SEGGER_RTT_Conf.h`（空的 = 全用默认值）。
- 库在自己的编译环境里构建，看不到工程的 `include/`，所以配了
  `build_flags = -I$PROJECT_DIR/include`（必须用绝对路径）。
- `Syscalls/SEGGER_RTT_Syscalls_GCC.c` 同样没被拉下来，所以示例自己实现了
  `_write()` 转发到 RTT channel 0，`printf()` 也能走 RTT。

看输出用 `JLinkRTTViewer` / `JLinkRTTLogger`，**不是** `pio device monitor`
（那看的是串口，这个示例没用串口）。

## swm341-lvgl-rtt：800x480 RGB 屏 + LVGL 8.4

参考厂商 `02.SWM34S TFT-LCD 驱动例程 / 00.TTL-RGB Demo / 05.SWM34SR&Vxxx_800x480_ebike演示例程`，
去掉触摸/JPEG/SPI flash，只留下屏 + LVGL + RTT 控制台。

```
include/lv_conf.h          LVGL 配置（基于 8.4.0 模板改了 5 处）
include/board_lcd.h        板型选择 + SDRAM 内存布局
include/SEGGER_RTT_Conf.h  RTT 配置
src/board_lcd.c            SDRAM 引脚 → SysTick → RGB565 引脚 + LCD 时序
src/lv_port_disp.c         LVGL 显示驱动（双 framebuffer 直接翻转）
src/main.c                 LVGL 初始化 + 界面 + RTT 日志
```

### 硬件前提

**必须有 SDRAM**。芯片内部只有 64 KB SRAM，而一屏 800x480 RGB565 就要 750 KB：

```
0x80000000   framebuffer 1 (768 000 B)
0x800BB800   framebuffer 2 (768 000 B)
0x80177000   LVGL 堆 128 KB（lv_conf.h 的 LV_MEM_ADR）
```

两块 framebuffer 都在 SDRAM 里，LVGL 画一块、LCD 控制器扫另一块，`flush_cb` 只把
layer 0 的地址改成刚画完的那块（这就是厂商 `lv_port_disp3.c` 的做法）。LVGL 的堆
也搬到 SDRAM（`LV_MEM_ADR`），内部 SRAM 留给栈、`.bss` 和 RTT 控制块——实测
RAM 只用了 3.4 KB / 64 KB，Flash 224 KB / 512 KB。

### 板型和引脚

`include/board_lcd.h` 里用宏选板，`platformio.ini` 里默认是 **64 脚**那块：

```ini
build_flags =
    -I$PROJECT_DIR/include
    -DSWM34S_LCM_PCBV=SWM34SRE_PIN64_A001
```

| 宏 | 板子 | 背光 | 复位 | HSYNC / DEN |
| --- | --- | --- | --- | --- |
| `SWM34SRE_PIN64_A001` | SWM34SRET6 LQFP64（`SWDM-QFP64-34SREB2` 5 寸 800x480） | **PB13** | PD1 | PB3 / PB4 |
| `SWM34SVE_PIN100_A001` | SWM34SVET6 LQFP100（7 寸，PA1 选通 AP3012 升压） | PD9 | PD1 | PB3 / PB4 |
| `SWM34SVE_PIN100_A002` | SWM34SVET6 LQFP100（A002/A003） | PD9 | PD1 | PM8 / PM11 |

R/G/B 数据线和 DCLK/VSYNC 三种板子都一样，只有 HSYNC/DEN 和背光脚不同。**选错就是黑屏**
——背光不亮（还可能被当成 B2 主动拉低）、DE 没有输出。引脚表来自厂商
`Config.h` + `dev_rgb.c`，并用 2024 年 navi-meter 例程的 `board/swm34s/swm34sre/a1/dev_lcdc.c`
逐条核对过；SDRAM 的 39 根引脚也和厂商 `common/sdram/dev_sdram.c` 完全一致。

### 时钟

```ini
board_build.synwit_clock = pll_xtal12m_120m
```

板子默认是 20 MHz 内部 RC，跑不动屏。120 MHz / `ClkDiv 4` = 30 MHz DOTCLK，
配合厂商那组 800x480 时序（Hfp 64 / Hbp 46 / Vfp 22 / Vbp 23）约 62 Hz。
厂商例程跑 150 MHz 配 `ClkDiv 5`，同样是 30 MHz，两者都可以。

### 启动自检

上电后 RTT 里除了版本信息还会打三行，黑屏时先看它：

```
board: SWM34SRE_PIN64_A001 (SWM34SRET6 LQFP64)
SDRAM: ok (framebuffer 1+2, LVGL heap)
LCD: BL pin = 1, 12 frames in 200 ms
```

- `SDRAM`：往两块 framebuffer 和 LVGL 堆的地址写图案读回来，失败会指出是哪个窗口。
- `BL pin`：回读背光脚的实际电平（`IDR`）。选错板型时这里会是 0 或读到别的脚。
- `frames`：清掉 LCD 出帧完成标志后数 200 ms 内又置起来几次，800x480@62 Hz 约 12 次，
  0 说明 LCD 控制器根本没在扫。

### 与厂商 SDK 的差异（已在本工程里改掉）

- 新的 CSL 把 `SDRAM_InitStructure.TimeTRFC` / `SDRAM_TRFC_9` 改名成
  `TimeTRC` / `SDRAM_TRC_9`，并且**新增了 `RefreshTime`**（整片刷新窗口，填 64 ms）。
  照抄老例程会编译不过，留空则刷新过密。
- 老例程用的 `GPIO_AtomicSetBit/ClrBit` 在这个 CSL 里只是 `GPIO_SetBit/ClrBit` 的宏。

### 构建与运行

```bash
cd swm341-lvgl-rtt
pio run                      # LVGL 从 PlatformIO 仓库拉，RTT 从 GitHub clone
pio run -t upload            # probe-rs run：烧完挂住打印 RTT（Ctrl-C 退出）
```

屏幕上会显示标题、运行时间、每 10 秒走一圈的进度条，右下角/左下角有 LVGL 自带的
FPS 与堆占用指示；同样的信息每秒打一次到 RTT channel 0。

## 关于烧录

`pio run` 只做编译。烧录链路没有硬件实测过，需要先在 `platformio.ini` 里
配好 `upload_protocol` / `upload_command`，参见顶层 `README.md` 的
「烧录与调试」一节。
