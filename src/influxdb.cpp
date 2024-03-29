
#define USE_ESP_IDF_LOG 1

static constexpr const char *const TAG = "influxdb";

static constexpr const char *const boardName = "baja";

#include "influxdb.h"
#include "string_utils.h"
#include <esp_http_client.h>
#include <string>
#include "bms.h"
#include <HTTPClient.h>

// InfluxDB 2 server url, e.g. http://192.168.1.48:8086 (Use: InfluxDB UI -> Load Data -> Client Libraries)
#define INFLUXDB_URL "https://us-central1-1.gcp.cloud2.influxdata.com:443/api/v2/write"
// InfluxDB 2 server or cloud API authentication token (Use: InfluxDB UI -> Load Data -> Tokens -> <select token>)
#define INFLUXDB_TOKEN "T49oTVwIN0yQVku1Z7qJ-eFWZgXe3gu-KMBDxIoodSKr_DLuHZV_yc1WvOSbDvQpbUG8Xwu1puHMcm_t54PRuQ=="
// InfluxDB 2 organization name or id (Use: InfluxDB UI -> Settings -> Profile -> <name under tile> )
#define INFLUXDB_ORG "richardmcd@gmail.com"
// InfluxDB 2 bucket name (Use: InfluxDB UI -> Load Data -> Buckets)
#define INFLUXDB_BUCKET "bms"

/// integer -> hex string
template <typename I>
std::string to_hex(I w, size_t hex_len = sizeof(I) << 1)
{
    static const char *digits = "0123456789ABCDEF";
    std::string rc(hex_len, '0');
    for (size_t i = 0, j = (hex_len - 1) * 4; i < hex_len; ++i, j -= 4)
    {
        rc[i] = digits[(w >> j) & 0x0F];
    }
    return rc;
}

/// RFC: https://www.ietf.org/rfc/rfc1738.txt
static inline std::string url_encode(const std::string source)
{
    const std::string reserved_characters = "?#/:;+@&=";
    const std::string illegal_characters = "%<>{}|\\\"^`!*'()$,[]";
    std::string encoded = "";

    // reserve the size of the source string plus 25%, this space will be
    // reclaimed if the final string length is shorter.
    encoded.reserve(source.length() + (source.length() / 4));

    // process the source string character by character checking for any that
    // are outside the ASCII printable character range, in the reserve character
    // list or illegal character list. For accepted characters it will be added
    // to the encoded string directly, any that require encoding will be hex
    // encoded before being added to the encoded string.
    for (auto ch : source)
    {
        if (ch <= 0x20 || ch >= 0x7F ||
            reserved_characters.find(ch) != std::string::npos ||
            illegal_characters.find(ch) != std::string::npos)
        {
            // if it is outside the printable ASCII character *OR* is in either the
            // reserved or illegal characters we need to encode it as %HH.
            // NOTE: space will be encoded as "%20" and not as "+", either is an
            // acceptable option per the RFC.
            encoded.append("%").append(to_hex(ch));
        }
        else
        {
            encoded += ch;
        }
    }
    // shrink the buffer to the actual length
    encoded.shrink_to_fit();
    return encoded;
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
        BLog_e(TAG, "HTTP Client Error encountered");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        BLog_i(TAG, "HTTP Client Connected!");
        break;
    case HTTP_EVENT_HEADERS_SENT:
        BLog_i(TAG, "HTTP Client sent all request headers");
        break;
    case HTTP_EVENT_ON_HEADER:
        BLog_i(TAG, "Header: key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        // ESP_LOG_BUFFER_HEXDUMP(TAG, evt->data, evt->data_len, esp_log_level_t:: BLog_dEBUG);
        BLog_i(TAG, "HTTP Client data recevied: len=%d", evt->data_len);
        break;
    case HTTP_EVENT_ON_FINISH:
        BLog_i(TAG, "HTTP Client finished");
        break;
    case HTTP_EVENT_DISCONNECTED:
        BLog_i(TAG, "HTTP Client Disconnected!");
        break;
    }
    return ESP_OK;
}

