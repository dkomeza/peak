#include "esc/kt.h"

#include <driver/gpio.h>
#include <driver/uart.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define KT_RX_PACKET_SIZE 12
#define KT_TX_PACKET_SIZE 13
#define KT_POWER_GPIO GPIO_NUM_9
#define KT_NORMAL_SPEED_LIMIT_KPH 25

static const uart_port_t s_port = UART_NUM_1;
static SemaphoreHandle_t s_mutex;
static bool s_initialized;
static esc_kt_update_cb_t s_update_callback;
static void *s_update_context;

typedef struct {
  esc_kt_settings_t settings;
  esc_kt_snapshot_t snapshot;
} kt_state_t;

static kt_state_t s_state;

static uint8_t kt_max_gear(esc_ride_mode_t mode) {
  return mode == ESC_RIDE_MODE_MOUNTAIN ? 5 : 3;
}

static bool kt_valid_ride_mode(esc_ride_mode_t mode) {
  return mode == ESC_RIDE_MODE_NORMAL || mode == ESC_RIDE_MODE_MOUNTAIN;
}

static void kt_notify(const esc_kt_snapshot_t *snapshot) {
  if (s_update_callback != NULL) {
    s_update_callback(snapshot, s_update_context);
  }
}

static uint8_t kt_clamp_gear(uint8_t gear, esc_ride_mode_t mode) {
  if (gear < 1) {
    return 1;
  }
  uint8_t max_gear = kt_max_gear(mode);
  return gear > max_gear ? max_gear : gear;
}

static bool kt_valid_packet(const uint8_t *packet) {
  if (packet[0] != 0x41 || packet[2] != 0 || packet[10] != 0 ||
      packet[11] != 0) {
    return false;
  }

  uint8_t checksum = 0;
  for (size_t i = 0; i < KT_RX_PACKET_SIZE; ++i) {
    if (i != 6) {
      checksum ^= packet[i];
    }
  }
  return checksum == packet[6];
}

static void kt_parse_packet(const uint8_t *packet) {
  uint16_t wheel_period_ms = ((uint16_t)packet[3] << 8) | packet[4];
  esc_kt_snapshot_t snapshot;

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_state.snapshot.battery_percent = packet[1];
  s_state.snapshot.speed_kph =
      wheel_period_ms == 0
          ? 0
          : ((float)s_state.settings.wheel_circumference_mm / wheel_period_ms) *
                3.6f;
  s_state.snapshot.wheel_rpm =
      wheel_period_ms == 0 ? 0 : 60000.0f / wheel_period_ms;
  s_state.snapshot.power_w = (uint16_t)packet[8] * 13;
  s_state.snapshot.motor_temp_c = (int8_t)packet[9] + 15;
  s_state.snapshot.throttle_active = (packet[7] & 0x01u) != 0;
  s_state.snapshot.cruise_active = (packet[7] & 0x08u) != 0;
  s_state.snapshot.assist_active = (packet[7] & 0x10u) != 0;
  s_state.snapshot.brake_active = (packet[7] & 0x20u) != 0;
  s_state.snapshot.has_telemetry = true;
  snapshot = s_state.snapshot;
  xSemaphoreGive(s_mutex);
  kt_notify(&snapshot);
}

static void kt_create_packet(uint8_t *packet) {
  kt_state_t state;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  state = s_state;
  xSemaphoreGive(s_mutex);

  uint8_t speed_limit = state.snapshot.ride_mode == ESC_RIDE_MODE_NORMAL
                            ? KT_NORMAL_SPEED_LIMIT_KPH
                            : state.settings.max_speed_kph;
  uint8_t assist = state.snapshot.walk_active
                       ? 6
                       : kt_clamp_gear(state.snapshot.gear,
                                       state.snapshot.ride_mode);
  uint8_t speed = speed_limit - 10;

  packet[0] = state.settings.p5;
  packet[1] = assist | (state.settings.light << 7);
  packet[2] = ((speed & 0x1f) << 3) | (state.settings.wheel_code >> 2);
  packet[3] = state.settings.p1;
  packet[4] = (state.settings.p2 & 0x07) |
              ((state.settings.p3 & 0x01) << 3) |
              ((state.settings.p4 & 0x01) << 4) | (speed & 0x20) |
              ((state.settings.wheel_code & 0x03) << 6);
  packet[5] = 0;
  packet[6] = ((state.settings.c1 & 0x07) << 3) |
              (state.settings.c2 & 0x07);
  packet[7] = 0x80 | ((state.settings.c14 & 0x03) << 5) |
              (state.settings.c5 & 0x0f);
  packet[8] = ((state.settings.c4 & 0x07) << 5) |
              (state.settings.c12 & 0x07);
  packet[9] = 0x14;
  packet[10] = ((state.settings.c13 & 0x07) << 2) | 0x01;
  packet[11] = 0x32;
  packet[12] = 0x0e;

  for (size_t i = 0; i < KT_TX_PACKET_SIZE; ++i) {
    if (i != 5) {
      packet[5] ^= packet[i];
    }
  }
  packet[5] ^= 0x03;
}

