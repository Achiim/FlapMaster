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

time_t             currentNextKickoffTime  = 0;
time_t             previousNextKickoffTime = 0;
bool               nextKickoffChanged      = false;
bool               nextKickoffFarAway      = true;
bool               matchIsLive             = false;
bool               isSomeThingNew          = false;
int                ligaMatchLiveCount      = 0;                                 // number of live matches in current matchday
int                ligaNextMatchCount      = 0;                                 // number of next matches in current matchday
LiveMatchInfo      liveMatches[30]         = {};                                // max. 10 Live-Spiele
LiveMatchGoalInfo  goalsInfos[30]          = {};                                // max. 10 live matches with goals
LiveMatchGoalInfo* currentGoalInfo         = nullptr;
double             diffSecondsUntilKickoff = 0;
std::string        nextKickoffString;

PollMode         currentPollMode           = POLL_MODE_NONE;                    // global poll mode of poll mananger
PollMode         nextPollMode              = POLL_MODE_NONE;
const PollScope* activeCycle               = nullptr;
size_t           activeCycleLength         = 0;
uint32_t         pollManagerDynamicWait    = 0;                                 // wait time according to current poll mode
uint32_t         pollManagerStartOfWaiting = 0;                                 // time_t when entering waiting

LigaSnapshot snap[2];                                                           // actual and previous table
uint8_t      snapshotIndex = 0;                                                 // 0 or 1

// Routines
const char* pollModeToString(PollMode mode) {
    switch (mode) {
        case POLL_MODE_LIVE:
            return "POLL_MODE_LIVE";
        case POLL_MODE_PRELIVE:
            return "POLL_MODE_PRELIVE";
        case POLL_MODE_REACTIVE:
            return "POLL_MODE_REACTIVE";
        case POLL_MODE_RELAXED:
            return "POLL_MODE_RELAXED";
        case POLL_MODE_ONCE:
            return "POLL_MODE_ONCE";
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
        case CALC_CURRENT_SEASON:
            return "CALC_CURRENT_SEASON";
        case FETCH_NEXT_KICKOFF:
            return "FETCH_NEXT_KICKOFF";
        case FETCH_NEXT_MATCH_LIST:
            return "FETCH_NEXT_MATCH_LIST";
        case FETCH_LIVE_MATCHES:
            return "FETCH_LIVE_MATCHES";
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
        case POLL_MODE_ONCE:
            activeCycle       = onceCycle;
            activeCycleLength = sizeof(onceCycle) / sizeof(PollScope);
            break;
        case POLL_MODE_RELAXED:
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
        case POLL_MODE_ONCE:
            polltime = POLL_NOWAIT;
            break;
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
    Liga->ligaPrintln("PollManager is in mode %S and will wait for %d:%02d minutes", pollModeToString(currentPollMode), min, sec);
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
    bool synchStatus = waitForTime(15000, true);                                // Block until system time is synced (bounded wait with logging)
    if (!synchStatus) {
        Liga->ligaPrintln("Time NOT synchronized within timeout, retry later!"); // Log failure to sync time
    }

    #ifdef LIGAVERBOSE
        {
        time_t     now      = time(NULL);                                       // get local time as time_t
        struct tm* timeinfo = localtime(&now);                                  // convert to local time structure
        char       buffer[32];
        strftime(buffer, sizeof(buffer), "%d.%m.%Y %H:%M:%S", timeinfo);
        TraceScope trace;
        Liga->ligaPrintln("actual synchronized time %s", buffer);               // show time
        }
    #endif
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
int calcCurrentSeason() {
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
esp_err_t _http_event_handler_pollForNextMatchList(esp_http_client_event_t* evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (evt->data_len > 0) {
                jsonBuffer.append((char*)evt->data, evt->data_len);
            }
            break;

        case HTTP_EVENT_ON_FINISH: {
            DynamicJsonDocument  doc(jsonBuffer.length() * 1.2);
            DeserializationError error = deserializeJson(doc, jsonBuffer);
            if (error) {
                Liga->ligaPrintln("pollForNextMatchList JSON-Error at matchday data: %s", error.c_str());
                jsonBuffer.clear();
                break;
            }

            JsonArray matches = doc.as<JsonArray>();
            time_t    now     = time(nullptr);
            time_t    minDiff = INT32_MAX;

            // Erste Runde: minimalen Abstand zum aktuellen Zeitpunkt finden
            for (JsonObject match : matches) {
                const char* kickoffStr = match["matchDateTime"] | "";
                if (strlen(kickoffStr) == 0)
                    continue;

                struct tm tm = {};
                strptime(kickoffStr, "%Y-%m-%dT%H:%M:%S", &tm);
                tm.tm_isdst    = -1;                                            // System soll Sommerzeit selbst erkennen
                time_t kickoff = mktime(&tm);
                time_t diff    = kickoff > now ? kickoff - now : INT32_MAX;     // nur zukünftige Spiele

                if (diff < minDiff) {
                    minDiff = diff;                                             // neuer minimaler Abstand
                }
            }

            // Zweite Runde: alle Spiele mit minimalem Abstand sammeln
            ligaNextMatchCount = 0;
            for (JsonObject match : matches) {
                const char* kickoffStr = match["matchDateTime"] | "";
                uint32_t    matchID    = match["matchID"] | 0;
                if (strlen(kickoffStr) == 0 || matchID == 0)
                    continue;

                struct tm tm = {};
                strptime(kickoffStr, "%Y-%m-%dT%H:%M:%S", &tm);
                tm.tm_isdst    = -1;
                time_t kickoff = mktime(&tm);
                time_t diff    = kickoff > now ? kickoff - now : INT32_MAX;

                if (kickoff <= now)
                    continue;                                                   // vergangenes Spiel ignorieren

                if (diff == minDiff && ligaNextMatchCount < LIGA_MAX_TEAMS) {   // alle Spiele mit minimalem Abstand
                    liveMatches[ligaNextMatchCount].matchID = matchID;
                    liveMatches[ligaNextMatchCount].kickoff = kickoff;

                    const char* team1                     = match["team1"]["teamName"] | "Team A";
                    const char* team2                     = match["team2"]["teamName"] | "Team B";
                    liveMatches[ligaNextMatchCount].team1 = team1;
                    liveMatches[ligaNextMatchCount].team2 = team2;

                    Liga->ligaPrintln("Match %u: %s vs %s, Kickoff %s", matchID, team1, team2, kickoffStr);
                    ligaNextMatchCount++;
                }
            }

            Liga->ligaPrintln("found next matches: %d", ligaNextMatchCount);
            jsonBuffer.clear();
            break;
        }

        case HTTP_EVENT_ERROR:
            Liga->ligaPrintln("error while looking for next matches");
            break;
    }

    return ESP_OK;
}

bool LigaTable::pollForNextMatchList(int matchdayOffset) {
    if (ligaMatchday + matchdayOffset >= 34 || ligaMatchday + matchdayOffset < 1) {
        Liga->ligaPrintln("pollForNextMatchList: invalide %d (current matchday %d)", ligaMatchday + matchdayOffset, ligaMatchday);
        return false;
    }
    char url[128];                                                              // ausreichend groß für die komplette URL
    sprintf(url, "https://api.openligadb.de/getmatchdata/%s/%d/%d", leagueShortcut(activeLeague), ligaSeason, ligaMatchday + matchdayOffset);

    esp_http_client_config_t config = {};
    config.url                      = url;
    config.host                     = "api.openligadb.de";
    config.user_agent               = flapUserAgent;
    config.use_global_ca_store      = false;
    config.event_handler            = _http_event_handler_pollForNextMatchList;
    config.cert_pem                 = OPENLIGA_CA;
    config.buffer_size              = 2048;
    config.timeout_ms               = 5000;

    #ifdef LIGAVERBOSE
        {
        TraceScope trace;
        ligaPrintln("get next match list (season = %d, matchday = %d)", ligaSeason, ligaMatchday + matchdayOffset);
        }
    #endif

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
        return false;

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ligaPrintln("get next match list HTTP-Fehler: %s", esp_err_to_name(err));
        return false;
    }

    esp_http_client_cleanup(client);
    return err == ESP_OK;
}

esp_err_t _http_event_handler_matchGoals(esp_http_client_event_t* evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (evt->data_len > 0) {
                jsonBuffer.append((char*)evt->data, evt->data_len);
            }
            break;

        case HTTP_EVENT_ON_FINISH: {
            if (!currentGoalInfo)
                break;

            DynamicJsonDocument  doc(jsonBuffer.length() * 1.2);
            DeserializationError error = deserializeJson(doc, jsonBuffer);
            if (error) {
                Serial.printf("JSON-Fehler bei Match %u: %s\n", currentGoalInfo->matchID, error.c_str());
                break;
            }

            JsonArray goals            = doc["goals"];
            currentGoalInfo->goalCount = 0;

            for (JsonObject goal : goals) {
                uint8_t     minute   = goal["goalMinute"] | 0;
                const char* teamName = goal["scoringTeam"]["teamName"] | "";

                if (currentGoalInfo->goalCount < 10) {
                    currentGoalInfo->goalMinutes[currentGoalInfo->goalCount] = minute;
                    strncpy(currentGoalInfo->scoringTeam[currentGoalInfo->goalCount], teamName, 31);
                    currentGoalInfo->scoringTeam[currentGoalInfo->goalCount][31] = '\0';
                    currentGoalInfo->goalCount++;
                }
            }

            Serial.printf("Match %u: %u Tore\n", currentGoalInfo->matchID, currentGoalInfo->goalCount);
            for (uint8_t g = 0; g < currentGoalInfo->goalCount; ++g) {
                Serial.printf(" → Tor für %s in Minute %u\n", currentGoalInfo->scoringTeam[g], currentGoalInfo->goalMinutes[g]);
            }

            jsonBuffer.clear();
            break;
        }

        case HTTP_EVENT_ERROR:
            Serial.println("error while request for match goal data.");
            break;
    }

    return ESP_OK;
}

