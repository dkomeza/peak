#include "esc/kt.h"

#include <driver/gpio.h>
#include <driver/uart.h>
#include <string.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "portmacro.h"

#define RX_PACKET_SIZE 12
#define TX_PACKET_SIZE 13
#define KT_POWER_GPIO GPIO_NUM_9

static SemaphoreHandle_t esc_kt_data_mutex;
static SemaphoreHandle_t peak_kt_data_mutex;
static TaskHandle_t esc_kt_send_task_handle;
static volatile bool esc_kt_send_task_running;

static const uart_port_t KT_PORT = UART_NUM_1;

static esc_kt_data_t esc_kt_data = {0};
static peak_kt_data_t peak_kt_data = {0};

static uint8_t kt_clamp_assist_level(uint8_t assist_level,
                                     esc_ride_mode_t ride_mode) {
  uint8_t max_assist =
      ride_mode == ESC_RIDE_MODE_MOUNTAIN ? (uint8_t)5 : (uint8_t)3;

  if (assist_level < 1) {
    return 1;
  }

  if (assist_level > max_assist) {
    return max_assist;
  }

  return assist_level;
}

bool esc_is_valid_packet(uint8_t *packet) {
  if (packet[0] != 0x41 || packet[2] != 0 || packet[10] != 0 || packet[11] != 0)
    return false;

  uint8_t checksum = 0;

  for (int i = 0; i < RX_PACKET_SIZE; i++) {
    if (i == 6)
      continue; // Skip checksum byte

    checksum ^= packet[i];
  }

  return checksum == packet[6];
}

void esc_parse_packet(uint8_t *packet) {
  xSemaphoreTake(esc_kt_data_mutex, portMAX_DELAY);

  esc_kt_data.battery_level = packet[1];

  uint16_t wheel_period_ms = (packet[3] << 8) | packet[4];
  esc_kt_data.rpm = wheel_period_ms > 0 ? (60000.0 / wheel_period_ms) : 0;
  esc_kt_data.speed = wheel_period_ms > 0
                          ? ((float)peak_kt_data.wheel_size.circumference_mm /
                             wheel_period_ms) *
                                3.6
                          : 0;

  esc_kt_data.power = packet[8] * 13;
  esc_kt_data.motor_temp = (int8_t)packet[9] + 15;

  esc_kt_data.throttle = packet[7] & 0x01;
  esc_kt_data.cruise = packet[7] & 0x08;
  esc_kt_data.assist = packet[7] & 0x10;
  esc_kt_data.brake = packet[7] & 0x20;

  xSemaphoreGive(esc_kt_data_mutex);
}

void peak_create_packet(uint8_t *packet) {
  xSemaphoreTake(peak_kt_data_mutex, portMAX_DELAY);

  uint8_t assist_level =
      kt_clamp_assist_level(peak_kt_data.assist_level, peak_kt_data.ride_mode);

  uint8_t B2 = ((peak_kt_data.max_speed - 10) & 0x1F) << 3 |
               peak_kt_data.wheel_size.val >> 2;

  uint8_t B4 = (peak_kt_data.P2 & 0x07);
  B4 |= (peak_kt_data.P3 & 0x01) << 3;
  B4 |= (peak_kt_data.P4 & 0x01) << 4;
  B4 |= ((peak_kt_data.max_speed - 10) & 0x20);
  B4 |= (peak_kt_data.wheel_size.val & 0x03) << 6;

  packet[0] = peak_kt_data.P5;
  packet[1] = assist_level | (peak_kt_data.light << 7);
  packet[2] = B2;
  packet[3] = peak_kt_data.P1;
  packet[4] = B4;
  packet[5] = 0; // Checksum placeholder
  packet[6] = (peak_kt_data.C1 & 0x07) << 3 | (peak_kt_data.C2 & 0x07);
  packet[7] = 0x80 | (peak_kt_data.C14 & 0x03) << 5 | (peak_kt_data.C5 & 0x0F);
  packet[8] = (peak_kt_data.C4 & 0x07) << 5 | (peak_kt_data.C12 & 0x07);
  packet[9] = 0x14;
  packet[10] = (peak_kt_data.C13 & 0x07) << 2 | 0x01;
  packet[11] = 0x32;
  packet[12] = 0x0E;

  for (int i = 0; i < TX_PACKET_SIZE; i++) {
    if (i == 5)
      continue; // Skip checksum byte

    packet[5] ^= packet[i];
  }

  packet[5] ^= 0x03;

  xSemaphoreGive(peak_kt_data_mutex);
}

