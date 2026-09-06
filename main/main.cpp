#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "ActivityTracker.h"
#include "NmeaParser.h"
#include "OledLayout.h"
#include "Ssd1306.h"
#include "StatusJson.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"

namespace {

extern const char webPageStart[] asm("_binary_web_page_html_start");
extern const char webPageEnd[] asm("_binary_web_page_html_end");

constexpr char kLogTag[] = "TrackBox";
constexpr char kStorageRoot[] = "/spiffs";
constexpr char kAccessPointPassword[] = "trackbox1";
constexpr std::uint32_t kGpsOfflineMs = 3000;
constexpr std::uint32_t kGpsBaud = 38400;
constexpr uart_port_t kGpsUart = UART_NUM_1;
constexpr gpio_num_t kGpsTxPin = GPIO_NUM_4;
constexpr gpio_num_t kGpsRxPin = GPIO_NUM_5;
constexpr gpio_num_t kButtonAPin = GPIO_NUM_6;
constexpr gpio_num_t kButtonBPin = GPIO_NUM_7;
constexpr gpio_num_t kOledDcPin = GPIO_NUM_9;
constexpr gpio_num_t kOledResetPin = GPIO_NUM_10;
constexpr gpio_num_t kOledMosiPin = GPIO_NUM_11;
constexpr gpio_num_t kOledClockPin = GPIO_NUM_12;

gpstracker::NmeaParser gParser;
gpstracker::ActivityTracker gActivity;
gpstracker::GpsFix gFix;
gpstracker::Ssd1306 gOled;
portMUX_TYPE gStateMux = portMUX_INITIALIZER_UNLOCKED;
SemaphoreHandle_t gStorageMutex = nullptr;
httpd_handle_t gHttpServer = nullptr;
std::array<char, 33> gWifiSsid{};
std::uint64_t gLastGpsUpdateMs = 0;
bool gStorageReady = false;
std::uint32_t gActivityCount = 0;
std::uint32_t gNextActivityId = 1;
std::uint32_t gCurrentActivityId = 0;
FILE* gTrackFile = nullptr;
std::string gActivityStartedAt;
unsigned gPointsSinceFlush = 0;
std::uint64_t gGpsByteCount = 0;
std::uint64_t gLastGpsDiagnosticMs = 0;
bool gGpsRawBytesLogged = false;

class StorageGuard {
 public:
  StorageGuard() : locked_(gStorageMutex != nullptr &&
                           xSemaphoreTake(gStorageMutex, portMAX_DELAY) == pdTRUE) {}
  ~StorageGuard() {
    if (locked_) {
      xSemaphoreGive(gStorageMutex);
    }
  }
  explicit operator bool() const { return locked_; }