void pollGoalsForLiveMatches(const LiveMatchInfo liveMatches[], uint8_t matchCount, LiveMatchGoalInfo goalInfos[]) {
    for (uint8_t i = 0; i < matchCount; ++i) {
        const uint32_t matchID = liveMatches[i].matchID;
        String         url     = "https://api.openligadb.de/getmatchdata/" + String(matchID);

        currentGoalInfo          = &goalInfos[i];
        currentGoalInfo->matchID = matchID;
        jsonBuffer.clear();

        esp_http_client_config_t config = {.url = url.c_str(), .method = HTTP_METHOD_GET, .event_handler = _http_event_handler_matchGoals};

        esp_http_client_handle_t client = esp_http_client_init(&config);
        esp_err_t                err    = esp_http_client_perform(client);
        esp_http_client_cleanup(client);

        if (err != ESP_OK) {
            Serial.printf("Error while requwsr for matchID %u\n", matchID);
        }
    }
}

esp_err_t _http_event_handler_nextLiveMatches(esp_http_client_event_t* evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (evt->data_len > 0) {
                jsonBuffer.append((char*)evt->data, evt->data_len);
            }
            break;

        case HTTP_EVENT_ON_FINISH: {
            ligaMatchLiveCount = 0;

            DynamicJsonDocument     doc(17 * 1024);
            StaticJsonDocument<512> filter;
            filter[0]["matchID"]                          = true;
            filter[0]["matchDateTime"]                    = true;
            filter[0]["matchIsFinished"]                  = true;
            filter["goals"][0]["goalMinute"]              = true;
            filter["goals"][0]["scoringTeam"]["teamName"] = true;

            DeserializationError error = deserializeJson(doc, jsonBuffer);
            if (error) {
                Serial.printf("JSON-Fehler (NextLiveMatches): %s\n", error.c_str());
                break;
            }

            JsonArray    matches   = doc.as<JsonArray>();
            const time_t now       = time(nullptr);
            const time_t maxFuture = 10 * 60;                                   // max. 10 Minuten in der Zukunft
            const time_t maxPast   = 150 * 60;                                  // max. 2,5 Stunden in der Vergangenheit

            for (JsonObject match : matches) {
                if (match["matchIsFinished"] == true)
                    continue;

                const char* kickoffStr = match["matchDateTime"];
                if (!kickoffStr)
                    continue;

                struct tm tmKickoff = {};
                if (!strptime(kickoffStr, "%Y-%m-%dT%H:%M:%S", &tmKickoff))
                    continue;
                tmKickoff.tm_isdst = -1;
                time_t kickoffTime = mktime(&tmKickoff);

                double delta = difftime(kickoffTime, now);
                if (delta >= -maxPast && delta <= maxFuture) {
                    if (ligaMatchLiveCount < sizeof(liveMatches) / sizeof(liveMatches[0])) {
                        liveMatches[ligaMatchLiveCount].matchID = match["matchID"] | 0;
                        liveMatches[ligaMatchLiveCount].kickoff = kickoffTime;
                        ligaMatchLiveCount++;
                    }
                }

                // determine nearest kickoff time
                if (currentNextKickoffTime == 0 || kickoffTime < currentNextKickoffTime) {
                    currentNextKickoffTime = kickoffTime;
                    nextKickoffString      = kickoffStr;
                    nextKickoffChanged     = true;
                    showNextKickoff();
                }
            }

            Serial.printf("Erkannte Live-Matches: %u\n", ligaMatchLiveCount);
            for (uint8_t i = 0; i < ligaMatchLiveCount; ++i) {
                char buf[32];
                strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M", localtime(&liveMatches[i].kickoff));
                Serial.printf("LIVE #%u → matchID=%u, Kickoff=%s\n", i + 1, liveMatches[i].matchID, buf);
            }

            jsonBuffer.clear();
            break;
        }

        case HTTP_EVENT_ERROR:
            jsonBuffer.clear();
            Serial.println("error while request for live matches.");
        case HTTP_EVENT_DISCONNECTED:
            jsonBuffer.clear();
            break;
    }

    return ESP_OK;
}

