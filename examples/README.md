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

## 关于烧录

`pio run` 只做编译。烧录链路没有硬件实测过，需要先在 `platformio.ini` 里
配好 `upload_protocol` / `upload_command`，参见顶层 `README.md` 的
「烧录与调试」一节。
