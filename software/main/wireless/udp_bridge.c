#include "udp_bridge.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "transport_iface.h"

static const char *TAG = "udp_bridge";

#define UDP_RX_BUFFER_SIZE 1024

static struct {
  int sock;
  struct sockaddr_storage peer_addr;
  socklen_t peer_addr_len;
  bool peer_valid;
} s_state = {.sock = -1, .peer_valid = false};

static SemaphoreHandle_t s_mutex;
static udp_receive_cb_t s_receive_callback;
static void *s_receive_user_data;
static uint16_t s_port;
static TaskHandle_t s_task_handle = NULL;
static volatile bool s_running = false;

static void update_peer(struct sockaddr_storage *addr, socklen_t addr_len) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_state.peer_addr = *addr;
  s_state.peer_addr_len = addr_len;
  s_state.peer_valid = true;
  xSemaphoreGive(s_mutex);
}

static void process_incoming_traffic(int sock, uint8_t *rx_buffer) {
  while (s_running) {
    struct sockaddr_storage source_addr;
    socklen_t source_addr_len = sizeof(source_addr);

    int len = recvfrom(sock, rx_buffer, UDP_RX_BUFFER_SIZE, 0,
                       (struct sockaddr *)&source_addr, &source_addr_len);

    if (len < 0) {
      // If we are stopping, this error is expected. Just break and exit.
      if (!s_running)
        break;

      ESP_LOGW(TAG, "Receive failed: errno %d", errno);
      break; // Unexpected error, break to recreate socket
    }

    if (len > 0) {
      update_peer(&source_addr, source_addr_len);

      if (s_receive_callback != NULL) {
        s_receive_callback(rx_buffer, (size_t)len, s_receive_user_data);
      }
    }
  }
}

static void udp_server_task(void *arg) {
  uint8_t rx_buffer[UDP_RX_BUFFER_SIZE];

  while (s_running) {
    struct sockaddr_in listen_addr = {
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_family = AF_INET,
        .sin_port = htons(s_port),
    };

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
      ESP_LOGW(TAG, "Socket creation failed: errno %d", errno);
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    if (bind(sock, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
      ESP_LOGW(TAG, "Socket bind failed: errno %d", errno);
      close(sock);
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    ESP_LOGI(TAG, "UDP listening on port %d", s_port);

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.sock = sock;
    xSemaphoreGive(s_mutex);

    // Blocks here until data arrives OR the socket is closed by
    // udp_bridge_stop()
    process_incoming_traffic(sock, rx_buffer);

    // --- Cleanup Phase ---
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    // Only close it if stop() hasn't already closed it
    if (s_state.sock >= 0) {
      shutdown(s_state.sock, 0);
      close(s_state.sock);
      s_state.sock = -1;
    }
    s_state.peer_valid = false;
    xSemaphoreGive(s_mutex);

    if (s_running) {
      // We broke out due to a random socket error, wait before restarting
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }

  // Task is officially dead. Clear the handle so udp_bridge_stop() can unblock.
  s_task_handle = NULL;
  vTaskDelete(NULL);
}

esp_err_t udp_bridge_start(uint16_t port, udp_receive_cb_t callback,
                           void *user_data) {
  if (s_running)
    return ESP_OK; // Already running

  if (s_mutex == NULL) {
    s_mutex = xSemaphoreCreateMutex();
  }

  s_port = port;
  s_receive_callback = callback;
  s_receive_user_data = user_data;
  s_running = true;

  if (xTaskCreate(udp_server_task, "udp_task", 4096, NULL, 5, &s_task_handle) !=
      pdPASS) {
    s_running = false;
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

esp_err_t udp_bridge_stop(void) {
  if (!s_running) {
    return ESP_OK;
  }

  s_running = false;

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (s_state.sock >= 0) {
    shutdown(s_state.sock, 0); // Stops RX/TX operations
    close(s_state.sock);       // Frees the file descriptor
    s_state.sock = -1;
  }
  xSemaphoreGive(s_mutex);

  while (s_task_handle != NULL) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  ESP_LOGI(TAG, "UDP bridge stopped gracefully");
  return ESP_OK;
}

esp_err_t udp_bridge_send(const uint8_t *data, size_t len) {
  if (data == NULL || len == 0)
    return ESP_ERR_INVALID_ARG;

  xSemaphoreTake(s_mutex, portMAX_DELAY);

  if (!s_state.peer_valid || s_state.sock < 0) {
    xSemaphoreGive(s_mutex);
    return ESP_ERR_INVALID_STATE;
  }

  int sent =
      sendto(s_state.sock, data, len, 0, (struct sockaddr *)&s_state.peer_addr,
             s_state.peer_addr_len);

  xSemaphoreGive(s_mutex);

  return (sent == len) ? ESP_OK : ESP_FAIL;
}

static esp_err_t udp_transport_start(transport_rx_cb_t cb, void *user_data) {
  return udp_bridge_start(65102, cb, user_data);
}

const transport_iface_t transport_udp = {.name = "UDP Bridge",
                                         .start = udp_transport_start,
                                         .send = udp_bridge_send,
                                         .stop = udp_bridge_stop};

const transport_iface_t *get_udp_transport_iface(void) {
  return &transport_udp;
}
