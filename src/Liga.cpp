// ###################################################################################################
//
//    ██      ██  ██████   █████
//    ██      ██ ██       ██   ██
//    ██      ██ ██   ███ ███████
//    ██      ██ ██    ██ ██   ██
//    ███████ ██  ██████  ██   ██
//
////
// ################################################################################################## by Achim ####
// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=ANSI%20Regular&t=LIGA
/*



*/
#include <Arduino.h>
#include <time.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "ArduinoJson.h"
#include <StreamUtils.h>                                                        // 🔧 ChunkedDecodingStream
#include "secret.h"
#include "FlapTasks.h"
#include "Liga.h"
#include "cert.all"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
// #include "esp_wifi.h"
// #include "esp_event.h"
// #include "nvs_flash.h"
#include "esp_http_client.h"

#define WIFI_SSID "DEIN_SSID"
#define WIFI_PASS "DEIN_PASS"

// initialize global variables
std::string currentLastChange  = "";                                            //
std::string previousLastChange = "";                                            //
std::string dateTimeBuffer     = "";                                            // special situation last c hange date is not JSON, just sting

String      lastMatchChecksum = "";
String      currentChecksum   = "";
std::string jsonBuffer        = "";
String      currentGroupName  = "";

int ligaSeason   = 0;                                                           // global actual Season
int ligaMatchday = 0;                                                           // global actual Matchday

// Routines
bool sendRequest(const String& url, String& response) {
    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode > 0) {
        response = http.getString();
        http.end();
        return true;
    } else {
        Serial.printf("HTTP Fehler: %s\n", http.errorToString(httpCode).c_str());
        http.end();
        return false;
    }
}

// --- Construct Liga object BEFORE any usage --------------------------------
void createLigaInstance() {
    if (!Liga) {
        Liga = new LigaTable();                                                 // Allocate LigaTable instance on heap (global pointer)
    }
}

void configureTime() {
    #ifdef LIGAVERBOSE
        {
        TraceScope trace;
        Liga->ligaPrintln("Configure time zone: CET with daylight-saving rules (Berlin style)"); ///< Pure log for visibility
        }
    #endif

    // --- Timezone + NTP sync ---------------------------------------------------
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov"); // Configure CET/CEST with common NTP servers
    waitForTime();                                                              // Block until system time is synced (bounded wait with logging)
}

// --- Init Liga Task  ---------------------------------------------------
bool initLigaTask() {
    createLigaInstance();
    if (!connectToWifi()) {                                                     // Ensure WiFi/TLS preconditions for OpenLigaDB are met
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(300));

    configureTime();

    if (!openLigaDBHealthCheck()) {
        return false;                                                           // ggf. Retry oder Offline-Modus aktivieren
    }
    vTaskDelay(pdMS_TO_TICKS(400));
    ligaSeason = getCurrentSeason();
    return true;
}

void printTime(const char* label) {
    time_t now = time(nullptr);
    if (now < 100000) {
        Liga->ligaPrintln("[%s] time not set (epoch=%ld)", label, (long)now);
    } else {
        struct tm tmLoc;
        localtime_r(&now, &tmLoc);                                              // Epoch -> lokale Zeit
        char buf[32];
        // eigenes Format: TT.MM.JJJJ hh:mm:ss TZ
        strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M:%S %Z", &tmLoc);
        Liga->ligaPrintln("[%s] %s", label, buf);
    }
}
int getCurrentSeason() {
    time_t     now      = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    int        season   = timeinfo->tm_year + 1900;
    // Saison beginnt meist im August → bei Monat < 8: Vorjahr
    if (timeinfo->tm_mon < 7) {
        season -= 1;
    }
    Liga->ligaPrintln("actuell season: %d", season);
    return season;
}
bool openLigaDBHealthCheck() {
    IPAddress ip;                                                               // Temp buffer for DNS resolution logging
    if (WiFi.hostByName("api.openligadb.de", ip)) {
        Liga->ligaPrintln("DNS ok: %s -> %s", "api.openligadb.de", ip.toString().c_str()); // Helpful diagnostic for name resolution
    } else {
        Liga->ligaPrintln("DNS failed!");                                       // DNS failure hint (will affect subsequent HTTP)
    }

    String response;
    bool   success = sendRequest("https://api.openligadb.de/getavailablesports", response);
    return success && response.length() > 0;
}
/**
 * @brief Wait until system time has been synchronized (NTP).
 *
 * This function polls the system time until it reaches a reasonable
 * value (after epoch ~2023-11-14, i.e. > 1700000000 seconds) or
 * until the timeout expires.
 *
 * It is typically used at startup after WiFi connection to ensure
 * that NTP synchronization has completed before making HTTPS requests.
 *
 * @param maxMs Maximum time to wait in milliseconds (default: 15000 ms).
 * @return True if time was successfully synchronized before timeout,
 *         false otherwise.
 */
