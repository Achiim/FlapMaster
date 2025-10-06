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
#include "Liga.h"
#include "esp_http_client.h"
#include "FlapTasks.h"

#define WIFI_SSID "DEIN_SSID"
#define WIFI_PASS "DEIN_PASS"

// initialize global variables
char   jsonBuffer[24 * 1024];                                                   // buffer for deserialization in event-handlers
size_t jsonBufferPos                = 0;                                        // write position in json buffer
bool   jsonBufferPrepared           = false;                                    // bupper not preparted
int    realJsonBufferSize           = 0;                                        // cunked buffers size cummulated
String currentLastChangeOfMatchday  = "";                                       // openLigaDB Matchday change state
String previousLastChangeOfMatchday = "";                                       // openLigaDB Matchday change state
char   lastScanTimestamp[32]        = {0};
bool   currentMatchdayChanged       = false;                                    // no chances

League activeLeague = League::BL1;                                              // use default BL1
int    ligaMaxTeams = LIGA1_MAX_TEAMS;
int    ligaSeason   = 0;                                                        // global unknown Season
int    ligaMatchday = 0;                                                        // global unknown Matchday
int    liveMatchID  = 0;                                                        // iD of current live match

time_t currentNextKickoffTime  = 0;
time_t previousNextKickoffTime = 0;
bool   nextKickoffChanged      = false;
bool   nextKickoffFarAway      = true;
bool   ligaConnectionRefused   = false;
bool   matchIsLive             = false;
bool   isSomeThingNew          = false;

MatchInfo liveMatches[MAX_MATCHES_PER_MATCHDAY] = {};                           // max. 10 Live-matches
int       ligaLiveMatchCount                    = 0;                            // number of live matches in current matchday
int       ligaFiniMatchCount                    = 0;                            // number of finisched live matches in current matchday
int       ligaLiveMatchIndex                    = 0;                            // index to current live matches

MatchInfo nextMatches[MAX_MATCHES_PER_MATCHDAY] = {};                           // max. 10 next-matches
int       ligaNextMatchCount                    = 0;                            // number of next matches in current matchday

MatchInfo planMatches[MAX_MATCHES_PER_MATCHDAY] = {};                           // max. 10 planned-matches
int       ligaPlanMatchCount                    = 0;                            // number of next matches in current matchday

LiveMatchGoalInfo goalsInfos[MAX_GOALS_PER_MATCHDAY] = {};                      // max. 50 live goals
int               liveGoalCount                      = 0;
int               lastGoalID                         = 0;                       // last goal ID to detect new goals

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

uint8_t currScoreTeam1 = 0;                                                     // current score of team 1
uint8_t currScoreTeam2 = 0;                                                     // current score of team 2
uint8_t prevScoreTeam1 = 0;                                                     // previous score of team 1
uint8_t prevScoreTeam2 = 0;                                                     // previous score of team 2

// ===== Routines ==========================================

std::map<uint32_t, LiveMatchGoalInfo*> getLatestGoalsPerMatch() {
    std::map<uint32_t, LiveMatchGoalInfo*> latest;
    for (auto& goal : goalsInfos) {
        latest[goal.matchID] = &goal;                                           // Zeiger auf Original!
    }
    return latest;
}

String getTeam1Name(uint32_t matchID) {
    for (uint8_t i = 0; i < ligaLiveMatchCount; ++i) {
        if (liveMatches[i].matchID == matchID) {
            return String(liveMatches[i].team1.c_str());
        }
    }
    return "???";
}
String getTeam2Name(uint32_t matchID) {
    for (uint8_t i = 0; i < ligaLiveMatchCount; ++i) {
        if (liveMatches[i].matchID == matchID) {
            return String(liveMatches[i].team2.c_str());
        }
    }
    return "???";
}

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
    waitForTime();                                                              // wait for time to be synchronized
    createLigaInstance();

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

    Liga->ligaPrintln("actuall season: %d", season);                            ///< Log calculated season
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
        String ipStr = ip.toString();
        Liga->ligaPrintln("DNS ok: %s -> %s", "api.openligadb.de", ipStr.c_str()); ///< Helpful diagnostic for name resolution
    } else {
        Liga->ligaPrintln("DNS failed!");                                       ///< DNS failure hint (will affect subsequent HTTP)
        return false;
    }
    return true;
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
        case POLL_MODE_NONE:
            return "POLL_MODE_NONE";
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
        case FETCH_LIVE_GOALS:
            return "FETCH_LIVE_GOALS";
        case SHOW_NEXT_KICKOFF:
            return "SHOW_NEXT_KICKOFF";
        case CALC_LIVE_TABLE:
            return "CALC_LIVE_TABLE";
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
        case POLL_MODE_NONE:
            activeCycle       = noCycle;
            activeCycleLength = sizeof(noCycle) / sizeof(PollScope);
            break;
        default:
            activeCycle       = relaxedCycle;                                   ///< Default fallback cycle
            activeCycleLength = sizeof(relaxedCycle) / sizeof(PollScope);
            break;
    }

    // Log the selected polling cycle for debugging
    Liga->ligaPrintln("%s = %s", pollModeToString(currentPollMode), pollCycleToString(activeCycle, activeCycleLength).c_str());
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

