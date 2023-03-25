# synwit Platform

## 描述

- 本工程用于提供华芯微特芯片在platformio上开发。目标是支持全系列华芯微特MCU产品，为优秀的国产芯片事业做出自己的贡献。

## 已经支持的主板
* swm341sret6 (Cortex-M33)

## 已支持的例程

- gpio led

- gpio key

- systick

- lcd ugui

- lcd lvgl&touch

- SDRAM

- uart printf

  后期逐步完善各外设的基本历程和常用的项目方案，如差分升级、操作系统、运动控制、电机控制等。

## 如何使用？

请参考： https://mp.weixin.qq.com/s/BGEzg61vUJ7flYhudNscbQ

```bash
pio platform install https://github.com/hysonglet/platformio_synwit.git
```

