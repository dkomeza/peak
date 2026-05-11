#include "esc/kt.h"

#include <driver/uart.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "portmacro.h"

#define RX_PACKET_SIZE 12
#define TX_PACKET_SIZE 13

static const char *TAG = "esc_kt";

static SemaphoreHandle_t esc_kt_data_mutex;
static SemaphoreHandle_t peak_kt_data_mutex;

static const uart_port_t KT_PORT = UART_NUM_1;

static esc_kt_data_t esc_kt_data = {0};
static peak_kt_data_t peak_kt_data = {0};

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

  uint8_t B2 = ((peak_kt_data.max_speed - 10) & 0x1F) << 3 |
               peak_kt_data.wheel_size.val >> 2;

  uint8_t B4 = (peak_kt_data.P2 & 0x07);
  B4 |= (peak_kt_data.P3 & 0x01) << 3;
  B4 |= (peak_kt_data.P4 & 0x01) << 4;
  B4 |= ((peak_kt_data.max_speed - 10) & 0x20);
  B4 |= (peak_kt_data.wheel_size.val & 0x03) << 6;

  packet[0] = peak_kt_data.P5;
  packet[1] = peak_kt_data.assist_level | peak_kt_data.light << 7;
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

  while (1) {
    peak_create_packet(tx_buffer);
    uart_write_bytes(KT_PORT, tx_buffer, TX_PACKET_SIZE);
    vTaskDelay(pdMS_TO_TICKS(100));
  }
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

void esc_kt_init(void) {
  esc_kt_data_mutex = xSemaphoreCreateMutex();
  peak_kt_data_mutex = xSemaphoreCreateMutex();

  xTaskCreate(esc_kt_receive_task, "esc_kt_receive_task", 4096, NULL, 5, NULL);
  xTaskCreate(esc_kt_send_task, "esc_kt_send_task", 4096, NULL, 5, NULL);
}

void esc_kt_get_data(esc_kt_data_t *data) {
  xSemaphoreTake(esc_kt_data_mutex, portMAX_DELAY);
  *data = esc_kt_data;
  xSemaphoreGive(esc_kt_data_mutex);
}
