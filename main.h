#ifndef __MAIN_H
#define __MAIN_H
#ifdef __cplusplus
extern "C" {
#endif
#include "stm32c0xx_hal.h"
extern ADC_HandleTypeDef hadc1;
extern UART_HandleTypeDef huart2;
void Error_Handler(void);
#ifdef __cplusplus
}
#endif
#endif