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

    Liga task to poll openLigaDB for Bundesliga data

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
std::string jsonBuffer                   = "";                                  // buffer for deserialization in event-handlers
String      currentLastChangeOfMatchday  = "";                                  // openLigaDB Matchday change state
String      previousLastChangeOfMatchday = "";                                  // openLigaDB Matchday change state
std::string dateTimeBuffer               = "";                                  // temp buffer to request
char        lastScanTimestamp[32]        = {0};
bool        currentMatchdayChanged       = false;                               // no chances

League activeLeague = League::BL1;                                              // use default BL1
int    ligaMaxTeams = LIGA1_MAX_TEAMS;
int    ligaSeason   = 0;                                                        // global unknown Season
int    ligaMatchday = 0;                                                        // global unknown Matchday
int    liveMatchID  = 0;                                                        // iD of current live match

time_t currentNextKickoffTime  = 0;
time_t previousNextKickoffTime = 0;
bool   nextKickoffChanged      = false;
bool   nextKickoffFarAway      = true;
bool   matchIsLive             = false;
bool   isSomeThingNew          = false;

MatchInfo liveMatches[10]    = {};                                              // max. 10 Live-matches
int       ligaLiveMatchCount = 0;                                               // number of live matches in current matchday
int       ligaFiniMatchCount = 0;                                               // number of finisched live matches in current matchday

MatchInfo nextMatches[10]    = {};                                              // max. 10 next-matches
int       ligaNextMatchCount = 0;                                               // number of next matches in current matchday

MatchInfo planMatches[10]    = {};                                              // max. 10 planned-matches
int       ligaPlanMatchCount = 0;                                               // number of next matches in current matchday

LiveMatchGoalInfo goalsInfos[30] = {};                                          // max. 30 live goals
int               liveGoalCount  = 0;
int               lastGoalID     = 0;                                           // last goal ID to detect new goals

double      diffSecondsUntilKickoff = 0;
std::string nextKickoffString;

PollScope        currentPollScope          = CHECK_FOR_CHANGES;                 // current
PollMode         currentPollMode           = POLL_MODE_NONE;                    // global poll mode of poll mananger
PollMode         nextPollMode              = POLL_MODE_NONE;
const PollScope* activeCycle               = nullptr;
size_t           activeCycleLength         = 0;
uint32_t         pollManagerDynamicWait    = 0;                                 // wait time according to current poll mode
uint32_t         pollManagerStartOfWaiting = 0;                                 // time_t when entering waiting

LigaSnapshot snap[2];                                                           // actual and previous table
uint8_t      snapshotIndex = 0;                                                 // 0 or 1

// ===== Routines ==========================================

/**
 * @brief Constructs the global LigaTable instance if not already initialized.
 *
 * Ensures that the Liga object exists before any usage. Allocates it on the heap.
 */
void createLigaInstance() {
    if (!Liga) {
        Liga = new LigaTable();                                                 ///< Allocate LigaTable instance on heap (global pointer)
    }
}

/**
 * @brief Configures the system time zone and synchronizes time via NTP.
 *
 * Sets CET/CEST time zone rules and waits for NTP synchronization.
 * Logs diagnostic information if verbose mode is enabled.
 */
void configureTime() {
    #ifdef LIGAVERBOSE
        {
        TraceScope trace;
        Liga->ligaPrintln("Configure time zone: CET with daylight-saving rules (Berlin style)"); ///< Pure log for visibility
        }
    #endif

    // Configure CET/CEST with common NTP servers
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");

    // Block until system time is synced (bounded wait with logging)
    bool synchStatus = waitForTime(15000, true);
    if (!synchStatus) {
        Liga->ligaPrintln("Time NOT synchronized within timeout, retry later!"); ///< Log failure to sync time
    }

    #ifdef LIGAVERBOSE
        {
        time_t     now      = time(NULL);                                       ///< Get local time as time_t
        struct tm* timeinfo = localtime(&now);                                  ///< Convert to local time structure
        char       buffer[32];
        strftime(buffer, sizeof(buffer), "%d.%m.%Y %H:%M:%S", timeinfo);
        TraceScope trace;
        Liga->ligaPrintln("actual synchronized time %s", buffer);               ///< Show synchronized time
        }
    #endif
}

