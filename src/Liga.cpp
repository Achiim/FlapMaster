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
#include <stdio.h>
#include <string.h>
#include "ArduinoJson.h"
#include "secret.h"
#include "FlapTasks.h"
#include "Liga.h"
#include "cert.all"

#include "esp_http_client.h"

#define WIFI_SSID "DEIN_SSID"
#define WIFI_PASS "DEIN_PASS"

// initialize global variables
std::string jsonBuffer                   = "";
String      currentLastChangeOfMatchday  = "";                                  // openLigaDB Matchday change state
String      previousLastChangeOfMatchday = "";                                  // openLigaDB Matchday change state
std::string dateTimeBuffer               = "";                                  // temp buffer to request
bool        currentMatchdayChanged       = false;                               // no chances

League activeLeague = League::BL1;                                              // use default BL1
int    ligaSeason   = 0;                                                        // global unknown Season
int    ligaMatchday = 0;                                                        // global unknown Matchday

time_t      currentNextKickoffTime  = 0;
time_t      previousNextKickoffTime = 0;
bool        nextKickoffChanged      = false;
bool        nextKickoffFarAway      = true;
double      diffSecondsUntilKickoff = 0;
std::string nextKickoffString;

PollMode         currentPollMode   = POLL_MODE_RELAXED;                         // global poll mode of poll mananger
PollMode         nextPollMode      = POLL_MODE_RELAXED;
const PollScope* activeCycle       = nullptr;
size_t           activeCycleLength = 0;

LigaSnapshot snap[2];                                                           // actual and previous table
uint8_t      snapshotIndex = 0;                                                 // 0 or 1

// Routines
const char* pollModeToString(PollMode mode) {
    switch (mode) {
        case POLL_MODE_LIVE:
            return "POLL_LIVE";
        case POLL_MODE_PRELIVE:
            return "POLL_PRELIVE";
        case POLL_MODE_REACTIVE:
            return "POLL_REACTIVE";
        case POLL_MODE_RELAXED:
            return "POLL_RELAXED";
        default:
            return "unknown Poll Mode";
    }
}

const char* pollScopeToString(PollScope scope) {
    switch (scope) {
        case CHECK_FOR_CHANGES:
            return "CHECK_FOR_CHANGES";
        case FETCH_TABLE:
            return "FETCH_TABLE";
        case FETCH_CURRENT_MATCHDAY:
            return "FETCH_CURRENT_MATCHDAY";
        case FETCH_CURRENT_SEASON:
            return "FETCH_CURRENT_SEASON";
        case FETCH_NEXT_KICKOFF:
            return "FETCH_NEXT_KICKOFF";
        case FETCH_NEXT_MATCH:
            return "FETCH_NEXT_MATCH";
        case FETCH_LIVE_MATCHES:
            return "FETCH_NEXT_KICKOFF";
        case FETCH_GOALS:
            return "FETCH_GOALS";
        case SHOW_NEXT_KICKOFF:
            return "SHOW_NEXT_KICKOFF";
        case CALC_LEADER_CHANGE:
            return "CALC_LEADER_CHANGE";
        case CALC_RELEGATION_GHOST_CHANGE:
            return "CALC_RELEGATION_GHOST_CHANGE";
        case CALC_RED_LANTERN_CHANGE:
            return "CALC_RED_LANTERN_CHANGE";
        case CALC_GOALS:
            return "CALC_RED_LANTERN_CHANGE";
        default:
            return "UNKNOWN_SCOPE";
    }
}

String pollCycleToString(const PollScope* cycle, size_t length) {
    String result = "{";
    for (size_t i = 0; i < length; ++i) {
        result += pollScopeToString(cycle[i]);
        if (i < length - 1)
            result += ", ";
    }
    result += "}";
    return result;
}

