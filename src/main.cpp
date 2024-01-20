#include <Arduino.h>
// #include <M5Core2.h>
#include <M5Unified.h>
#include <SD.h>
#include <SPIFFS.h>

#include "burner_bms.h"
#include "bms.h"
#include "bms_can.h"
#include "tft.h"
#include "ota.h"
#include "influxdb.h"

#include "WebServerSupervisor.h"

WebServerSupervisor WebServerSupervisor;
bool wifi_isconnected = true;

static String TAG = "Main";

BMS bms;
TFT tft;
bms_can can;

TaskHandle_t bmsTaskHandle = NULL;
void bmsTask(void *param) {
  while (true) {
    bms.update();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
  struct tm timeinfo;

void setup()
{
  Serial.begin(115200);
  // Start debugging
  DBGINI(&Serial, ESP32Timestamp::TimestampSinceStart)
  DBGSTA
  DBGLEV(Debug);
  esp_log_level_set("*", ESP_LOG_VERBOSE);
  BLog_d(TAG, "booting");
  M5.begin();
  esp_log_level_set("*", ESP_LOG_VERBOSE);
  M5.Log.setLogLevel(m5::log_target_serial, (esp_log_level_t)ESP_LOG_VERBOSE);

  // SPIFFS.format();
  SPIFFS.begin();
  SD.begin();
  BLog_d(TAG, "starting webserver");
  WebServerSupervisor.init();
  configTime(0, 0, "time.google.com");
  if(!getLocalTime(&timeinfo)){
    BLog_d(TAG, "Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");

  // Important to start BMS after tft, so that M5 doesn't steal i2c
  tft.init();
  tft.PrepareTFT_Voltage();
  SetupOTA();
  can.begin();
  // BMS code running on core 2
  bms.init();
  xTaskCreatePinnedToCore(bmsTask, "bms", 4096, nullptr, configMAX_PRIORITIES - 1, &bmsTaskHandle, 0);
}

uint32_t loop_cnt = 0;

void loop()
{
    BLog_d(TAG, "ota...");
  ArduinoOTA.handle();
    BLog_d(TAG, "tft...");
  tft.DrawTFT_Voltage();
  if ((loop_cnt % 30) == 0) {
    BLog_d(TAG, "influx...");
    influxEvent();
  }
  loop_cnt++;
  vTaskDelay(pdMS_TO_TICKS(1000));


}
