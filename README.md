# 1997B 简易数字频率计

基于 **STM32F103C8T6 + STM32CubeMX + HAL + CMake** 实现 1997 年全国大学生电子设计竞赛 B 题《简易数字频率计》。

当前项目以“先完成基本要求，再逐步扩展发挥部分”为原则。第一阶段重点是把题目拆成可验证的硬件测量链路和 STM32 定时器任务，并保持业务代码与 CubeMX 生成代码分层。

> 题目资料：[`docs/全国大学生电子设计大赛历年题目.pdf`](docs/全国大学生电子设计大赛历年题目.pdf)

---

## 软件工程结构

自定义代码目录已与 `embed-stm32c8t6-template` 的分层语义对齐。这个仓库仍然是 CubeMX + HAL 工程，因此 CubeMX 生成的 `Core/`、`Drivers/` 和 `cmake/stm32cubemx/` 保持原位，不把生成代码强行搬进模板的 `src/user`。

```text
firmware/
├── Core/                       # CubeMX 生成：main、中断、GPIO/TIM/DMA 初始化
├── Drivers/                    # STM32 HAL / CMSIS
├── src/
│   ├── app/                    # 赛题业务、测量算法、状态机、UI 调度
│   │   ├── instrument.*
│   │   ├── frequency_auto.*
│   │   ├── frequency_meter.*
│   │   ├── interval_meter.*
│   │   ├── pulse_width_meter.*
│   │   ├── self_calibration.*
│   │   └── instrument_ui.*
│   ├── driver/
│   │   └── measurement_hw.*    # TIM / DMA / Capture / Gate 等片内外设测量驱动
│   ├── bsp/
│   │   └── instrument_ui_port.*# 显示、LED、刷新旋钮等板级硬件端口
│   └── common/                 # 与具体赛题无关的通用组件（当前暂无）
└── CMakeLists.txt
```

依赖方向保持为：

```text
Core/main
   ↓
app
 ├────→ driver
 ├────→ bsp
 └────→ common
```

这次重构只调整目录与构建引用，**不改变测频、周期、脉宽、DMA、溢出扩展和高低频自动切换算法**。

---

## 1. 题目要求

### 1.1 基本要求

| 功能 | 输入范围 | 精度 / 其他要求 |
| --- | --- | --- |
| 频率测量 | 方波、正弦波；0.5V～5V；1Hz～1MHz | 误差 ≤ 0.1% |
| 周期测量 | 方波、正弦波；0.5V～5V；1Hz～1MHz | 误差 ≤ 0.1% |
| 脉冲宽度测量 | 脉冲波；0.5V～5V；脉宽 ≥ 100μs | 误差 ≤ 1% |
| 显示 | 十进制数字显示 | 刷新时间 1～10s 连续可调；三种测量功能分别用不同颜色 LED 指示 |
| 自校 | 时标信号 | 1MHz |
| 电源 | 自行设计并制作稳压电源 | 满足整机供电要求 |

### 1.2 发挥部分

1. 频率测量扩展到 **0.1Hz～10MHz**，测量误差 ≤ 0.01%，最大闸门时间 ≤ 10s。
2. 测量周期脉冲信号的占空比：频率 1Hz～1kHz、占空比 10%～90%，误差 ≤ 1%。
3. 在 1Hz～1MHz 范围内进行小信号频率测量，并设计抗干扰措施。

当前开发优先完成基本要求，发挥部分暂不作为第一阶段的设计约束。

---

## 2. 题目分析

这道题表面上是“数字频率计”，实际上包含三个不同层次的问题：

1. **把外部模拟/数字信号变成 STM32 能稳定识别的数字边沿。**
2. **用定时器测量边沿的数量或边沿之间的时间。**
3. **把测量结果组织成频率、周期、脉宽等仪器功能，并完成显示和模式管理。**

### 2.1 正弦波和方波为什么可以共用一套测量逻辑

STM32 不需要判断输入原来是正弦波还是方波。完整测量链路应当先经过硬件前置电路：

