
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

class WebServerSupervisor
{
public:
    WebServerSupervisor();
    static void init();
    static String UUIDString;

private:
    static String TemplateProcessor(const String &var);
    static void SetCacheAndETag(AsyncWebServerResponse *response, String ETag);
    static void SetCacheAndETagGzip(AsyncWebServerResponse *response, String ETag);
    static void modules(AsyncWebServerRequest *request);
    static void monitor2(AsyncWebServerRequest *request);
    static void monitor3(AsyncWebServerRequest *request);
    static void PrintStreamComma(AsyncResponseStream *response, const char *text, uint32_t value);
    static void PrintStream(AsyncResponseStream *response, const char *text, uint32_t value);
    static void PrintStreamCommaBoolean(AsyncResponseStream *response, const char *text, bool value);
    static void SendSuccess(AsyncWebServerRequest *request);
    static void SendFailure(AsyncWebServerRequest *request);
    static String uuidToString(uint8_t *uuidLocation);
    static void generateUUID();
    static bool validateXSS(AsyncWebServerRequest*);
};