/**
 * @brief Initializes the Liga task by setting up WiFi, time, and API health check.
 *
 * Ensures all prerequisites are met before polling OpenLigaDB.
 *
 * @return true If initialization was successful.
 * @return false If WiFi or API health check failed.
 */
bool initLigaTask() {
    createLigaInstance();

    if (!connectToWifi()) {                                                     ///< Ensure WiFi/TLS preconditions for OpenLigaDB are met
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(300));                                             ///< Short delay before time configuration
    configureTime();

    if (!openLigaDBHealthCheck()) {
        return false;                                                           ///< Consider retry or fallback to offline mode
    }

    return true;
}

/**
 * @brief Prints the current system time with a custom label.
 *
 * Formats time as DD.MM.YYYY hh:mm:ss TZ and logs it via Liga.
 *
 * @param label A descriptive label to prefix the time output.
 */
void printTime(const char* label) {
    time_t now = time(nullptr);

    if (now < 100000) {
        Liga->ligaPrintln("[%s] time not set (epoch=%ld)", label, (long)now);   ///< Time not yet synchronized
    } else {
        struct tm tmLoc;
        localtime_r(&now, &tmLoc);                                              ///< Convert epoch to local time
        char buf[32];
        strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M:%S %Z", &tmLoc);             ///< Format time string
        Liga->ligaPrintln("[%s] %s", label, buf);                               ///< Print formatted time
    }
}

/**
 * @brief Calculates the current football season based on system date.
 *
 * Assumes season starts in August. If current month is before August, subtract one year.
 *
 * @return int The calculated season year.
 */
int calcCurrentSeason() {
    time_t     now      = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    int        season   = timeinfo->tm_year + 1900;

    // Season usually starts in August → if month < 8: use previous year
    if (timeinfo->tm_mon < 7) {
        season -= 1;
    }

    Liga->ligaPrintln("actuell season: %d", season);                            ///< Log calculated season
    return season;
}

/**
 * @brief Performs a DNS and API health check for OpenLigaDB.
 *
 * Resolves the API hostname and sends a test request to verify availability.
 *
 * @return true If DNS resolution and API response were successful.
 * @return false If either DNS or HTTP request failed.
 */
bool openLigaDBHealthCheck() {
    IPAddress ip;                                                               ///< Temp buffer for DNS resolution logging

    if (WiFi.hostByName("api.openligadb.de", ip)) {
        Liga->ligaPrintln("DNS ok: %s -> %s", "api.openligadb.de", ip.toString().c_str()); ///< Helpful diagnostic for name resolution
    } else {
        Liga->ligaPrintln("DNS failed!");                                       ///< DNS failure hint (will affect subsequent HTTP)
    }

    String response;
    bool   success = sendRequest("https://api.openligadb.de/getavailablesports", response);

    return success && response.length() > 0;                                    ///< Return true only if response is non-empty
}

/**
 * @brief Converts a PollMode enum value to its corresponding string representation.
 *
 * @param mode The PollMode value to convert.
 * @return const char* A string literal representing the PollMode.
 */
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
            return "unknown Poll Mode";                                         ///< Fallback for undefined PollMode values
    }
}

/**
 * @brief Converts a PollScope enum value to its corresponding string representation.
 *
 * @param scope The PollScope value to convert.
 * @return const char* A string literal representing the PollScope.
 */
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
            return "CALC_GOALS";
        default:
            return "UNKNOWN_SCOPE";                                             ///< Fallback for undefined PollScope values
    }
}

/**
 * @brief Converts a sequence of PollScope values into a formatted string list.
 *
 * @param cycle Pointer to an array of PollScope values.
 * @param length Number of elements in the cycle array.
 * @return String A comma-separated string representation of the cycle.
 */
