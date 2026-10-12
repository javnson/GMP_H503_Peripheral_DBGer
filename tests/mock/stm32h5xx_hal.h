#ifndef TEST_STM32H5XX_HAL_H
#define TEST_STM32H5XX_HAL_H
#include <stdint.h>
typedef struct { uint32_t IDR, BSRR, OTYPER, MODER; } GPIO_TypeDef;
typedef struct { uint32_t Pin, Mode, Pull, Speed; } GPIO_InitTypeDef;
extern GPIO_TypeDef mock_gpio_b;
#define GPIOB (&mock_gpio_b)
#define GPIO_PIN_5 (1U << 5)
#define GPIO_PIN_6 (1U << 6)
#define GPIO_PIN_SET 1U
#define GPIO_PIN_RESET 0U
#define GPIO_MODE_OUTPUT_OD 1U
#define GPIO_MODE_IT_RISING_FALLING 2U
#define GPIO_NOPULL 0U
#define GPIO_SPEED_FREQ_LOW 0U
#define GPIO_SPEED_FREQ_VERY_HIGH 3U
#define EXTI5_IRQn 16
#define EXTI6_IRQn 17
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
#define __HAL_GPIO_EXTI_CLEAR_RISING_IT(mask) ((void)(mask))
#define __HAL_GPIO_EXTI_CLEAR_FALLING_IT(mask) ((void)(mask))
static inline void HAL_NVIC_DisableIRQ(int irq) { (void)irq; }
static inline void HAL_NVIC_EnableIRQ(int irq) { (void)irq; }
static inline void HAL_NVIC_SetPriority(int irq, unsigned priority, unsigned sub) {
  (void)irq; (void)priority; (void)sub;
}
static inline void HAL_GPIO_Init(GPIO_TypeDef *gpio, const GPIO_InitTypeDef *config) {
  (void)gpio; (void)config;
}
static inline void HAL_GPIO_WritePin(GPIO_TypeDef *gpio, uint32_t pin, uint32_t state) {
  gpio->BSRR = state ? pin : (pin << 16U);
}
void HAL_GPIO_EXTI_IRQHandler(uint16_t pin);
void HAL_GPIO_EXTI_Rising_Callback(uint16_t pin);
void HAL_GPIO_EXTI_Falling_Callback(uint16_t pin);
uint32_t HAL_GetTick(void);
#endif
