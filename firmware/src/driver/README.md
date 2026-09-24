# driver：STM32 片内外设驱动层（Driver）

放置 TIM、DMA、输入捕获、硬件 Gate 等片内外设测量驱动。

当前 `measurement_hw.c/.h` 集中封装 TIM1/TIM2/TIM3/TIM4 与 DMA 的底层测量动作以及 HAL 回调处理。

边界：Driver 负责“硬件怎么测”，不负责“当前赛题应该选择哪种测量算法”。