/*
PollMode determinePollMode(time_t nextKickoff) {
   time_t now = time(nullptr);                                                  ///< Get current system time

   if (nextKickoff == 0)
       return POLL_MODE_RELAXED;                                                ///< No kickoff scheduled → relaxed mode

   double diff = difftime(nextKickoff, now);                                    ///< Time difference now until kickoff in seconds

   if (diff > 0 && diff <= TEN_MINUTES_BEFORE_MATCH)
       return POLL_MODE_PRELIVE;                                                ///< Kickoff in future within next 10 minutes → prelive mode

   if (diff <= 0 && diff >= -MAX_MATCH_DURATION)
       return POLL_MODE_LIVE;                                                   ///< Kickoff is now or max. 2,5h in the past → live mode

   return POLL_MODE_RELAXED;                                                    ///< Default fallback → relaxed mode
}
*/
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
    uint32_t pollWaitTime = 0;

    switch (mode) {
        case POLL_MODE_NONE:
            pollWaitTime = POLL_WAIT;                                           ///< Stopp polling for POLL_WAIT minutes
            break;
        case POLL_MODE_ONCE:
            pollWaitTime = POLL_NOWAIT;                                         ///< Immediate polling
            break;
        case POLL_MODE_LIVE:
            pollWaitTime = POLL_DURING_GAME;                                    ///< Frequent polling during live match
            break;
        case POLL_MODE_REACTIVE:
            pollWaitTime = POLL_GET_ALL_CHANGES;                                ///< Polling for all changes
            break;
        case POLL_MODE_PRELIVE:
            pollWaitTime = POLL_10MIN_BEFORE_KICKOFF;                           ///< Polling shortly before kickoff
            break;
        case POLL_MODE_RELAXED:
            /*
                if (diffSecondsUntilKickoff && diffSecondsUntilKickoff <= 1 * 60 * 60) {
                    // kickoff nears within next 1h → relaxed polling but quicker
                    pollWaitTime = diffSecondsUntilKickoff / 3;                 ///< quicker relaxed polling interval
                } else {
                    // Case 4: no immediate kickoff and no game in progress → relaxed polling
                    pollWaitTime = POLL_NORMAL;                                 ///< Default relaxed polling interval
                }
     */
            pollWaitTime = POLL_NORMAL;                                         ///< Default relaxed polling interval
            break;
        default:
            pollWaitTime = POLL_NORMAL;                                         ///< Fallback to relaxed interval
            break;
    }

    int totalseconds = pollWaitTime / 1000;
    int min          = totalseconds / 60;
    int sec          = totalseconds % 60;

    // Log the current polling mode and delay in minutes and seconds
    Liga->ligaPrintln("PollManager is in mode %S and will wait for %d minutes %02d seconds", pollModeToString(currentPollMode), min, sec);
    return pollWaitTime;
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
    int matchdayOffset = 0;
    if (evt->user_data != nullptr) {
        matchdayOffset = *((int*)evt->user_data);                               // get matchdayOffset
    }

    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA: {
            if (readHttpResult(evt)) {
                vTaskDelay(1);                                                  // feed watch dog
            } else {
                Liga->ligaPrintln("(_http_event_handler_pollForNextMatchList) JSON-Buffer error");
            }
            break;
        }
        case HTTP_EVENT_ON_FINISH: {
            StaticJsonDocument<512> filter;
            filter[0]["matchID"]       = true;
            filter[0]["matchDateTime"] = true;

            // Team-Namen hinzufügen
            filter[0]["team1"]["teamName"] = true;
            filter[0]["team2"]["teamName"] = true;

            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
            DynamicJsonDocument doc(realJsonBufferSize * 1.2);
            #pragma GCC diagnostic pop
            if (!deserializeHttpResult(doc, DeserializationOption::Filter(filter))) {
                Liga->ligaPrintln("(_http_event_handler_pollForNextMatchList) JSON-Buffer not deserialized");
                jsonBufferPrepared = false;
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
            for (int i = 0; i < MAX_MATCHES_PER_MATCHDAY; i++) {
                nextMatches[i].clear();                                         ///< Clear next matches array
                planMatches[i].clear();                                         ///< Clear planned matches array
            }

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

                    Liga->ligaPrintln("next Match %u: Kickoff %s | %s vs %s", matchID, kickoffStr, team1, team2);
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

                    Liga->ligaPrintln("plan Match %u: Kickoff %s | %s vs %s", matchID, kickoffStr, team1, team2);
                    ligaPlanMatchCount++;
                }
            }

            // Log summary of matches found
            Liga->ligaPrintln("found next matches: %d for matchday: %d", ligaNextMatchCount, ligaMatchday + matchdayOffset);
            Liga->ligaPrintln("found planned matches: %d for matchday: %d", ligaPlanMatchCount, ligaMatchday + matchdayOffset);

            jsonBufferPrepared = false;
            realJsonBufferSize = 0;
            break;
        }

        case HTTP_EVENT_DISCONNECTED: {
            jsonBufferPrepared = false;
            realJsonBufferSize = 0;
            break;
        }
        case HTTP_EVENT_ERROR: {
            Liga->ligaPrintln("error while looking for next matches");          ///< Log HTTP error
            jsonBufferPrepared = false;
            realJsonBufferSize = 0;
            break;
        }
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
    int offset = matchdayOffset;                                                // to be transfered to event handler
    // Validate matchday range (1–34 inclusive)
    if (ligaMatchday + matchdayOffset > 34 || ligaMatchday + matchdayOffset < 1) {
        Liga->ligaPrintln("pollForNextMatchList: invalide %d (current matchday %d)", ligaMatchday + matchdayOffset, ligaMatchday);
        return false;
    }

    // Construct API URL for match data
    String url = "https://api.openligadb.de/getmatchdata/" + String(leagueShortcut(activeLeague)) + "/" + String(ligaSeason) + "/" +
                 String(ligaMatchday + matchdayOffset);

    // Configure HTTP client for secure request
    esp_http_client_config_t config = {};
    config.url                      = url.c_str();
    config.host                     = "api.openligadb.de";
    config.user_agent               = flapUserAgent;
    config.use_global_ca_store      = false;
    config.event_handler            = _http_event_handler_pollForNextMatchList;
    config.cert_pem                 = OPENLIGA_CA;
    config.buffer_size              = 2048;
    config.timeout_ms               = 5000;
    config.user_data                = (void*)&offset;                           // matchday offset

    #ifdef LIGAVERBOSE
        {
        TraceScope trace;
        ligaPrintln("get next match list for %s: season = %d, matchday = %d", leagueName(activeLeague), ligaSeason,
        ligaMatchday + matchdayOffset);                                         ///< Log request details
        }
    #endif

    // Initialize HTTP client
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
        return false;

    // Perform HTTPS request
    esp_err_t err = esp_http_client_perform(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    client = nullptr;

    if (err != ESP_OK) {
        ligaPrintln("poll next match list HTTP-Fehler: %s", esp_err_to_name(err)); ///< Log error
        return false;
    }

    return err == ESP_OK;                                                       ///< Return true if request succeeded
}
/*
//
void calculateDeltasForMatch(uint32_t matchID) {
    int prevScore1 = 0, prevScore2 = 0, prevPts1 = 0, prevPts2 = 0;
    for (auto& goal : goalsInfos) {
        if (goal.matchID != matchID)
            continue;

        // delta goals shot
        goal.goalsTeam1Delta = goal.scoreTeam1 - prevScore1;
        goal.goalsTeam2Delta = goal.scoreTeam2 - prevScore2;

        // delta points got
        int currPts1          = (goal.scoreTeam1 > goal.scoreTeam2) ? 3 : (goal.scoreTeam1 == goal.scoreTeam2 ? 1 : 0);
        int currPts2          = (goal.scoreTeam2 > goal.scoreTeam1) ? 3 : (goal.scoreTeam1 == goal.scoreTeam2 ? 1 : 0);
        goal.pointsTeam1Delta = currPts1 - prevPts1;
        goal.pointsTeam2Delta = currPts2 - prevPts2;

        // actualize previous goals and points
        prevScore1 = goal.scoreTeam1;
        prevScore2 = goal.scoreTeam2;
        prevPts1   = currPts1;
        prevPts2   = currPts2;
    }
}
*/

