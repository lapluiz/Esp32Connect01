#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include <stddef.h> // Resolve o erro do NULL
#include <stdio.h>

#define LED_STRIP_GPIO_PIN 7
#define LED_STRIP_LED_COUNT 6
static const char *TAG = "LED_TASK";
static led_strip_handle_t led_strip = NULL;

void configure_led_strip(void) {
  led_strip_config_t strip_config = {
      .strip_gpio_num = LED_STRIP_GPIO_PIN,
      .max_leds = LED_STRIP_LED_COUNT,
      .led_pixel_format =
          LED_PIXEL_FORMAT_GRB, // Nome corrigido para a biblioteca atual
      .led_model = LED_MODEL_WS2812,
      .flags.invert_out = false,
  };

  led_strip_rmt_config_t rmt_config = {
      .resolution_hz = 10 * 1000 * 1000, // 10 MHz
      .flags.with_dma = false,
  };

  ESP_ERROR_CHECK(
      led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
  ESP_ERROR_CHECK(led_strip_clear(led_strip));
  ESP_LOGI(TAG, "Fita configurada com sucesso!");
}

void led_animation_task(void *pvParameter) {
  configure_led_strip();

  while (1) {
    for (int i = 0; i < LED_STRIP_LED_COUNT; i++) {
      led_strip_clear(led_strip);
      led_strip_set_pixel(led_strip, i, 40, 0, 0); // Vermelho
      led_strip_refresh(led_strip);
      vTaskDelay(pdMS_TO_TICKS(50));
    }

    for (int i = LED_STRIP_LED_COUNT - 1; i >= 0; i--) {
      led_strip_clear(led_strip);
      led_strip_set_pixel(led_strip, i, 0, 0, 40); // Azul
      led_strip_refresh(led_strip);
      vTaskDelay(pdMS_TO_TICKS(50));
    }
  }
}

void app_main(void) {
  ESP_LOGI(TAG, "Iniciando FreeRTOS...");
  xTaskCreate(led_animation_task, "led_animation_task", 4096, NULL, 5, NULL);
}