```text
正弦波 / 方波
      │
      ▼
输入保护 / 衰减或放大 / 偏置
      │
      ▼
比较器 + 施密特迟滞整形
      │
      ▼
干净的 0/3.3V 数字脉冲
      │
      ▼
STM32 TIM 输入
```

STM32 最终只处理数字边沿：

- 统计固定时间窗口内有多少个边沿；
- 测量两个边沿之间经过了多少个 Timer Tick；
- 测量上升沿到下降沿之间经过了多少个 Timer Tick。

因此，“支持正弦波和方波”主要考察的是 **输入调理 + 比较整形 + 数字测量** 的完整系统设计，而不是在软件里做波形识别。

> 当前仓库主要在实现 STM32 测量逻辑。输入保护、比较器、施密特整形等模拟前端将在后续硬件阶段补充。

---

## 3. 为什么频率测量不能只用一种方法

题目要求覆盖 **1Hz～1MHz**，跨度达到 6 个数量级。

### 3.1 高频：闸门计数法

在固定闸门时间 `Tgate` 内统计输入脉冲数 `N`：

```text
f = N / Tgate
```

若采用 1s 闸门：

```text
100 kHz → 1 秒约得到 100000 个脉冲
1 MHz   → 1 秒约得到 1000000 个脉冲
```

高频时脉冲数量足够多，±1 个计数量化误差所占比例很小，因此适合闸门计数法。

但在 1Hz 附近：

```text
1 Hz × 1s = 1 个脉冲
```

此时 ±1 个计数会导致非常大的相对误差，所以不能让 1s 闸门法覆盖整个频段。

### 3.2 低频：周期法

测量相邻两个同极性边沿之间的时间：

```text
T = ΔCNT × Ttick
f = 1 / T
```

低频时一个周期包含大量 Timer Tick，时间分辨率很高，因此更适合周期法。

### 3.3 第一版选择：高低频双方法

```text
                    被测信号
                       │
              ┌────────┴────────┐
              ▼                 ▼
            低频               高频
              │                 │
        输入捕获测周期       闸门脉冲计数
              │                 │
          f = 1 / T        f = N / Tgate
              └────────┬────────┘
                       ▼
                    最终频率
```

具体切换阈值将在 TIM2/TIM3 实际配置和硬件测试后确定，不在设计阶段过早写死。

---

## 4. STM32F103C8T6 时钟方案

板上使用 **8MHz HSE 外部晶振**：

```text
HSE 8MHz
   │
   ▼
PLL ×9
   │
   ▼
SYSCLK 72MHz
   │
   ├── APB1 = 36MHz → TIM2/TIM3/TIM4 Clock = 72MHz
   │
   └── APB2 = 72MHz → TIM1 Clock = 72MHz
```

因此当前主要定时器都以 **72MHz** 作为硬件时钟基准，后续通过 PSC 决定 CNT 的时间分辨率。

---

## 5. 定时器资源分工

第一版采用“**一个 Timer 一个主要职责**”的方式，优先保证结构清晰、容易验证和容易调试；不为了节省资源而过早进行运行时动态复用。

| 定时器 | 第一版职责 | 主要工作模式 | 状态 |
| --- | --- | --- | --- |
| TIM1 | 1MHz 自校时标输出 | PWM Output | 已配置并启动 |
| TIM2 | 高频频率测量 | ETR 外部脉冲计数 + Gated Slave | 已配置并接入连续测量流程 |
| TIM3 | 周期 / 脉冲宽度测量 | CH1 Direct TI1 上升沿 DMA + CH2 Indirect TI1 下降沿中断 | 已配置并接入周期 / 脉宽测量流程 |
| TIM4 | 高频测频闸门时间基准 | One Pulse + TRGO Enable | 已配置并接入连续测量流程 |

### 5.1 TIM1：1MHz 自校信号

当前 TIM1 时钟：

```text
fTIM1 = 72MHz
```

PWM 公式：

```text
fPWM = fTIM / ((PSC + 1) × (ARR + 1))
```

当前配置：

```text
PSC  = 0
ARR  = 71
CCR1 = 36
```

因此：