// ============================================================================
// @brief Safely append incoming HTTP data chunks to a fixed-size char buffer.
// @param evt                Pointer to the ESP HTTP client event structure.
// ============================================================================
bool readHttpResult(esp_http_client_event_t* evt) {
    if (!jsonBufferPrepared) {
        jsonBufferPos      = 0;
        realJsonBufferSize = 0;
        jsonBufferPrepared = true;
        memset(jsonBuffer, 0, sizeof(jsonBuffer));                              // clear buffer
    }

    if (evt->data_len == 0 || evt->data == nullptr) {
        Serial.println("[read HTTP result] evt->data_len = 0 or nullptr\n");
        return false;
    }

    size_t copyLen = evt->data_len;
    if (jsonBufferPos + copyLen >= sizeof(jsonBuffer)) {                        // prevent buffer overflow
        copyLen = sizeof(jsonBuffer) - jsonBufferPos - 1;
        Serial.printf("[read HTTP result] JSON buffer overflow (%u bytes truncated)\n", evt->data_len - copyLen);
    }

    memcpy(&jsonBuffer[jsonBufferPos], evt->data, copyLen);                     // copy http result to jsonBuffer
    jsonBufferPos += copyLen;
    realJsonBufferSize += copyLen;

    return true;
}

// --- Variante 1: ohne Filter ---
bool deserializeHttpResult(DynamicJsonDocument& doc) {
    if (!jsonBufferPrepared || realJsonBufferSize == 0) {
        Serial.println("[deserializeHttpResult] Buffer not prepared or empty");
        return false;
    }

    Liga->ligaPrintln("JSON-Buffer used size vs real size: %d : %d", realJsonBufferSize, sizeof(jsonBuffer));
    DeserializationError error = deserializeJson(doc, jsonBuffer);
    if (error) {
        Serial.printf("[deserializeHttpResult] JSON error: %s\n", error.c_str());
        return false;
    }
    return true;
}

// --- Variante 2: mit Filter ---
template <typename TFilter>
bool deserializeHttpResult(DynamicJsonDocument& doc, const TFilter& filter) {
    if (!jsonBufferPrepared || realJsonBufferSize == 0) {
        Serial.println("[deserializeHttpResult] Buffer not prepared or empty");
        return false;
    }

    Liga->ligaPrintln("JSON-Buffer used size vs real size: %d : %d", realJsonBufferSize, sizeof(jsonBuffer));
    DeserializationError error = deserializeJson(doc, jsonBuffer, DeserializationOption::Filter(filter));
    if (error) {
        Serial.printf("[deserializeHttpResult] JSON (filtered) error: %s\n", error.c_str());
        return false;
    }
    return true;
}

