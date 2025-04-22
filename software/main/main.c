#include "display.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

static const char *TAG = "PEAK";

// // Forward declarations for drawing functions
// void draw_pixel(int x, int y, uint16_t color);
// void fill_rectangle(int x, int y, int width, int height, uint16_t color);
// void flush_display(void);

// // Simple RGB565 Color Macro
// #define RGB565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

// // Draw a single pixel in the framebuffer
// void draw_pixel(int x, int y, uint16_t color)
// {
//   // Check boundaries
//   if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT)
//   {
//     return;
//   }
//   // Calculate index and set pixel
//   framebuffer[y * DISPLAY_WIDTH + x] = color;
// }

// // Fill a rectangle in the framebuffer
// void fill_rectangle(int x, int y, int width, int height, uint16_t color)
// {
//   // Clamp coordinates to screen boundaries
//   int x1 = (x < 0) ? 0 : x;
//   int y1 = (y < 0) ? 0 : y;
//   int x2 = (x + width > DISPLAY_WIDTH) ? DISPLAY_WIDTH : x + width;
//   int y2 = (y + height > DISPLAY_HEIGHT) ? DISPLAY_HEIGHT : y + height;

//   // Check if rectangle is completely outside
//   if (x1 >= x2 || y1 >= y2)
//   {
//     return;
//   }

//   // Fill the clamped rectangle area
//   for (int current_y = y1; current_y < y2; current_y++)
//   {
//     for (int current_x = x1; current_x < x2; current_x++)
//     {
//       framebuffer[current_y * DISPLAY_WIDTH + current_x] = color;
//     }
//     // More optimized fill for horizontal line (requires care with color format/byte order)
//     // Example (verify for your color format/endianness if using):
//     // uint16_t *line_ptr = &framebuffer[current_y * DISPLAY_WIDTH + x1];
//     // for(int i = 0; i < (x2 - x1); i++) {
//     //     *line_ptr++ = color;
//     // }
//   }
// }

// // Function to clear the entire framebuffer
// void clear_framebuffer(uint16_t color)
// {
//   // Efficiently clear using fill_rectangle or memset
//   // Using fill_rectangle ensures boundary checks if needed elsewhere,
//   // but memset is often faster for a solid color fill if directly mapped.
//   // fill_rectangle(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, color);

//   // Using memset (Faster for single color)
//   // Need to be careful if color != 0, as memset operates byte-wise.
//   // For black (0x0000), memset is perfect:
//   if (color == 0)
//   {
//     memset(framebuffer, 0, FRAME_BUFFER_SIZE);
//   }
//   else
//   {
//     // For other colors, loop might be safer/easier than byte-wise memset
//     fill_rectangle(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, color);
//   }
// }

// // Function to send the framebuffer content to the display
// void flush_display()
// {
//   ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, framebuffer));
// }

// // Example task to draw and update the display
// void display_task(void *pvParameters)
// {
//   ESP_LOGI(TAG, "Starting display task...");

//   int center_x = DISPLAY_WIDTH / 2;
//   int center_y = DISPLAY_HEIGHT / 2;
//   int rect_size = 20;
//   float angle = 0.0f;

//   uint16_t color_red = RGB565(255, 0, 0);
//   uint16_t color_green = RGB565(0, 255, 0);
//   uint16_t color_blue = RGB565(0, 0, 255);
//   uint16_t color_black = 0x0000; // We'll calculate background dynamically

//   // Initialize FPS timer

//   while (1)
//   {
//     int64_t start_time = esp_timer_get_time(); // Time measurement START

//     // --- 1. Calculate Animated Background Color ---
//     // Use time to create a cycling effect (e.g., pulsing green component)
//     int64_t current_time_us = esp_timer_get_time();
//     // Cycle green component over ~5 seconds using sine wave
//     float green_sine = sinf(current_time_us * 2.0f * M_PI / 5000000.0f);
//     // Map sine wave (-1 to 1) to green value (e.g., 30 to 225)
//     uint8_t green_val = (uint8_t)(127.5f + green_sine * 97.5f);
//     // Keep Red and Blue somewhat constant, maybe a slight variation
//     uint8_t red_val = 50;
//     uint8_t blue_val = 60 + (uint8_t)(cosf(current_time_us * 2.0f * M_PI / 8000000.0f) * 30); // Slower blue pulse

//     uint16_t background_color = RGB565(red_val, green_val, blue_val);

//     // --- 2. Clear the framebuffer with the calculated color ---
//     clear_framebuffer(color_black);

//     // --- 3. Draw other elements into the framebuffer ---
//     // Example: Draw a rotating rectangle (same as before)
//     int rect_x = center_x + (int)(cosf(angle) * 50) - rect_size / 2;
//     int rect_y = center_y + (int)(sinf(angle) * 50) - rect_size / 2;
//     fill_rectangle(rect_x, rect_y, rect_size, rect_size, color_red);

//     // Draw fixed rectangles
//     fill_rectangle(10, 10, 30, 30, color_green);
//     fill_rectangle(DISPLAY_WIDTH - 40, DISPLAY_HEIGHT - 40, 30, 30, color_blue);

//     // Draw some pixels
//     for (int i = 0; i < DISPLAY_WIDTH; i += 10)
//     {
//       draw_pixel(i, 0, color_green);
//       draw_pixel(i, DISPLAY_HEIGHT - 1, color_blue);
//     }

//     // --- 4. Flush the framebuffer to the display ---
//     flush_display();

//     // --- 5. Performance Calculation & Logging ---
//     int64_t end_time = esp_timer_get_time(); // Time measurement END
//     int64_t frame_duration_us = end_time - start_time;

//     // --- 6. Update state for next frame ---
//     angle += 0.01f; // Increment angle for rotation
//     if (angle > 2 * M_PI)
//     {
//       angle -= 2 * M_PI;
//     }

//     // --- 7. Delay? ---
//     // vTaskDelay is simple but less precise for targeting FPS.
//     // For potentially higher/more consistent FPS, you might remove explicit delays
//     // or implement a more precise frame timing mechanism if needed,
//     // but vsync/DMA callbacks would be better (more complex).
//     // For now, let's keep a minimal delay to prevent watchdog timeouts if drawing is very fast.
//     vTaskDelay(pdMS_TO_TICKS(1)); // Minimal delay to yield task
//   }
// }

void app_main(void)
{
  ESP_LOGI(TAG, "--- EBIKE DISPLAY START ---");

  // Initialize LCD
  esp_err_t ret = initialize_lcd();
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "LCD Initialization Failed!");
    return; // Stop if LCD init fails
  }

  // Sleep for a moment to allow the display to stabilize
  vTaskDelay(pdMS_TO_TICKS(1000));
  // Set brightness to maximum
  set_brightness(255);

  vTaskDelay(pdMS_TO_TICKS(1000));
  // Set smooth brightness to min then max
  set_smooth_brightness(0);
  vTaskDelay(pdMS_TO_TICKS(2000));
  set_smooth_brightness(255);

  ESP_LOGI(TAG, "--- Initialization Complete ---");
}