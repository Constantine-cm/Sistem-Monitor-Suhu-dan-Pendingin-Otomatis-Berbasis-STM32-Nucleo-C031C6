/* USER CODE BEGIN Header */
/* Monitor Suhu V-TELEMETRY: Dengan Serial Data Logging */
/* USER CODE END Header */

#include "main.h"
#include <stdio.h> // Wajib untuk sprintf
#include <string.h>

ADC_HandleTypeDef hadc1;
UART_HandleTypeDef huart2; // Handle Serial

/* Mapping Baru (PA2/PA3 bebas untuk Serial) */
/* A=PA1, B=PA8, C=PA9, D=PA4, E=PA5, F=PA6, G=PA7 */
GPIO_TypeDef* segPorts[7] = {GPIOA, GPIOA, GPIOA, GPIOA, GPIOA, GPIOA, GPIOA};
uint16_t segPins[7] = {GPIO_PIN_1, GPIO_PIN_8, GPIO_PIN_9, // Perubahan disini
                       GPIO_PIN_4, GPIO_PIN_5, GPIO_PIN_6, GPIO_PIN_7};

/* Peta Karakter (0-9, -, C) */
const uint8_t seg7_digit[12][7] = {
    {1,1,1,1,1,1,0}, {0,1,1,0,0,0,0}, {1,1,0,1,1,0,1}, {1,1,1,1,0,0,1},
    {0,1,1,0,0,1,1}, {1,0,1,1,0,1,1}, {1,0,1,1,1,1,1}, {1,1,1,0,0,0,0},
    {1,1,1,1,1,1,1}, {1,1,1,1,0,1,1}, {0,0,0,0,0,0,1}, {1,0,0,1,1,1,0}
};

/* Global Variables */
int alarmMuted = 0;
int isSystemLocked = 0; 
int isMaintenanceMode = 0;
uint32_t lastBlinkTime = 0;
uint32_t lastLogTime = 0; // Timer untuk log
uint8_t ledState = 0;
char logBuffer[64]; // Buffer pesan teks

/* Prototypes */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_USART2_UART_Init(void);
void seg7_display(uint8_t num);
void ResetAllOutputs(void);
void PlayStartupAnimation(void);
void run_smart_cooldown(void);
void log_to_pc(char* message);

int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_USART2_UART_Init(); // Init Serial

  log_to_pc("\r\n=== SYSTEM REBOOT ===\r\n");
  PlayStartupAnimation();
  log_to_pc("[SYS] Monitoring Started...\r\n");

  while (1)
  {
    int buzzerShouldSound = 0;

    /* A. LOGIKA TOMBOL */
    if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_RESET) {
      uint32_t pressTime = HAL_GetTick();
      while (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_RESET) {
        if (HAL_GetTick() - pressTime > 2000) break;
      }

      // Long Press -> Maintenance
      if (HAL_GetTick() - pressTime > 2000) {
        isMaintenanceMode = !isMaintenanceMode;
        isSystemLocked = 0; alarmMuted = 0;
        
        if(isMaintenanceMode) log_to_pc("[MODE] Masuk MAINTENANCE Mode\r\n");
        else log_to_pc("[MODE] Kembali ke NORMAL Mode\r\n");

        ResetAllOutputs();
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET); HAL_Delay(200);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);
        while (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_RESET) {}
      } 
      // Short Press -> Reset
      else if (HAL_GetTick() - pressTime > 50) {
        if (isMaintenanceMode == 0) {
          if (isSystemLocked == 1) {
             HAL_ADC_Start(&hadc1);
             HAL_ADC_PollForConversion(&hadc1, 10);
             int tempDigit = (HAL_ADC_GetValue(&hadc1) * 10) / 4096;
             
             if (tempDigit < 7) {
               log_to_pc("[BTN] Manual Reset Triggered\r\n");
               run_smart_cooldown(); 
               isSystemLocked = 0; alarmMuted = 0;
             } else {
               log_to_pc("[BTN] Reset Gagal: Suhu Masih Tinggi!\r\n");
             }
          } else {
             alarmMuted = !alarmMuted;
             if(alarmMuted) log_to_pc("[BTN] Alarm Muted\r\n");
             else log_to_pc("[BTN] Alarm Unmuted\r\n");
          }
        }
      }
    }

    /* B. LOGIKA OPERASI */
    if (isMaintenanceMode == 1) 
    {
      ResetAllOutputs();
      seg7_display(10); 
      if ((HAL_GetTick() / 500) % 2 == 0) HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET);
      else HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);
    }
    else 
    {
      HAL_ADC_Start(&hadc1);
      HAL_ADC_PollForConversion(&hadc1, 10);
      uint32_t adc = HAL_ADC_GetValue(&hadc1);
      HAL_ADC_Stop(&hadc1);

      uint8_t digit = (adc * 10) / 4096;
      if (digit > 9) digit = 9;

      if (digit >= 9) {
          if(isSystemLocked == 0) log_to_pc("!! CRITICAL: SYSTEM LOCKED !!\r\n");
          isSystemLocked = 1;
      }

      /* TELEMETRI (Kirim data tiap 1 detik) */
      if (HAL_GetTick() - lastLogTime > 1000) {
          // Format pesan: "SUHU: 5 (ADC: 2048) - OK"
          sprintf(logBuffer, "SUHU: %d (ADC: %lu) - %s\r\n", 
                  digit, adc, (isSystemLocked ? "LOCKED" : (digit>=7 ? "BAHAYA" : "AMAN")));
          log_to_pc(logBuffer);
          lastLogTime = HAL_GetTick();
      }

      /* Display & Output Logic (Sama seperti sebelumnya) */
      if (isSystemLocked == 1) {
        if ((HAL_GetTick() / 500) % 2 == 0) {
            ResetAllOutputs();
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6, GPIO_PIN_SET);
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET); 
        } else {
            for(int i=0; i<7; i++) HAL_GPIO_WritePin(segPorts[i], segPins[i], GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET);
        }
      } else {
        seg7_display(digit);
      }

      if (isSystemLocked == 0 || (isSystemLocked == 1 && (HAL_GetTick() / 500) % 2 != 0)) {
          ResetAllOutputs(); 
      }
      
      if (digit >= 7 || isSystemLocked == 1) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET); 
        int blinkSpeed = 500;
        if (digit == 8) blinkSpeed = 200;
        if (digit == 9 || isSystemLocked == 1) blinkSpeed = 60;

        if (HAL_GetTick() - lastBlinkTime > blinkSpeed) {
          ledState = !ledState;
          lastBlinkTime = HAL_GetTick();
        }
        if (ledState) {
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET); 
            if (alarmMuted == 0) buzzerShouldSound = 1;
        }
      } 
      else if (digit >= 4) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET); alarmMuted = 0; 
      }
      else {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET); alarmMuted = 0;
      }
    }

    for (int i = 0; i < 50; i++) {
        if (buzzerShouldSound == 1) HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_2); 
        else HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2, GPIO_PIN_RESET);
        HAL_Delay(1); 
    }
  }
}