/**
 * @brief HTTP event handler for polling goals in live matches.
 *
 * This function processes HTTP events received during a request to fetch goal data
 * for live football matches. It handles incoming data, parses JSON, and extracts
 * goal-related information for further processing and display.
 *
 * @param evt Pointer to the HTTP client event structure.
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t _http_event_handler_pollForGoalsInLiveMatches(esp_http_client_event_t* evt) {
    int matchIndex = -1;
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA: {
            if (readHttpResult(evt)) {
                vTaskDelay(1);                                                  // feed watch dog
            } else {
                Liga->ligaPrintln("(_http_event_handler_pollForGoalsInLiveMatches) JSON-Buffer error");
            }
            break;
        }
        case HTTP_EVENT_ON_FINISH: {
            // search matchIndex in liveMatches[i]
            for (int i = 0; i < ligaLiveMatchCount; i++) {
                if (liveMatches[i].matchID == liveMatchID) {
                    matchIndex = i;
                }
            }

            // if (matchIndex < 0)
            //     Liga->ligaPrintln("(_http_event_handler_pollForGoalsInLiveMatches) no live match with matchID %d", liveMatchID);

            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
            DynamicJsonDocument doc(realJsonBufferSize * 1.2);
            #pragma GCC diagnostic pop
            if (!deserializeHttpResult(doc)) {
                Liga->ligaPrintln("(_http_event_handler_pollForGoalsInLiveMatches) JSON-Buffer not deserialized");
                jsonBufferPrepared = false;
                break;
            }

            /// @brief Extract team names from JSON.
            String team1Name = doc["team1"]["teamName"] | "";
            String team2Name = doc["team2"]["teamName"] | "";

            uint32_t matchID = doc["matchID"] | 0;
            if (matchID != liveMatchID) {                                       ///< is goals matchID same as matchID from requested live match
                // Serial.printf("Live match ID = %d not found\n", liveMatchID);
                return false;                                                   ///< live match not found.
            }

            if (doc["matchID"] != 0) {
                // for each match generate dummy entry: 0:0
                // Liga->ligaPrintln("Goal index %d is dummy entry: 0:0 for match %d", liveGoalCount, liveMatchID);
                goalsInfos[liveGoalCount].matchID       = doc["matchID"];
                goalsInfos[liveGoalCount].goalID        = 0;
                goalsInfos[liveGoalCount].scoreTeam1    = 0;
                goalsInfos[liveGoalCount].scoreTeam2    = 0;
                goalsInfos[liveGoalCount].goalMinute    = 0;
                goalsInfos[liveGoalCount].result        = "0:0";
                goalsInfos[liveGoalCount].scoringTeam   = "";
                goalsInfos[liveGoalCount].scoringPlayer = "";
                goalsInfos[liveGoalCount].isOwnGoal     = false;
                goalsInfos[liveGoalCount].isOvertime    = false;
                goalsInfos[liveGoalCount].isPenalty     = false;
                liveGoalCount++;                                                // next goal
            }

            prevScoreTeam1 = 0;
            prevScoreTeam2 = 0;

            JsonArray goals = doc["goals"];

            /// @brief Iterate over all goal entries in the JSON array.
            for (JsonObject goal : goals) {
                vTaskDelay(1);                                                  // feed watch dog
                /// @brief Extract goal details.
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

                // determine scoring team
                currScoreTeam1          = scoreTeam1;
                currScoreTeam2          = scoreTeam2;
                std::string scoringTeam = "";

                // Liga->ligaPrintln("evaluate goals for live match with index  = %d", matchIndex);
                // Team 1 scored
                if (currScoreTeam1 > prevScoreTeam1 && liveMatches[matchIndex].team1.length() > 0) {
                    scoringTeam = liveMatches[matchIndex].team1;
                }
                // Team 2 scored
                else if (currScoreTeam2 > prevScoreTeam2 && liveMatches[matchIndex].team2.length() > 0) {
                    scoringTeam = liveMatches[matchIndex].team2;
                }

                Liga->ligaPrintln("Goal in matchID = %d for %s in minute %u' scored by %s: %s - %s", liveMatchID, scoringTeam.c_str(), minute, scorer,
                                  liveMatches[matchIndex].team1.c_str(), liveMatches[matchIndex].team2.c_str());
                // Liga->ligaPrintln("Goal in match %d for %s in minute %u' scored by %s", matchID, scoringTeam.c_str(), minute, scorer);

                /// @brief Display goal details if it's a new goal.
                // if (goalID > lastGoalID) {
                /*
                                                        Serial.println("⚽ Goal-EVENT");
                                                        Serial.printf(" → Goal-ID: %u\n", goalID);
                                                        Serial.printf(" → Minute: %u\n", minute);
                                                        Serial.printf(" → Score: %u:%u\n", scoreTeam1, scoreTeam2);
                                                        Serial.printf(" → Scorer: %s (ID: %u)\n", scorer, scorerID);
                                                        Serial.printf(" → Penalty: %s\n", isPenalty ? "yes" : "no");
                                                        Serial.printf(" → Own goal: %s\n", isOwnGoal ? "yes" : "no");
                                                        Serial.printf(" → Overtime: %s\n", isOvertime ? "yes" : "no");
                                                        if (comment && strlen(comment) > 0) {
                                                            Serial.printf(" → Comment: %s\n", comment);
                                                        }
                                                        Serial.println("-----------------------------");
                                    */
                //}

                auto& liveGoal = goalsInfos[liveGoalCount];

                /// @brief Optionally store goal data if within matchday limit.
                if (liveGoalCount < MAX_GOALS_PER_MATCHDAY) {
                    liveGoal.goalID        = goalID;
                    liveGoal.matchID       = matchID;
                    liveGoal.goalMinute    = minute;
                    liveGoal.scoreTeam1    = scoreTeam1;
                    liveGoal.scoreTeam2    = scoreTeam2;
                    liveGoal.result        = String(scoreTeam1) + ":" + String(scoreTeam2);
                    liveGoal.scoringPlayer = scorer;
                    liveGoal.scoringTeam   = scoringTeam.c_str();
                    liveGoal.isOwnGoal     = isOwnGoal;
                    liveGoal.isPenalty     = isPenalty;
                    liveGoal.isOvertime    = isOvertime;

                    lastGoalID = goalID;                                        ///< Update last processed goal ID.
                    liveGoalCount++;                                            // next goal
                }
            }

            /// @brief Check if the match has finished.
            bool matchFinished = doc["matchIsFinished"] | false;
            if (matchFinished)
                ligaFiniMatchCount++;                                           ///< Increment finished match counter.

            /// @brief Log if no goals were found during polling.
            matchIsLive = (ligaLiveMatchCount > 0);                             // actualize live match flag
            if (!matchIsLive)
                Liga->ligaPrintln("no Live-Match detected");

            /// @brief Check if all live matches have finished.
            if (ligaLiveMatchCount == ligaFiniMatchCount) {
                Liga->ligaPrintln("no live matches, no goals to fetch");
                matchIsLive = false;                                            ///< Reset live match flag.
            }

            realJsonBufferSize = 0;
            jsonBufferPrepared = false;
            break;
        }

        case HTTP_EVENT_DISCONNECTED:
            jsonBufferPrepared = false;
            realJsonBufferSize = 0;
            break;

        case HTTP_EVENT_ERROR:
            /// @brief Handle HTTP error event.
            // Liga->ligaPrintln("(_http_event_handler_pollForGoalsInLiveMatches) JSON-Buffer reserved: %d", jsonBuffer.capacity());
            Serial.println("Error while fetching goal data.");
            jsonBufferPrepared = false;
            realJsonBufferSize = 0;
            break;
    }

    return ESP_OK;
}

/**
 * @brief Polls the OpenLigaDB API for goals in currently live matches.
 *
 * This function iterates over all live matches, sends HTTP requests to fetch match data,
 * and processes goal events using a dedicated event handler. It resets goal counters,
 * clears previous goal data, and updates the live match status accordingly.
 *
 */