void pollForGoalsInLiveMatches(LiveMatchGoalInfo goalInfos[]) {
    for (uint8_t i = 0; i < ligaMatchLiveCount; ++i) {
        const uint32_t matchID = liveMatches[i].matchID;
        char           url[128];
        snprintf(url, sizeof(url), "https://api.openligadb.de/getmatchdata/%d", matchID);

        currentGoalInfo            = &goalInfos[i];
        currentGoalInfo->matchID   = matchID;
        currentGoalInfo->goalCount = 0;
        jsonBuffer.clear();

        esp_http_client_config_t config    = {};
        config.url                         = url;
        config.host                        = "api.openligadb.de";
        config.user_agent                  = flapUserAgent;
        config.event_handler               = _http_event_handler_matchGoals;
        config.cert_pem                    = OPENLIGA_CA;
        config.use_global_ca_store         = false;
        config.skip_cert_common_name_check = false;
        config.buffer_size                 = 6 * 1024;
        config.timeout_ms                  = 5000;

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client == NULL) {
            Liga->ligaPrintln("HTTP-Client error while initializing");
            continue;
        }

        esp_err_t err = esp_http_client_perform(client);
        if (err != ESP_OK) {
            Liga->ligaPrintln("error while request for live matches: %s", esp_err_to_name(err));
        }

        for (uint8_t i = 0; i < ligaMatchLiveCount; ++i) {
            const LiveMatchGoalInfo& info = goalInfos[i];
            Liga->ligaPrintln("Match %u: %u Goles", info.matchID, info.goalCount);
            for (uint8_t g = 0; g < info.goalCount; ++g) {
                Liga->ligaPrintln(" → Goal for %s in minute %u", info.scoringTeam[g], info.goalMinutes[g]);
            }
        }

        esp_http_client_cleanup(client);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    if (ligaMatchLiveCount == 0) {
        Liga->ligaPrintln("no live matches, no goals to fetch");
    }
}