 private:
  bool locked_{false};
};

std::uint64_t nowMs() {
  return static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
}

void activityPath(char* output, const std::size_t size,
                  const std::uint32_t id, const char* extension) {
  std::snprintf(output, size, "%s/activity-%06u.%s", kStorageRoot,
                static_cast<unsigned>(id),
                extension);
}

std::vector<std::uint32_t> storedActivityIds() {
  std::vector<std::uint32_t> ids;
  DIR* directory = opendir(kStorageRoot);
  if (directory == nullptr) {
    return ids;
  }
  while (const dirent* entry = readdir(directory)) {
    unsigned parsedId = 0;
    char tail = '\0';
    if (std::sscanf(entry->d_name, "activity-%6u.json%c", &parsedId, &tail) == 1) {
      ids.push_back(static_cast<std::uint32_t>(parsedId));
    }
  }
  closedir(directory);
  std::sort(ids.begin(), ids.end(), std::greater<std::uint32_t>());
  return ids;
}

std::string readTextFile(const char* path) {
  FILE* file = std::fopen(path, "rb");
  if (file == nullptr) {
    return {};
  }
  std::fseek(file, 0, SEEK_END);
  const long length = std::ftell(file);
  std::rewind(file);
  std::string content;
  if (length > 0 && length < 65536) {
    content.resize(static_cast<std::size_t>(length));
    const auto read = std::fread(content.data(), 1, content.size(), file);
    content.resize(read);
  }
  std::fclose(file);
  return content;
}

gpstracker::GpsFix currentFix(const std::uint64_t now) {
  gpstracker::GpsFix fix;
  std::uint64_t lastUpdate = 0;
  portENTER_CRITICAL(&gStateMux);
  fix = gFix;
  lastUpdate = gLastGpsUpdateMs;
  portEXIT_CRITICAL(&gStateMux);
  if (lastUpdate == 0 || now - lastUpdate > kGpsOfflineMs) {
    fix.locationValid = false;
  }
  return fix;
}

gpstracker::ActivitySnapshot activitySnapshot(const std::uint64_t now) {
  portENTER_CRITICAL(&gStateMux);
  const auto snapshot = gActivity.snapshot(now);
  portEXIT_CRITICAL(&gStateMux);
  return snapshot;
}

void writeTrackPoint(const gpstracker::GpsFix& fix) {
  if (gTrackFile == nullptr) {
    return;
  }
  StorageGuard guard;
  if (!guard) {
    return;
  }
  const auto utc = gpstracker::formatIsoUtc(fix);
  const int written = std::fprintf(
      gTrackFile, "%s,%.7f,%.7f,%.1f,%.1f,%.1f,%u,%.1f\n", utc.c_str(),
      fix.latitude, fix.longitude, fix.altitudeM, fix.speedKmh, fix.courseDeg,
      static_cast<unsigned>(fix.satellites), fix.hdop);
  if (written < 0) {
    gStorageReady = false;
    ESP_LOGE(kLogTag, "Track write failed");
    return;
  }
  if (++gPointsSinceFlush >= 5) {
    std::fflush(gTrackFile);
    gPointsSinceFlush = 0;
  }
}

bool beginActivity(const gpstracker::GpsFix& fix, const std::uint64_t now) {
  if (!gStorageReady || !fix.usable() || gTrackFile != nullptr) {
    return false;
  }
  const std::uint32_t id = gNextActivityId++;
  char path[64]{};
  activityPath(path, sizeof(path), id, "csv");
  {
    StorageGuard guard;
    if (!guard) {
      return false;
    }
    gTrackFile = std::fopen(path, "wb");
    if (gTrackFile == nullptr) {
      ESP_LOGE(kLogTag, "Cannot create %s", path);
      return false;
    }
    std::fputs("utc,lat,lon,alt_m,speed_kmh,course_deg,sats,hdop\n",
               gTrackFile);
  }

  bool started = false;
  portENTER_CRITICAL(&gStateMux);
  started = gActivity.start(fix, now);
  if (started) {
    gCurrentActivityId = id;
  }
  portEXIT_CRITICAL(&gStateMux);
  if (!started) {
    StorageGuard guard;
    std::fclose(gTrackFile);
    gTrackFile = nullptr;
    std::remove(path);
    return false;
  }
  gActivityStartedAt = gpstracker::formatIsoUtc(fix);
  gPointsSinceFlush = 0;
  writeTrackPoint(fix);
  ESP_LOGI(kLogTag, "Activity %u started", static_cast<unsigned>(id));
  return true;
}

bool completeActivity(const std::uint64_t now) {
  gpstracker::ActivitySnapshot snapshot;
  std::uint32_t id = 0;
  bool finished = false;
  portENTER_CRITICAL(&gStateMux);
  finished = gActivity.finish(now);
  snapshot = gActivity.snapshot(now);
  id = gCurrentActivityId;
  portEXIT_CRITICAL(&gStateMux);
  if (!finished || id == 0) {
    return false;
  }

  gpstracker::StoredActivitySummary summary;
  summary.id = id;
  summary.mode = snapshot.mode;
  summary.startedAt = gActivityStartedAt;
  summary.elapsedMs = snapshot.elapsedMs;
  summary.distanceM = snapshot.distanceM;
  summary.averageSpeedKmh = snapshot.averageSpeedKmh;
  summary.maxSpeedKmh = snapshot.maxSpeedKmh;
  summary.pointCount = snapshot.pointCount;

  char summaryPath[64]{};
  activityPath(summaryPath, sizeof(summaryPath), id, "json");
  bool saved = false;
  {
    StorageGuard guard;
    if (gTrackFile != nullptr) {
      std::fflush(gTrackFile);
      std::fclose(gTrackFile);
      gTrackFile = nullptr;
    }
    FILE* file = std::fopen(summaryPath, "wb");
    if (file != nullptr) {
      const auto json = gpstracker::makeActivitySummaryJson(summary);
      saved = std::fwrite(json.data(), 1, json.size(), file) == json.size();
      std::fclose(file);
    }
  }
  portENTER_CRITICAL(&gStateMux);
  gCurrentActivityId = 0;
  if (saved) {
    ++gActivityCount;
  }
  portEXIT_CRITICAL(&gStateMux);
  if (!saved) {
    gStorageReady = false;
    ESP_LOGE(kLogTag, "Activity summary save failed");
    return false;
  }
  ESP_LOGI(kLogTag, "Activity %u saved", static_cast<unsigned>(id));
  return true;
}

gpstracker::DeviceStatus deviceStatus() {
  const auto now = nowMs();
  gpstracker::DeviceStatus status;
  status.gps = currentFix(now);
  status.activity = activitySnapshot(now);
  status.storageReady = gStorageReady;
  portENTER_CRITICAL(&gStateMux);
  status.activityCount = gActivityCount;
  portEXIT_CRITICAL(&gStateMux);
  std::size_t total = 0;
  std::size_t used = 0;
  if (gStorageReady && esp_spiffs_info("storage", &total, &used) == ESP_OK) {
    status.freeBytes = total >= used ? total - used : 0;
  }
  status.wifiSsid = gWifiSsid.data();
  return status;
}

esp_err_t sendError(httpd_req_t* request, const char* status,
                    const char* message) {
  httpd_resp_set_status(request, status);
  httpd_resp_set_type(request, "application/json");
  return httpd_resp_sendstr(request, message);
}

bool queryValue(httpd_req_t* request, const char* key, char* output,
                const std::size_t outputSize) {
  const std::size_t length = httpd_req_get_url_query_len(request);
  if (length == 0 || length >= 128) {
    return false;
  }
  char query[128]{};
  if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK) {
    return false;
  }
  return httpd_query_key_value(query, key, output, outputSize) == ESP_OK;
}