void selectPollCycle(PollMode mode) {
    switch (mode) {
        case POLL_MODE_LIVE:
            activeCycle       = liveCycle;
            activeCycleLength = sizeof(liveCycle) / sizeof(PollScope);
            break;
        case POLL_MODE_REACTIVE:
            activeCycle       = reactiveCycle;
            activeCycleLength = sizeof(reactiveCycle) / sizeof(PollScope);
            break;
        case POLL_MODE_PRELIVE:
            activeCycle       = preLiveCycle;
            activeCycleLength = sizeof(preLiveCycle) / sizeof(PollScope);
            break;
        case POLL_NORMAL:
        default:
            activeCycle       = relaxedCycle;
            activeCycleLength = sizeof(relaxedCycle) / sizeof(PollScope);
            break;
    }
    Liga->ligaPrintln("PollScope = %s", pollCycleToString(activeCycle, activeCycleLength).c_str());
}

PollMode determinePollMode(time_t nextKickoff) {
    time_t now = time(nullptr);
    if (nextKickoff == 0)
        return POLL_MODE_RELAXED;

    double diff = difftime(nextKickoff, now);
    if (diff <= 0)
        return POLL_MODE_LIVE;
    if (diff <= 600)
        return POLL_MODE_PRELIVE;
    return POLL_MODE_RELAXED;
}

bool checkForMatchdayChanges() {
    return currentMatchdayChanged;
}

bool tableChanged() {
    // Vergleich mit vorheriger Tabellenstruktur
    return true;                                                                // Dummy
}

bool kickoffChanged() {
    return nextKickoffChanged;
}

uint32_t getPollDelay(PollMode mode) {
    uint32_t polltime = 0;
    switch (mode) {
        case POLL_MODE_LIVE:
            polltime = POLL_DURING_GAME;
            break;
        case POLL_MODE_REACTIVE:
            polltime = POLL_GET_ALL_CHANGES;
            break;
        case POLL_MODE_PRELIVE:
            polltime = POLL_10MIN_BEFORE_KICKOFF;
            break;
        case POLL_MODE_RELAXED:
            polltime = POLL_NORMAL;
            break;
        default:
            polltime = POLL_NORMAL;
            break;
    }
    int totalseconds = polltime / 1000;
    int min          = totalseconds / 60;
    int sec          = totalseconds % 60;
    Liga->ligaPrintln("PollManager will wait for %d:%02d minutes:seconds", min, sec);
    return polltime;
}

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

// only show next kickoff from stored data (no openLigaDB access)
void showNextKickoff() {
    #ifdef LIGAVERBOSE
        {
        TraceScope trace;
        Liga->ligaPrintln("next Kickoff: %s", nextKickoffString.c_str());
        }
    #endif
    time_t now              = time(NULL);
    diffSecondsUntilKickoff = difftime(currentNextKickoffTime, now);

    int totalMinutes = static_cast<int>(diffSecondsUntilKickoff / 60);
    int days         = totalMinutes / (24 * 60);
    int hours        = (totalMinutes % (24 * 60)) / 60;
    int minutes      = totalMinutes % 60;

    #ifdef LIGAVERBOSE
        {
        TraceScope trace;
        Liga->ligaPrintln("next Kickoff in %d days, %02d hours, %02d minutes", days, hours, minutes);
        }
    #endif
}

