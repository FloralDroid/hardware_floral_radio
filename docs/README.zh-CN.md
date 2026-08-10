# Floral Radio 模拟

[English](README.md)

本模块负责模拟蜂窝身份、注册状态、信号、小区、SIM、通话和短信，并通过
`floral-rild` 提供标准 Android Radio HAL 适配层。FloralDevice 使用
`floral.device.radio.IRadioState/default` 控制该模型。

服务启动时首先校验 `/ipc/floral_stream/radio.json`。有效的挂载配置会成为
本次启动的设备身份并启用蜂窝模拟。文件缺失、不可读或无效时，Radio HAL 仍保持
可用，但报告无线电关闭、SIM 缺失、未注册、信号未知且没有小区；不会生成默认身份，
也不会读取持久化身份作为回退。

仓库中的[示例配置](../examples/radio.json)已由模型测试直接校验，可以作为
`radio.json` 挂载到宿主实例目录。身份字段按一个整体接收：MCC、MNC 必须是 IMSI
前缀，IMEI 和 ICCID 必须具有有效校验位，电话号码及 SIM 字段也必须满足各自格式。

运行时 FHC1 控制使用有界租约覆盖注册、信号、小区和 SIM 状态。通话与短信事件
仍通过 Android 标准电话 API 暴露，并可通过 FHC1 命令检查。线协议位于
`packages/services/FloralDevice/docs/protocols/FHC1.md`。

## 构建集成

```sh
source build/envsetup.sh
lunch redroid_x86_64-userdebug
m floral-rild floral_radio_model_test
```