bool queryActivityId(httpd_req_t* request, std::uint32_t& id) {
  char value[16]{};
  if (!queryValue(request, "id", value, sizeof(value))) {
    return false;
  }
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(value, &end, 10);
  if (end == value || *end != '\0' || parsed == 0 || parsed > 999999) {
    return false;
  }
  id = static_cast<std::uint32_t>(parsed);
  return true;
}

esp_err_t rootHandler(httpd_req_t* request) {
  httpd_resp_set_type(request, "text/html; charset=utf-8");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_send(request, webPageStart,
                         static_cast<ssize_t>(webPageEnd - webPageStart));
}

esp_err_t statusHandler(httpd_req_t* request) {
  const auto json = gpstracker::makeStatusJson(deviceStatus());
  httpd_resp_set_type(request, "application/json");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_send(request, json.c_str(), json.size());
}

esp_err_t activitiesHandler(httpd_req_t* request) {
  if (!gStorageReady) {
    return sendError(request, "503 Service Unavailable",
                     "{\"error\":\"storage unavailable\"}");
  }
  httpd_resp_set_type(request, "application/json");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  StorageGuard guard;
  const auto ids = storedActivityIds();
  httpd_resp_send_chunk(request, "[", 1);
  bool first = true;
  for (const auto id : ids) {
    char path[64]{};
    activityPath(path, sizeof(path), id, "json");
    const auto summary = readTextFile(path);
    if (summary.empty() || summary.front() != '{') {
      continue;
    }
    if (!first) {
      httpd_resp_send_chunk(request, ",", 1);
    }
    first = false;
    httpd_resp_send_chunk(request, summary.c_str(), summary.size());
  }
  httpd_resp_send_chunk(request, "]", 1);
  return httpd_resp_send_chunk(request, nullptr, 0);
}