String pollCycleToString(const PollScope* cycle, size_t length) {
    String result = "{";
    for (size_t i = 0; i < length; ++i) {
        result += pollScopeToString(cycle[i]);                                  ///< Append string representation of each scope
        if (i < length - 1)
            result += ", ";                                                     ///< Add separator between elements
    }
    result += "}";
    return result;
}

/**
 * @brief Selects the appropriate polling cycle based on the given PollMode.
 *
 * Sets the global activeCycle and activeCycleLength variables accordingly.
 * Also prints the selected cycle for diagnostic purposes.
 *
 * @param mode The PollMode to use for selecting the cycle.
 */
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
        case POLL_MODE_RELAXED:
            activeCycle       = relaxedCycle;
            activeCycleLength = sizeof(relaxedCycle) / sizeof(PollScope);
            break;
        case POLL_MODE_ONCE:
            activeCycle       = onceCycle;
            activeCycleLength = sizeof(onceCycle) / sizeof(PollScope);
            break;
        default:
            activeCycle       = relaxedCycle;                                   ///< Default fallback cycle
            activeCycleLength = sizeof(relaxedCycle) / sizeof(PollScope);
            break;
    }

    // Log the selected polling cycle for debugging
    Liga->ligaPrintln("PollScope = %s", pollCycleToString(activeCycle, activeCycleLength).c_str());
}
/**
 * @brief Determines the appropriate PollMode based on the next scheduled kickoff time.
 *
 * If no kickoff is scheduled, relaxed polling is used.
 * If the kickoff is in the past, live polling is triggered.
 * If the kickoff is within 10 minutes, prelive polling is used.
 *
 * @param nextKickoff The timestamp of the next scheduled kickoff.
 * @return PollMode The selected polling mode.
 */
PollMode determinePollMode(time_t nextKickoff) {
    time_t now = time(nullptr);                                                 ///< Get current system time

    if (nextKickoff == 0)
        return POLL_MODE_RELAXED;                                               ///< No kickoff scheduled → relaxed mode

    double diff = difftime(nextKickoff, now);                                   ///< Time difference in seconds

    if (diff <= 0)
        return POLL_MODE_LIVE;                                                  ///< Kickoff is now or in the past → live mode

    if (diff <= 600)
        return POLL_MODE_PRELIVE;                                               ///< Kickoff within 10 minutes → prelive mode

    return POLL_MODE_RELAXED;                                                   ///< Default fallback → relaxed mode
}

/**
 * @brief Checks whether the current matchday has changed.
 *
 * Used to trigger reactive polling or updates.
 *
 * @return true If the matchday has changed.
 * @return false If no change was detected.
 */
bool checkForMatchdayChanges() {
    return currentMatchdayChanged;
}

/**
 * @brief Returns the polling delay in milliseconds based on the current PollMode.
 *
 * Also logs the selected mode and delay in human-readable format.
 *
 * @param mode The polling mode to evaluate.
 * @return uint32_t The delay in milliseconds before the next poll.
 */
uint32_t getPollDelay(PollMode mode) {
    uint32_t polltime = 0;

    switch (mode) {
        case POLL_MODE_ONCE:
            polltime = POLL_NOWAIT;                                             ///< Immediate polling
            break;
        case POLL_MODE_LIVE:
            polltime = POLL_DURING_GAME;                                        ///< Frequent polling during live match
            break;
        case POLL_MODE_REACTIVE:
            polltime = POLL_GET_ALL_CHANGES;                                    ///< Polling for all changes
            break;
        case POLL_MODE_PRELIVE:
            polltime = POLL_10MIN_BEFORE_KICKOFF;                               ///< Polling shortly before kickoff
            break;
        case POLL_MODE_RELAXED:
            polltime = POLL_NORMAL;                                             ///< Default relaxed polling interval
            break;
        default:
            polltime = POLL_NORMAL;                                             ///< Fallback to relaxed interval
            break;
    }

    int totalseconds = polltime / 1000;
    int min          = totalseconds / 60;
    int sec          = totalseconds % 60;

    // Log the current polling mode and delay in minutes and seconds
    Liga->ligaPrintln("PollManager is in mode %S and will wait for %d:%02d minutes", pollModeToString(currentPollMode), min, sec);

    return polltime;
}

