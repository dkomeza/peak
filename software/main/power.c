#include "power.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_sleep.h"

#define POWER_WAKE_GPIO GPIO_NUM_7

esp_err_t power_enter_deep_sleep(void) {
  ESP_RETURN_ON_FALSE(esp_sleep_is_valid_wakeup_gpio(POWER_WAKE_GPIO),
                      ESP_ERR_NOT_SUPPORTED, "power",
                      "GPIO%d cannot wake from deep sleep", POWER_WAKE_GPIO);
  ESP_RETURN_ON_ERROR(esp_sleep_enable_ext1_wakeup_io(
                          1ULL << POWER_WAKE_GPIO, ESP_EXT1_WAKEUP_ANY_LOW),
                      "power", "Failed to configure Power-button wake");
  return esp_deep_sleep_try_to_start();
}