```text
CNT Clock = 72MHz
Ttick     ≈ 13.89ns
PWM 周期  = 72 × 13.89ns ≈ 1μs
fPWM      = 1MHz
Duty      = 36 / 72 = 50%
```

输出引脚：

```text
TIM1_CH1 → PA8
```

PA8 配置为复用推挽输出，GPIO Speed 使用 Medium。

> 当前 1MHz 自校信号和系统测量时基来自同一个 8MHz HSE。它可以验证 PWM 输出、输入前端和测量逻辑链路，但不能作为独立于系统晶振的绝对频率基准。若后续需要验证绝对时基精度，应引入独立参考源或更高稳定度的时钟基准。

### 5.2 TIM2 + TIM4：高频闸门计数链路

当前高频测频链路已经完成 CubeMX/HAL 配置和软件闭环：

```text
外部脉冲
   │
   ▼
PA0 / TIM2_ETR
   │
   ▼
TIM2 外部时钟计数
   ▲
   │ Gate：ITR3
   │
TIM4 TRGO = ENABLE
   │
   ▼
1s One Pulse 闸门
```

TIM2：

```text
PSC        = 0
ARR        = 65535
Clock      = ETR Mode 2
Slave Mode = Gated
Trigger    = ITR3（来自 TIM4）
```

TIM4：

```text
fTIM4 = 72MHz
PSC   = 7199
ARR   = 9999
```

因此：

```text
CNT Clock = 72MHz / (7199 + 1) = 10kHz
Ttick     = 100μs
Tgate     = 10000 × 100μs = 1s
```

TIM4 使用 One Pulse 模式。TIM4 启动时 `CEN=1`，`TRGO=ENABLE` 打开 TIM2 的硬件 Gate；1 秒后 TIM4 Update 事件使 One Pulse 自动停止，`CEN=0`，硬件 Gate 同步关闭，TIM2 不再接收外部计数。

由于 TIM2 是 16 位定时器，1MHz 输入在 1 秒内会产生约 1,000,000 个计数，超过 65535，因此软件通过 TIM2 Update 中断统计溢出次数：

```text
total_count = overflow_count × 65536 + CNT
```

`measurement_hw` 已提供：

```text
MeasurementHw_FrequencyCounterStart()
MeasurementHw_FrequencyCounterIsReady()
MeasurementHw_FrequencyCounterGetCount()
```

并实现 `HAL_TIM_PeriodElapsedCallback()`：

- TIM2 Update：累计 16 位计数器溢出次数；
- TIM4 Update：在闸门关闭后读取 TIM2 CNT，并锁存最终总脉冲数；
- 对 TIM2 恰好在闸门边界产生 Update、但中断尚未执行的情况进行补偿，避免漏计一次 65536；
- 下一轮启动失败时保留上一轮 ready/result，允许上层继续重试。

当前新增 `frequency_meter` 应用层：

```text
FrequencyMeter_Init()
FrequencyMeter_Task()
FrequencyMeter_GetFrequencyHz()
FrequencyMeter_IsValid()
```

其运行过程为：

```text
Instrument_Init()
      │
      ├── 启动 TIM1 自校 PWM
      │
      └── FrequencyMeter_Init()
                │
                ▼
       启动第一轮 1s 测量

Instrument_Task()
      │
      ▼
FrequencyMeter_Task()
      │
      ├── 未完成 → 立即返回，不阻塞 CPU
      │
      └── 已完成
            │
            ├── 读取 total_count
            ├── 保存 frequency_hz
            └── 立即启动下一轮 1s 测量
```

由于当前闸门固定为 1 秒：

```text
frequency_hz = total_count
```

至此，TIM2 + TIM4 的高频频率测量软件闭环已经形成。当前尚未接显示，因此测量结果暂存在 `frequency_meter` 内，可通过 `FrequencyMeter_GetFrequencyHz()` 读取。

---

## 6. 周期与脉冲宽度方案

周期和脉冲宽度本质上都是“两个边沿之间的时间间隔”，当前统一使用 TIM3 输入捕获实现。

TIM3_CH1 已使用 DMA 连续捕获 TI1 上升沿，并通过软件溢出计数把 16 位 CCR1 扩展为完整时间戳：

