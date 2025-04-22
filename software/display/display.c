#include "include/display.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"

// --- LCD configuration ---
#define LCD_HOST SPI2_HOST

#define PIN_SCLK GPIO_NUM_12
#define PIN_MOSI GPIO_NUM_11
#define PIN_RST GPIO_NUM_4
#define PIN_CS GPIO_NUM_10
#define PIN_DC GPIO_NUM_2
#define PIN_BL GPIO_NUM_8

#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 320

#define BYTES_PER_PIXEL 2
#define FS_BUFFER_SIZE (DISPLAY_WIDTH * DISPLAY_HEIGHT * BYTES_PER_PIXEL)
#define LCD_PIXEL_CLOCK_HZ 60000000 // 60MHz

#define LCD_CMD_BITS 8
#define LCD_PARAM_BITS 8
#define LCD_TRANS_QUEUE_DEPTH 10

// --- Backlight PWM Configuration ---
#define BL_LEDC_TIMER LEDC_TIMER_0
#define BL_LEDC_MODE LEDC_LOW_SPEED_MODE // Or LEDC_HIGH_SPEED_MODE if preferred/needed
#define BL_LEDC_CHANNEL LEDC_CHANNEL_0
#define BL_LEDC_DUTY_RES LEDC_TIMER_8_BIT
#define BL_LEDC_FREQUENCY (20000)
#define BL_MAX_DUTY ((1 << BL_LEDC_DUTY_RES) - 1)

uint16_t *framebuffer = NULL;
esp_lcd_panel_handle_t panel_handle = NULL;

static const char *TAG = "DISPLAY";

uint8_t bl_brightness = 0; // Global variable to store brightness level

esp_err_t initialize_backlight_pwm();

