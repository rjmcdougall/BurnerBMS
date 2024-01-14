#include <Arduino.h>
//#include <M5Core2.h>
#include <M5Unified.h>
#include <SD.h>
#include <SPIFFS.h>
#include <ArduinoOTA.h>


#include "burner_bms.h"
#include "bms.h"

#include "WebServerSupervisor.h"

WebServerSupervisor WebServerSupervisor;

static String TAG = "Main";

BMS bms;


void SetupOTA()
{

  ArduinoOTA.setPort(3232);
  ArduinoOTA.setPassword("1jiOOx12AQgEco4e");

  ArduinoOTA
      .onStart([]()
               {
                 String type;
                 if (ArduinoOTA.getCommand() == U_FLASH)
                   type = "sketch";
                 else // U_SPIFFS
                   type = "filesystem";

                 // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
                 ESP_LOGI(TAG, "Start updating %s", type);
               });
  ArduinoOTA.onEnd([]()
                   { ESP_LOGD(TAG, "\nEnd"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        { ESP_LOGD(TAG, "Progress: %u%%\r", (progress / (total / 100))); });
  ArduinoOTA.onError([](ota_error_t error)
                     {
                       ESP_LOGD(TAG, "Error [%u]: ", error);
                       if (error == OTA_AUTH_ERROR)
                         ESP_LOGE(TAG, "Auth Failed");
                       else if (error == OTA_BEGIN_ERROR)
                         ESP_LOGE(TAG, "Begin Failed");
                       else if (error == OTA_CONNECT_ERROR)
                         ESP_LOGE(TAG, "Connect Failed");
                       else if (error == OTA_RECEIVE_ERROR)
                         ESP_LOGE(TAG, "Receive Failed");
                       else if (error == OTA_END_ERROR)
                         ESP_LOGE(TAG, "End Failed");
                     });

  ArduinoOTA.setHostname(WiFi.getHostname());
  ArduinoOTA.setMdnsEnabled(true);
  ArduinoOTA.begin();
}


void setup() {
  Serial.begin(115200);
  // Start debugging
  DBGINI(&Serial, ESP32Timestamp::TimestampSinceStart)
	DBGSTA
  DBGLEV(Debug);
  esp_log_level_set("*", ESP_LOG_VERBOSE);
  BLog_d(TAG, "booting");
  M5.begin();
  M5.Lcd.setTextColor(YELLOW);
  M5.Lcd.print("Hello World");
  esp_log_level_set("*", ESP_LOG_VERBOSE);
  M5.Log.setLogLevel(m5::log_target_serial,  (esp_log_level_t)ESP_LOG_VERBOSE); 


  //SPIFFS.format(); 
  SPIFFS.begin();
  //SD.begin();
  BLog_d(TAG, "starting webserver");
  bms.init();
  WebServerSupervisor.init();
  //SetupOTA();

}

void loop() {
  // put your main code here, to run repeatedly:
}

