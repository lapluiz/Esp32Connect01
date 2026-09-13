#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

/* ==============================================================================
 * CONFIGURAÇÕES DE HARDWARE E PINOS
 * ==============================================================================
 */
#define I2C_MASTER_SCL_IO 6
#define I2C_MASTER_SDA_IO 5
#define I2C_MASTER_NUM 0
#define I2C_MASTER_FREQ_HZ 400000
#define I2C_MASTER_TIMEOUT_MS 1000

/* ==============================================================================
 * ENDEREÇOS E CONSTANTES DO DISPLAY SSD1306
 * ==============================================================================
 */
#define OLED_I2C_ADDRESS 0x3C
#define OLED_CMD_STREAM 0x00
#define OLED_DATA_STREAM 0x40

#define LCD_WIDTH 72
#define LCD_HEIGHT 40
#define LCD_PAGES 5

// Frame buffer local em RAM (72 colunas x 5 páginas)
static uint8_t fb[LCD_PAGES][LCD_WIDTH];
static const char *TAG = "OLED_DEBUG";

// Mini-fonte 5x7 simplificada para as letras E, S, P, 3, 2
static const uint8_t font_5x7[][5] = {
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x22, 0x41, 0x49, 0x49, 0x36}, // 3
    {0x62, 0x51, 0x49, 0x49, 0x46}  // 2
};

/* ==============================================================================
 * FUNÇÕES DE COMUNICAÇÃO I2C E HARDWARE
 * ==============================================================================
 */
esp_err_t oled_send_cmd(uint8_t cmd) {
  uint8_t data[2] = {OLED_CMD_STREAM, cmd};
  return i2c_master_write_to_device(I2C_MASTER_NUM, OLED_I2C_ADDRESS, data, 2,
                                    I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

void oled_init() {
  oled_send_cmd(0xAE); // Display OFF
  oled_send_cmd(0x20); // Endereçamento horizontal
  oled_send_cmd(0x00);

  oled_send_cmd(0xD3); // Offset vertical de hardware (Y=12)
  oled_send_cmd(12);

  oled_send_cmd(0x40); // Start line = 0
  oled_send_cmd(0xA1); // Inverte eixo X
  oled_send_cmd(0xC8); // Inverte eixo Y

  oled_send_cmd(0xA8); // Multiplex ratio para 40 linhas reais (0x27 = 39)
  oled_send_cmd(0x27);

  oled_send_cmd(0x8D); // Charge pump ligado
  oled_send_cmd(0x14);
  oled_send_cmd(0xAF); // Display ON
}

/* ==============================================================================
 * FUNÇÕES DE MANIPULAÇÃO DO FRAME BUFFER
 * ==============================================================================
 */
void fb_clear() {
  for (int p = 0; p < LCD_PAGES; p++) {
    for (int x = 0; x < LCD_WIDTH; x++) {
      fb[p][x] = 0x00;
    }
  }
}

void fb_draw_pixel(int x, int y, uint8_t val) {
  if (x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT)
    return;
  int page = y / 8;
  int bit = y % 8;
  if (val) {
    fb[page][x] |= (1 << bit);
  } else {
    fb[page][x] &= ~(1 << bit);
  }
}

void fb_draw_char(int x, int y, char c) {
  int index = -1;
  if (c == 'E')
    index = 0;
  else if (c == 'S')
    index = 1;
  else if (c == 'P')
    index = 2;
  else if (c == '3')
    index = 3;
  else if (c == '2')
    index = 4;

  if (index == -1)
    return;

  for (int col = 0; col < 5; col++) {
    uint8_t line = font_5x7[index][col];
    for (int row = 0; row < 7; row++) {
      if (line & (1 << row)) {
        fb_draw_pixel(x + col, y + row, 1);
      }
    }
  }
}

void fb_draw_string(int x, int y, const char *str) {
  while (*str) {
    fb_draw_char(x, y, *str);
    x += 6; // Largura do caractere (5px) + 1px de espaçamento
    str++;
  }
}

void oled_flush() {
  for (uint8_t page = 0; page < LCD_PAGES; page++) {
    oled_send_cmd(0xB0 + page);
    int x_offset = 30; // Offset horizontal para a janela de 72px
    oled_send_cmd(0x00 | (x_offset & 0x0F));
    oled_send_cmd(0x10 | ((x_offset >> 4) & 0x0F));

    uint8_t data[LCD_WIDTH + 1];
    data[0] = OLED_DATA_STREAM;
    for (int x = 0; x < LCD_WIDTH; x++) {
      data[1 + x] = fb[page][x];
    }

    i2c_master_write_to_device(I2C_MASTER_NUM, OLED_I2C_ADDRESS, data,
                               LCD_WIDTH + 1,
                               I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
  }
}

/* ==============================================================================
 * TASK DO DISPLAY (FREERTOS)
 * ==============================================================================
 */
void display_task(void *pvParameter) {
  i2c_config_t conf = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = I2C_MASTER_SDA_IO,
      .scl_io_num = I2C_MASTER_SCL_IO,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master.clk_speed = I2C_MASTER_FREQ_HZ,
  };

  i2c_param_config(I2C_MASTER_NUM, &conf);
  i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);

  oled_init();

  // Limpa o buffer de memória
  fb_clear();

  // Passa a coordenada X e Y diretamente nos parâmetros da função
  fb_draw_string(20, 30, "ESP32");

  // Envia o frame buffer renderizado para o display físico
  oled_flush();

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

/* ==============================================================================
 * FUNÇÃO PRINCIPAL
 * ==============================================================================
 */
void app_main() {
  xTaskCreate(display_task, "display_task", 4096, NULL, 5, NULL);
}