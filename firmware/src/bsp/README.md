# bsp：板级支持层（Bsp）

放置与实际板级器件连接直接相关的接口。

当前 `instrument_ui_port.c/.h` 是显示器、模式 LED、刷新时间 ADC 等 UI 硬件端口。后续更换 OLED/LCD/数码管时优先只修改本层。