// ---------- getestet und funktioniert----------------------
// HTTP Event Handler for live matches
esp_err_t _http_event_handler_pollForLiveMatches(esp_http_client_event_t* evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (evt->data_len > 0) {
                jsonBuffer.append((char*)evt->data, evt->data_len);
            }
            break;

        case HTTP_EVENT_ON_FINISH: {
            size_t                  jsonSize = jsonBuffer.size();
            DynamicJsonDocument     doc(30 * 1024);                             // Reserve etwas mehr Speicher
            StaticJsonDocument<512> filter;
            filter[0]["matchID"]           = true;
            filter[0]["matchDateTime"]     = true;
            filter[0]["matchIsFinished"]   = true;
            filter[0]["time1"]["timeName"] = true;
            filter[0]["time2"]["timeName"] = true;

            DeserializationError error = deserializeJson(doc, jsonBuffer);
            if (error) {
                Liga->ligaPrintln("(_http_event_handler_liveMatches) JSON-Error: %s", error.c_str());
                jsonBuffer.clear();
                return ESP_FAIL;
            }

            if (!doc.is<JsonArray>()) {
                Liga->ligaPrintln("(_http_event_handler_liveMatches) unexpected JSON-Format");
                jsonBuffer.clear();
                return ESP_FAIL;
            }

            ligaMatchLiveCount  = 0;
            const time_t now    = time(nullptr);
            const time_t maxAge = 150 * 60;                                     // 2,5 Stunden

            for (JsonObject match : doc.as<JsonArray>()) {
                if (match["matchIsFinished"] == true)
                    continue;

                const char* kickoffStr = match["matchDateTime"];
                if (!kickoffStr)
                    continue;

                struct tm tmKickoff = {};
                if (!strptime(kickoffStr, "%Y-%m-%dT%H:%M:%S", &tmKickoff)) {
                    Liga->ligaPrintln("invalide date/time: %s", kickoffStr);
                    continue;
                }

                tmKickoff.tm_isdst = -1;                                        // Sommerzeit automatisch erkennen
                time_t kickoffTime = mktime(&tmKickoff);
                double delta       = difftime(now, kickoffTime);

                if (delta >= 0 && delta <= maxAge) {
                    const char* name1 = match["team1"]["teamName"];
                    const char* name2 = match["team2"]["teamName"];

                    if (name1 && name2) {
                        Liga->ligaPrintln("LIVE: %s vs %s", name1, name2);
                        Liga->ligaPrintln("Kickoff: %s", kickoffStr);
                    } else {
                        Liga->ligaPrintln("LIVE match recognized, but TeamName missing");
                    }
                    ligaMatchLiveCount++;
                }
                // determine nearest kickoff time
                if (currentNextKickoffTime == 0 || kickoffTime < currentNextKickoffTime) {
                    currentNextKickoffTime = kickoffTime;
                    nextKickoffString      = kickoffStr;
                    nextKickoffChanged     = true;
                    showNextKickoff();
                }
            }

            Liga->ligaPrintln("recognized Live matches: %d", ligaMatchLiveCount);

            if (ligaMatchLiveCount > 0) {
                matchIsLive = true;                                             // at least one match is live
            } else {
                matchIsLive = false;                                            // no live matches
            }

            jsonBuffer.clear();
            break;
        }

        case HTTP_EVENT_DISCONNECTED:
        case HTTP_EVENT_ERROR:
            jsonBuffer.clear();
            break;

        default:
            break;
    }

    return ESP_OK;
}

// fetch live matches of current matchday and print them to log
void LigaTable::pollForLiveMatches() {
    char url[128];
    snprintf(url, sizeof(url), "https://api.openligadb.de/getmatchdata/%s/%d/%d", leagueShortcut(activeLeague), ligaSeason, ligaMatchday);

    esp_http_client_config_t config    = {};
    config.url                         = url;
    config.host                        = "api.openligadb.de";
    config.user_agent                  = flapUserAgent;
    config.use_global_ca_store         = false;
    config.event_handler               = _http_event_handler_pollForLiveMatches;
    config.cert_pem                    = OPENLIGA_CA;
    config.use_global_ca_store         = false;
    config.skip_cert_common_name_check = false;
    config.buffer_size                 = 6144;
    config.timeout_ms                  = 5000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        Liga->ligaPrintln("HTTP-Client konnte nicht initialisiert werden");
        return;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        Liga->ligaPrintln("Fehler beim Abrufen der Livespiele: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

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

    if (diffSecondsUntilKickoff >= 0 && diffSecondsUntilKickoff <= 600) {
        nextKickoffFarAway = false;                                             // kickoff in less then 10 Minutes
        #ifdef LIGAVERBOSE
            {
            Liga->ligaPrintln("we are 10 minutes befor next kickoff (next live is not far away)");
            }
        #endif
    } else if (diffSecondsUntilKickoff < 0 && diffSecondsUntilKickoff >= -9000) {
        nextKickoffFarAway = false;                                             // match is live or even over ( kickoff < 2,5h)
        matchIsLive        = true;
        #ifdef LIGAVERBOSE
            {
            Liga->ligaPrintln("match is live");
            }
        #endif
    } else {
        nextKickoffFarAway = true;                                              // kickoff will be far away in the future
        matchIsLive        = false;
        #ifdef LIGAVERBOSE
            {
            Liga->ligaPrintln("next match is over or far away in the future");
            }
        #endif
    }
}

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
                tmKickoff.tm_isdst     = -1;                                    // System soll Sommerzeit selbst erkennen
                currentNextKickoffTime = mktime(&tmKickoff);                    // save next Kickoff time
                if (currentNextKickoffTime != previousNextKickoffTime)
                    nextKickoffChanged = true;
                else
                    nextKickoffChanged = false;

                previousNextKickoffTime = currentNextKickoffTime;
                showNextKickoff();

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
        jsonBuffer.clear();
    }

    if (evt->event_id == HTTP_EVENT_DISCONNECTED || evt->event_id == HTTP_EVENT_ERROR) {
        jsonBuffer.clear();
    }

    return ESP_OK;
}

