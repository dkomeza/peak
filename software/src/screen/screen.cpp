#include <Arduino.h>
#include <FS.h>
#include <atomic>
#include <queue>

#include <esp_task.h>
#include <esp_task_wdt.h>

#include <TFT_eSPI.h>

#include "datatypes.h"
#include "screen.h"
#include "pages/page.h"
#include "pages/home.h"

#define TARGET_FPS 30
#define TARGET_DELAY (1000 / TARGET_FPS) // in microseconds

static const int BACKLIGHT_PIN = 8;
static const int minBrightness = 55;

TaskHandle_t renderTaskHandle = nullptr;

TFT_eSPI screen::tft;
std::atomic<Page *> currentPage = nullptr; // Use atomic for thread-safe access

// Create an error queue to handle error messages without getting into race conditions, stored as a queue of (string, int) pairs
std::queue<std::pair<String, int>> errorQueue;

void renderTask(void *pvParameters);

void setupLED();
void render();

void screen::setup(bool crashRecovery)
{
    setupLED();

    tft.init();
    tft.fillScreen(TFT_BLACK);

    screen::setBrightness(100);

    if (!SPIFFS.begin(true))
    {
        Serial.println("Failed to mount SPIFFS");
        screen::displayError("SPIFFS Mount Failed", 0);
        return;
    }

    bool fontsLoaded = true;
    if (!SPIFFS.exists("/inter_14.vlw"))
        fontsLoaded = false;

    if (!fontsLoaded)
    {
        Serial.println("Fonts not found in SPIFFS");
        screen::displayError("Fonts Not Found", 0);
        return;
    }

    if (crashRecovery)
    {
        Serial.println("Crash recovery mode enabled");
        screen::displayError("Crash Recovery Mode", 0);
        return;
    }

    currentPage = new Home(&tft); // Set the initial page to homePage

    xTaskCreatePinnedToCore(
        renderTask, // Task function
        "RenderTask",
        8192,
        NULL,
        1,
        NULL,
        RENDER_CORE); // Core ID
}

void screen::displayError(const char *message, int duration)
{
    if (message == nullptr || strlen(message) == 0)
    {
        Serial.println("Attempted to display an empty error message");
        return;
    }

    // Push the error message into the queue
    errorQueue.push(std::make_pair(String(message), duration));
}

void setupLED()
{
    pinMode(BACKLIGHT_PIN, OUTPUT);
    screen::setBrightness(0);
}

void renderTask(void *pvParameters)
{
    long currentTime = 0;
    long lastTime = 0;
    long endTime = 0;
    long targetDelay = 0;

    float fps = 0.0;

    esp_task_wdt_add(NULL);

    while (true)
    {
        esp_task_wdt_reset();

        currentTime = micros();
        fps = 1000000.0 / (currentTime - lastTime);
        lastTime = currentTime;

        render();

        // Serial.printf("FPS: %.2f\n", fps);

        endTime = micros();
        targetDelay = TARGET_DELAY - (endTime - currentTime) / 1000; // in microseconds
        if (targetDelay < 1)
            targetDelay = 1; // minimum delay to avoid blocking

        vTaskDelay(targetDelay / portTICK_RATE_MS);
    }

    renderTaskHandle = nullptr; // Clear task handle to allow restart
    vTaskDelete(NULL);
}

void render()
{
    using namespace screen;

    if (!errorQueue.empty())
    {
        auto error = errorQueue.front();
        errorQueue.pop();

        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_RED);
        tft.setTextSize(2);
        tft.setTextFont(2);
        tft.setTextDatum(MC_DATUM);
        tft.drawString(error.first, tft.width() / 2, tft.height() / 2);

        if (error.second > 0)
        {
            vTaskDelay(error.second / portTICK_RATE_MS); // Wait for the specified duration
            tft.fillScreen(TFT_BLACK);                   // Clear the screen after displaying the error
        }
        return; // Exit early if there's an error to display
    }

    Page *page = currentPage.load(); // Use atomic load for thread-safe access

    if (page == nullptr)
    {
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_WHITE);
        tft.setTextSize(2);
        tft.setTextFont(2);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("No Page Set", tft.width() / 2, tft.height() / 2);
        vTaskDelay(2000 / portTICK_RATE_MS); // Wait for a second before returning
        return;
    }

    page->render();
}

void screen::setBrightness(int brightness)
{
    if (brightness != 0 && brightness < minBrightness)
        brightness = minBrightness;

    analogWrite(BACKLIGHT_PIN, brightness);
}

void screen::setBrightness(int brightness, int duration)
{
}

void screen::setPage(Page *page)
{
    if (page == nullptr)
    {
        Serial.println("Attempted to set a null page");
        return;
    }

    Page *oldPage = currentPage.exchange(page); // Atomically set the new page and get the old one
    if (oldPage != nullptr)
    {
        delete oldPage; // Free memory of the old page
    }
}