void pollForGoalsInLiveMatches() {
    liveGoalCount      = 0;
    ligaFiniMatchCount = 0;                                                     ///< Reset counters for goals and finished matches.

    /// @brief Clear all stored goal information from previous polling.
    for (int i = 0; i < MAX_GOALS_PER_MATCHDAY; ++i) {
        goalsInfos[i].clear();
    }

    /// @brief Iterate over all currently live matches.
    for (ligaLiveMatchIndex = 0; ligaLiveMatchIndex < ligaLiveMatchCount; ++ligaLiveMatchIndex) {
        liveMatchID = liveMatches[ligaLiveMatchIndex].matchID;                  // liveMatchID to be checked in eventHandler

        /// @brief Construct the API URL for the current match.
        String url = "https://api.openligadb.de/getmatchdata/" + String(liveMatchID);

        /// @brief Configure the HTTP client for the API request.
        esp_http_client_config_t config    = {};
        config.url                         = url.c_str();
        config.host                        = "api.openligadb.de";
        config.user_agent                  = flapUserAgent;
        config.event_handler               = _http_event_handler_pollForGoalsInLiveMatches;
        config.cert_pem                    = OPENLIGA_CA;
        config.use_global_ca_store         = false;
        config.skip_cert_common_name_check = false;
        config.buffer_size                 = 2048;
        config.timeout_ms                  = 5000;

        /// @brief Initialize the HTTP client.
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client == NULL) {
            Liga->ligaPrintln("HTTP-Client error while initializing");
            continue;                                                           ///< Skip to next match if client setup fails.
        }

        /// @brief Perform the HTTP request to fetch match data.
        esp_err_t err = esp_http_client_perform(client);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        client = nullptr;

        if (err != ESP_OK) {
            Liga->ligaPrintln("error while request for live goals: %s", esp_err_to_name(err));
        }
        /// @brief Delay between requests to avoid overloading the API.
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

esp_err_t _http_event_handler_pollForLiveMatches(esp_http_client_event_t* evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA: {
            if (readHttpResult(evt)) {
                vTaskDelay(1);                                                  // feed watch dog
            } else {
                Liga->ligaPrintln("(_http_event_handler_pollForLiveMatches) JSON-Buffer error");
            }
            break;
        }

        case HTTP_EVENT_ON_FINISH: {
            StaticJsonDocument<512> filter;
            filter[0]["matchID"]           = true;
            filter[0]["matchDateTime"]     = true;
            filter[0]["matchIsFinished"]   = true;
            filter[0]["team1"]["teamName"] = true;
            filter[0]["team2"]["teamName"] = true;

            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
            DynamicJsonDocument doc(realJsonBufferSize * 1.2);
            #pragma GCC diagnostic pop
            if (!deserializeHttpResult(doc, DeserializationOption::Filter(filter))) {
                Liga->ligaPrintln("(_http_event_handler_pollForLiveMatches) JSON-Buffer not deserialized");
                jsonBufferPrepared = false;
                break;
            }

            for (int i = 0; i < MAX_MATCHES_PER_MATCHDAY; ++i)
                liveMatches[i].clear();

            ligaLiveMatchCount  = 0;                                            // init count of live matches
            const time_t now    = time(nullptr);
            const time_t maxAge = MAX_MATCH_DURATION;

            for (JsonObject match : doc.as<JsonArray>()) {
                if (match["matchIsFinished"] == true)
                    continue;                                                   // skipp finished matches

                const char* kickoffStr = match["matchDateTime"];
                if (!kickoffStr)
                    continue;                                                   // skip match without kickoff

                struct tm tmKickoff = {};
                if (!strptime(kickoffStr, "%Y-%m-%dT%H:%M:%S", &tmKickoff)) {
                    Liga->ligaPrintln("Ungültiges Datum: %s", kickoffStr);
                    continue;                                                   // skip match with invalide kickoff
                }

                tmKickoff.tm_isdst = -1;
                time_t kickoffTime = mktime(&tmKickoff);
                double delta       = difftime(now, kickoffTime);

                if (delta >= 0 && delta <= maxAge) {
                    const char* name1 = match["team1"]["teamName"];
                    const char* name2 = match["team2"]["teamName"];

                    if (name1 && name2) {
                        Liga->ligaPrintln("LIVE: %s vs %s", name1, name2);
                        Liga->ligaPrintln("Kickoff since: %s", kickoffStr);
                    } else {
                        Liga->ligaPrintln("LIVE-Match recognized, but TeamName missing");
                    }

                    liveMatches[ligaLiveMatchCount].matchID = match["matchID"] | 0;
                    liveMatches[ligaLiveMatchCount].kickoff = kickoffTime;
                    liveMatches[ligaLiveMatchCount].team1   = name1;
                    liveMatches[ligaLiveMatchCount].team2   = name2;
                    ligaLiveMatchCount++;
                    Liga->ligaPrintln("Recognized Live-Match #: %d", ligaLiveMatchCount);
                }

                if (currentNextKickoffTime == 0 || kickoffTime < currentNextKickoffTime) {
                    currentNextKickoffTime = kickoffTime;
                    nextKickoffString      = kickoffStr;
                    nextKickoffChanged     = true;
                    showNextKickoff();
                }
            }

            matchIsLive = (ligaLiveMatchCount > 0);                             // actualize live match flag
            if (!matchIsLive)
                Liga->ligaPrintln("no Live-Match detected");

            jsonBufferPrepared = false;
            realJsonBufferSize = 0;
            break;
        }

        case HTTP_EVENT_DISCONNECTED:
            jsonBufferPrepared = false;
            realJsonBufferSize = 0;
            break;

        case HTTP_EVENT_ERROR: {
            jsonBufferPrepared = false;
            realJsonBufferSize = 0;
            break;
        }

        default:
            break;
    }

    return ESP_OK;
}
/**
 * @brief Fetches live matches of the current matchday and logs them.
 *
 * This method constructs a request URL for the OpenLigaDB API based on the active league,
 * season, and matchday. It initializes an HTTP client, performs the request, and delegates
 * response handling to the live match event handler.
 */
void LigaTable::pollForLiveMatches() {
    /// @brief Construct the API URL for the current matchday.
    String url =
        "https://api.openligadb.de/getmatchdata/" + String(leagueShortcut(activeLeague)) + "/" + String(ligaSeason) + "/" + String(ligaMatchday);

    /// @brief Configure the HTTP client for the request.
    esp_http_client_config_t config    = {};
    config.url                         = url.c_str();
    config.host                        = "api.openligadb.de";
    config.user_agent                  = flapUserAgent;
    config.use_global_ca_store         = false;
    config.event_handler               = _http_event_handler_pollForLiveMatches;
    config.cert_pem                    = OPENLIGA_CA;
    config.use_global_ca_store         = false;
    config.skip_cert_common_name_check = false;
    config.buffer_size                 = 2048;
    config.timeout_ms                  = 5000;

    /// @brief Initialize the HTTP client.
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        Liga->ligaPrintln("HTTP client could not be initialized");
        return;
    }

    /// @brief Perform the HTTP request to fetch live match data.
    esp_err_t err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        Liga->ligaPrintln("Error while fetching live matches: %s", esp_err_to_name(err));
    }
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

    if (diffSecondsUntilKickoff >= 0 && diffSecondsUntilKickoff <= SIXTY_MINUTES_BEFORE_MATCH) {
        nextKickoffFarAway = false;                                             // kickoff in less then 10 Minutes
        #ifdef LIGAVERBOSE
            {
            Liga->ligaPrintln("we are 60 minutes before next kickoff (next live is not far away)");
            }
        #endif
    } else if (diffSecondsUntilKickoff < 0 && diffSecondsUntilKickoff >= -MAX_MATCH_DURATION) {
        nextKickoffFarAway = false;                                             // match is live or even over ( kickoff < 2,5h)
        matchIsLive        = true;
        #ifdef LIGAVERBOSE
            {
            Liga->ligaPrintln("match is live or in overtime");
            }
        #endif
    } else {
        nextKickoffFarAway = true;                                              // kickoff will be far away in the future
// matchIsLive        = false; // some other match can be live
#ifdef LIGAVERBOSE
    {
    Liga->ligaPrintln("next match is over or far away in the future");
    }
#endif
    }
}