```text
timestamp = overflow_count × 65536 + CCR
```

相邻两个上升沿得到：

```text
delta_ticks = timestamp2 - timestamp1
period_ns   = delta_ticks × 125 / 9
frequency   = 72MHz / delta_ticks
```

周期法频率当前以 mHz 整数保存，避免 STM32F103 上不必要的浮点运算。

### 周期

```text
上升沿 A                      上升沿 B
   ↑                             ↑
   └──────────── T ──────────────┘

T = timestamp_B - timestamp_A
```

### 脉冲宽度

```text
          HIGH
      ┌──────────┐
──────┘          └──────
      ↑          ↑
    上升沿      下降沿

PulseWidth = timestamp_fall - timestamp_rise
```

当前软件方案：

```text
TIM3_CH1：Direct TI1，Rising，DMA
TIM3_CH2：Indirect TI1，Falling，中断
```

CH1 保存最近一次上升沿时间戳，CH2 下降沿到来后与最近上升沿配对，得到高电平脉宽。App 层已经加入 `pulse_width_meter`，输出 `pulse_width_ticks` 和 `pulse_width_ns`。

CubeMX 已将 TIM3_CH2 配置为 Input Capture Indirect TI、Falling、DIV1、Filter 0；CH1 / CH2 共用 PA6 / TI1，因此不额外占用 PA7。CH2 的 CC2 中断只在脉宽显示模式启用，避免高频输入时产生不必要的大量中断。

---

## 7. 显示、模式 LED 与 1～10 秒刷新

软件层已经加入 `instrument_ui` 和 `instrument_ui_port`：

```text
测量结果
   │
   ▼
instrument_ui
   ├── 频率模式
   ├── 周期模式
   ├── 脉宽模式
   ├── 1~10s 刷新调度
   └── stale / valid 状态
   │
   ▼
instrument_ui_port
   ├── Display Render
   ├── Mode LED
   └── Refresh ADC
```

刷新时间使用 12 位 ADC 原始值连续映射：

```text
ADC = 0       → 1000ms
ADC = 4095    → 10000ms
中间值        → 线性映射到 1~10s
```

当前 `instrument_ui_port.c` 是默认空硬件适配层。后续确定 OLED / LCD / 数码管、LED GPIO 和 ADC 通道后，只替换该端口实现，不需要修改测量算法和 UI 调度逻辑。

三种测量模式由 `InstrumentUiMode` 管理，模式切换时调用 `InstrumentUiPort_SetModeLed()`；具体三种 LED 的颜色和引脚留到硬件阶段决定。

---

## 8. 软件分层

项目从早期就把业务代码放入 `firmware/App`，避免后续 `main.c` 膨胀后再进行大范围拆分。

```text
main.c
  │
  ▼
instrument
  │
  ├── self_calibration
  ├── frequency_meter       （TIM2/TIM4 闸门法）
  ├── interval_meter        （TIM3 周期法）
  ├── pulse_width_meter     （TIM3 脉宽）
  ├── frequency_auto        （高低频自动选择）
  └── instrument_ui         （模式 / 显示 / 刷新调度）
  │
  ▼
measurement_hw
  │
  ▼
HAL / TIM / GPIO / IRQ
```

各层职责：

| 层 | 职责 |
| --- | --- |
| `Core/` | CubeMX 生成代码；时钟、GPIO、TIM 等底层初始化 |
| `instrument` | 仪器应用编排；当前负责启动自校和高频频率测量，并在主循环调用测频任务 |
| `self_calibration` | 1MHz 自校业务逻辑 |
| `frequency_meter` | TIM2/TIM4 1 秒闸门法；保存高频测量结果并连续触发下一轮 |
| `interval_meter` | TIM3 周期法；完成 overflow 扩展、完整时间戳、delta_ticks、周期和周期法频率 |
| `pulse_width_meter` | TIM3 上升沿到下降沿的高电平脉宽计算，输出 tick / ns |
| `frequency_auto` | 在闸门法与周期法之间自动选择；当前使用 2kHz / 5kHz 滞回阈值，待实机校准 |
| `instrument_ui` | 管理频率 / 周期 / 脉宽三种显示模式、1~10s 刷新调度和最新 UI 数据帧 |
| `instrument_ui_port` | 显示设备、模式 LED、刷新 ADC 的硬件适配层；当前为 no-op，等待硬件选型 |
| `measurement_hw` | 对 HAL、Timer、CNT、CCR、中断等硬件操作进行集中封装；包含 TIM1 自校、TIM2/TIM4 闸门计数和 TIM3 捕获底层接口 |
| `main.c` | 系统初始化后只调用 `Instrument_Init()` 和 `Instrument_Task()` |

