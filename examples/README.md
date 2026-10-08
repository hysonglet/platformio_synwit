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

## 关于烧录

`pio run` 只做编译。烧录链路没有硬件实测过，需要先在 `platformio.ini` 里
配好 `upload_protocol` / `upload_command`，参见顶层 `README.md` 的
「烧录与调试」一节。