// Event-Handler to get kickoff date of next upcoming match
esp_err_t _http_event_handler_pollForNextKickoff(esp_http_client_event_t* evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA: {
            if (readHttpResult(evt)) {
                vTaskDelay(1);                                                  // feed watch dog
            } else {
                Liga->ligaPrintln("(_http_event_handler_pollForNextKickoff) JSON-Buffer error");
            }
            break;
        }
        case HTTP_EVENT_ON_FINISH: {
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
            DynamicJsonDocument doc(realJsonBufferSize * 1.2);
            #pragma GCC diagnostic pop
            if (!deserializeHttpResult(doc)) {
                Liga->ligaPrintln("(_http_event_handler_pollForNextKickoff) JSON-Buffer not deserialized");
                break;
            }

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

            jsonBufferPrepared = false;
            realJsonBufferSize = 0;
            break;
        }

        case HTTP_EVENT_DISCONNECTED: {
            jsonBufferPrepared = false;
            realJsonBufferSize = 0;
            break;
        }
        case HTTP_EVENT_ERROR: {
            Liga->ligaPrintln("(_http_event_handler_pollForNextKickoff) undefined next kickoff");
            nextKickoffString      = "";                                        // time string undefined
            struct tm tmKickoff    = {};
            currentNextKickoffTime = mktime(&tmKickoff);                        // undefined next Kickoff time
            nextKickoffChanged     = false;
            jsonBufferPrepared     = false;
            realJsonBufferSize     = 0;
            break;
        }

        default:
            break;
    }
    return ESP_OK;
}

// Ermittlung des nächsten Anpfiffzeitpunkt
bool LigaTable::pollForNextKickoff() {
    String url = "https://api.openligadb.de/getnextmatchbyleagueshortcut/" + String(leagueShortcut(activeLeague));

    esp_http_client_config_t config    = {};
    config.url                         = url.c_str();
    config.host                        = "api.openligadb.de";
    config.user_agent                  = flapUserAgent;
    config.event_handler               = _http_event_handler_pollForNextKickoff;
    config.cert_pem                    = OPENLIGA_CA;
    config.use_global_ca_store         = false;
    config.skip_cert_common_name_check = false;
    config.buffer_size                 = 100;
    config.timeout_ms                  = 5000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
        return false;

    esp_err_t err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        #ifdef LIGAVERBOSE
            {
            TraceScope trace;
            ligaPrintln("poll for next kickoff HTTP-Fehler: %s", esp_err_to_name(err));
            }
        #endif
        return false;
    }
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
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA: {
            if (readHttpResult(evt)) {
                vTaskDelay(1);                                                  // feed watch dog
            } else {
                Liga->ligaPrintln("(_http_event_handler_pollForTable) JSON-Buffer error");
            }
            break;
        }

        case HTTP_EVENT_ON_FINISH: {
            Liga->ligaPrintln("JSON-Buffer used size vs real size: %d : %d", realJsonBufferSize, sizeof(jsonBuffer));
            currentLastChangeOfMatchday = jsonBuffer;
            currentLastChangeOfMatchday.remove(0, 1);                           // entfernt das erste Zeichen (das Anführungszeichen)
            currentLastChangeOfMatchday.remove(19);                             // entfernt alles ab Position 19 (also ".573")

            if (previousLastChangeOfMatchday != currentLastChangeOfMatchday) {
                currentMatchdayChanged = true;
                #ifdef LIGAVERBOSE
                    {
                    TraceScope trace;
                    Liga->ligaPrintln("OpenLigaDB has changes against my local data, please fetch actual data");
                    }
                #endif
            } else {
                currentMatchdayChanged = false;
                #ifdef LIGAVERBOSE
                    {
                    TraceScope trace;
                    Liga->ligaPrintln("OpenLigaDB has no changes");
                    }
                #endif
            }
            previousLastChangeOfMatchday = currentLastChangeOfMatchday;         // remember actual chang as last change
            #ifdef LIGAVERBOSE
                struct tm tmLastChange = {};
                strptime(currentLastChangeOfMatchday.c_str(), "%Y-%m-%dT%H:%M:%S", &tmLastChange);
                tmLastChange.tm_isdst = -1;                                     // automatische Sommerzeit-Erkennung
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
                Liga->ligaPrintln("last change date of matchday %d in openLigaDB: %s", ligaMatchday, currentLastChangeOfMatchday.c_str());
                Liga->ligaPrintln("this was %d days, %d hours, %d minutes, %d seconds ago", days, hours, minutes, seconds);
                }
            #endif

            jsonBufferPrepared = false;
            realJsonBufferSize = 0;
            break;
        }

        case HTTP_EVENT_DISCONNECTED: {
            jsonBufferPrepared = false;
            realJsonBufferSize = 0;
            break;                                                              // do same as when HTTP_EVENT_ERROR
        }
        case HTTP_EVENT_ERROR: {
            jsonBufferPrepared = false;
            realJsonBufferSize = 0;
        }
    }

    return ESP_OK;
}

// Ermittlung der letzten Änderung des aktuellen Spieltags
bool LigaTable::pollForChanges() {
    // ausreichend groß für die komplette URL
    String url =
        "https://api.openligadb.de/getlastchangedate/" + String(leagueShortcut(activeLeague)) + "/" + String(ligaSeason) + "/" + String(ligaMatchday);

    esp_http_client_config_t config    = {};
    config.url                         = url.c_str();
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
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    client = nullptr;

    if (err != ESP_OK) {
        #ifdef ERRORVERBOSE
            {
            TraceScope trace;
            ligaPrintln("poll for change HTTP-Fehler: %s", esp_err_to_name(err));
            }
        #endif
        currentMatchdayChanged = false;                                         // no change detected
        return false;
    }

    return err == ESP_OK;
}