bool waitForTime(uint32_t maxMs, bool report) {
    time_t    now = 0;
    struct tm timeinfo;
    uint32_t  start = millis();
    while ((millis() - start) < maxMs) {
        now = time(nullptr);
        if (now > 1600000000 && getLocalTime(&timeinfo)) {                      // Zeit nach 2020 und gültig
            if (report)
                printTime("waitForTime");
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (report)
        printTime("waitForTime");
    return false;
}

// =========== nicht fertig =======================================

esp_err_t _http_event_handler_match(esp_http_client_event_t* evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        Serial.printf("Chunk empfangen: %d Bytes\n", evt->data_len);
        jsonBuffer.append((char*)evt->data, evt->data_len);
        Serial.printf("Buffergröße vor JSON: %d\n", jsonBuffer.length());
    }

    if (evt->event_id == HTTP_EVENT_ON_FINISH) {
        DynamicJsonDocument  doc(10 * 1024);
        DeserializationError error = deserializeJson(doc, jsonBuffer);
        if (!error) {
            currentChecksum = "";                                               // Reset

            for (JsonObject match : doc.as<JsonArray>()) {
                String matchId = String((int)match["matchID"]);
                bool   isLive  = match["matchIsLive"];
                int    goals   = match["matchResults"].size();

                currentChecksum += matchId + String(isLive) + String(goals);
            }
        } else {
            Serial.printf("JSON-Fehler: %s\n", error.c_str());
        }

        jsonBuffer = "";                                                        // Buffer leeren
    }

    return ESP_OK;
}

void pollNextMatch() {
    String response;
    String url = "https://api.openligadb.de/getnextmatch/bl1/2025";
    if (sendRequest(url, response)) {
        Serial.println("Nächstes Spiel erhalten");
        // parseJsonNextMatch(response);
    }
}

// ---------- getestet und funktioniert----------------------
// Event-Handler zur Ermittlung der letzten Änderung des aktuellen Spieltags
esp_err_t _http_event_handler_changes(esp_http_client_event_t* evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        dateTimeBuffer.append((char*)evt->data, evt->data_len);
    }

    if (evt->event_id == HTTP_EVENT_ON_FINISH) {
        previousLastChange = currentLastChange;
        currentLastChange  = dateTimeBuffer;
        dateTimeBuffer.clear();
    }

    return ESP_OK;
}

// Ermittlung der letzten Änderung des aktuellen Spieltags
bool LigaTable::pollForChanges(bool& outchanged) {
    outchanged = false;
    char url[128];                                                              // ausreichend groß für die komplette URL
    sprintf(url, "https://api.openligadb.de/getlastchangedate/bl1/%d/%d", ligaSeason, ligaMatchday);
    esp_http_client_config_t config = {};
    config.url                      = url;
    config.event_handler            = _http_event_handler_changes;
    config.cert_pem                 = OPENLIGA_CA;
    config.buffer_size              = 100;
    config.timeout_ms               = 5000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        #ifdef ERRORVERBOSE
            {
            TraceScope trace;
            ligaPrintln("HTTP-Client error");
            }
        #endif
        outchanged = false;                                                     // no change detected
        return false;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        #ifdef ERRORVERBOSE
            {
            TraceScope trace;
            ligaPrintln("HTTP-Fehler: %s", esp_err_to_name(err));
            }
        #endif

        outchanged = false;                                                     // no change detected
        return false;
    }
    esp_http_client_cleanup(client);
    if (previousLastChange != currentLastChange) {
        outchanged = true;
        #ifdef ERRORVERBOSE
            {
            TraceScope trace;
            ligaPrintln("Letzte Änderung: %s", currentLastChange.c_str());
            }
        #endif
    }
    return err == ESP_OK;
}

// zur Ermittlung des aktuellen Spieltags
esp_err_t _http_event_handler_matchday(esp_http_client_event_t* evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        jsonBuffer.append((char*)evt->data, evt->data_len);
    }

    if (evt->event_id == HTTP_EVENT_ON_FINISH) {
        DynamicJsonDocument  doc(512);
        DeserializationError error = deserializeJson(doc, jsonBuffer);
        if (!error) {
            ligaMatchday = doc["groupOrderID"];
        } else {
            //    Serial.printf("JSON-Fehler (Spieltag): %s\n", error.c_str());
        }

        jsonBuffer.clear();
    }
    return ESP_OK;
}

// zur Ermittlung der aktuellen Tabelle
esp_err_t _http_event_handler_table(esp_http_client_event_t* evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        jsonBuffer.append((char*)evt->data, evt->data_len);
    }

    if (evt->event_id == HTTP_EVENT_ON_FINISH) {
        size_t               jsonSize = jsonBuffer.size();                      // oder evt->data_len aufsummiert
        DynamicJsonDocument  doc(jsonSize * 1.2);
        DeserializationError error = deserializeJson(doc, jsonBuffer);
        int                  r     = 1;
        if (!error) {
            JsonArray table = doc.as<JsonArray>();
            for (JsonObject team : table) {
                int         rank   = r++;
                const char* name   = team["teamName"];
                const char* nick   = team["shortName"];
                int         points = team["points"];
                int         td     = team["goalDiff"];
                Serial.printf("Platz %d: %s - %s Punkte: %d TD: %d\n", rank, name, nick, points, td);
            }
        } else {
            Serial.printf("JSON-Fehler (Tabelle): %s\n", error.c_str());
        }

        jsonBuffer = "";                                                        // Buffer leeren
    }
    return ESP_OK;
}