esp_err_t activityGetHandler(httpd_req_t* request) {
  std::uint32_t id = 0;
  if (!queryActivityId(request, id)) {
    return sendError(request, "400 Bad Request", "{\"error\":\"bad id\"}");
  }
  char summaryPath[64]{};
  char csvPath[64]{};
  activityPath(summaryPath, sizeof(summaryPath), id, "json");
  activityPath(csvPath, sizeof(csvPath), id, "csv");
  StorageGuard guard;
  const auto summary = readTextFile(summaryPath);
  FILE* file = std::fopen(csvPath, "rb");
  if (summary.empty() || file == nullptr) {
    if (file != nullptr) {
      std::fclose(file);
    }
    return sendError(request, "404 Not Found", "{\"error\":\"not found\"}");
  }
  httpd_resp_set_type(request, "application/json");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  const std::string prefix = "{\"summary\":" + summary + ",\"points\":[";
  httpd_resp_send_chunk(request, prefix.c_str(), prefix.size());
  char line[192]{};
  std::fgets(line, sizeof(line), file);
  bool first = true;
  while (std::fgets(line, sizeof(line), file) != nullptr) {
    char utc[32]{};
    double latitude = 0;
    double longitude = 0;
    double altitude = 0;
    double speed = 0;
    if (std::sscanf(line, "%31[^,],%lf,%lf,%lf,%lf", utc, &latitude,
                    &longitude, &altitude, &speed) != 5) {
      continue;
    }
    char point[96]{};
    std::snprintf(point, sizeof(point), "%s[%.7f,%.7f,%.1f]",
                  first ? "" : ",", latitude, longitude, speed);
    first = false;
    if (httpd_resp_send_chunk(request, point, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
      break;
    }
  }
  std::fclose(file);
  httpd_resp_send_chunk(request, "]}", 2);
  return httpd_resp_send_chunk(request, nullptr, 0);
}

esp_err_t activityDeleteHandler(httpd_req_t* request) {
  std::uint32_t id = 0;
  if (!queryActivityId(request, id)) {
    return sendError(request, "400 Bad Request", "{\"error\":\"bad id\"}");
  }
  portENTER_CRITICAL(&gStateMux);
  const bool active = id == gCurrentActivityId;
  portEXIT_CRITICAL(&gStateMux);
  if (active) {
    return sendError(request, "409 Conflict", "{\"error\":\"active\"}");
  }
  char jsonPath[64]{};
  char csvPath[64]{};
  activityPath(jsonPath, sizeof(jsonPath), id, "json");
  activityPath(csvPath, sizeof(csvPath), id, "csv");
  StorageGuard guard;
  if (std::remove(jsonPath) != 0) {
    return sendError(request, "404 Not Found", "{\"error\":\"not found\"}");
  }
  std::remove(csvPath);
  portENTER_CRITICAL(&gStateMux);
  if (gActivityCount > 0) {
    --gActivityCount;
  }
  portEXIT_CRITICAL(&gStateMux);
  httpd_resp_set_status(request, "204 No Content");
  return httpd_resp_send(request, nullptr, 0);
}

esp_err_t downloadHandler(httpd_req_t* request) {
  std::uint32_t id = 0;
  char format[8]{};
  if (!queryActivityId(request, id) ||
      !queryValue(request, "format", format, sizeof(format)) ||
      (std::strcmp(format, "csv") != 0 && std::strcmp(format, "gpx") != 0)) {
    return sendError(request, "400 Bad Request", "{\"error\":\"bad query\"}");
  }
  char path[64]{};
  activityPath(path, sizeof(path), id, "csv");
  StorageGuard guard;
  FILE* file = std::fopen(path, "rb");
  if (file == nullptr) {
    return sendError(request, "404 Not Found", "{\"error\":\"not found\"}");
  }
  char disposition[80]{};
  std::snprintf(disposition, sizeof(disposition),
                "attachment; filename=trackbox-%06u.%s",
                static_cast<unsigned>(id), format);
  httpd_resp_set_hdr(request, "Content-Disposition", disposition);
  if (std::strcmp(format, "csv") == 0) {
    httpd_resp_set_type(request, "text/csv; charset=utf-8");
    std::array<char, 1024> buffer{};
    std::size_t count = 0;
    while ((count = std::fread(buffer.data(), 1, buffer.size(), file)) > 0) {
      if (httpd_resp_send_chunk(request, buffer.data(), count) != ESP_OK) {
        break;
      }
    }
  } else {
    httpd_resp_set_type(request, "application/gpx+xml");
    httpd_resp_send_chunk(
        request,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?><gpx version=\"1.1\" "
        "creator=\"TrackBox\" xmlns=\"https://www.topografix.com/GPX/1/1\"><trk>"
        "<name>TrackBox activity</name><trkseg>",
        HTTPD_RESP_USE_STRLEN);
    char line[192]{};
    std::fgets(line, sizeof(line), file);
    while (std::fgets(line, sizeof(line), file) != nullptr) {
      char utc[32]{};
      double latitude = 0;
      double longitude = 0;
      double altitude = 0;
      double speed = 0;
      if (std::sscanf(line, "%31[^,],%lf,%lf,%lf,%lf", utc, &latitude,
                      &longitude, &altitude, &speed) != 5) {
        continue;
      }
      char point[192]{};
      std::snprintf(point, sizeof(point),
                    "<trkpt lat=\"%.7f\" lon=\"%.7f\"><ele>%.1f</ele>"
                    "<time>%s</time></trkpt>",
                    latitude, longitude, altitude, utc);
      if (httpd_resp_send_chunk(request, point, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
        break;
      }
    }
    httpd_resp_send_chunk(request, "</trkseg></trk></gpx>",
                          HTTPD_RESP_USE_STRLEN);
  }
  std::fclose(file);
  return httpd_resp_send_chunk(request, nullptr, 0);
}

esp_err_t healthHandler(httpd_req_t* request) {
  const auto status = deviceStatus();
  httpd_resp_set_type(request, "application/json");
  if (!status.storageReady) {
    return sendError(request, "503 Service Unavailable",
                     "{\"ok\":false,\"storage\":false}");
  }
  return httpd_resp_sendstr(request, status.gps.usable()
                                        ? "{\"ok\":true,\"gps\":true}"
                                        : "{\"ok\":true,\"gps\":false}");
}

void startHttpServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 8;
  ESP_ERROR_CHECK(httpd_start(&gHttpServer, &config));
  const httpd_uri_t root{.uri = "/", .method = HTTP_GET,
                         .handler = rootHandler, .user_ctx = nullptr};
  const httpd_uri_t status{.uri = "/api/status", .method = HTTP_GET,
                           .handler = statusHandler, .user_ctx = nullptr};
  const httpd_uri_t activities{.uri = "/api/activities", .method = HTTP_GET,
                               .handler = activitiesHandler, .user_ctx = nullptr};
  const httpd_uri_t activityGet{.uri = "/api/activity", .method = HTTP_GET,
                                .handler = activityGetHandler, .user_ctx = nullptr};
  const httpd_uri_t activityDelete{.uri = "/api/activity", .method = HTTP_DELETE,
                                   .handler = activityDeleteHandler, .user_ctx = nullptr};
  const httpd_uri_t download{.uri = "/api/download", .method = HTTP_GET,
                             .handler = downloadHandler, .user_ctx = nullptr};
  const httpd_uri_t health{.uri = "/api/health", .method = HTTP_GET,
                           .handler = healthHandler, .user_ctx = nullptr};
  ESP_ERROR_CHECK(httpd_register_uri_handler(gHttpServer, &root));
  ESP_ERROR_CHECK(httpd_register_uri_handler(gHttpServer, &status));
  ESP_ERROR_CHECK(httpd_register_uri_handler(gHttpServer, &activities));
  ESP_ERROR_CHECK(httpd_register_uri_handler(gHttpServer, &activityGet));
  ESP_ERROR_CHECK(httpd_register_uri_handler(gHttpServer, &activityDelete));
  ESP_ERROR_CHECK(httpd_register_uri_handler(gHttpServer, &download));
  ESP_ERROR_CHECK(httpd_register_uri_handler(gHttpServer, &health));
}

void initNvs() {
  esp_err_t result = nvs_flash_init();
  if (result == ESP_ERR_NVS_NO_FREE_PAGES ||
      result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    result = nvs_flash_init();
  }
  ESP_ERROR_CHECK(result);
}

void initStorage() {
  const esp_vfs_spiffs_conf_t config{.base_path = kStorageRoot,
                                     .partition_label = "storage",
                                     .max_files = 8,
                                     .format_if_mount_failed = true};
  gStorageReady = esp_vfs_spiffs_register(&config) == ESP_OK;
  if (!gStorageReady) {
    ESP_LOGE(kLogTag, "SPIFFS mount failed");
    return;
  }
  const auto ids = storedActivityIds();
  gActivityCount = static_cast<std::uint32_t>(ids.size());
  gNextActivityId = ids.empty() ? 1 : ids.front() + 1;
  ESP_LOGI(kLogTag, "SPIFFS ready: activities=%u next=%u",
           static_cast<unsigned>(gActivityCount),
           static_cast<unsigned>(gNextActivityId));
}

void startAccessPoint() {
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_ap();
  wifi_init_config_t initConfig = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&initConfig));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  std::array<std::uint8_t, 6> mac{};
  ESP_ERROR_CHECK(esp_read_mac(mac.data(), ESP_MAC_WIFI_SOFTAP));
  std::snprintf(gWifiSsid.data(), gWifiSsid.size(), "TrackBox-%02X%02X", mac[4],
                mac[5]);
  wifi_config_t accessPoint{};
  std::strncpy(reinterpret_cast<char*>(accessPoint.ap.ssid), gWifiSsid.data(),
               sizeof(accessPoint.ap.ssid) - 1);
  accessPoint.ap.ssid_len = std::strlen(gWifiSsid.data());
  std::strncpy(reinterpret_cast<char*>(accessPoint.ap.password),
               kAccessPointPassword, sizeof(accessPoint.ap.password) - 1);
  accessPoint.ap.channel = 1;
  accessPoint.ap.max_connection = 4;
  accessPoint.ap.authmode = WIFI_AUTH_WPA2_PSK;
  accessPoint.ap.pmf_cfg.capable = true;
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &accessPoint));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_LOGI(kLogTag, "Wi-Fi: %s / %s / http://192.168.4.1", gWifiSsid.data(),
           kAccessPointPassword);
}

