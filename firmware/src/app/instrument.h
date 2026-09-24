#ifndef INSTRUMENT_H
#define INSTRUMENT_H

/**
 * @brief 初始化仪器应用层，并启动当前需要的各功能模块。
 */
void Instrument_Init(void);

/**
 * @brief 执行仪器应用层轮询任务，由主循环持续调用。
 */
void Instrument_Task(void);

#endif