/**
 * @brief Sends an HTTP GET request to the specified URL and stores the response.
 *
 * Uses the Arduino HTTPClient to perform the request.
 *
 * @param url The target URL for the GET request.
 * @param response A reference to a String where the response will be stored.
 * @return true If the request was successful and a response was received.
 * @return false If the request failed or returned an error code.
 */
bool sendRequest(const String& url, String& response) {
    HTTPClient http;
    http.begin(url);                                                            ///< Initialize HTTP client with target URL

    int httpCode = http.GET();                                                  ///< Perform GET request

    if (httpCode > 0) {
        response = http.getString();                                            ///< Store response body
        http.end();                                                             ///< Clean up HTTP client
        return true;
    } else {
        Serial.printf("HTTP Fehler: %s\n", http.errorToString(httpCode).c_str()); ///< Log error
        http.end();                                                             ///< Clean up even on failure
        return false;
    }
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

/**
 * @brief HTTP event handler for polling the next match list from OpenLigaDB.
 *
 * Handles incoming HTTP data, deserializes the JSON response, and extracts upcoming matches.
 * Matches are categorized into "next" (soonest) and "planned" (later) based on kickoff time.
 *
 * @param evt Pointer to the HTTP client event structure.
 * @return esp_err_t ESP_OK on success, or error code on failure.
 */
esp_err_t _http_event_handler_pollForNextMatchList(esp_http_client_event_t* evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            // Append incoming chunk to the global JSON buffer
            if (evt->data_len > 0) {
                jsonBuffer.append((char*)evt->data, evt->data_len);
            }
            break;

        case HTTP_EVENT_ON_FINISH: {
            // Allocate JSON document with estimated size (20% overhead)
            DynamicJsonDocument doc(jsonBuffer.length() * 1.2);

            // Attempt to parse JSON from buffer
            DeserializationError error = deserializeJson(doc, jsonBuffer);
            if (error) {
                Liga->ligaPrintln("pollForNextMatchList JSON-Error at matchday data: %s", error.c_str());
                jsonBuffer.clear();                                             ///< Clear buffer to prepare for next request
                break;
            }

            JsonArray matches = doc.as<JsonArray>();
            time_t    now     = time(nullptr);
            time_t    minDiff = INT32_MAX;

            // First pass: find the smallest time difference to current time
            for (JsonObject match : matches) {
                const char* kickoffStr = match["matchDateTime"] | "";
                if (strlen(kickoffStr) == 0)
                    continue;

                struct tm tm = {};
                strptime(kickoffStr, "%Y-%m-%dT%H:%M:%S", &tm);
                tm.tm_isdst    = -1;                                            ///< Let system determine daylight saving
                time_t kickoff = mktime(&tm);
                time_t diff    = kickoff > now ? kickoff - now : INT32_MAX;

                if (diff < minDiff) {
                    minDiff = diff;                                             ///< Update minimum time difference
                }
            }

            // Second pass: collect matches with minimal time difference
            ligaNextMatchCount = 0;
            ligaPlanMatchCount = 0;

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
                    continue;                                                   ///< Ignore matches in the past

                if (diff == minDiff && ligaNextMatchCount < ligaMaxTeams) {
                    // Store match as "next" match
                    nextMatches[ligaNextMatchCount].matchID = matchID;
                    nextMatches[ligaNextMatchCount].kickoff = kickoff;

                    const char* team1                     = match["team1"]["teamName"] | "Team A";
                    const char* team2                     = match["team2"]["teamName"] | "Team B";
                    nextMatches[ligaNextMatchCount].team1 = team1;
                    nextMatches[ligaNextMatchCount].team2 = team2;

                    Liga->ligaPrintln("Match %u: %s vs %s, Kickoff %s", matchID, team1, team2, kickoffStr);
                    ligaNextMatchCount++;
                } else {
                    // Store match as "planned" match
                    planMatches[ligaPlanMatchCount].matchID = matchID;

                    const char* kickoffStr = match["matchDateTime"] | "";
                    struct tm   tm         = {};
                    strptime(kickoffStr, "%Y-%m-%dT%H:%M", &tm);
                    tm.tm_isdst                             = -1;
                    time_t kickoff                          = mktime(&tm);
                    planMatches[ligaPlanMatchCount].kickoff = kickoff;

                    const char* team1                     = match["team1"]["teamName"] | "Team A";
                    const char* team2                     = match["team2"]["teamName"] | "Team B";
                    planMatches[ligaPlanMatchCount].team1 = team1;
                    planMatches[ligaPlanMatchCount].team2 = team2;

                    Liga->ligaPrintln("planned Match %u: %s vs %s, Kickoff %s", matchID, team1, team2, kickoff);
                    ligaPlanMatchCount++;
                }
            }

            // Log summary of matches found
            Liga->ligaPrintln("found next matches: %d", ligaNextMatchCount);
            Liga->ligaPrintln("found planned matches: %d", ligaPlanMatchCount);

            jsonBuffer.clear();                                                 ///< Reset buffer for next request
            break;
        }

        case HTTP_EVENT_ERROR:
            Liga->ligaPrintln("error while looking for next matches");          ///< Log HTTP error
            break;
    }

    return ESP_OK;
}