esp_err_t initialize_lcd()
{
  ESP_LOGI(TAG, "Initializing LCD...");
  // Set up backlight
  ESP_ERROR_CHECK(initialize_backlight_pwm());
  // Set initial brightness to 0 (off)
  set_brightness(0);

  // -- -1. Initialize SPI Bus-- -
  spi_bus_config_t buscfg = {
      .sclk_io_num = PIN_SCLK,
      .mosi_io_num = PIN_MOSI,
      .miso_io_num = -1, // Not used for ST7789
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = FS_BUFFER_SIZE + 16, // Max transfer size slightly larger than buffer
      .flags = SPICOMMON_BUSFLAG_MASTER};
  ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO)); // Use DMA
  ESP_LOGI(TAG, "SPI bus initialized");

  // -- -2. Initialize LCD Panel IO (SPI) -- -
  esp_lcd_panel_io_handle_t io_handle = NULL;
  esp_lcd_panel_io_spi_config_t io_config = {
      .cs_gpio_num = PIN_CS,
      .dc_gpio_num = PIN_DC,
      .spi_mode = 0, // SPI mode 0
      .pclk_hz = LCD_PIXEL_CLOCK_HZ + 1,
      .trans_queue_depth = LCD_TRANS_QUEUE_DEPTH,
      .on_color_trans_done = NULL, // Callback not used in simple single buffer mode
      .user_ctx = NULL,
      .lcd_cmd_bits = LCD_CMD_BITS,
      .lcd_param_bits = LCD_PARAM_BITS,
      .flags = {
          .dc_low_on_data = 0,
          .octal_mode = 0,
          .lsb_first = 0,
      }};
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));
  ESP_LOGI(TAG, "LCD Panel IO (SPI) initialized");

  // --- 3. Initialize LCD Panel Driver (ST7789) ---
  esp_lcd_panel_dev_config_t panel_config = {
      .reset_gpio_num = PIN_RST,
      .rgb_endian = LCD_RGB_ENDIAN_RGB, // Check your specific display, might need BGR
      .bits_per_pixel = 16,             // RGB565
      .flags = {
          .reset_active_high = 0,
      },
      .vendor_config = NULL // Can add vendor specific init commands here if needed
  };
  ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));
  ESP_LOGI(TAG, "LCD Panel Driver (ST7789) initialized");

  // --- 4. Perform Panel Initialization Sequence ---
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true)); // ST7789 usually needs inversion
  ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 0, 0));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true)); // Turn display on
  ESP_LOGI(TAG, "Panel initialization sequence complete");

  // --- 5. Allocate Framebuffer ---
  framebuffer = (uint16_t *)heap_caps_malloc(FS_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
  if (framebuffer == NULL)
  {
    ESP_LOGE(TAG, "Failed to allocate framebuffer in PSRAM/DMA RAM!");
    // Fallback attempt to general SPIRAM (if not DMA capable)
    framebuffer = (uint16_t *)heap_caps_malloc(FS_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    if (framebuffer == NULL)
    {
      ESP_LOGE(TAG, "Failed to allocate framebuffer in any SPIRAM!");
      // Fallback to internal RAM (May be too small!)
      framebuffer = (uint16_t *)heap_caps_malloc(FS_BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
      if (framebuffer == NULL)
      {
        ESP_LOGE(TAG, "Failed to allocate framebuffer in Internal DMA RAM!");
        return ESP_ERR_NO_MEM; // Allocation failed
      }
      else
      {
        ESP_LOGW(TAG, "Framebuffer allocated in Internal RAM (Performance may suffer)");
      }
    }
    else
    {
      ESP_LOGI(TAG, "Framebuffer allocated in general SPIRAM.");
    }
  }
  else
  {
    ESP_LOGI(TAG, "Framebuffer allocated in PSRAM/DMA RAM (%d bytes)", FS_BUFFER_SIZE);
  }
  // Clear framebuffer initially
  memset(framebuffer, 0, FS_BUFFER_SIZE);

  ESP_LOGI(TAG, "LCD Initialization Complete");
  return ESP_OK;
}

esp_err_t initialize_backlight_pwm()
{
  ESP_LOGI(TAG, "Initializing LEDC for backlight PWM...");

  // --- Configure Timer ---
  ledc_timer_config_t ledc_timer = {
      .speed_mode = BL_LEDC_MODE,
      .timer_num = BL_LEDC_TIMER,
      .duty_resolution = BL_LEDC_DUTY_RES,
      .freq_hz = BL_LEDC_FREQUENCY,
      .clk_cfg = LEDC_AUTO_CLK};
  ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));
  ESP_LOGI(TAG, "LEDC Timer configured (Timer %d, %d Hz, %d-bit)",
           BL_LEDC_TIMER, BL_LEDC_FREQUENCY, BL_LEDC_DUTY_RES);

  // --- Configure Channel ---
  ledc_channel_config_t ledc_channel = {
      .speed_mode = BL_LEDC_MODE,
      .channel = BL_LEDC_CHANNEL,
      .timer_sel = BL_LEDC_TIMER,
      .intr_type = LEDC_INTR_DISABLE,
      .gpio_num = PIN_BL,
      .duty = 0, // Start with backlight off
      .hpoint = 0};
  ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
  ESP_LOGI(TAG, "LEDC Channel configured (Channel %d, GPIO %d)",
           BL_LEDC_CHANNEL, PIN_BL);

  ESP_ERROR_CHECK(ledc_fade_func_install(0)); // Use 0 for default interrupt flags

  return ESP_OK;
}

void set_brightness(uint8_t brightness)
{
  uint32_t duty = (brightness * BL_MAX_DUTY) / 255;
  ESP_ERROR_CHECK(ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty));
  ESP_ERROR_CHECK(ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL));
  bl_brightness = brightness; // Store the current brightness level
}

void set_smooth_brightness(uint8_t brightness)
{
  // Calculate the difference between the current and target brightness
  int16_t delta = (int16_t)brightness - (int16_t)bl_brightness;

  // Determine the required duration for the transition (5 ms per 1 step of difference)
  uint32_t duration = abs(delta) * 5; // 5 ms per unit difference

  uint32_t duty = (brightness * BL_MAX_DUTY) / 255;
  ESP_ERROR_CHECK(ledc_set_fade_with_time(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty, duration));
  ESP_ERROR_CHECK(ledc_fade_start(BL_LEDC_MODE, BL_LEDC_CHANNEL, LEDC_FADE_NO_WAIT));
  bl_brightness = brightness; // Store the current brightness level
}