// Ermittlung des nächsten Anpfiffzeitpunkt
bool LigaTable::pollNextKickoff() {
    char url[128];                                                              // ausreichend groß für die komplette URL
    sprintf(url, "https://api.openligadb.de/getnextmatchbyleagueshortcut/%s", leagueShortcut(activeLeague));
    esp_http_client_config_t config    = {};
    config.url                         = url;
    config.host                        = "api.openligadb.de";
    config.user_agent                  = flapUserAgent;
    config.event_handler               = _http_event_handler_nextKickoff;
    config.cert_pem                    = OPENLIGA_CA;
    config.use_global_ca_store         = false;
    config.skip_cert_common_name_check = false;
    config.buffer_size                 = 100;
    config.timeout_ms                  = 5000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
        return false;

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        #ifdef LIGAVERBOSE
            {
            TraceScope trace;
            ligaPrintln("HTTP-Fehler: %s", esp_err_to_name(err));
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
esp_err_t _http_event_handler_pollForChanges(esp_http_client_event_t* evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        dateTimeBuffer.append((char*)evt->data, evt->data_len);
    }

    if (evt->event_id == HTTP_EVENT_ON_FINISH) {
        currentLastChangeOfMatchday = dateTimeBuffer.c_str();
        currentLastChangeOfMatchday.remove(0, 1);                               // entfernt das erste Zeichen (das Anführungszeichen)
        currentLastChangeOfMatchday.remove(19);                                 // entfernt alles ab Position 19 (also ".573")
        dateTimeBuffer.clear();
    }

    if (evt->event_id == HTTP_EVENT_DISCONNECTED || evt->event_id == HTTP_EVENT_ERROR) {
        dateTimeBuffer.clear();
    }

    return ESP_OK;
}

// Ermittlung der letzten Änderung des aktuellen Spieltags
bool LigaTable::pollForChanges() {
    char url[128];                                                              // ausreichend groß für die komplette URL
    sprintf(url, "https://api.openligadb.de/getlastchangedate/%s/%d/%d", leagueShortcut(activeLeague), ligaSeason, ligaMatchday);
    esp_http_client_config_t config    = {};
    config.url                         = url;
    config.host                        = "api.openligadb.de";
    config.event_handler               = _http_event_handler_pollForChanges;
    config.user_agent                  = flapUserAgent;
    config.cert_pem                    = OPENLIGA_CA;
    config.use_global_ca_store         = false;
    config.skip_cert_common_name_check = false;
    config.buffer_size                 = 100;
    config.timeout_ms                  = 5000;

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
        strptime(currentLastChangeOfMatchday.c_str(), "%Y-%m-%dT%H:%M:%S", &tmLastChange);
        tmLastChange.tm_isdst = -1;                                             // automatische Sommerzeit-Erkennung
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
        ligaPrintln("this was %d days, %d hours, %d minutes, %d seconds ago", days, hours, minutes, seconds);
        }
    #endif
    return err == ESP_OK;
}

// zur Ermittlung des aktuellen Spieltags
esp_err_t _http_event_handler_pollCurrentMatchday(esp_http_client_event_t* evt) {
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

    if (evt->event_id == HTTP_EVENT_DISCONNECTED || evt->event_id == HTTP_EVENT_ERROR) {
        jsonBuffer.clear();
    }
    return ESP_OK;
}

// zur Ermittlung der aktuellen Tabelle
esp_err_t _http_event_handler_pollForTable(esp_http_client_event_t* evt) {
    if (jsonBuffer.capacity() < evt->data_len * 10) {
        jsonBuffer.reserve(evt->data_len * 10);                                 // großzügig reservieren
    }
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        jsonBuffer.append((char*)evt->data, evt->data_len);
    }

    if (evt->event_id == HTTP_EVENT_ON_FINISH) {
        size_t               jsonSize = jsonBuffer.size();                      // oder evt->data_len aufsummiert
        DynamicJsonDocument  doc(jsonSize * 1.2);
        DeserializationError error = deserializeJson(doc, jsonBuffer);
        if (!error) {
            LigaSnapshot& snapshot = snap[snapshotIndex];                       // local point to snap to be used
            snapshotIndex ^= 1;                                                 // Toggle between 0 und 1
            snapshot.clear();                                                   // Reset actual-Snapshot

            JsonArray table    = doc.as<JsonArray>();                           // read JSON-Data into snap[snapshotIndex]
            snapshot.teamCount = table.size();                                  // number of teams

            snapshot.season       = ligaSeason;
            snapshot.matchday     = ligaMatchday;
            snapshot.fetchedAtUTC = time(nullptr);                              // actual timestamp
            for (uint8_t i = 0; i < snapshot.teamCount && i < LIGA_MAX_TEAMS; ++i) {
                JsonObject team = table[i];
                LigaRow&   row  = snapshot.rows[i];

                row.pos  = i + 1;
                row.sp   = team["matches"] | 0;
                row.pkt  = team["points"] | 0;
                row.w    = team["won"] | 0;
                row.l    = team["lost"] | 0;
                row.d    = team["draw"] | 0;
                row.g    = team["goals"] | 0;
                row.og   = team["opponentGoals"] | 0;
                row.diff = team["goalDiff"] | 0;

                const char* name = team["teamName"] | "";                       // liefert "" wenn null
                strncpy(row.team, name ? name : "", sizeof(row.team) - 1);
                row.team[sizeof(row.team) - 1] = '\0';                          // Teamname

                row.flap = flapForTeamStrict(row.team);                         // Position auf Flap-Display

                String dfb = dfbCodeForTeamStrict(row.team);                    // liefert "" wenn null
                strncpy(row.dfb, dfb.c_str(), sizeof(row.dfb) - 1);
                row.dfb[3] = '\0';                                              // manuell nullterminieren

                // Serial.printf("Platz %d: %s Punkte: %d TD: %d\n", row.pos, row.team, row.pkt, row.diff);
            }
        } else {
            Serial.printf("JSON-Fehler (Tabelle): %s\n", error.c_str());
        }

        jsonBuffer = "";                                                        // Buffer leeren
    }

    if (evt->event_id == HTTP_EVENT_DISCONNECTED || evt->event_id == HTTP_EVENT_ERROR) {
        jsonBuffer.clear();
    }

    return ESP_OK;
}