/**
 * @brief Builds a list of upcoming matches for the current matchday, optionally offset by ONE to check next Matchday.
 * Constructs the appropriate OpenLigaDB API URL and performs an HTTPS request to retrieve match data.
 * Validates the matchday range and logs diagnostics if verbose mode is enabled.
 *
 * @param matchdayOffset Offset from the current matchday (can be positive or negative).
 * @return true If the request was successful and match data was retrieved.
 * @return false If the matchday is invalid or the HTTP request failed.
 */
bool LigaTable::pollForNextMatchList(int matchdayOffset) {
    // Validate matchday range (1–34 inclusive)
    if (ligaMatchday + matchdayOffset > 34 || ligaMatchday + matchdayOffset < 1) {
        Liga->ligaPrintln("pollForNextMatchList: invalide %d (current matchday %d)", ligaMatchday + matchdayOffset, ligaMatchday);
        return false;
    }

    char url[128];                                                              ///< Sufficiently sized buffer for full API URL

    // Construct API URL for match data
    sprintf(url, "https://api.openligadb.de/getmatchdata/%s/%d/%d", leagueShortcut(activeLeague), ligaSeason, ligaMatchday + matchdayOffset);

    // Configure HTTP client for secure request
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
        ligaPrintln("get next match list (season = %d, matchday = %d)", ligaSeason, ligaMatchday + matchdayOffset); ///< Log request details
        }
    #endif

    // Initialize HTTP client
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
        return false;

    // Perform HTTPS request
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ligaPrintln("get next match list HTTP-Fehler: %s", esp_err_to_name(err)); ///< Log error
        return false;
    }

    // Clean up HTTP client resources
    esp_http_client_cleanup(client);

    return err == ESP_OK;                                                       ///< Return true if request succeeded
}

