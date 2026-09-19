#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "mdns.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "led_strip.h"

// --- CONFIGURAÇÕES DE REDE ---
// TODO: O usuário deve alterar estes valores
#define WIFI_SSID      "SEU_WIFI_SSID"
#define WIFI_PASS      "SUA_WIFI_SENHA"

// --- CONFIGURAÇÕES DE SERVIDOR E GPIO ---
// TODO: O usuário deve alterar o IP para o do seu PC na rede local
#define SERVER_URL     "http://192.168.1.15:3000/api/esp32/data"
#define BUTTON_GPIO    9
#define LED_SIM_1_GPIO 4
#define LED_SIM_2_GPIO 5

#define LED_STRIP_GPIO_PIN 7
#define LED_STRIP_LED_COUNT 5

static const char *TAG = "ESP32_MAIN";
static led_strip_handle_t led_strip = NULL;

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static int s_retry_num = 0;

// Configuração da fita LED
void configure_led_strip(void) {
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO_PIN,
        .max_leds = LED_STRIP_LED_COUNT,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10 MHz
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    ESP_ERROR_CHECK(led_strip_clear(led_strip));
}

// Manipulador de eventos Wi-Fi
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 5) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Tentando reconectar...");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// Inicializa Wi-Fi
void wifi_init_sta(void) {
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Conectado ao SSID:%s", WIFI_SSID);
        // Atualiza LED de status para verde
        for(int i=0; i<LED_STRIP_LED_COUNT; i++) {
            led_strip_set_pixel(led_strip, i, 0, 40, 0); // Verde
        }
        led_strip_refresh(led_strip);
    } else {
        ESP_LOGI(TAG, "Falha ao conectar");
        for(int i=0; i<LED_STRIP_LED_COUNT; i++) {
            led_strip_set_pixel(led_strip, i, 40, 0, 0); // Vermelho
        }
        led_strip_refresh(led_strip);
    }
}

// Inicializa mDNS
void initialise_mdns(void) {
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("mrinventtor"));
    ESP_ERROR_CHECK(mdns_instance_name_set("ESP32 Web Server"));
    ESP_LOGI(TAG, "mDNS configurado. Acesse http://mrinventtor.local");
}

// Handlers HTTP
esp_err_t get_handler(httpd_req_t *req) {
    const char* html_page = 
        "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Mr. Inventtor</title>"
        "<style>body{font-family:Arial;text-align:center;margin-top:50px;background:#222;color:#fff;}"
        "button{padding:15px 30px;font-size:20px;margin:10px;cursor:pointer;border:none;border-radius:5px;}"
        ".btn1{background:#007bff;color:#fff;}.btn2{background:#ffc107;color:#000;}</style>"
        "</head><body><h1>Simulador ESP32</h1>"
        "<button class='btn1' onclick=\"fetch('/simulacao1').then(r=>r.text()).then(a=>alert(a))\">Simulação 1</button>"
        "<button class='btn2' onclick=\"fetch('/simulacao2').then(r=>r.text()).then(a=>alert(a))\">Simulação 2</button>"
        "</body></html>";
    httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t simulacao1_handler(httpd_req_t *req) {
    gpio_set_level(LED_SIM_1_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(1000));
    gpio_set_level(LED_SIM_1_GPIO, 0);
    const char* resp = "Simulacao 1 ativada!";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t simulacao2_handler(httpd_req_t *req) {
    gpio_set_level(LED_SIM_2_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(1000));
    gpio_set_level(LED_SIM_2_GPIO, 0);
    const char* resp = "Simulacao 2 ativada!";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Inicia Servidor
httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_get = { .uri = "/", .method = HTTP_GET, .handler = get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_get);
        httpd_uri_t uri_sim1 = { .uri = "/simulacao1", .method = HTTP_GET, .handler = simulacao1_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_sim1);
        httpd_uri_t uri_sim2 = { .uri = "/simulacao2", .method = HTTP_GET, .handler = simulacao2_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_sim2);
        return server;
    }
    return NULL;
}

// HTTP POST para o Node.js
void send_post_request(void) {
    esp_http_client_config_t config = {
        .url = SERVER_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Falha ao iniciar cliente HTTP");
        return;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "temperatura", 25.5);
    cJSON_AddNumberToObject(root, "umidade", 60);
    cJSON_AddStringToObject(root, "status", "Botão Pressionado");
    
    char *post_data = cJSON_PrintUnformatted(root);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST Status = %d, length = %d",
                 esp_http_client_get_status_code(client),
                 (int)esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "Falha no POST: %s", esp_err_to_name(err));
    }

    cJSON_Delete(root);
    free(post_data);
    esp_http_client_cleanup(client);
}

// Task de monitoramento do botão
void button_task(void *pvParameter) {
    // Configura botão no pino GPIO com Pull-up interno
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .pull_down_en = 0,
        .pull_up_en = 1,
    };
    gpio_config(&io_conf);

    int last_state = 1;
    while(1) {
        int current_state = gpio_get_level(BUTTON_GPIO);
        if(current_state == 0 && last_state == 1) {
            // LOW signal detectado (botão pressionado)
            ESP_LOGI(TAG, "Botao pressionado! Enviando POST...");
            send_post_request();
            vTaskDelay(pdMS_TO_TICKS(500)); // Debounce
        }
        last_state = current_state;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void app_main(void) {
    // Inicialização da NVS para Wi-Fi
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Inicializa LEDs de Simulação
    gpio_set_direction(LED_SIM_1_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_SIM_2_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_SIM_1_GPIO, 0);
    gpio_set_level(LED_SIM_2_GPIO, 0);

    // Configura Fita LED (usada para status Wi-Fi)
    configure_led_strip();
    for(int i=0; i<LED_STRIP_LED_COUNT; i++) {
        led_strip_set_pixel(led_strip, i, 0, 0, 40); // Azul (Conectando)
    }
    led_strip_refresh(led_strip);

    // Conecta Wi-Fi
    wifi_init_sta();

    // Inicia serviços de rede
    initialise_mdns();
    start_webserver();

    // Inicia monitoramento do botão
    xTaskCreate(button_task, "button_task", 4096, NULL, 5, NULL);
}