static void kt_receive_task(void *arg) {
  (void)arg;
  uint8_t packet[KT_RX_PACKET_SIZE] = {0};
  uint8_t byte;

  while (true) {
    if (uart_read_bytes(s_port, &byte, 1, portMAX_DELAY) > 0) {
      memmove(packet, packet + 1, sizeof(packet) - 1);
      packet[sizeof(packet) - 1] = byte;
      if (kt_valid_packet(packet)) {
        kt_parse_packet(packet);
      }
    }
  }
}

static void kt_send_task(void *arg) {
  (void)arg;
  uint8_t packet[KT_TX_PACKET_SIZE];

  while (true) {
    kt_create_packet(packet);
    uart_write_bytes(s_port, packet, sizeof(packet));
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

esp_err_t esc_kt_init(void) {
  if (s_initialized) {
    return ESP_OK;
  }

  s_mutex = xSemaphoreCreateMutex();
  if (s_mutex == NULL) {
    return ESP_ERR_NO_MEM;
  }

  s_state.snapshot.gear = 1;
  s_state.snapshot.ride_mode = ESC_RIDE_MODE_NORMAL;
  s_state.settings = (esc_kt_settings_t){
      .wheel_code = 30,
      .wheel_circumference_mm = 2298,
      .max_speed_kph = KT_NORMAL_SPEED_LIMIT_KPH,
  };

  uart_config_t config = {
      .baud_rate = 9600,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
  };
  esp_err_t err = uart_driver_install(s_port, 128, 128, 0, NULL, 0);
  if (err != ESP_OK) {
    return err;
  }
  if ((err = uart_param_config(s_port, &config)) != ESP_OK ||
      (err = uart_set_pin(s_port, 22, 21, UART_PIN_NO_CHANGE,
                          UART_PIN_NO_CHANGE)) != ESP_OK) {
    return err;
  }

  gpio_config_t power = {
      .pin_bit_mask = 1ULL << KT_POWER_GPIO,
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = false,
      .pull_down_en = false,
      .intr_type = GPIO_INTR_DISABLE,
  };
  if ((err = gpio_config(&power)) != ESP_OK ||
      (err = gpio_set_level(KT_POWER_GPIO, 1)) != ESP_OK) {
    return err;
  }
  TaskHandle_t receive_task;
  if (xTaskCreate(kt_receive_task, "esc_kt_rx", 4096, NULL, 5,
                  &receive_task) != pdPASS) {
    return ESP_ERR_NO_MEM;
  }
  if (xTaskCreate(kt_send_task, "esc_kt_tx", 4096, NULL, 5, NULL) != pdPASS) {
    vTaskDelete(receive_task);
    return ESP_ERR_NO_MEM;
  }

  esc_kt_snapshot_t snapshot;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  snapshot = s_state.snapshot;
  s_initialized = true;
  xSemaphoreGive(s_mutex);
  kt_notify(&snapshot);
  return ESP_OK;
}

esp_err_t esc_kt_set_update_callback(esc_kt_update_cb_t callback,
                                      void *context) {
  if (s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }
  s_update_callback = callback;
  s_update_context = context;
  return ESP_OK;
}

esp_err_t esc_kt_set_settings(const esc_kt_settings_t *settings) {
  if (settings == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  if (settings->wheel_circumference_mm == 0 || settings->max_speed_kph < 10 ||
      settings->max_speed_kph > 73) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_state.settings = *settings;
  xSemaphoreGive(s_mutex);
  return ESP_OK;
}

esp_err_t esc_kt_gear_up(void) {
  if (!s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }
  esc_kt_snapshot_t snapshot;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_state.snapshot.gear = kt_clamp_gear(s_state.snapshot.gear + 1,
                                         s_state.snapshot.ride_mode);
  snapshot = s_state.snapshot;
  xSemaphoreGive(s_mutex);
  kt_notify(&snapshot);
  return ESP_OK;
}

esp_err_t esc_kt_gear_down(void) {
  if (!s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }
  esc_kt_snapshot_t snapshot;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_state.snapshot.gear = kt_clamp_gear(s_state.snapshot.gear - 1,
                                         s_state.snapshot.ride_mode);
  snapshot = s_state.snapshot;
  xSemaphoreGive(s_mutex);
  kt_notify(&snapshot);
  return ESP_OK;
}

esp_err_t esc_kt_set_ride_mode(esc_ride_mode_t mode) {
  if (!kt_valid_ride_mode(mode)) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }
  esc_kt_snapshot_t snapshot;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_state.snapshot.ride_mode = mode;
  s_state.snapshot.gear = kt_clamp_gear(s_state.snapshot.gear, mode);
  snapshot = s_state.snapshot;
  xSemaphoreGive(s_mutex);
  kt_notify(&snapshot);
  return ESP_OK;
}

esp_err_t esc_kt_set_walk(bool enabled) {
  if (!s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }
  esc_kt_snapshot_t snapshot;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_state.snapshot.walk_active = enabled;
  snapshot = s_state.snapshot;
  xSemaphoreGive(s_mutex);
  kt_notify(&snapshot);
  return ESP_OK;
}

esp_err_t esc_kt_get_snapshot(esc_kt_snapshot_t *out) {
  if (out == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  *out = s_state.snapshot;
  xSemaphoreGive(s_mutex);
  return ESP_OK;
}