// zur Ermittlung des aktuellen Spieltags
esp_err_t _http_event_handler_pollCurrentMatchday(esp_http_client_event_t* evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_HEADER: {
            break;
        }
        case HTTP_EVENT_ON_DATA: {
            if (readHttpResult(evt)) {
                vTaskDelay(1);                                                  // feed watch dog
            } else {
                Liga->ligaPrintln("(_http_event_handler_pollCurrentMatchday) JSON-Buffer error");
            }
            break;
        }

        case HTTP_EVENT_ON_FINISH: {
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
            DynamicJsonDocument doc(realJsonBufferSize * 1.2);
            #pragma GCC diagnostic pop

            if (!deserializeHttpResult(doc)) {
                Liga->ligaPrintln("(_http_event_handler_pollCurrentMatchday) JSON-Buffer not deserialized");
                break;
            }

            ligaMatchday = doc["groupOrderID"];                                 // fetch current matchday

            jsonBufferPrepared = false;
            realJsonBufferSize = 0;
            break;
        }

        case HTTP_EVENT_DISCONNECTED: {
            jsonBufferPrepared = false;
            realJsonBufferSize = 0;
            break;
        }
        case HTTP_EVENT_ERROR: {
            jsonBufferPrepared = false;
            realJsonBufferSize = 0;
        }
    }
    return ESP_OK;
}