// zur Ermittlung der aktuellen Tabelle
bool LigaTable::pollForTable() {
    char url[128];                                                              // ausreichend groß für die komplette URL
    sprintf(url, "https://api.openligadb.de/getbltable/%s/%d", leagueShortcut(activeLeague), ligaSeason);
    esp_http_client_config_t config = {};
    config.url                      = url;
    config.host                     = "api.openligadb.de";
    config.user_agent               = flapUserAgent;
    config.use_global_ca_store      = false;
    config.event_handler            = _http_event_handler_pollForTable;
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
bool LigaTable::pollForCurrentMatchday() {
    char url[128];                                                              // ausreichend groß für die komplette URL
    sprintf(url, "https://api.openligadb.de/getcurrentgroup/%s", leagueShortcut(activeLeague));
    esp_http_client_config_t config = {};
    config.url                      = url;
    config.host                     = "api.openligadb.de";
    config.event_handler            = _http_event_handler_pollCurrentMatchday;
    config.user_agent               = flapUserAgent;
    config.use_global_ca_store      = false;
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
            ligaPrintln("HTTP-Fehler: %s", esp_err_to_name(err));
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

// ---------------------------

/**
 * @brief Detect whether the table leader changed between two snapshots.
 *
 * Compares row 0 ("leader") of both snapshots. If either snapshot has no
 * rows, this returns false. Identity check prefers the DFB short code when
 * both are present; otherwise falls back to an exact team-name comparison.
 *
 * @param oldSnap Previously active snapshot.
 * @param newSnap Newly active snapshot.
 * @param oldLeaderOut (optional) Receives the previous leader row (row 0).
 * @param newLeaderOut (optional) Receives the new leader row (row 0).
 * @return true if the leader team changed; false otherwise.
 *         Note: Returns false if either snapshot is empty (e.g. on first call). */
bool LigaTable::detectLeaderChange(const LigaSnapshot& oldSnap, const LigaSnapshot& newSnap, const LigaRow** oldLeaderOut,
                                   const LigaRow** newLeaderOut) {
    if (oldSnap.teamCount == 0 || newSnap.teamCount == 0) {
        #ifdef LIGAVERBOSE
            {
            TraceScope  trace;
            const char* leaderTeam = (newSnap.teamCount > 0) ? newSnap.rows[0].team : "(none)";
            ligaPrintln("no leader change: '%s' -> '%s'", leaderTeam, leaderTeam);
            }
        #endif
        return false;
    }
    const LigaRow& oldL = oldSnap.rows[0];
    const LigaRow& newL = newSnap.rows[0];

    // Prefer DFB code if both present
    if (oldL.dfb[0] != '\0' && newL.dfb[0] != '\0') {
        if (strcmp(oldL.dfb, newL.dfb) == 0) {
            if (oldLeaderOut)
                *oldLeaderOut = &oldL;
            if (newLeaderOut)
                *newLeaderOut = &newL;
                #ifdef LIGAVERBOSE
                    {
                    TraceScope trace;
                    ligaPrintln("no leader change: '%s' -> '%s' (pts %u→%u, diff %d→%d)", oldL.team, newL.team, oldL.pkt, newL.pkt, oldL.diff, newL.diff);
                    }
                #endif
            return false;                                                       // same leader
        }
    } else {
        // Fallback: exact team name compare
        if (strcmp(oldL.team, newL.team) == 0) {
            if (oldLeaderOut)
                *oldLeaderOut = &oldL;
            if (newLeaderOut)
                *newLeaderOut = &newL;
                #ifdef LIGAVERBOSE
                    {
                    TraceScope trace;
                    ligaPrintln("no leader change: '%s' -> '%s' (pts %u→%u, diff %d→%d)", oldL.team, newL.team, oldL.pkt, newL.pkt, oldL.diff, newL.diff);
                    }
                #endif
            return false;                                                       // same leader
        }
    }

    // Leader changed
    if (oldLeaderOut)
        *oldLeaderOut = &oldL;
    if (newLeaderOut)
        *newLeaderOut = &newL;
        #ifdef LIGAVERBOSE
            {
            TraceScope trace;
            ligaPrintln("leader change: '%s' -> '%s' (pts %u→%u, diff %d→%d)", oldL.team, newL.team, oldL.pkt, newL.pkt, oldL.diff, newL.diff);
            }
        #endif

    return true;
}

/**
 * @brief Look up the DFB code for a given team name (strict).
 *
 * Searches through the team mapping arrays (DFB1 and DFB2) to find a
 * team with a matching key. If a match is found, its DFB code string
 * is returned.
 *
 * Strict mode: if no team matches, an empty string is returned.
 *
 * @param teamName Team name as provided by OpenLigaDB.
 * @return String containing the DFB code, or empty string if not found.
 */
String dfbCodeForTeamStrict(const String& teamName) {
    String key = teamName;
    for (auto& e : DFB1)
        if (key == e.key)
            return e.code;
    for (auto& e : DFB2)
        if (key == e.key)
            return e.code;
    return "";                                                                  // strict: no match => return empty string
}

/**
 * @brief Look up the flap index for a given team name (strict).
 *
 * Searches through the team mapping arrays (DFB1 and DFB2) to find a
 * team with a matching key. If a match is found, its flap index is
 * returned.
 *
 * Strict mode: if no team matches, -1 is returned.
 *
 * @param teamName Team name as provided by OpenLigaDB.
 * @return Flap index (integer), or -1 if not found.
 */
int flapForTeamStrict(const String& teamName) {
    String key = teamName;
    for (auto& e : DFB1)
        if (key == e.key)
            return e.flap;
    for (auto& e : DFB2)
        if (key == e.key)
            return e.flap;
    return -1;                                                                  // strict: no match => return -1
}

/**
 * @brief Detect whether the "red lantern" (last place) team changed between two snapshots.
 *
 * Compares the last row of both snapshots. If either snapshot has no
 * rows, this returns false. Identity check prefers the DFB short code when
 * both are present; otherwise falls back to an exact team-name comparison.
 *
 * @param oldSnap Previously active snapshot.
 * @param newSnap Newly active snapshot.
 * @param oldLanternOut (optional) Receives the previous last-place row.
 * @param newLanternOut (optional) Receives the new last-place row.
 * @return true if the last-place team changed; false otherwise.
 *         Note: Returns false if either snapshot is empty (e.g. on first call).
 */
bool LigaTable::detectRedLanternChange(const LigaSnapshot& oldSnap, const LigaSnapshot& newSnap, const LigaRow** oldLanternOut,
                                       const LigaRow** newLanternOut) {
    if (oldSnap.teamCount == 0 || newSnap.teamCount == 0) {
        #ifdef LIGAVERBOSE
            {
            TraceScope  trace;
            const char* redLantern = (newSnap.teamCount > 0) ? newSnap.rows[newSnap.teamCount - 1].team : "(none)";
            ligaPrintln("no red lantern change: '%s' -> '%s'", redLantern, redLantern);
            }
        #endif
        return false;
    }

    const LigaRow& oldB = oldSnap.rows[oldSnap.teamCount - 1];
    const LigaRow& newB = newSnap.rows[newSnap.teamCount - 1];

    // Prefer DFB code if both present
    if (oldB.dfb[0] != '\0' && newB.dfb[0] != '\0') {
        if (strcmp(oldB.dfb, newB.dfb) == 0) {
            if (oldLanternOut)
                *oldLanternOut = &oldB;
            if (newLanternOut)
                *newLanternOut = &newB;
                #ifdef LIGAVERBOSE
                    {
                    TraceScope trace;
                    ligaPrintln("no red lantern change: '%s' -> '%s' (pts %u→%u, diff %d→%d)", oldB.team, newB.team, oldB.pkt, newB.pkt, oldB.diff,
                    newB.diff);
                    }
                #endif
            return false;                                                       // same last-place team
        }
    } else {
        // Fallback: exact team name compare
        if (strcmp(oldB.team, newB.team) == 0) {
            if (oldLanternOut)
                *oldLanternOut = &oldB;
            if (newLanternOut)
                *newLanternOut = &newB;
                #ifdef LIGAVERBOSE
                    {
                    TraceScope trace;
                    ligaPrintln("no red lantern change: '%s' -> '%s' (pts %u→%u, diff %d→%d)", oldB.team, newB.team, oldB.pkt, newB.pkt, oldB.diff,
                    newB.diff);
                    }
                #endif
            return false;                                                       // same last-place team
        }
    }

    // Red lantern changed
    if (oldLanternOut)
        *oldLanternOut = &oldB;
    if (newLanternOut)
        *newLanternOut = &newB;
        #ifdef LIGAVERBOSE
            {
            TraceScope trace;
            ligaPrintln("red lantern change: '%s' -> '%s' (pts %u→%u, diff %d→%d)", oldB.team, newB.team, oldB.pkt, newB.pkt, oldB.diff, newB.diff);
            }
        #endif

    return true;
}

/**
 * @brief Detect whether the "relegation ghost" (ranks 16 & 17) changed between two snapshots.
 *
 * Compares ranks 16 and 17 of both snapshots. If either snapshot has no rows,
 * this returns false but logs the current teams as "no change".
 * Note: Rank 18 ("red lantern") is handled separately.
 *
 * @param oldSnap Previously active snapshot.
 * @param newSnap Newly active snapshot.
 * @param oldZoneOut (optional) Receives the previous rows for ranks 16 & 17.
 * @param newZoneOut (optional) Receives the new rows for ranks 16 & 17.
 * @return true if the set of relegation-zone teams changed; false otherwise.
 */
bool LigaTable::detectRelegationGhostChange(const LigaSnapshot& oldSnap, const LigaSnapshot& newSnap, const LigaRow** oldZoneOut,
                                            const LigaRow** newZoneOut) {
    if (oldSnap.teamCount < 17 || newSnap.teamCount < 17) {
        #ifdef LIGAVERBOSE
            {
            TraceScope trace;
            ligaPrintln("no relegation ghost change: insufficient teams");
            }
        #endif
        return false;
    }

    const LigaRow* oldZone[2] = {&oldSnap.rows[15], &oldSnap.rows[16]};
    const LigaRow* newZone[2] = {&newSnap.rows[15], &newSnap.rows[16]};

    if (oldZoneOut)
        *oldZoneOut = oldZone[0];                                               // optional: oder beide separat übergeben
    if (newZoneOut)
        *newZoneOut = newZone[0];

    auto sameTeam = [](const LigaRow* a, const LigaRow* b) {
        if (a->dfb[0] != '\0' && b->dfb[0] != '\0')
            return strcmp(a->dfb, b->dfb) == 0;
        return strcmp(a->team, b->team) == 0;
    };

    bool changed = false;

    // Check entrants
    for (auto* n : newZone) {
        bool found = false;
        for (auto* o : oldZone) {
            if (sameTeam(o, n)) {
                found = true;
                break;
            }
        }
        if (!found) {
            #ifdef LIGAVERBOSE
                ligaPrintln("relegation ghost: new entrant '%s'", n->team);
            #endif
            changed = true;
        }
    }

    // Check exits
    for (auto* o : oldZone) {
        bool found = false;
        for (auto* n : newZone) {
            if (sameTeam(o, n)) {
                found = true;
                break;
            }
        }
        if (!found) {
            #ifdef LIGAVERBOSE
                ligaPrintln("relegation ghost: team left '%s'", o->team);
            #endif
            changed = true;
        }
    }

    if (!changed) {
        #ifdef LIGAVERBOSE
            ligaPrintln("no change in relegation ghost zone");
        #endif
    }

    return changed;
}

void processPollScope(PollScope scope) {
    #ifdef LIGAVERBOSE
        Liga->ligaPrintln("PollScope {%s}", pollScopeToString(scope));          // log current scope
    #endif
    switch (scope) {
        case CALC_CURRENT_SEASON:
            ligaSeason = calcCurrentSeason();                                   // calculate current season
            vTaskDelay(pdMS_TO_TICKS(400));
            break;

        case FETCH_CURRENT_MATCHDAY:
            Liga->pollForCurrentMatchday();                                     // get current matchday from openLigaDB
            vTaskDelay(pdMS_TO_TICKS(2000));
            break;

        case FETCH_TABLE:
            Liga->pollForTable();                                               // get actual table from openLigaDB
            vTaskDelay(pdMS_TO_TICKS(2000));
            break;

        case CHECK_FOR_CHANGES:
            Liga->pollForChanges();                                             // check for changes in openLigaDB
            isSomeThingNew = checkForMatchdayChanges();
            break;

        case FETCH_LIVE_MATCHES:
            Liga->pollForLiveMatches();                                         // get actual live matches from openLigaDB
            vTaskDelay(pdMS_TO_TICKS(1000));
            break;

        case FETCH_NEXT_MATCH_LIST:
            Liga->pollForNextMatchList(0);                                      // fetch actual live matches from openLigaDB current matchday
            vTaskDelay(pdMS_TO_TICKS(1000));
            if (ligaNextMatchCount <= 0)
                Liga->pollForNextMatchList(1);                                  // fetch actual live matches from openLigaDB next matchday
            break;

        case FETCH_NEXT_KICKOFF: {
            if (matchIsLive)
                break;                                                          // no need to fetch next kickoff if a match is live
            Liga->pollNextKickoff();                                            // get next kickoff time from openLigaDB
            vTaskDelay(pdMS_TO_TICKS(2000));
            break;
        }
        case SHOW_NEXT_KICKOFF:
            showNextKickoff();                                                  // get next kickoff time from stored value
            break;

        case FETCH_GOALS: {
            pollForGoalsInLiveMatches(goalsInfos);
            vTaskDelay(pdMS_TO_TICKS(1000));
            break;
        }

        case CALC_TABLE_CHANGE: {
            break;
        }

        case CALC_LEADER_CHANGE: {
            const LigaRow* oldLeaderOut = nullptr;
            const LigaRow* newLeaderOut = nullptr;
            Liga->detectLeaderChange(snap[snapshotIndex], snap[snapshotIndex ^ 1], &oldLeaderOut, &newLeaderOut);
            break;
        }

        case CALC_RELEGATION_GHOST_CHANGE: {
            const LigaRow* oldRZOut = nullptr;
            const LigaRow* newRZOut = nullptr;
            Liga->detectRelegationGhostChange(snap[snapshotIndex], snap[snapshotIndex ^ 1], &oldRZOut, &newRZOut);
            break;
        }

        case CALC_RED_LANTERN_CHANGE: {
            const LigaRow* oldRLOut = nullptr;
            const LigaRow* newRLOut = nullptr;
            Liga->detectRedLanternChange(snap[snapshotIndex], snap[snapshotIndex ^ 1], &oldRLOut, &newRLOut);
            break;
        }
    }
}

PollMode determineNextPollMode() {
    if (matchIsLive) {
        return POLL_MODE_LIVE;
    }

    if (!matchIsLive && currentPollMode == POLL_MODE_LIVE) {
        return POLL_MODE_ONCE;
    }

    if (!nextKickoffFarAway) {
        return POLL_MODE_PRELIVE;
    }

    if (isSomeThingNew && !matchIsLive) {
        return POLL_MODE_REACTIVE;
    }

    if (currentPollMode == POLL_MODE_REACTIVE) {
        return POLL_MODE_RELAXED;
    }

    if (currentPollMode == POLL_MODE_LIVE || currentPollMode == POLL_MODE_PRELIVE) {
        return POLL_MODE_REACTIVE;
    }

    if (currentPollMode == POLL_MODE_ONCE) {
        return POLL_MODE_RELAXED;
    }

    return currentPollMode;
}

// Erkennung von Teams, die Tore erzielt haben
bool LigaTable::detectScoringTeams(const LigaSnapshot& oldSnap, const LigaSnapshot& newSnap, const LigaRow* scorers[], uint8_t& scorerCount) {
    scorerCount = 0;
    if (oldSnap.teamCount == 0 || newSnap.teamCount == 0)
        return false;

    for (uint8_t i = 0; i < newSnap.teamCount && i < LIGA_MAX_TEAMS; ++i) {
        const LigaRow& newRow = newSnap.rows[i];

        // Suche passendes Team im alten Snapshot
        const LigaRow* oldRow = nullptr;
        for (uint8_t j = 0; j < oldSnap.teamCount && j < LIGA_MAX_TEAMS; ++j) {
            const LigaRow& candidate = oldSnap.rows[j];

            bool sameTeam = (candidate.dfb[0] != '\0' && newRow.dfb[0] != '\0') ? (strcmp(candidate.dfb, newRow.dfb) == 0)
                                                                                : (strcmp(candidate.team, newRow.team) == 0);

            if (sameTeam) {
                oldRow = &candidate;
                break;
            }
        }

        if (!oldRow)
            continue;                                                           // Team nicht gefunden → überspringen

        // Torerkennung: Tore oder Tordifferenz gestiegen
        if (newRow.g > oldRow->g || newRow.diff > oldRow->diff) {
            if (scorerCount < LIGA_MAX_TEAMS) {
                scorers[scorerCount++] = &newRow;

                #ifdef LIGAVERBOSE
                    TraceScope trace;
                    ligaPrintln("Goal detected: %s (Goal %d→%d, Diff %d→%d)", newRow.team, oldRow->g, newRow.g, oldRow->diff, newRow.diff);
                #endif
            }
        }
    }

    return scorerCount > 0;
}
