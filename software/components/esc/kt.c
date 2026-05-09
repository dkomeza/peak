#include "esc/kt.h"

#include <driver/uart.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "esc_kt";

SemaphoreHandle_t esc_kt_mutex;

#define RX_PACKET_SIZE 12
#define TX_PACKET_SIZE 13

const uart_port_t KT_PORT = UART_NUM_1;

void esc_kt_receive_task(void *arg) {
  (void)arg;
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void esc_kt_send_task(void *arg) {
  (void)arg;

  uint8_t tx_buffer[TX_PACKET_SIZE] = {0};

  while (1) {
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
  esc_kt_mutex = xSemaphoreCreateMutex();

  xTaskCreate(esc_kt_receive_task, "esc_kt_receive_task", 4096, NULL, 5, NULL);
  xTaskCreate(esc_kt_send_task, "esc_kt_send_task", 4096, NULL, 5, NULL);
}