// zur Ermittlung der aktuellen Tabelle
esp_err_t _http_event_handler_pollForTable(esp_http_client_event_t* evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_HEADER: {
            break;
        }
        case HTTP_EVENT_ON_DATA: {
            if (readHttpResult(evt)) {
                vTaskDelay(1);                                                  // feed watch dog
            } else {
                Liga->ligaPrintln("(_http_event_handler_pollForTable) JSON-Buffer error");
            }
            break;
        }
        case HTTP_EVENT_ON_FINISH: {
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
            DynamicJsonDocument doc(realJsonBufferSize * 1.2);
            #pragma GCC diagnostic pop
            if (!deserializeHttpResult(doc)) {
                Liga->ligaPrintln("(_http_event_handler_pollForTable) JSON-Buffer not deserialized");
                break;
            }

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

            jsonBufferPrepared = false;
            realJsonBufferSize = 0;
            break;
        }

        case HTTP_EVENT_DISCONNECTED: {
            jsonBufferPrepared = false;
            realJsonBufferSize = 0;
            break;
        }

        case HTTP_EVENT_ERROR: {
            jsonBufferPrepared = false;
            realJsonBufferSize = 0;
            break;
        }
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
/*
    if (matchIsLive) {
        {
            TraceScope trace;
            ligaPrintln("don't fetch: Flap Display Liga is generating live table from live goals");
        }
        return false;                                                           // if some game is live don't fecht table from openLigaDB
    }
*/
#ifdef LIGAVERBOSE
    {
    TraceScope trace;
    ligaPrintln("get actual Liga-Table for %s: season = %d, matchday = %d", leagueName(activeLeague), ligaSeason, ligaMatchday);
    }
#endif

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
        return false;

    esp_err_t err = esp_http_client_perform(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    client = nullptr;

    if (err != ESP_OK) {
        if (err == ESP_ERR_HTTP_CONNECT) {
            ligaConnectionRefused = true;
            Liga->ligaPrintln("openLigaDB refused connection; no chance to retry shortly -> wait 3-4 minutes to reconnect");
        } else {
            Liga->ligaPrintln("poll for table HTTP-Fehler: %s", esp_err_to_name(err));
        }
        return false;
    }
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
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    client = nullptr;

    if (err != ESP_OK) {
        #ifdef ERRORVERBOSE
            {
            TraceScope trace;
            ligaPrintln("poll for current matchday HTTP-Fehler: %s", esp_err_to_name(err));
            }
        #endif
        return false;
    }
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
    if (WiFi.status() == WL_CONNECTED) {                                        // already connected
        {
            #ifdef LIGAVERBOSE
                {
                TraceScope trace;
                Liga->ligaPrintln(" WLAN - allready connected");                // log connected
                }
            #endif
        }
        return true;
    }

    WiFi.mode(WIFI_STA);                                                        // set station mode (client)
    WiFi.setHostname("FlapMasterESP32");                                        // set name in WiFi
    WiFi.setSleep(false);                                                       // disable power-save mode to avoid TLS wake issues
    #ifdef LIGAVERBOSE
        {
        TraceScope trace;
        Liga->ligaPrintln("connect to WLAN - Hagelschlag");                     // log start of connection attempt
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
            if (matchIsLive) {
                Liga->ligaPrintln("no need to fetch next kickoff if a match is live");
                break;                                                          // no need to fetch next kickoff if a match is live
            }
            Liga->pollForNextKickoff();                                         // get next kickoff time from openLigaDB
            vTaskDelay(pdMS_TO_TICKS(2000));
            break;
        }
        case SHOW_NEXT_KICKOFF:
            showNextKickoff();                                                  // get next kickoff time from stored value
            break;

        case FETCH_LIVE_GOALS: {                                                // get live goals from live matches
            pollForGoalsInLiveMatches();
            vTaskDelay(pdMS_TO_TICKS(1000));
            break;
        }

        case CALC_LIVE_TABLE: {
            LigaSnapshot baseTable;
            LigaSnapshot tempLiveTable;
            memcpy(&baseTable, &snap[snapshotIndex ^ 1], sizeof(LigaSnapshot));
            if (recalcLiveTable(baseTable, tempLiveTable))                      // recalculate table with live goals
                printLigaLiveTable(tempLiveTable);                              // print recalculated live table
            vTaskDelay(pdMS_TO_TICKS(1000));
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
    // POLL MODE enter conditions
    if (ligaConnectionRefused) {
        Liga->ligaPrintln("switch Poll Mode because: openLigaDB prevents connection");
        ligaConnectionRefused = true;
        return POLL_MODE_NONE;
    }
    if (ligaMatchday == 0 || ligaSeason == 0) {                                 // enter ONCE polling because general data missing
        Liga->ligaPrintln("switch Poll Mode because: ligaMatchday = 0 or ligaSeason = 0");
        return POLL_MODE_ONCE;
    }
    if (matchIsLive) {                                                          // enter LIVE polling because some game is live
        Liga->ligaPrintln("switch Poll Mode because: there are live matches");
        return POLL_MODE_LIVE;
    }

    if (!nextKickoffFarAway) {                                                  // enter PRELIVE polling because next match is in sight
        Liga->ligaPrintln("switch Poll Mode because: next Kickoff is near");
        return POLL_MODE_PRELIVE;
    }

    if (isSomeThingNew) {                                                       // enter REACTIVE polling because there are changes in OLDB
        Liga->ligaPrintln("switch Poll Mode because: openLigaDB has changes");
        return POLL_MODE_REACTIVE;
    }

    // POLL MODE leave conditions
    switch (currentPollMode) {
        case POLL_MODE_LIVE:
        case POLL_MODE_PRELIVE:
            if (!matchIsLive && nextKickoffFarAway) {
                Liga->ligaPrintln("switch Poll Mode because: no live match and next Kickoff is far away");
                return POLL_MODE_REACTIVE;
            }
            break;
        case POLL_MODE_REACTIVE:
            if (!matchIsLive && nextKickoffFarAway) {
                Liga->ligaPrintln("switch Poll Mode because: no live match and next Kickoff is far away");
                return POLL_MODE_RELAXED;
            }
            break;
        case POLL_MODE_NONE:
            if (!ligaConnectionRefused) {
                Liga->ligaPrintln("poll wait time over -> try to continue in once pollmode");
                return POLL_MODE_ONCE;
            }
        case POLL_MODE_ONCE:
            if (!matchIsLive && nextKickoffFarAway) {
                Liga->ligaPrintln("switch Poll Mode because: no live match and next Kickoff is far away");
                return POLL_MODE_RELAXED;
            }
            break;
    }

    Liga->ligaPrintln("no valide condition for next Poll Mode found -> no change of poll mode");
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

void sortSnapshot(LigaSnapshot& snapshot) {
    // Temporäre Kopie der Zeilen
    std::vector<LigaRow> tempRows(snapshot.rows, snapshot.rows + snapshot.teamCount);

    // Sortieren nach Punkten, Diff, Tore
    std::sort(tempRows.begin(), tempRows.end(), [](const LigaRow& a, const LigaRow& b) {
        if (a.pkt != b.pkt)
            return a.pkt > b.pkt;
        if (a.diff != b.diff)
            return a.diff > b.diff;
        if (a.g != b.g)
            return a.g > b.g;
        return strcmp(a.team, b.team) < 0;                                      // optional alphabetisch
    });

    // Zurückkopieren ins Snapshot + Positionen setzen
    for (uint8_t i = 0; i < snapshot.teamCount; ++i) {
        snapshot.rows[i]     = tempRows[i];
        snapshot.rows[i].pos = i + 1;
    }
}

//
void applyMatchResult(LigaRow* row1, LigaRow* row2, int goals1, int goals2) {
    // Spiele erhöhen
    row1->sp++;
    row2->sp++;

    // Tore / Gegentore addieren
    row1->g += goals1;
    row1->og += goals2;
    row1->diff = row1->g - row1->og;

    row2->g += goals2;
    row2->og += goals1;
    row2->diff = row2->g - row2->og;

    // Punkte + Bilanz
    if (goals1 > goals2) {
        row1->pkt += 3;
        row1->w++;
        row2->l++;
    } else if (goals2 > goals1) {
        row2->pkt += 3;
        row2->w++;
        row1->l++;
    } else {
        row1->pkt++;
        row2->pkt++;
        row1->d++;
        row2->d++;
    }
}
//
LigaRow* findRow(LigaSnapshot& snapshot, const std::string& teamName) {
    for (uint8_t i = 0; i < snapshot.teamCount; ++i) {
        if (strcmp(snapshot.rows[i].team, teamName.c_str()) == 0) {
            return &snapshot.rows[i];
        }
    }
    return nullptr;                                                             // nicht gefunden
}

// recalculate Table
bool recalcLiveTable(LigaSnapshot& baseTable, LigaSnapshot& tempTable) {
    memcpy(&tempTable, &baseTable, sizeof(LigaSnapshot));                       // actualize Table on a copy
    bool liveTableChanged = false;

    for (uint8_t m = 0; m < ligaLiveMatchCount; ++m) {                          // operate all live matches
        const auto& match = liveMatches[m];

        const LiveMatchGoalInfo* lastGoal = nullptr;
        for (int g = liveGoalCount - 1; g >= 0; --g) {                          // search last goal for this match (highest goalID)

            if (goalsInfos[g].matchID == match.matchID) {
                lastGoal = &goalsInfos[g];                                      // pointer to last goal in this match found
                break;
            }
        }
        if (!lastGoal)
            continue;                                                           // unexpected: there is no goal available (also no 0:0) for this live match

        LigaRow* row1 = findRow(tempTable, match.team1);                        // search table row of match partner 1
        LigaRow* row2 = findRow(tempTable, match.team2);                        // search table row of match partner 2
        if (!row1 || !row2) {
            Liga->ligaPrintln("match partner team %s vs %s not found", match.team1.c_str(), match.team2.c_str());
            continue;
        }

        applyMatchResult(row1, row2, lastGoal->scoreTeam1, lastGoal->scoreTeam2); // calculate live table
        liveTableChanged = true;
    }

    if (!liveTableChanged)
        return false;
    sortSnapshot(tempTable);
    return true;
}

//
uint16_t utf8Length(const String& input) {
    uint16_t    count = 0;
    const char* s     = input.c_str();

    while (*s) {
        uint8_t c = *s;
        if ((c & 0x80) == 0x00) {                                               // 1-byte ASCII
            s += 1;
        } else if ((c & 0xE0) == 0xC0) {                                        // 2-byte
            s += 2;
        } else if ((c & 0xF0) == 0xE0) {                                        // 3-byte
            s += 3;
        } else if ((c & 0xF8) == 0xF0) {                                        // 4-byte (rare, e.g. emojis)
            s += 4;
        } else {
            s += 1;                                                             // ungültig, aber weiter
        }
        count += 1;
    }

    return count;
}
//
String padUtf8ToWidth(const String& input, uint8_t width) {
    String result = input;
    int    pad    = width - utf8Length(result);
    while (pad-- > 0)
        result += " ";
    return result;
}
//
void printLigaLiveTable(LigaSnapshot& snapshot) {
    Serial.println("┌─────┬──────────────────────────┬────┬────┬────┬────┬────┬────┬──────┬─────┐");
    Serial.println("│ Pos │ Mannschaft               │ Sp │  S │  U │  N │  T │ GT │ Diff │ Pkt │");
    Serial.println("├─────┼──────────────────────────┼────┼────┼────┼────┼────┼────┼──────┼─────┤");

    for (uint8_t i = 0; i < snapshot.teamCount; ++i) {
        const LigaRow& row      = snapshot.rows[i];
        String         teamName = padUtf8ToWidth(row.team, 24);

        Serial.printf("│ %3d │ %-24s │ %2d │ %2d │ %2d │ %2d │ %2d │ %2d │ %4d │ %3d │\n", row.pos, teamName.c_str(), row.sp, row.w, row.d, row.l,
                      row.g, row.og, row.diff, row.pkt);
    }

    Serial.println("└─────┴──────────────────────────┴────┴────┴────┴────┴────┴────┴──────┴─────┘");
}