原则：

```text
Core 描述“硬件如何初始化”
App 描述“项目如何使用硬件”
```

---

## 9. 当前工程状态

### 9.1 测量内核 V1

不考虑模拟前端、电源和实机误差校准时，当前 STM32 测量内核 V1 已经形成完整软件闭环：

```text
外部数字边沿
  ↓
TIM / DMA / IRQ
  ↓
CNT / CCR / overflow
  ↓
timestamp / delta_ticks
  ↓
频率 / 周期 / 脉宽
  ↓
高低频自动选法
  ↓
valid / timeout
  ↓
instrument_ui
```

无信号 / 陈旧数据处理已经加入：

- 1 秒闸门结果为 0 时，闸门法结果立即失效；
- 闸门法超过 2.5s 没有新的非零结果时失效；
- 周期法超过 3s 没有新的完整捕获结果时失效；
- 脉宽超过 3s 没有新的完整“上升沿 → 下降沿”结果时失效；
- `frequency_auto` 只从当前仍有效的数据源中选择最终频率。

已完成：

- STM32F103C8T6 基础工程；
- HSE 8MHz → PLL ×9 → SYSCLK 72MHz；
- SWD 调试接口保留；
- TIM1_CH1 / PA8 输出 1MHz、50% PWM 自校时标；
- PA8 GPIO Speed = Medium；
- TIM2 / PA0 配置为 ETR 外部脉冲计数；
- TIM2 配置为 Gated Slave，触发源为 TIM4 对应的 ITR3；
- TIM2 Update 中断已启用，用于扩展 16 位计数范围；
- TIM4 配置为 1 秒 One Pulse 闸门；
- TIM4 TRGO = ENABLE，已经建立 TIM4 → TIM2 的纯硬件 Gate 联动；
- TIM4 Update 中断已启用；
- `measurement_hw` 已实现 TIM2/TIM4 一次测量的启动、溢出统计、闸门结束锁存和结果读取；
- 新增 `frequency_meter`，实现高频测频结果保存和连续非阻塞测量；
- `Instrument_Init()` 已启动第一轮高频测量；
- `Instrument_Task()` 已接入 `FrequencyMeter_Task()`；
- CMake 已加入 `frequency_meter.c`；
- `Core` 与 `App` 业务层分离；
- GitHub Actions ARM Debug Build 已验证 TIM2/TIM4 底层实现可编译。

当前软件阶段已经可以作为 **“测量内核 V1 完成”** 的里程碑。

后续进入硬件 / 联调阶段：

1. 用实物信号验证 TIM2/TIM4 高频闸门法、TIM3 周期法和脉宽法；
2. 根据实际误差重新确定 2kHz / 5kHz 自动切换滞回阈值；
3. 选定 OLED / LCD / 数码管后实现 `instrument_ui_port` 显示输出；
4. 配置三种模式 LED 的 GPIO，并实现 `InstrumentUiPort_SetModeLed()`；
5. 配置电位器 ADC，并让 `InstrumentUiPort_ReadRefreshAdcRaw()` 返回 0~4095；
6. 设计并验证输入保护、比较器、施密特整形等模拟前端；
7. 基本要求实机通过后再进入发挥部分。

---

## 10. 构建

STM32 工程位于 `firmware/`：

```bash
cmake --preset Debug
cmake --build --preset Debug --parallel
```

CI 使用同一套 CMake 工程进行 ARM Debug 构建并生成 ELF / HEX / BIN 制品。
