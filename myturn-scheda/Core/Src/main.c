/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : MyTurn - Tappa 1 (gruppo 4 GOPHIES)
  ******************************************************************************
  * La scheda parla la lingua di MyTurn sulla seriale (USART2, 115200 baud):
  *   - B1 premuto:  OFF -> PENDING  scrive {"event":"REQUEST"}
  *                  PENDING/APPROVED -> OFF  scrive {"event":"RELEASE"}
  *   - dal PC:      {"command":"SET_LED","state":"APPROVED"}  (nessuna risposta)
  *                  {"command":"GET_LED"} -> {"led":"APPROVED"}
  * Niente HAL_Delay nel ciclo: il bottone risponde sempre, anche mentre il LED
  * lampeggia.
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* I tre stati del LED: UNA sola variabile, cambiata sia dal bottone sia dal PC */
typedef enum {
  LED_OFF = 0,     /* LED spento                         */
  LED_PENDING,     /* LED lampeggiante: ho chiesto turno  */
  LED_APPROVED     /* LED fisso: il PC ha approvato       */
} LedState;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define BLINK_MS     250   /* mezzo periodo del lampeggio                */
#define DEBOUNCE_MS  50    /* il bottone deve restare fermo per 50 ms    */
#define RX_MAX       64    /* lunghezza massima di una riga dal PC       */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
static LedState ledState = LED_OFF;      /* lo stato: la "verita'" della scheda */

/* Ricezione dalla seriale (riempite dall'interrupt) */
static uint8_t rxByte;                   /* il byte appena arrivato            */
static char rxBuf[RX_MAX];               /* la riga che si sta componendo      */
static uint8_t rxLen = 0;
static char cmdLine[RX_MAX];             /* l'ultima riga completa             */
static volatile uint8_t cmdReady = 0;    /* 1 = c'e' una riga da elaborare      */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */
static void sendLine(const char *text);
static const char *stateName(LedState s);
static void onButtonPressed(void);
static void handleCommand(const char *line);
static void updateLed(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* Scrive una riga sulla seriale, sempre con l'a capo finale */
static void sendLine(const char *text)
{
  HAL_UART_Transmit(&huart2, (uint8_t *)text, strlen(text), 100);
  HAL_UART_Transmit(&huart2, (uint8_t *)"\r\n", 2, 100);
}

static const char *stateName(LedState s)
{
  switch (s) {
    case LED_PENDING:  return "PENDING";
    case LED_APPROVED: return "APPROVED";
    default:           return "OFF";
  }
}

/* Regole del bottone (APPROVED non lo mette MAI il bottone) */
static void onButtonPressed(void)
{
  if (ledState == LED_OFF) {
    ledState = LED_PENDING;              /* chiedo il turno */
    sendLine("{\"event\":\"REQUEST\"}");
  } else {
    ledState = LED_OFF;                  /* rinuncio / ho finito */
    sendLine("{\"event\":\"RELEASE\"}");
  }
}

/* Comandi dal PC: riconosciamo le parole chiave con strstr */
static void handleCommand(const char *line)
{
  if (strstr(line, "\"SET_LED\"") != NULL) {
    if (strstr(line, "\"APPROVED\"") != NULL)      ledState = LED_APPROVED;
    else if (strstr(line, "\"PENDING\"") != NULL)  ledState = LED_PENDING;
    else if (strstr(line, "\"OFF\"") != NULL)      ledState = LED_OFF;
    /* SET_LED non risponde nulla */
  } else if (strstr(line, "\"GET_LED\"") != NULL) {
    char out[32];
    snprintf(out, sizeof(out), "{\"led\":\"%s\"}", stateName(ledState));
    sendLine(out);
  }
}

/* Il LED segue lo stato. Il lampeggio usa l'orologio, non HAL_Delay */
static void updateLed(void)
{
  static uint32_t lastToggle = 0;

  switch (ledState) {
    case LED_OFF:
      HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
      break;
    case LED_PENDING:
      if (HAL_GetTick() - lastToggle >= BLINK_MS) {
        HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
        lastToggle = HAL_GetTick();
      }
      break;
    case LED_APPROVED:
      HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
      break;
  }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  /* Antirimbalzo: ricordo l'ultima lettura e da quando non cambia */
  GPIO_PinState lastReading = GPIO_PIN_SET;   /* B1 a riposo = SET (alto) */
  GPIO_PinState stableButton = GPIO_PIN_SET;
  uint32_t lastChange = 0;

  /* Avvio la ricezione a interrupt: un byte alla volta */
  HAL_UART_Receive_IT(&huart2, &rxByte, 1);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    uint32_t now = HAL_GetTick();

    /* 1. Bottone con antirimbalzo NON bloccante */
    GPIO_PinState reading = HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin);
    if (reading != lastReading) {        /* e' cambiato: riparte il conteggio */
      lastReading = reading;
      lastChange = now;
    }
    if ((now - lastChange) >= DEBOUNCE_MS && reading != stableButton) {
      stableButton = reading;            /* fermo da 50 ms: e' un cambio vero */
      if (stableButton == GPIO_PIN_RESET) {
        onButtonPressed();               /* una pressione = una riga sola */
      }
    }

    /* 2. Comandi arrivati dal PC */
    if (cmdReady) {
      handleCommand(cmdLine);
      cmdReady = 0;
    }

    /* 3. Il LED segue lo stato */
    updateLed();
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 10;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LD2_Pin */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* Chiamata dall'interrupt ogni volta che arriva UN byte dal PC.
 * Accumula i caratteri; a CR o LF la riga e' completa. */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2) {
    if (rxByte == '\r' || rxByte == '\n') {          /* accettiamo CR e LF */
      if (rxLen > 0 && !cmdReady) {
        rxBuf[rxLen] = '\0';
        memcpy(cmdLine, rxBuf, rxLen + 1);           /* consegno la riga al main */
        cmdReady = 1;
      }
      rxLen = 0;                                     /* si ricomincia */
    } else if (rxLen < RX_MAX - 1) {
      rxBuf[rxLen++] = (char)rxByte;
    } else {
      rxLen = 0;                                     /* riga troppo lunga: scarto */
    }
    HAL_UART_Receive_IT(&huart2, &rxByte, 1);        /* aspetto il prossimo byte */
  }
}

/* Se la ricezione va in errore (es. overrun) la HAL si ferma: la riavviamo */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2) {
    rxLen = 0;
    HAL_UART_Receive_IT(&huart2, &rxByte, 1);
  }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