// =========== Poll for Goals in live matches ===========================
esp_err_t _http_event_handler_pollForGoalsInLiveMatches(esp_http_client_event_t* evt) {
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
                Serial.printf("JSON-Fehler: %s\n", error.c_str());
                break;
            }
            String team1Name = doc["team1"]["teamName"] | "";
            String team2Name = doc["team2"]["teamName"] | "";

            uint8_t prevScoreTeam1 = 0;
            uint8_t prevScoreTeam2 = 0;

            JsonArray goals = doc["goals"];

            for (JsonObject goal : goals) {
                uint32_t matchID = doc["matchID"] | 0;
                if (matchID != liveMatchID)
                    continue;                                                   // Filter MatchID

                uint32_t    goalID     = goal["goalID"] | 0;
                uint8_t     minute     = goal["matchMinute"] | 0;
                uint8_t     scoreTeam1 = goal["scoreTeam1"] | 0;
                uint8_t     scoreTeam2 = goal["scoreTeam2"] | 0;
                uint32_t    scorerID   = goal["goalGetterID"] | 0;
                const char* scorer     = goal["goalGetterName"] | "";
                bool        isPenalty  = goal["isPenalty"] | false;
                bool        isOwnGoal  = goal["isOwnGoal"] | false;
                bool        isOvertime = goal["isOvertime"] | false;
                const char* comment    = goal["comment"] | "";

                String scoringTeam;                                             // which team scored the goal?
                if (scoreTeam1 > prevScoreTeam1) {
                    scoringTeam = team1Name;
                } else if (scoreTeam2 > prevScoreTeam2) {
                    scoringTeam = team2Name;
                } else {
                    scoringTeam = "unknown";
                }

                prevScoreTeam1 = scoreTeam1;
                prevScoreTeam2 = scoreTeam2;

                if (goalID > lastGoalID) {                                      // new Goal-ID
                    Serial.printf("goal in minute %u' scrored by %s for %s\n", minute, scorer, scoringTeam.c_str());

                    Serial.println("⚽ Goal-EVENT");
                    Serial.printf(" → Goal-ID: %u\n", goalID);
                    Serial.printf(" → minute: %u\n", minute);
                    Serial.printf(" → score: %u:%u\n", scoreTeam1, scoreTeam2);
                    Serial.printf(" → scorer: %s (ID: %u)\n", scorer, scorerID);
                    Serial.printf(" → penalty: %s\n", isPenalty ? "yes" : "no");
                    Serial.printf(" → owngoal: %s\n", isOwnGoal ? "yes" : "no");
                    Serial.printf(" → overtime: %s\n", isOvertime ? "yes" : "no");
                    if (comment && strlen(comment) > 0) {
                        Serial.printf(" → comment: %s\n", comment);
                    }
                    Serial.println("-----------------------------");
                }
                // Optional: Daten speichern
                if (liveGoalCount < 30) {
                    goalsInfos[liveGoalCount].goalID        = goalID;
                    goalsInfos[liveGoalCount].matchID       = matchID;
                    goalsInfos[liveGoalCount].goalMinute    = minute;
                    goalsInfos[liveGoalCount].result        = String(scoreTeam1) + ":" + String(scoreTeam2);
                    goalsInfos[liveGoalCount].scoringPlayer = scorer;
                    goalsInfos[liveGoalCount].scoringTeam   = scoringTeam;
                    goalsInfos[liveGoalCount].isOwnGoal     = isOwnGoal;
                    goalsInfos[liveGoalCount].isPenalty     = isPenalty;
                    goalsInfos[liveGoalCount].isOvertime    = isOvertime;
                    liveGoalCount++;
                    lastGoalID = goalID;                                        // zuletzt verarbeitete Tor-ID
                }
            }

            bool matchFinished = false;
            matchFinished      = doc["matchIsFinished"] | false;
            if (matchFinished)
                ligaFiniMatchCount++;                                           // count finished matches

            jsonBuffer.clear();
            break;
        }

        case HTTP_EVENT_ERROR:
            Serial.println("Fehler beim Abrufen der Tor-Daten.");
            break;
    }

    return ESP_OK;
}