void esc_kt_receive_task(void *arg) {
  (void)arg;

  uint8_t rx_buffer[RX_PACKET_SIZE] = {0};
  uint8_t byte;

  while (1) {
    int rx_bytes = uart_read_bytes(KT_PORT, &byte, 1, portMAX_DELAY);

    if (rx_bytes > 0) {
      memmove(rx_buffer, rx_buffer + 1, RX_PACKET_SIZE - 1);
      rx_buffer[RX_PACKET_SIZE - 1] = byte;

      if (esc_is_valid_packet(rx_buffer)) {
        esc_parse_packet(rx_buffer);
      }
    }
  }
}

void esc_kt_send_task(void *arg) {
  (void)arg;

  uint8_t tx_buffer[TX_PACKET_SIZE] = {0};

  while (esc_kt_send_task_running) {
    peak_create_packet(tx_buffer);
    uart_write_bytes(KT_PORT, tx_buffer, TX_PACKET_SIZE);
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  esc_kt_send_task_handle = NULL;
  vTaskDelete(NULL);
}

void esc_kt_setup_uart(void) {
  const int uart_buffer_size = 128;

  QueueHandle_t uart_queue;
  ESP_ERROR_CHECK(uart_driver_install(KT_PORT, uart_buffer_size,
                                      uart_buffer_size, 10, &uart_queue, 0));

  uart_config_t uart_config = {
      .baud_rate = 9600,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
  };

  ESP_ERROR_CHECK(uart_param_config(KT_PORT, &uart_config));

  ESP_ERROR_CHECK(
      uart_set_pin(KT_PORT, 22, 21, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

void esc_kt_setup_power_gpio(void) {
  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << KT_POWER_GPIO),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = false,
      .pull_down_en = false,
      .intr_type = GPIO_INTR_DISABLE,
  };

  ESP_ERROR_CHECK(gpio_config(&io_conf));
  ESP_ERROR_CHECK(gpio_set_level(KT_POWER_GPIO, 1));
}

static esp_err_t esc_kt_start_send_task(void) {
  if (esc_kt_send_task_handle != NULL) {
    return ESP_OK;
  }

  esc_kt_send_task_running = true;

  BaseType_t ret = xTaskCreate(esc_kt_send_task, "esc_kt_send_task", 4096, NULL,
                               5, &esc_kt_send_task_handle);
  if (ret != pdPASS) {
    esc_kt_send_task_running = false;
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}

static esp_err_t esc_kt_stop_send_task(void) {
  if (esc_kt_send_task_handle == NULL) {
    return ESP_OK;
  }

  esc_kt_send_task_running = false;
  while (esc_kt_send_task_handle != NULL) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  return ESP_OK;
}

static esp_err_t esc_kt_set_power(void *ctx, bool enabled) {
  (void)ctx;

  esp_err_t ret = gpio_set_level(KT_POWER_GPIO, enabled ? 1 : 0);
  if (ret != ESP_OK) {
    return ret;
  }

  return enabled ? esc_kt_start_send_task() : esc_kt_stop_send_task();
}

static esp_err_t esc_kt_set_ride_mode(void *ctx, esc_ride_mode_t mode) {
  (void)ctx;

  xSemaphoreTake(peak_kt_data_mutex, portMAX_DELAY);
  peak_kt_data.ride_mode = mode;
  xSemaphoreGive(peak_kt_data_mutex);

  return ESP_OK;
}

static esp_err_t esc_kt_set_gear(void *ctx, uint8_t gear) {
  (void)ctx;

  xSemaphoreTake(peak_kt_data_mutex, portMAX_DELAY);
  peak_kt_data.assist_level = gear;
  xSemaphoreGive(peak_kt_data_mutex);

  return ESP_OK;
}

static const esc_controller_ops_t s_kt_controller_ops = {
    .name = "KT",
    .set_power = esc_kt_set_power,
    .set_ride_mode = esc_kt_set_ride_mode,
    .set_gear = esc_kt_set_gear,
    .set_support_mode = NULL,
};

void esc_kt_init(void) {
  esc_kt_data_mutex = xSemaphoreCreateMutex();
  peak_kt_data_mutex = xSemaphoreCreateMutex();

  esc_kt_setup_uart();
  esc_kt_setup_power_gpio();

  xTaskCreate(esc_kt_receive_task, "esc_kt_receive_task", 4096, NULL, 5, NULL);
  ESP_ERROR_CHECK(esc_kt_start_send_task());
}

esp_err_t esc_kt_controller_init(esc_controller_t *out) {
  if (out == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  out->ops = &s_kt_controller_ops;
  out->ctx = NULL;
  return ESP_OK;
}

void esc_kt_get_data(esc_kt_data_t *data) {
  xSemaphoreTake(esc_kt_data_mutex, portMAX_DELAY);
  *data = esc_kt_data;
  xSemaphoreGive(esc_kt_data_mutex);
}