/* --- FUNGSI TELEMETRI --- */
void log_to_pc(char* message) {
    HAL_UART_Transmit(&huart2, (uint8_t*)message, strlen(message), 100);
}

void run_smart_cooldown(void) {
    log_to_pc("[SYS] Memulai Pendinginan...\r\n");
    ResetAllOutputs();
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET); 
    for(int i = 0; i < 6; i++) { 
        seg7_display(11); HAL_Delay(250);
        for(int k=0; k<7; k++) HAL_GPIO_WritePin(segPorts[k], segPins[k], GPIO_PIN_RESET);
        HAL_Delay(250);
    }
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET);
    log_to_pc("[SYS] Pendinginan Selesai.\r\n");
}

void seg7_display(uint8_t num) {
    if(num > 11) num = 0;
    int i;
    for(i=0;i<7;i++) HAL_GPIO_WritePin(segPorts[i], segPins[i], seg7_digit[num][i] ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void ResetAllOutputs(void) {
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_4, GPIO_PIN_RESET);
}

void PlayStartupAnimation(void) {
  ResetAllOutputs();
  int urutan[6] = {0, 1, 2, 3, 4, 5}; 
  for (int loop = 0; loop < 1; loop++) { 
    for (int i = 0; i < 6; i++) {
      for(int k=0; k<7; k++) HAL_GPIO_WritePin(segPorts[k], segPins[k], GPIO_PIN_RESET);
      HAL_GPIO_WritePin(segPorts[urutan[i]], segPins[urutan[i]], GPIO_PIN_SET);
      HAL_Delay(50); 
    }
  }
  for(int k=0; k<7; k++) HAL_GPIO_WritePin(segPorts[k], segPins[k], GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET); HAL_Delay(100); HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET); HAL_Delay(100); HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET); HAL_Delay(100); HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);
  for(int k=0; k<100; k++) { HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_2); HAL_Delay(1); }
  ResetAllOutputs();
}

void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  HAL_RCC_OscConfig(&RCC_OscInitStruct);
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0);
}

static void MX_ADC1_Init(void) {
  ADC_ChannelConfTypeDef sConfig = {0};
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV2;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode = ADC_SCAN_SEQ_FIXED;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.LowPowerAutoPowerOff = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.SamplingTimeCommon1 = ADC_SAMPLETIME_1CYCLE_5;
  hadc1.Init.SamplingTimeCommon2 = ADC_SAMPLETIME_1CYCLE_5;
  hadc1.Init.OversamplingMode = DISABLE;
  hadc1.Init.TriggerFrequencyMode = ADC_TRIGGER_FREQ_HIGH;
  HAL_ADC_Init(&hadc1);
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;
  HAL_ADC_ConfigChannel(&hadc1, &sConfig);
}

static void MX_USART2_UART_Init(void) {
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  HAL_UART_Init(&huart2);
}

static void MX_GPIO_Init(void) {
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  HAL_GPIO_WritePin(GPIOA, 0xFFFF, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, 0xFFFF, GPIO_PIN_RESET);

  /* 7-SEGMENT (PA1, PA4-7, PA8, PA9) - Mapping Baru! */
  GPIO_InitStruct.Pin = GPIO_PIN_1|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* SERIAL PINS (PA2, PA3) */
  GPIO_InitStruct.Pin = GPIO_PIN_2|GPIO_PIN_3;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF1_USART2;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* CONTROL (PB0 - PB4) */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_4;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* BUTTON & ADC */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
  
  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

void Error_Handler(void) {
  __disable_irq();
  while (1) {}
}