void initGps() {
  const uart_config_t config{.baud_rate = static_cast<int>(kGpsBaud),
                             .data_bits = UART_DATA_8_BITS,
                             .parity = UART_PARITY_DISABLE,
                             .stop_bits = UART_STOP_BITS_1,
                             .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                             .rx_flow_ctrl_thresh = 0,
                             .source_clk = UART_SCLK_DEFAULT,
                             .flags = {}};
  ESP_ERROR_CHECK(uart_driver_install(kGpsUart, 2048, 0, 0, nullptr, 0));
  ESP_ERROR_CHECK(uart_param_config(kGpsUart, &config));
  ESP_ERROR_CHECK(uart_set_pin(kGpsUart, kGpsTxPin, kGpsRxPin,
                               UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
  ESP_LOGI(kLogTag, "GPS UART: RX=GPIO%d TX=GPIO%d baud=%u", kGpsRxPin,
           kGpsTxPin, static_cast<unsigned>(kGpsBaud));
}

enum class ButtonEvent { None, ShortPress, LongPress };

struct ButtonState {
  gpio_num_t pin{GPIO_NUM_NC};
  int raw{1};
  int stable{1};
  std::uint64_t changedMs{0};
  std::uint64_t pressedMs{0};
  bool longSent{false};
};

void initButton(ButtonState& button, const gpio_num_t pin) {
  button.pin = pin;
  gpio_config_t config{};
  config.pin_bit_mask = 1ULL << pin;
  config.mode = GPIO_MODE_INPUT;
  config.pull_up_en = GPIO_PULLUP_ENABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  ESP_ERROR_CHECK(gpio_config(&config));
  button.raw = gpio_get_level(pin);
  button.stable = button.raw;
}

ButtonEvent pollButton(ButtonState& button, const std::uint64_t now) {
  const int raw = gpio_get_level(button.pin);
  if (raw != button.raw) {
    button.raw = raw;
    button.changedMs = now;
  }
  if (raw != button.stable && now - button.changedMs >= 35) {
    button.stable = raw;
    if (raw == 0) {
      button.pressedMs = now;
      button.longSent = false;
    } else if (!button.longSent && now - button.pressedMs >= 50) {
      return ButtonEvent::ShortPress;
    }
  }
  if (button.stable == 0 && !button.longSent && now - button.pressedMs >= 2000) {
    button.longSent = true;
    return ButtonEvent::LongPress;
  }
  return ButtonEvent::None;
}

void handleButtonA(const ButtonEvent event, const std::uint64_t now) {
  const auto state = activitySnapshot(now).state;
  if (event == ButtonEvent::ShortPress) {
    if (state == gpstracker::RecordingState::Ready ||
        state == gpstracker::RecordingState::Finished) {
      beginActivity(currentFix(now), now);
    } else if (state == gpstracker::RecordingState::Recording) {
      portENTER_CRITICAL(&gStateMux);
      gActivity.pause(now);
      portEXIT_CRITICAL(&gStateMux);
      if (gTrackFile != nullptr) {
        std::fflush(gTrackFile);
      }
    } else if (state == gpstracker::RecordingState::Paused) {
      portENTER_CRITICAL(&gStateMux);
      gActivity.resume(now);
      portEXIT_CRITICAL(&gStateMux);
    }
  } else if (event == ButtonEvent::LongPress &&
             (state == gpstracker::RecordingState::Recording ||
              state == gpstracker::RecordingState::Paused)) {
    completeActivity(now);
  }
}

void handleButtonB(const ButtonEvent event) {
  if (event != ButtonEvent::ShortPress) {
    return;
  }
  portENTER_CRITICAL(&gStateMux);
  gActivity.toggleMode();
  portEXIT_CRITICAL(&gStateMux);
}

void drawOled() {
  const auto now = nowMs();
  const auto fix = currentFix(now);
  const auto activity = activitySnapshot(now);
  gpstracker::OledInput input;
  input.gpsValid = fix.usable();
  input.satellites = fix.satellites;
  input.activity = activity;
  if (!gStorageReady) {
    input.screen = gpstracker::OledScreen::StorageError;
  } else if (activity.state == gpstracker::RecordingState::Recording) {
    input.screen = fix.usable() ? gpstracker::OledScreen::Recording
                                : gpstracker::OledScreen::GpsLost;
  } else if (activity.state == gpstracker::RecordingState::Paused) {
    input.screen = gpstracker::OledScreen::Paused;
  } else if (activity.state == gpstracker::RecordingState::Finished) {
    input.screen = gpstracker::OledScreen::Summary;
  } else {
    input.screen = fix.usable() ? gpstracker::OledScreen::Ready
                                : gpstracker::OledScreen::Searching;
  }
  const auto layout = gpstracker::makeOledLayout(input);
  gOled.clear();
  const int titleX = std::max(0, (128 -
      (static_cast<int>(std::strlen(layout.title.data())) * 6 - 1)) / 2);
  gOled.drawAsciiText(titleX, 1, layout.title.data());
  gOled.drawHorizontalLine(10);
  if (layout.primaryNumeric) {
    gOled.drawPrimaryText(layout.primaryX, 14, layout.primary.data());
    const int unitX = std::max(0, (128 -
        (static_cast<int>(std::strlen(layout.unit.data())) * 6 - 1)) / 2);
    gOled.drawAsciiText(unitX, 36, layout.unit.data());
  } else {
    gOled.drawAsciiText(layout.primaryX, 20, layout.primary.data(), 2);
  }
  const int footerX = std::max(0, (128 -
      (static_cast<int>(std::strlen(layout.footer.data())) * 6 - 1)) / 2);
  gOled.drawAsciiText(footerX, 55, layout.footer.data());
  gOled.present();
}

void oledTask(void*) {
  while (true) {
    if (gOled.ready()) {
      drawOled();
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

}  // namespace

extern "C" void app_main() {
  gStorageMutex = xSemaphoreCreateMutex();
  ESP_ERROR_CHECK(gStorageMutex == nullptr ? ESP_ERR_NO_MEM : ESP_OK);
  initNvs();
  initStorage();
  initGps();
  ButtonState buttonA;
  ButtonState buttonB;
  initButton(buttonA, kButtonAPin);
  initButton(buttonB, kButtonBPin);
  if (!gOled.beginSpi(kOledMosiPin, kOledClockPin, kOledResetPin, kOledDcPin)) {
    ESP_LOGE(kLogTag, "SSD1306 SPI initialization failed");
  }
  startAccessPoint();
  startHttpServer();
  xTaskCreate(oledTask, "oled", 4096, nullptr, 4, nullptr);

  std::array<std::uint8_t, 256> bytes{};
  while (true) {
    const int count = uart_read_bytes(kGpsUart, bytes.data(), bytes.size(),
                                      pdMS_TO_TICKS(20));
    if (count > 0) {
      gGpsByteCount += static_cast<std::uint64_t>(count);
      if (!gGpsRawBytesLogged) {
        gGpsRawBytesLogged = true;
        ESP_LOG_BUFFER_HEXDUMP(kLogTag, bytes.data(),
                               std::min<std::size_t>(64, count), ESP_LOG_INFO);
      }
    }
    for (int index = 0; index < count; ++index) {
      const auto result = gParser.push(bytes[static_cast<std::size_t>(index)]);
      if (result != gpstracker::ParseResult::Updated) {
        continue;
      }
      const auto fix = gParser.fix();
      const auto now = nowMs();
      gpstracker::ObserveResult observed = gpstracker::ObserveResult::NotRecording;
      portENTER_CRITICAL(&gStateMux);
      gFix = fix;
      gLastGpsUpdateMs = now;
      observed = gActivity.observe(fix, now);
      portEXIT_CRITICAL(&gStateMux);
      if (observed == gpstracker::ObserveResult::Accepted) {
        writeTrackPoint(fix);
      }
    }
    const auto now = nowMs();
    handleButtonA(pollButton(buttonA, now), now);
    handleButtonB(pollButton(buttonB, now));
    if (now - gLastGpsDiagnosticMs >= 2000) {
      gLastGpsDiagnosticMs = now;
      const auto fix = currentFix(now);
      ESP_LOGI(kLogTag,
               "GPS DIAG bytes=%llu nmea=%u checksum=%u malformed=%u "
               "location=%d time=%d date=%d sats=%u hdop=%.1f",
               static_cast<unsigned long long>(gGpsByteCount),
               static_cast<unsigned>(gParser.validSentences()),
               static_cast<unsigned>(gParser.checksumErrors()),
               static_cast<unsigned>(gParser.malformedSentences()),
               fix.locationValid ? 1 : 0, fix.timeValid ? 1 : 0,
               fix.dateValid ? 1 : 0,
               static_cast<unsigned>(fix.satellites), fix.hdop);
    }
  }
}