// ---------- getestet und funktioniert----------------------
// Event-Handler to get kickoff date of next upcoming match
esp_err_t _http_event_handler_nextKickoff(esp_http_client_event_t* evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        jsonBuffer.append((char*)evt->data, evt->data_len);
    }

    if (evt->event_id == HTTP_EVENT_ON_FINISH) {
        size_t               jsonSize = jsonBuffer.length();
        DynamicJsonDocument  doc(jsonSize * 1.2);
        DeserializationError error = deserializeJson(doc, jsonBuffer);

        if (!error) {
            nextKickoffString = doc["matchDateTime"].as<std::string>();         // time string from request
            if (!nextKickoffString.empty()) {
                struct tm tmKickoff = {};
                strptime(nextKickoffString.c_str(), "%Y-%m-%dT%H:%M:%S", &tmKickoff); //  Umwandlung in time_t
                currentNextKickoffTime = mktime(&tmKickoff);                    // save next Kickoff time
                if (currentNextKickoffTime != previousNextKickoffTime)
                    nextKickoffChanged = true;
                else
                    nextKickoffChanged = false;

                previousNextKickoffTime = currentNextKickoffTime;
                showNextKickoff();

                if (diffSecondsUntilKickoff >= 0 && diffSecondsUntilKickoff <= 600) {
                    nextKickoffFarAway = false;                                 // kickoff in less then 10 Minutes
                } else if (diffSecondsUntilKickoff < 0 && diffSecondsUntilKickoff >= -7200) {
                    nextKickoffFarAway = false;                                 // match is live or even over ( kickoff < 2h)
                } else {
                    nextKickoffFarAway = true;                                  // kickoff will be far away in the future
                }

            } else {
                #ifdef LIGAVERBOSE
                    {
                    Liga->ligaPrintln("next Kickoff missing or empty");
                    }
                #endif
            }

        } else {
            #ifdef ERRORVERBOSE
                {
                Liga->ligaPrintln("JSON-Fehler (Kickoff): %s", error.c_str());
                }
            #endif
        }
        jsonBuffer = "";
    }
    return ESP_OK;
}

// Ermittlung des nächsten Anpfiffzeitpunkt
bool LigaTable::pollNextKickoff() {
    char url[128];                                                              // ausreichend groß für die komplette URL
    sprintf(url, "https://api.openligadb.de/getnextmatchbyleagueshortcut/%s", leagueShortcut(activeLeague));
    esp_http_client_config_t config = {};
    config.url                      = url;
    config.user_agent               = flapUserAgent;
    config.event_handler            = _http_event_handler_nextKickoff;
    config.cert_pem                 = OPENLIGA_CA;
    config.buffer_size              = 2048;
    config.timeout_ms               = 5000;

    #ifdef LIGAVERBOSE
        {
        TraceScope trace;
        ligaPrintln("get next kickoff time");
        }
    #endif

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
        return false;

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        #ifdef LIGAVERBOSE
            {
            TraceScope trace;
            ligaPrintln("HTTP-Fehler: %s\n", esp_err_to_name(err));
            }
        #endif
        return false;
    }
    esp_http_client_cleanup(client);
    if (nextKickoffChanged) {
        #ifdef LIGAVERBOSE
            {
            TraceScope trace;
            ligaPrintln("kickoff time has changed");
            }
        #endif
    }
    return true;
}

// Event-Handler zur Ermittlung der letzten Änderung des aktuellen Spieltags
esp_err_t _http_event_handler_changes(esp_http_client_event_t* evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        dateTimeBuffer.append((char*)evt->data, evt->data_len);
    }

    if (evt->event_id == HTTP_EVENT_ON_FINISH) {
        currentLastChangeOfMatchday = dateTimeBuffer.c_str();
        currentLastChangeOfMatchday.remove(0, 1);                               // entfernt das erste Zeichen (das Anführungszeichen)
        currentLastChangeOfMatchday.remove(19);                                 // entfernt alles ab Position 19 (also ".573")
        dateTimeBuffer.clear();
    }

    return ESP_OK;
}