// zur Ermittlung der aktuellen Tabelle
bool LigaTable::pollTable() {
    esp_http_client_config_t config = {};
    config.url                      = "https://api.openligadb.de/getbltable/bl1/2025";
    config.event_handler            = _http_event_handler_table;
    config.cert_pem                 = OPENLIGA_CA;
    config.buffer_size              = 2048;
    config.timeout_ms               = 5000;

    #ifdef LIGAVERBOSE
        {
        TraceScope trace;
        ligaPrintln("get actual Liga-Table (season = %d, matchday = %d)", ligaSeason, ligaMatchday);
        }
    #endif

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
        return false;

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        Serial.printf("HTTP-Fehler: %s\n", esp_err_to_name(err));
        return false;
    }
    esp_http_client_cleanup(client);
    return err == ESP_OK;
}

// zur Ermittlung des aktellen Spieltags
bool LigaTable::pollCurrentMatchday() {
    char url[128];                                                              // ausreichend groß für die komplette URL
    sprintf(url, "https://api.openligadb.de/getcurrentgroup/bl1");
    esp_http_client_config_t config = {};
    config.url                      = url;
    config.event_handler            = _http_event_handler_matchday;
    config.cert_pem                 = OPENLIGA_CA;
    config.buffer_size              = 100;
    config.timeout_ms               = 5000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
        return false;

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        #ifdef ERRORVERBOSE
            {
            TraceScope trace;
            ligaPrintln("HTTP-Fehler: %s\n", esp_err_to_name(err));
            }
        #endif
        return false;
    }
    esp_http_client_cleanup(client);
    #ifdef LIGAVERBOSE
        {
        TraceScope trace;
        ligaPrintln("actual matchday: %d", ligaMatchday);
        }
    #endif

    return err == ESP_OK && ligaMatchday > 0;
}

/**
 * @brief Constructor for the LigaTable class.
 *
 * Initializes time synchronization, clears internal snapshot buffers,
 * and sets the active buffer index.
 *
 * Behavior:
 *  - Calls `configTzTime()` to configure the system time with CET/CEST
 *    rules and synchronize against NTP servers (pool.ntp.org, time.nist.gov).
 *  - Clears both snapshot buffers `buf_[0]` and `buf_[1]` to ensure a
 *    defined empty state at startup.
 *  - Initializes the `active_` atomic with 0, indicating which buffer
 *    is currently active.
 *
 * @note This sets up the environment so that all match date/time
 *       conversions are done correctly in the CET/CEST timezone,
 *       and provides a double-buffer system for safe concurrent access.
 */
LigaTable::LigaTable() {}

/**
 * @brief Establish a WiFi connection for LigaTable.
 *
 * This method ensures that the ESP32 is connected to the configured WiFi
 * network. If already connected, it returns immediately. Otherwise, it
 * attempts to connect using the credentials (`ssid`, `password`).
 *
 * Behavior:
 *  - If `WiFi.status()` is already `WL_CONNECTED`, return `true`.
 *  - Set WiFi mode to station (`WIFI_STA`) and disable WiFi sleep to
 *    avoid sporadic TLS errors when waking up.
 *  - Print "Verbinde mit WLAN" to indicate the connection attempt.
 *  - Call `WiFi.begin()` with the configured SSID and password.
 *  - Poll the WiFi status every 200 ms until connected or timeout
 *    after 15 seconds.
 *  - If timeout occurs, log a message and return `false`.
 *  - On success, return `true`.
 *
 * @return true if WiFi is connected, false if connection failed or timed out.
 */
bool connectToWifi() {
    if (WiFi.status() == WL_CONNECTED)                                          // already connected
        return true;
    WiFi.mode(WIFI_STA);                                                        // set station mode (client)
    WiFi.setSleep(false);                                                       // disable power-save mode to avoid TLS wake issues
    #ifdef LIGAVERBOSE
        {
        TraceScope trace;
        Liga->ligaPrintln("connect to WLAN");                                   // log start of connection attempt
        }
    #endif

    WiFi.begin(ssid, password);                                                 // start WiFi connection
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {                                     // wait until connected
        delay(200);
        if (millis() - t0 > 15000) {                                            // check for 15 s timeout
            Liga->ligaPrintln("WLAN-Timeout (15s)");
            return false;                                                       // fail after timeout
        }
    }
    return true;                                                                // success
}