/// Generates and send module data to InfluxDB.
void influxEvent()
{
    if (!wifi_isconnected)
    {
        BLog_e(TAG, "Influx enabled, but WIFI not connected");
        return;
    }

    HTTPClient httpInflux;
    int httpCode = 0;

    std::string authtoken;
    std::string url;
    std::string bms_data;
    bms_data.reserve(768);

    std::string deviceName = boardName;
    BLog_d(TAG, "Send Influxdb data - wifi mac %s", deviceName.c_str());
    std::string preamble = std::string("pack,board=").append(deviceName).append(" ");
    std::string voltage = std::string("voltage=").append(std::to_string(bms.getVoltageMv() / 1000.0)).append(",");
    std::string current = std::string("current=").append(std::to_string(bms.getCurrentMa() / 1000.0)).append(",");
    std::string fcc = std::string("fullChargeCapacityAh=").append(std::to_string(bms.getFullChargeCapacityMah())).append(",");
    std::string rcc = std::string("remainingCapacityAh=").append(std::to_string(bms.getRemainingCapacityMah())).append(",");
    std::string soc = std::string("soc=").append(std::to_string(bms.getSocPct())).append(",");
    std::string soh = std::string("soh=").append(std::to_string(bms.getSohPct())).append(",");
    std::string minct = std::string("minCellTemperature=").append(std::to_string(bms.getMinCellTemp())).append(",");
    std::string maxct = std::string("maxCellTemperature=").append(std::to_string(bms.getMaxCellTemp())).append(",");
    std::string tmpic1 = std::string("temperature_ic1=").append(std::to_string(bms.getChipTempC())).append(",");
    std::string tmpic2 = std::string("temperature_ic2=").append(std::to_string(bms.getFETTempC())).append("\n");
    bms_data.append(preamble)
        .append(voltage)
        .append(current)
        .append(fcc)
        .append(rcc)
        .append(soc)
        .append(soh)
        .append(minct)
        .append(maxct)
        .append(tmpic1)
        .append(tmpic2)
        .append("\n");

    for (uint8_t i = 0; i < bms.getNumberCells(); i++)
    {
        std::string cell_preamble = std::string("bms,board=").append(deviceName);
        std::string cell_volt = std::string(" cell_").append(std::to_string(i)).append("_v=").append(std::to_string(bms.getCellVoltageMv(i) / 1000.0)).append(",");
        std::string cell_balance = std::string("cell_").append(std::to_string(i)).append("_balance_mah=").append(std::to_string(bms.getCellVoltageMv(i))).append("\n");
        bms_data.append(cell_preamble)
            .append(cell_volt)
            .append(cell_balance);
    }

    bms_data.shrink_to_fit();

    // If we did not generate any module data we can exit early, although this should never happen.
    if (bms_data.empty() || bms_data.length() == 0)
    {
        BLog_i(TAG, "No module data to send to InfluxDB");
        return;
    }

    // Show URL we are logging to...
    BLog_d(TAG, "URL %s", INFLUXDB_URL);

    url.reserve(sizeof(INFLUXDB_URL) + sizeof(INFLUXDB_ORG) + sizeof(INFLUXDB_BUCKET));
    url.append(INFLUXDB_URL);
    url.append("?org=").append(url_encode(INFLUXDB_ORG));
    url.append("&bucket=").append(url_encode(INFLUXDB_BUCKET));
    url.append("&precision=s");
    url.shrink_to_fit();

    // Submit POST request via HTTP
    httpInflux.begin(url.c_str());
    httpInflux.addHeader(F("Content-Type"), F("text/plain"));

    httpInflux.addHeader("Authorization", "Token " + String(INFLUXDB_TOKEN));
    httpCode = httpInflux.POST(bms_data.c_str());
    BLog_d(TAG, "Post: %s", bms_data.c_str());
    BLog_d(TAG, "Influx [HTTPS] POST...  Code: %d\n", httpCode);
    httpInflux.end();
}

// Generates and send module data to InfluxDB.
void influxEventDiag()
{
    if (!wifi_isconnected)
    {
        BLog_e(TAG, "Influx enabled, but WIFI not connected");
        return;
    }

    HTTPClient httpInflux;
    int httpCode = 0;

    std::string authtoken;
    std::string url;
    std::string bms_data;
    bms_data.reserve(768);
    std::map<std::string, int> diagData;

    diagData = bms.getBqzDiagData();

    if (diagData.size() == 0)
    {
        BLog_d(TAG, "no diag data to send");
        return;
    }

    std::string deviceName = boardName;
    BLog_d(TAG, "Send Influxdb data - wifi mac %s", deviceName.c_str());
    std::string preamble = std::string("bqz,board=").append(deviceName).append(" ");

    bms_data.append(preamble);
    auto it = diagData.begin();

    for (const auto &[key, value] : diagData)
    {
        BLog_d(TAG, "key %s value %d ", key, value);
        bms_data.append(key).append("=").append(std::to_string(value));
        if (std::next(it) != diagData.end())
        {
            bms_data.append(",");
        }
        it = std::next(it);
    }
    bms_data.append("\n");
    bms_data.shrink_to_fit();

    // If we did not generate any module data we can exit early, although this should never happen.
    if (bms_data.empty() || bms_data.length() == 0)
    {
        BLog_i(TAG, "No module data to send to InfluxDB");
        return;
    }

    // Show URL we are logging to...
    BLog_d(TAG, "URL %s", INFLUXDB_URL);

    url.reserve(sizeof(INFLUXDB_URL) + sizeof(INFLUXDB_ORG) + sizeof(INFLUXDB_BUCKET));
    url.append(INFLUXDB_URL);
    url.append("?org=").append(url_encode(INFLUXDB_ORG));
    url.append("&bucket=").append(url_encode(INFLUXDB_BUCKET));
    url.append("&precision=s");
    url.shrink_to_fit();

    // Submit POST request via HTTP
    httpInflux.begin(url.c_str());
    httpInflux.addHeader(F("Content-Type"), F("text/plain"));

    httpInflux.addHeader("Authorization", "Token " + String(INFLUXDB_TOKEN));
    httpCode = httpInflux.POST(bms_data.c_str());
    BLog_d(TAG, "Post: %s", bms_data.c_str());
    BLog_d(TAG, "Influx [HTTPS] POST...  Code: %d\n", httpCode);
    httpInflux.end();
}