void pollForGoalsInLiveMatches(LiveMatchGoalInfo goalInfos[]) {
    liveGoalCount      = 0;
    ligaFiniMatchCount = 0;                                                     // reset goal count
    for (uint8_t i = 0; i < ligaLiveMatchCount; ++i) {
        liveMatchID = liveMatches[i].matchID;
        char url[128];
        snprintf(url, sizeof(url), "https://api.openligadb.de/getmatchdata/%d", liveMatchID);

        jsonBuffer.clear();

        esp_http_client_config_t config    = {};
        config.url                         = url;
        config.host                        = "api.openligadb.de";
        config.user_agent                  = flapUserAgent;
        config.event_handler               = _http_event_handler_pollForGoalsInLiveMatches;
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

        esp_http_client_cleanup(client);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    if (liveGoalCount == 0) {
        Liga->ligaPrintln("no live goals found");
    }

    if (ligaLiveMatchCount == ligaFiniMatchCount) {
        Liga->ligaPrintln("no live matches, no goals to fetch");
        matchIsLive = false;                                                    // rest live flag
    }
}
// HTTP Event Handler for live matches
esp_err_t _http_event_handler_pollForLiveMatches(esp_http_client_event_t* evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (evt->data_len > 0) {
                if (jsonBuffer.capacity() < 32 * 1024) {
                    jsonBuffer.reserve(32 * 1024);
                }

                jsonBuffer.append((char*)evt->data, evt->data_len);
            }
            break;

        case HTTP_EVENT_ON_FINISH: {
            size_t jsonSize = jsonBuffer.size();
            Liga->ligaPrintln("(_http_event_handler_pollForLiveMatches) JSON-Buffer size: %d", jsonSize);
            DynamicJsonDocument     doc(30 * 1024);                             // Reserve etwas mehr Speicher
            StaticJsonDocument<512> filter;
            filter[0]["matchID"]            = true;
            filter[0]["matchDateTime"]      = true;
            filter[0]["matchIsFinished"]    = true;
            filter[0]["team1"]["teamName1"] = true;
            filter[0]["team2"]["teamName2"] = true;

            DeserializationError error = deserializeJson(doc, jsonBuffer);
            if (error) {
                Liga->ligaPrintln("(_http_event_handler_pollForLiveMatches) JSON-Error: %s", error.c_str());
                jsonBuffer.clear();
                return ESP_FAIL;
            }

            if (!doc.is<JsonArray>()) {
                Liga->ligaPrintln("(_http_event_handler_pollForLiveMatches) unexpected JSON-Format");
                jsonBuffer.clear();
                return ESP_FAIL;
            }

            ligaLiveMatchCount  = 0;
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
                    liveMatches[ligaLiveMatchCount].matchID = match["matchID"] | 0;
                    liveMatches[ligaLiveMatchCount].kickoff = kickoffTime;
                    liveMatches[ligaLiveMatchCount].team1   = name1;
                    liveMatches[ligaLiveMatchCount].team2   = name2;
                    ligaLiveMatchCount++;
                    Liga->ligaPrintln("recognized Live matches: %d", ligaLiveMatchCount);
                }
                // determine nearest kickoff time
                if (currentNextKickoffTime == 0 || kickoffTime < currentNextKickoffTime) {
                    currentNextKickoffTime = kickoffTime;
                    nextKickoffString      = kickoffStr;
                    nextKickoffChanged     = true;
                    showNextKickoff();
                }
            }

            if (ligaLiveMatchCount > 0) {
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
            for (uint8_t i = 0; i < snapshot.teamCount && i < ligaMaxTeams; ++i) {
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

    time_t     now      = time(NULL);                                           // get local time as time_t
    struct tm* timeinfo = localtime(&now);                                      // convert to local time structure
    strftime(lastScanTimestamp, sizeof(lastScanTimestamp), "%d.%m.%Y %H:%M:%S", timeinfo); // save last scan time as string

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
    if (ligaMatchday == 0 || ligaSeason == 0) {
        return POLL_MODE_ONCE;                                                  // no matchday known yet -> fetch once
    }
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

    for (uint8_t i = 0; i < newSnap.teamCount && i < ligaMaxTeams; ++i) {
        const LigaRow& newRow = newSnap.rows[i];

        // Suche passendes Team im alten Snapshot
        const LigaRow* oldRow = nullptr;
        for (uint8_t j = 0; j < oldSnap.teamCount && j < ligaMaxTeams; ++j) {
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
            if (scorerCount < ligaMaxTeams) {
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
