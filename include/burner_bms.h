
#define ESP32DEBUGGING
#include "esp_log.h"
#include <Arduino.h>

#include <ESP32Logger.h>
#include <M5Unified.h>
#include "utility/m5unified_common.h"
#include "utility/I2C_Class.hpp"

#ifndef _burner_bms_h
#define _burner_bms_h

#define USEM5LOGGER

#ifdef USE_LOGGER
#define BLog_d(tag, logmsg, ...) { String logstr = tag + ":" + String(__func__) + ": " + String(logmsg); Log.log(ESP32LogLevel::Debug, logstr.c_str(), ##__VA_ARGS__); }
#define BLog_i(tag, logmsg, ...) { String logstr = tag + ":" + String(__func__) + ": " + String(logmsg); Log.log(ESP32LogLevel::Info, logstr.c_str(), ##__VA_ARGS__); }
#define BLog_e(tag, logmsg, ...) { String logstr = tag + ":" + String(__func__) + ": " + String(logmsg); Log.log(ESP32LogLevel::Error, logstr.c_str(), ##__VA_ARGS__); }
#endif

#ifdef USEM5LOGGER
#define  BLog_d(TAG, format, ...) M5.Log(ESP_LOG_DEBUG  , M5UNIFIED_LOG_FORMAT(D, format), ##__VA_ARGS__)
#define  BLog_i(TAG, format, ...) M5.Log(ESP_LOG_DEBUG  , M5UNIFIED_LOG_FORMAT(D, format), ##__VA_ARGS__)
#define  BLog_e(TAG, format, ...) M5.Log(ESP_LOG_DEBUG  , M5UNIFIED_LOG_FORMAT(D, format), ##__VA_ARGS__)
#endif

#ifdef USEESPLOGGER
#define  BLog_d(TAG, str, ...) ESP_LOGD(TAG,str, ##__VA_ARGS__)
#define  BLog_i(TAG, str, ...) ESP_LOGI(TAG,str, ##__VA_ARGS__)
#define  BLog_e(TAG, str, ...) ESP_LOGE(TAG,str, ##__VA_ARGS__)
#endif



//#endif #define M5_LOGE(format, ...) M5.Log(ESP_LOG_ERROR  , M5UNIFIED_LOG_FORMAT(E, format), ##__VA_ARGS__)


#endif // _burner_bms_h