// Ermittlung der letzten Änderung des aktuellen Spieltags
bool LigaTable::pollForChanges() {
    char url[128];                                                              // ausreichend groß für die komplette URL
    sprintf(url, "https://api.openligadb.de/getlastchangedate/%s/%d/%d", leagueShortcut(activeLeague), ligaSeason, ligaMatchday);
    esp_http_client_config_t config = {};
    config.url                      = url;
    config.event_handler            = _http_event_handler_changes;
    config.user_agent               = flapUserAgent;
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
        currentMatchdayChanged = false;                                         // no change detected
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
        currentMatchdayChanged = false;                                         // no change detected
        return false;
    }
    esp_http_client_cleanup(client);
    if (previousLastChangeOfMatchday != currentLastChangeOfMatchday) {
        currentMatchdayChanged = true;
        #ifdef LIGAVERBOSE
            {
            TraceScope trace;
            ligaPrintln("OpenLigaDB has changes against my local data, please fetch actual data");
            }
        #endif
    } else {
        currentMatchdayChanged = false;
        #ifdef LIGAVERBOSE
            {
            TraceScope trace;
            ligaPrintln("OpenLigaDB has no changes");
            }
        #endif
    }
    previousLastChangeOfMatchday = currentLastChangeOfMatchday;                 // remember actual chang as last change
    #ifdef LIGAVERBOSE
        struct tm tmLastChange = {};
        tmLastChange.tm_isdst  = -1;                                            // automatische Sommerzeit-Erkennung
        strptime(currentLastChangeOfMatchday.c_str(), "%Y-%m-%dT%H:%M:%S", &tmLastChange);
        time_t lastChangeTime = mktime(&tmLastChange);
        time_t now            = time(NULL);
        double diffSeconds    = difftime(now, lastChangeTime);
        int    totalMinutes   = static_cast<int>(diffSeconds / 60);
        int    days           = totalMinutes / (24 * 60);
        int    hours          = (totalMinutes % (24 * 60)) / 60;
        int    minutes        = totalMinutes % 60;
        int    seconds        = static_cast<int>(diffSeconds) % 60;
        {
        TraceScope trace;
        ligaPrintln("last change date of matchday %d in openLigaDB: %s", ligaMatchday, currentLastChangeOfMatchday.c_str());
        ligaPrintln("this was was %d days, %d hours, %d minutes, %d seconds ago", days, hours, minutes, seconds);
        }
    #endif
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
        if (!error) {
            snapshotIndex ^= 1;                                                 // Toggle between 0 und 1
            LigaSnapshot& snapshot = snap[snapshotIndex];                       // local point to snap to be used
            snapshot.clear();                                                   // Reset actual-Snapshot

            JsonArray table    = doc.as<JsonArray>();                           // read JSON-Data into snap[snapshotIndex]
            snapshot.teamCount = table.size();                                  // number of teams

            snap[snapshotIndex].season       = ligaSeason;
            snap[snapshotIndex].matchday     = ligaMatchday;
            snap[snapshotIndex].fetchedAtUTC = time(nullptr);                   // actual timestamp
            for (uint8_t i = 0; i < snapshot.teamCount && i < LIGA_MAX_TEAMS; ++i) {
                JsonObject team = table[i];
                LigaRow&   row  = snapshot.rows[i];

                row.pos    = i + 1;
                row.sp     = team["matches"] | 0;
                row.pkt    = team["points"] | 0;
                row.w      = team["won"] | 0;
                row.l      = team["lost"] | 0;
                row.d      = team["draw"] | 0;
                row.g      = team["goals"] | 0;
                row.og     = team["opponentGoals"] | 0;
                row.diff   = team["goalDiff"] | 0;
                row.flap   = i + 1;                                             // Beispiel: Position auf Flap-Display
                row.dfb[0] = '\0';                                              // erst mal nix

                const char* name = team["teamName"] | "";                       // liefert "" wenn null
                strncpy(row.team, name ? name : "", sizeof(row.team) - 1);
                row.team[sizeof(row.team) - 1] = '\0';

                // Serial.printf("Platz %d: %s Punkte: %d TD: %d\n", row.pos, row.team, row.pkt, row.diff);
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
    char url[128];                                                              // ausreichend groß für die komplette URL
    sprintf(url, "https://api.openligadb.de/getbltable/%s/%d", leagueShortcut(activeLeague), ligaSeason);
    esp_http_client_config_t config = {};
    config.url                      = url;
    config.user_agent               = flapUserAgent;
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
    sprintf(url, "https://api.openligadb.de/getcurrentgroup/%s", leagueShortcut(activeLeague));
    esp_http_client_config_t config = {};
    config.url                      = url;
    config.event_handler            = _http_event_handler_matchday;
    config.user_agent               = flapUserAgent;
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
