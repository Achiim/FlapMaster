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
#include "FlapTasks.h"
#include "Liga.h"
#include "LigaHelper.h"
#include "secret.h"                                                             // const char* ssid, const char* password
#include "cert.all"                                                             // certificate for https with OpenLigaDB

League activeLeague = League::BL1;

// 1. Bundesliga
static const DfbMap DFB1[] = {{"FC Bayern München", "FCB", 1},     {"Borussia Dortmund", "BVB", 7}, {"RB Leipzig", "RBL", 13},
                              {"Bayer 04 Leverkusen", "B04", 2},   {"1. FSV Mainz 05", "M05", 8},   {"Borussia Mönchengladbach", "BMG", 14},
                              {"Eintracht Frankfurt", "SGE", 3},   {"VfL Wolfsburg", "WOB", 9},     {"1. FC Union Berlin", "FCU", 15},
                              {"SC Freiburg", "SCF", 4},           {"TSG Hoffenheim", "TSG", 10},   {"VfB Stuttgart", "VFB", 16},
                              {"SV Werder Bremen", "SVW", 5},      {"FC Augsburg", "FCA", 11},      {"1. FC Köln", "KOE", 17},
                              {"1. FC Heidenheim 1846", "HDH", 6}, {"Hamburger SV", "HSV", 12},     {"FC St. Pauli", "STP", 18}};

// 2. Bundesliga (ergänzbar – strikt nach Namen)
static const DfbMap DFB2[] = {{"Hertha BSC", "BSC", 19},           {"VfL Bochum", "BOC", 25},         {"Eintracht Braunschweig", "EBS", 30},
                              {"SV Darmstadt 98", "SVD", 20},      {"Fortuna Düsseldorf", "F95", 26}, {"SV 07 Elversberg", "ELV", 31},
                              {"SpVgg Greuther Fürth", "SGF", 21}, {"Hannover 96", "H96", 27},        {"1. FC Kaiserslautern", "FCK", 32},
                              {"Karlsruher SC", "KSC", 22},        {"1. FC Magdeburg", "FCM", 28},    {"1. FC Nürnberg", "FCN", 33},
                              {"SC Paderborn 07", "SCP", 23},      {"Holstein Kiel", "KSV", 29},      {"FC Schalke 04", "S04", 34},
                              {"Preußen Münster", "PRM", 24},      {"Arminia Bielefeld", "DSC", 35},  {"Dynamo Dresden", "DYN", 0}};

// to be used for dynamic polling
static String         s_lastChange;                                             // dein vorhandener ISO-String
static time_t         s_lastChangeEpoch      = 0;                               // Epoch zum schnellen Vergleich
static time_t         s_nextKickoffEpoch     = 0;                               // nächster Anstoß (Epoch UTC)
static uint32_t       s_lastKickoffRefreshMs = 0;                               // wann zuletzt berechnet
static const uint32_t KICKOFF_REFRESH_MS     = 300000;                          // 5 min

/// Robust JSON GET helper: handles CL=0, chunked, retry, auto buffer/stream
bool LigaTable::httpGetJsonRobust(HTTPClient& http, WiFiClientSecure& client, const String& url, JsonDocument& doc, int maxRetry) {
    for (int attempt = 0; attempt < maxRetry; attempt++) {
        if (!http.begin(client, url)) {
            ligaPrintln("http.begin() failed @ %s", url.c_str());
            return false;
        }

        int code = http.GET();
        if (code != HTTP_CODE_OK) {
            ligaPrintln("HTTP GET %s -> %d", url.c_str(), code);
            http.end();
            delay(200);
            continue;
        }

        // --- Content-Length auswerten ---
        int                  len = http.getSize();                              // -1 = unbekannt
        DeserializationError err;

        if (len > 0 && len < 8192) {
            // Kleine Antwort → in String holen
            String payload = http.getString();
            err            = deserializeJson(doc, payload);
        } else {
            // Große/unbekannte Antwort → direkt streamen
            Stream* s = http.getStreamPtr();
            if (!s) {
                ligaPrintln("HTTP stream null @ %s", url.c_str());
                http.end();
                continue;
            }
            err = deserializeJson(doc, *s);
        }

        http.end();

        if (!err) {
            return true;                                                        // ✅ Erfolgreich
        }

        if (err == DeserializationError::IncompleteInput) {
            ligaPrintln("Warnung: JSON unvollständig @ %s, Daten evtl. nutzbar", url.c_str());
            return true;                                                        // trotzdem OK zurückgeben
        }

        ligaPrintln("JSON parse error @ %s: %s", url.c_str(), err.c_str());
        delay(200);
    }
    return false;
}

/**
 * @brief Perform an HTTPS GET request and parse the response as JSON.
 *
 * This helper wraps an HTTPClient with a WiFiClientSecure to fetch
 * JSON data from a given URL. It ensures time is synchronized (for TLS),
 * configures the HTTP client for non-chunked responses, and retries once
 * on error. If the response code is 200 (OK), it deserializes the JSON
 * into the provided ArduinoJson `JsonDocument`.
 *
 * Diagnostic logging is printed via Serial on connection failures,
 * HTTP errors, or JSON parse errors.
 *
 * @param http   Reference to an HTTPClient instance (reused across calls).
 * @param client Reference to a WiFiClientSecure (TLS) instance.
 * @param url    Target URL (HTTPS).
 * @param doc    Reference to an ArduinoJson document to populate.
 * @return       True on success (valid JSON parsed), false otherwise.
 */
bool LigaTable::httpGetJsonWith(HTTPClient& http, WiFiClientSecure& client, const char* url, JsonDocument& doc) {
    // ensure NTP time is valid before TLS handshake
    waitForTime();                                                              // wait for time to be synchronized

    // configure HTTP client
    http.setReuse(true);
    http.setConnectTimeout(8000);
    http.setTimeout(8000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.useHTTP10(true);                                                       // enforce HTTP/1.0 → no chunked transfer
    http.addHeader("Connection", "close");                                      // explicit close header
    http.setUserAgent("ESP32-Flap/1.0");
    http.addHeader("Accept", "application/json");
    http.addHeader("Accept-Encoding", "identity");

    const char* hdrs[] = {"Content-Type", "Content-Encoding", "Transfer-Encoding"};
    http.collectHeaders(hdrs, 3);

    // allow up to 2 attempts
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!http.begin(client, url)) {
            #ifdef ERRORVERBOSE
                {
                TraceScope trace;
                ligaPrintln("HTTP begin failed: %s  (freeHeap=%u)\n", url, (unsigned)ESP.getFreeHeap());
                }
            #endif
            http.end();
            delay(150);
            continue;
        }

        int code = http.GET();
        if (code != HTTP_CODE_OK) {
            #ifdef ERRORVERBOSE
                {
                TraceScope trace;
                ligaPrintln("GET %s -> %d (%s)\n", url, code, http.errorToString(code).c_str());
                }
            #endif
            http.end();
            if (code < 0)
                delay(150);                                                     // short retry delay on transport error
            continue;
        }
        // Content-Length prüfen
        const String cl = http.header("Content-Length");
        if (cl.length() && cl.toInt() == 0) {
            // leerer 200er → als transient werten
            http.end();
            delay(200);
            // einmaliger zweiter Versuch:
            if (!http.begin(client, url)) {
                delay(200);
                return false;
            }
            code = http.GET();
            if (code != HTTP_CODE_OK) {
                http.end();
                return false;
            }
        }

        Stream* s = http.getStreamPtr();
        if (s && s->peek() == 0xEF) {
            uint8_t bom[3];
            s->readBytes(bom, 3);
        }

        DeserializationError err = deserializeJson(doc, *s);
        http.end();
        if (err) {
            if (err == DeserializationError::IncompleteInput) {
                // kurzer Retry-Block
                delay(220);
                if (!http.begin(client, url))
                    return false;
                code = http.GET();
                if (code != HTTP_CODE_OK) {
                    http.end();
                    return false;
                }
                s = http.getStreamPtr();
                if (s && s->peek() == 0xEF) {
                    uint8_t bom[3];
                    s->readBytes(bom, 3);
                }
                err = deserializeJson(doc, *s);
                http.end();
                if (!err)
                    return true;
            }

            Serial.printf("JSON parse error @ %s: %s\n", url, err.c_str());
            Serial.printf("Bad JSON. CT=%s CE=%s TE=%s CL=%s\n", http.header("Content-Type").c_str(), http.header("Content-Encoding").c_str(),
                          http.header("Transfer-Encoding").c_str(), cl.c_str());
            return false;
        }
        return true;
        // get response stream
        s = http.getStreamPtr();
        // skip BOM if present
        if (s && s->peek() == 0xEF) {
            uint8_t bom[3];
            s->readBytes(bom, 3);
        }

        // parse JSON
        err = deserializeJson(doc, *s);
        http.end();

        if (err) {
            Serial.printf("JSON parse error @ %s: %s\n", url, err.c_str());

            // log diagnostic response headers
            const String ct = http.header("Content-Type");
            const String ce = http.header("Content-Encoding");
            const String te = http.header("Transfer-Encoding");
            const int    cl = http.header("Content-Length").toInt();
            #ifdef ERRORVERBOSE
                {
                TraceScope trace;
                ligaPrintln("Bad JSON. CT=%s CE=%s TE=%s CL=%d\n", ct.c_str(), ce.c_str(), te.c_str(), cl);
                }
            #endif
            return false;
        }

        // success
        return true;
    }

    // all attempts failed
    return false;
}

// ----- Kernfunktionen -----

/**
 * @brief Load the current matchday JSON for the selected league (BL1/BL2).
 *
 * Strategy:
 *  1) Try GET /getcurrentgroup/<lg> (where <lg> is "bl1" or "bl2"):
 *     - Accepts object, 1-element array, integer or string formats.
 *     - Extracts groupOrderID and (if present) leagueSeason.
 *  2) If season is still unknown, infer it via GET /getmatchdata/<lg>.
 *  3) Finally fetch the exact matchday via GET /getmatchdata/<lg>/<season>/<group>.
 *
 * Notes:
 *  - Uses HTTPS with your httpGetJsonRobust(), which already handles TLS setup.
 *  - Builds URLs with snprintf to avoid String temporaries where not needed.
 *
 * @param outDoc            Destination JsonDocument with the matchday data.
 * @param league            League selector (e.g., League::BL1 or League::BL2).
 * @param outSeason         Optional: receives resolved season (if non-null).
 * @param outGroupOrderId   Optional: receives resolved groupOrderID (if non-null).
 * @return true on success, false otherwise.
 */
bool LigaTable::loadCurrentMatchday(JsonDocument& outDoc, League league, int* outSeason, int* outGroupOrderId) {
    const char*      lg     = leagueShortcut(league);                           // "bl1" or "bl2"
    WiFiClientSecure client = makeSecureClient();
    HTTPClient       http;

    int season       = 0;
    int groupOrderID = 0;

    // --- 1) Get current group (safer parsing) ---
    {
        char url[128];
        snprintf(url, sizeof(url), "https://api.openligadb.de/getcurrentgroup/%s", lg);

        if (http.begin(client, url)) {
            int code = http.GET();
            if (code == HTTP_CODE_OK) {
                String               payload = http.getString();                // read full response
                JsonDocument         gdoc;
                DeserializationError err = deserializeJson(gdoc, payload);
                if (!err) {
                    if (gdoc.is<JsonObject>()) {
                        JsonObject g = gdoc.as<JsonObject>();
                        season       = g["leagueSeason"] | g["LeagueSeason"] | 0;
                        groupOrderID = g["groupOrderID"] | g["GroupOrderID"] | 0;
                    } else if (gdoc.is<JsonArray>() && gdoc.as<JsonArray>().size() > 0) {
                        JsonObject g = gdoc[0].as<JsonObject>();
                        season       = g["leagueSeason"] | g["LeagueSeason"] | 0;
                        groupOrderID = g["groupOrderID"] | g["GroupOrderID"] | 0;
                    }
                } else {
                    Serial.printf("[collector] JSON error @ getcurrentgroup: %s\n", err.c_str());
                }
            } else {
                Serial.printf("[collector] getcurrentgroup failed -> %d\n", code);
            }
            http.end();
        }
    }

    // --- 2) If season is still missing, fallback via getmatchdata/<lg> ---
    if (groupOrderID > 0 && season == 0) {
        char url[128];
        //        snprintf(url, sizeof(url), "https://api.openligadb.de/getmatchdata/%s", lg);

        if (http.begin(client, url)) {
            int code = http.GET();
            if (code == HTTP_CODE_OK) {
                String               payload = http.getString();
                JsonDocument         tmp;
                DeserializationError err = deserializeJson(tmp, payload);
                if (!err) {
                    JsonArray matches = tmp.as<JsonArray>();
                    if (!matches.isNull() && matches.size() > 0) {
                        season = matches[0]["leagueSeason"] | matches[0]["LeagueSeason"] | 0;
                    }
                }
            }
            http.end();
        }
    }

    // --- 3) Load the exact matchday ---
    if (groupOrderID > 0 && season > 0) {
        char url[160];
        snprintf(url, sizeof(url), "https://api.openligadb.de/getmatchdata/%s/%d/%d", lg, season, groupOrderID);
        Serial.printf("[collector] GET %s\n", url);

        if (httpGetJsonRobust(http, client, url, outDoc)) {
            if (outSeason)
                *outSeason = season;
            if (outGroupOrderId)
                *outGroupOrderId = groupOrderID;
            return true;
        }
        Serial.println("[collector] getmatchdata/<lg>/<season>/<group> failed (primary path)");
    }

    return false;
}

/**
 * @brief Collect new goal events from live matches of a given league.
 *
 * This function queries the current matchday from the OpenLigaDB API,
 * iterates through all matches, and extracts all goals that have not yet been seen.
 * Goals are sorted chronologically by the total score (s1+s2 ascending).
 *
 * @param league   The league to query (BL1 or BL2).
 * @param out      Pointer to an array of LiveGoalEvent where results are stored.
 * @param maxOut   Maximum number of goal events to write into @p out.
 * @return Number of new goals written into @p out.
 *
 * @note Uses per-match MatchState objects to avoid reporting the same goal twice.
 */
size_t LigaTable::collectNewGoalsAcrossLive(League league, LiveGoalEvent* out, size_t maxOut) {
    size_t outN = 0;

    // 1) URL für laufende Spiele (kleiner Endpoint!)
    char url[96];
    snprintf(url, sizeof(url), "https://api.openligadb.de/getmatchdata/%s", leagueShortcut(league));

    WiFiClientSecure client = makeSecureClient();
    HTTPClient       http;

    // 2) JSON laden
    DynamicJsonDocument doc(32768);                                             // 32 KB reicht, weil nur laufende Spiele
    if (!httpGetJsonRobust(http, client, url, doc)) {
        ligaPrintln("[getGoalsLive] fetch failed: %s", url);
        return 0;
    }

    // 3) Matches prüfen
    JsonArray matches = doc.as<JsonArray>();
    if (matches.isNull() || matches.size() == 0)
        return 0;

    const time_t nowT = toUtcTimeT(nowUTC_ISO());

    // 4) Schleife über alle laufenden Spiele
    for (JsonObject m : matches) {
        // Teamdaten
        JsonObject  t1      = m["team1"].isNull() ? m["Team1"] : m["team1"];
        JsonObject  t2      = m["team2"].isNull() ? m["Team2"] : m["team1"];
        const char* n1      = strOr(t1, "?", "teamName", "TeamName");
        const char* n2      = strOr(t2, "?", "teamName", "TeamName");
        int         team1Id = intOr(t1, 0, "teamId", "TeamId");
        int         team2Id = intOr(t2, 0, "teamId", "TeamId");

        const char* kickC = strOr(m, "", "matchDateTimeUTC", "MatchDateTimeUTC");
        if (!*kickC)
            continue;

        time_t kickT = toUtcTimeT(kickC);
        if (kickT == 0 || nowT < kickT)
            continue;                                                           // noch nicht gestartet

        int mid = intOr(m, 0, "matchID", "MatchID");
        if (mid <= 0)
            continue;

        // MatchState holen/anlegen
        MatchState* st = stateFor(static_cast<uint8_t>(league), mid);
        if (!st)
            continue;
        int maxSeen = st->lastGoalID;

        // Goals-Array
        JsonArray goals = !m["goals"].isNull() ? m["goals"].as<JsonArray>() : m["Goals"].as<JsonArray>();
        if (goals.isNull())
            continue;

        // letzte Updatezeit (optional für Logging)
        const char* lastUpd = strOr(m, "", "lastUpdateDateTime", "LastUpdateDateTime");

        // Alle Goals einsammeln
        for (JsonObject g : goals) {
            int gid = intOr(g, 0, "goalID", "GoalID");
            if (gid <= st->lastGoalID)
                continue;
            if (outN >= maxOut)
                break;

            LiveGoalEvent& ev = out[outN++];
            ev.matchID        = mid;
            ev.kickOffUTC     = kickC;
            ev.goalTimeUTC    = lastUpd;
            ev.minute         = intOr(g, -1, "matchMinute", "MatchMinute");
            ev.isPenalty      = boolOr(g, false, "isPenalty", "IsPenalty");
            ev.isOwnGoal      = boolOr(g, false, "isOwnGoal", "IsOwnGoal");
            ev.isOvertime     = boolOr(g, false, "isOvertime", "IsOvertime", "IsOverTime");
            ev.score1         = intOr(g, 0, "scoreTeam1", "ScoreTeam1");
            ev.score2         = intOr(g, 0, "scoreTeam2", "ScoreTeam2");
            ev.scorer         = strOr(g, "?", "goalGetterName", "GoalGetterName");

            ev.team1 = n1;
            ev.team2 = n2;

            // Wer hat getroffen?
            int scorerTeamId = intOr(g, 0, "scorerTeamID", "ScorerTeamID", "teamId");
            if (scorerTeamId == team1Id) {
                ev.scoredFor = n1;
            } else if (scorerTeamId == team2Id) {
                ev.scoredFor = n2;
            } else {
                // Fallback anhand der Score-Änderung
                if (ev.score1 > st->lastScore1)
                    ev.scoredFor = n1;
                else if (ev.score2 > st->lastScore2)
                    ev.scoredFor = n2;
                else
                    ev.scoredFor = "?";
            }

            // State updaten
            st->lastScore1 = ev.score1;
            st->lastScore2 = ev.score2;
            if (gid > maxSeen)
                maxSeen = gid;
        }

        if (maxSeen > st->lastGoalID)
            st->lastGoalID = maxSeen;

        if (outN >= maxOut)
            break;
    }

    return outN;
}

/**
 * @brief Retrieve the "last change" timestamp for a given season and group.
 *
 * This function queries the OpenLigaDB API endpoint
 *   /getlastchangedate/<league>/<season>/<group>
 * and extracts the last change date string. The API response format may
 * vary: it can be either a raw string or a JSON object containing a field
 * "lastChangeDate".
 *
 * Changes compared to the previous version:
 *  - Added a `League league` parameter to support both BL1 and BL2.
 *  - URL is built dynamically using leagueShortcut(league) instead of hard-coded "bl1".
 *
 * @param league  League selector (e.g., League::BL1 or League::BL2).
 * @param season  League season (e.g., 2025).
 * @param group   Group (matchday) number within the season.
 * @param out     Reference to a String that will receive the timestamp in ISO format.
 * @return true if a non-empty timestamp was successfully extracted, false otherwise.
 */
bool LigaTable::getLastChange(League league, int season, int group, String& out) {
    const char*      lg     = leagueShortcut(league);
    WiFiClientSecure client = makeSecureClient();
    HTTPClient       http;

    char url[160];
    snprintf(url, sizeof(url), "https://api.openligadb.de/getlastchangedate/%s/%d/%d", lg, season, group);

    ligaPrintln("[getLastChange] GET %s", url);

    if (!http.begin(client, url)) {
        ligaPrintln("http.begin() failed @ %s", url);
        return false;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        ligaPrintln("HTTP GET %s -> %d", url, code);
        http.end();
        return false;
    }

    out = http.getString();
    http.end();

    // JSON parse ist hier gar nicht nötig, weil out bereits ein ISO-Zeitstempel ist
    out.trim();
    return !out.isEmpty();
}

/**
 * @brief Retrieve the current season and group (matchday) for a given league.
 *
 * This function queries the rolling endpoint:
 *   /getmatchdata/<league>
 * where <league> is "bl1" or "bl2", and extracts from the first match:
 *   - leagueSeason
 *   - group.groupOrderID (matchday number)
 *
 * @param league League selector (e.g., League::BL1 or League::BL2).
 * @param season Reference to int that will receive the league season (e.g. 2025).
 * @param group  Reference to int that will receive the current matchday number.
 * @return true if both season and group were successfully extracted (>0), false otherwise.
 */
/**
 * @brief Determine current season and group (matchday) for a given league.
 *
 * Handles multiple possible formats from the API:
 *  - JSON object { "leagueSeason": ..., "groupOrderID": ... }
 *  - JSON array with one or more objects
 *  - Plain integer (groupOrderID only)
 *  - Plain string with a number
 *
 * @param league   League selector (BL1, BL2).
 * @param outSeason Output: season number.
 * @param outGroup  Output: matchday number.
 * @return true on success, false otherwise.
 */

// --- Helper: falls noch nicht vorhanden ---
static bool httpGetTextRobust(HTTPClient& http, WiFiClientSecure& client, const String& url, String& out, int retries = 3,
                              uint32_t readTimeoutMs = 12000) {
    for (int a = 0; a < retries; ++a) {
        http.setTimeout(readTimeoutMs);
        http.setConnectTimeout(readTimeoutMs);
        http.addHeader("Accept", "text/plain");
        http.addHeader("Accept-Encoding", "identity");
        // optional: http.useHTTP10(true);

        if (!http.begin(client, url)) {
            http.end();
            continue;
        }
        int code = http.GET();
        if (code == HTTP_CODE_OK) {
            out = http.getString();
            http.end();
            return true;
        }
        http.end();
        delay(150);
    }
    return false;
}
/*
bool LigaTable::getNextUpcoming(League league, NextMatch& out) {
    char url[128];
    snprintf(url, sizeof(url), "https://api.openligadb.de/getnextmatchbyleagueshortcut/%s", leagueShortcut(league));

    WiFiClientSecure client = makeSecureClient();
    HTTPClient       http;
    JsonDocument     doc;

    if (!httpGetJsonRobust(http, client, url, doc)) {
        ligaPrintln("[getNextUpcoming] fetch failed: %s", url);
        return false;
    }

    if (!doc.is<JsonObject>()) {
        ligaPrintln("[getNextUpcoming] response malformed: %s", url);
        return false;
    }

    JsonObject  m       = doc.as<JsonObject>();
    const char* timeUtc = m["matchDateTimeUTC"] | "";
    const char* team1   = m["team1"]["teamName"] | "";
    const char* team2   = m["team2"]["teamName"] | "";

    if (strlen(timeUtc) == 0 || strlen(team1) == 0 || strlen(team2) == 0) {
        ligaPrintln("[getNextUpcoming] no valid next match in response: %s", url);
        return false;
    }

    out.dateUTC = String(timeUtc);
    out.team1   = String(team1);
    out.team2   = String(team2);

    ligaPrintln("[getNextUpcoming] next match: %s vs. %s at %s", out.team1.c_str(), out.team2.c_str(), out.dateUTC.c_str());

    return true;
}
*/
/**
 * @brief Poll the "last change" timestamp of the league table.
 *
 * This function checks the "last change" timestamps of the current and the
 * previous matchday. It then determines which one is more recent and compares
 * it against the last stored change.
 *
 * If the timestamp has changed, the internal state is updated and the function
 * returns true, signaling that new data should be fetched.
 *
 * @param league       League selector (e.g., League::BL1 or League::BL2).
 * @param seasonOut    Optional: pointer to int receiving the current season.
 * @param matchdayOut  Optional: pointer to int receiving the current matchday.
 * @return true if a newer "last change" was detected, false otherwise.
 */
bool LigaTable::pollLastChange(League league, int& seasonOut, int& matchdayOut) {
    int season = 0;
    int group  = 0;

    // 1) aktuelle Saison + Spieltag bestimmen
    if (!getSeasonAndGroup(league, season, group)) {
        Serial.println("[Liga] getSeasonAndGroup failed");
        return false;
    }

    seasonOut        = season;
    matchdayOut      = group;
    currentSeason_   = season;
    currentMatchDay_ = group;

    // 2) LastChange für aktuellen und vorherigen Spieltag
    auto fetchLc = [&](int s, int g) -> String {
        char url[128];
        snprintf(url, sizeof(url), "https://api.openligadb.de/getlastchangedate/%s/%d/%d", leagueShortcut(league), s, g);

        WiFiClientSecure client = makeSecureClient();
        HTTPClient       http;
        http.setReuse(false);
        http.setTimeout(6000);

        if (!http.begin(client, url)) {
            ligaPrintln("[getLastChange] http.begin failed: %s", url);
            return "";
        }

        int code = http.GET();
        if (code != HTTP_CODE_OK) {
            ligaPrintln("[getLastChange] GET %s -> %d", url, code);
            http.end();
            return "";
        }

        String body = http.getString();
        http.end();

        body.trim();
        if (body.length() > 0 && body[0] == '"') {
            body.remove(0, 1);
            if (!body.isEmpty() && body[body.length() - 1] == '"')
                body.remove(body.length() - 1);
        }
        return body;
    };

    String lcCur  = fetchLc(season, group);
    String lcPrev = (group > 1) ? fetchLc(season, group - 1) : "";

    if (lcCur.isEmpty() && lcPrev.isEmpty())
        return false;

    time_t tCur   = lcCur.isEmpty() ? 0 : toUtcTimeT(lcCur);
    time_t tPrev  = lcPrev.isEmpty() ? 0 : toUtcTimeT(lcPrev);
    String latest = (tPrev > tCur) ? lcPrev : lcCur;

    if (latest.isEmpty() || latest == s_lastChange)
        return false;

    ligaPrintln("LigaTable changed at \"%s\" please actualize", latest.c_str());

    s_lastChange      = latest;
    s_lastChangeEpoch = toUtcTimeT(latest);
    return true;
}

int LigaTable::currentSeasonFromDate() {
    struct tm t;
    getLocalTime(&t);                                                           // setzt t mit aktueller Uhrzeit (RTC/NTP muss laufen)

    int year = t.tm_year + 1900;
    int mon  = t.tm_mon + 1;

    if (mon >= 7) {
        return year;                                                            // neue Saison startet im Juli
    } else {
        return year - 1;                                                        // bis Juni gilt noch die Vorjahres-Saison
    }
}

void LigaTable::getGoalsLive() {
    LiveGoalEvent evs[maxGoalsPerMatchday];
    size_t        n = getGoalsLive(activeLeague, evs, maxGoalsPerMatchday);

    if (n > 0) {
        for (size_t i = 0; i < n; i++) {
            const LiveGoalEvent& g = evs[i];
            Serial.printf("[TOR] %s  %s vs %s  %d’  %s  (%d:%d) --> Tor für %s%s%s\n", g.goalTimeUTC.c_str(), g.team1.c_str(), g.team2.c_str(),
                          g.minute, g.scorer.c_str(), g.score1, g.score2, g.scoredFor.c_str(), g.isPenalty ? " [Elfer]" : "",
                          g.isOwnGoal ? " [Eigentor]" : "", g.isOvertime ? " [n.V.]" : "");
        }
    }
}

size_t LigaTable::getGoalsLive(League league, LiveGoalEvent* out, size_t maxOut) {
    size_t outN = 0;

    WiFiClientSecure client = makeSecureClient();
    HTTPClient       http;

    char url[128];
    snprintf(url, sizeof(url), "https://api.openligadb.de/getmatchdata/%s", leagueShortcut(league));
    ligaPrintln("[getGoalsLive] GET %s", url);

    // Puffer für JSON
    StaticJsonDocument<16384> doc;                                              // groß genug für laufende Spiele

    // --- HTTP + JSON laden ---
    if (!http.begin(client, url)) {
        ligaPrintln("[getGoalsLive] http.begin failed");
        return 0;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        ligaPrintln("[getGoalsLive] HTTP GET %s -> %d", url, code);
        http.end();
        return 0;
    }

    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();

    if (err && err != DeserializationError::IncompleteInput) {
        ligaPrintln("[getGoalsLive] JSON parse error: %s", err.c_str());
        return 0;
    }
    if (err == DeserializationError::IncompleteInput) {
        ligaPrintln("[getGoalsLive] Warnung: JSON unvollständig, nutze trotzdem Daten");
    }

    JsonArray matches = doc.as<JsonArray>();
    if (matches.isNull() || matches.size() == 0)
        return 0;

    // --- Alle laufenden Spiele durchgehen ---
    for (JsonObject m : matches) {
        int mid = intOr(m, 0, "matchID", "MatchID");
        if (mid <= 0)
            continue;

        const char* kickC   = strOr(m, "", "matchDateTimeUTC", "MatchDateTimeUTC");
        const char* lastUpd = strOr(m, "", "lastUpdateDateTime", "LastUpdateDateTime");

        JsonObject  t1      = m["team1"];
        JsonObject  t2      = m["team2"];
        const char* name1   = strOr(t1, "", "teamName", "TeamName");
        const char* name2   = strOr(t2, "", "teamName", "TeamName");
        int         team1Id = intOr(t1, 0, "teamId", "TeamId");
        int         team2Id = intOr(t2, 0, "teamId", "TeamId");

        MatchState* st = stateFor(static_cast<uint8_t>(league), mid);
        if (!st)
            continue;

        JsonArray goals = !m["goals"].isNull() ? m["goals"].as<JsonArray>() : m["Goals"].as<JsonArray>();
        if (goals.isNull())
            continue;

        for (JsonObject g : goals) {
            int gid = intOr(g, 0, "goalID", "GoalID");
            if (gid <= st->lastGoalID)
                continue;
            if (outN >= maxOut)
                break;

            LiveGoalEvent& ev = out[outN++];
            ev.matchID        = mid;
            ev.kickOffUTC     = kickC;
            ev.goalTimeUTC    = lastUpd;
            ev.minute         = intOr(g, -1, "matchMinute", "MatchMinute");
            ev.isPenalty      = boolOr(g, false, "isPenalty", "IsPenalty");
            ev.isOwnGoal      = boolOr(g, false, "isOwnGoal", "IsOwnGoal");
            ev.isOvertime     = boolOr(g, false, "isOvertime", "IsOvertime");
            ev.score1         = intOr(g, 0, "scoreTeam1", "ScoreTeam1");
            ev.score2         = intOr(g, 0, "scoreTeam2", "ScoreTeam2");
            ev.scorer         = strOr(g, "", "goalGetterName", "GoalGetterName", "goalGetter", "GoalGetter");
            ev.team1          = name1;
            ev.team2          = name2;

            int scorerTid = intOr(g, 0, "scorerTeamID", "ScorerTeamID", "teamId");
            if (scorerTid == team1Id)
                ev.scoredFor = name1;
            else if (scorerTid == team2Id)
                ev.scoredFor = name2;
            else
                ev.scoredFor = "?";

            if (gid > st->lastGoalID)
                st->lastGoalID = gid;
            st->lastScore1 = ev.score1;
            st->lastScore2 = ev.score2;
        }
        if (outN >= maxOut)
            break;
    }

    return outN;
}

/**
 * @brief Check the availability and health of the OpenLigaDB API for a given league.
 *
 * Steps:
 *  1) Resolve DNS for api.openligadb.de
 *  2) Establish TLS and perform a GET request on the endpoint
 *     /getcurrentgroup/<league> (where <league> = "bl1" or "bl2")
 *  3) Validate HTTP status and response body
 *  4) Parse JSON and ensure a valid groupOrderID field is present
 *
 * @param league    League selector (League::BL1 or League::BL2).
 * @param timeoutMs Timeout in milliseconds for the request (default: 5000 ms).
 * @return ApiHealth struct with status, http code, elapsed time, error type, and message.
 */
ApiHealth LigaTable::checkOpenLigaDB(League league, unsigned timeoutMs) {
    const char* host = "api.openligadb.de";
    const char* lg   = leagueShortcut(league);                                  // "bl1" or "bl2"

    char url[128];
    snprintf(url, sizeof(url), "https://api.openligadb.de/getcurrentgroup/%s", lg);

    unsigned long t0 = millis();

    // --- 1) DNS lookup ---
    IPAddress ip;
    if (WiFi.hostByName(host, ip) != 1) {
        return {false, 0, int(millis() - t0), "DNS_ERROR", "hostByName failed"};
    }

    // --- 2) TLS + HTTP ---
    WiFiClientSecure client = makeSecureClient();
    client.setTimeout(timeoutMs / 1000);

    HTTPClient http;
    http.setConnectTimeout(timeoutMs);
    http.setTimeout(timeoutMs);
    http.addHeader("Accept", "application/json");
    http.addHeader("Accept-Encoding", "identity");

    if (!http.begin(client, url)) {
        return {false, 0, int(millis() - t0), "TCP/TLS_ERROR", "http.begin failed"};
    }

    int code = http.GET();
    if (code <= 0) {
        String msg = "HTTPClient GET failed: " + String(code);
        http.end();
        return {false, code, int(millis() - t0), "HTTP_ERROR", msg};
    }
    if (code != 200) {
        String msg = "HTTP " + String(code) + " (" + http.errorToString(code) + ")";
        http.end();
        return {false, code, int(millis() - t0), "HTTP_ERROR", msg};
    }

    String body = http.getString();
    http.end();

    // --- 3) Check if response looks like JSON ---
    body.trim();
    if (!(body.startsWith("{") || body.startsWith("["))) {
        return {false, 200, int(millis() - t0), "JSON_ERROR", "No JSON at top-level"};
    }

    JsonDocument         doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        return {false, 200, int(millis() - t0), "JSON_ERROR", err.c_str()};
    }

    // --- 4) Ensure mandatory field groupOrderID is present ---
    JsonObject g     = doc.is<JsonArray>() ? doc.as<JsonArray>()[0] : doc.as<JsonObject>();
    int        group = g["groupOrderID"] | g["GroupOrderID"] | 0;
    if (group <= 0) {
        return {false, 200, int(millis() - t0), "JSON_ERROR", "groupOrderID missing"};
    }

    return {true, 200, int(millis() - t0), "UP", "OK"};
}

/**
 * @brief Refresh the epoch timestamp of the next scheduled kickoff for a given league.
 *
 * Throttled probe that queries the rolling endpoint:
 *   /getmatchdata/<league>
 * where <league> is "bl1" or "bl2", extracts all kickoff timestamps and
 * stores the earliest future kickoff (relative to `now`) into s_nextKickoffEpoch.
 *
 * Behavior:
 *  - Returns immediately if called again before KICKOFF_REFRESH_MS elapsed.
 *  - Uses matchUtc(...) to robustly normalize the datetime field to ISO-UTC.
 *  - Chooses the minimal kickoff time strictly greater than now.
 *
 * @param league League selector (League::BL1 or League::BL2).
 */
void LigaTable::refreshNextKickoffEpoch(League league) {
    // Rate-limit refresh: only update if enough time has passed
    if (millis() - s_lastKickoffRefreshMs < KICKOFF_REFRESH_MS)
        return;
    s_lastKickoffRefreshMs = millis();

    WiFiClientSecure client = makeSecureClient();                               // create a TLS session
    HTTPClient       http;                                                      // HTTP client instance

    // Build rolling endpoint URL: e.g. /getmatchdata/bl1 or /getmatchdata/bl2
    const char* lg = leagueShortcut(league);
    char        url[96];
    snprintf(url, sizeof(url), "https://api.openligadb.de/getmatchdata/%s", lg);

    JsonDocument doc;                                                           // lightweight JSON doc
    if (!httpGetJsonRobust(http, client, url, doc))
        return;

    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull() || arr.size() == 0)
        return;

    const time_t nowT = time(nullptr);                                          // current epoch time
    time_t       best = 0;                                                      // best candidate for next kickoff

    for (JsonObject m : arr) {
        // Normalize kickoff timestamp to "...Z" form (ISO-UTC)
        String dt = matchUtc(m);
        if (!dt.length())
            continue;

        // Convert to epoch seconds (UTC)
        time_t t = toUtcTimeT(dt);

        // Keep the earliest kickoff that is still in the future
        if (t > nowT && (best == 0 || t < best))
            best = t;
    }

    // Store result if we found a valid upcoming kickoff
    if (best)
        s_nextKickoffEpoch = best;
}

/**
 * @brief Decide the polling interval (in milliseconds) depending on game context.
 *
 * This method dynamically chooses how often the client should poll OpenLigaDB
 * for updates, based on the time of the next kickoff and the time of the last
 * detected change.
 *
 * Logic:
 *  - Calls refreshNextKickoffEpoch(activeLeague) to update the timestamp of
 *    the next scheduled kickoff.
 *  - If the next kickoff is within 10 minutes, use a *fast polling interval*
 *    (POLL_10MIN_BEFORE_KICKOFF, e.g. 60 seconds).
 *  - If the last change (goal, result update, etc.) was within the last 15 min,
 *    assume the match is live and poll even faster
 *    (POLL_DURING_GAME, e.g. 20 seconds).
 *  - Otherwise, default to a *slow/relaxed polling interval*
 *    (POLL_NORMAL, e.g. 15 minutes).
 *
 * @return Polling interval in milliseconds.
 */
uint32_t LigaTable::decidePollMs() {
    // Always refresh knowledge about next kickoff before deciding
    refreshNextKickoffEpoch(activeLeague);

    time_t nowT = time(nullptr);                                                // current epoch time

    // Case 1: less than 10 minutes until kickoff → fast polling
    if (s_nextKickoffEpoch && (s_nextKickoffEpoch - nowT) <= 10 * 60) {
        return POLL_10MIN_BEFORE_KICKOFF;                                       // e.g. 60 s
    }

    // Case 2: a recent change was detected within the last 15 minutes → live game
    if (s_lastChangeEpoch && (nowT - s_lastChangeEpoch) <= 15 * 60) {
        return POLL_DURING_GAME;                                                // e.g. 3 s
    }

    // Case 3: no imminent kickoff and no recent updates → relaxed polling
    return POLL_NORMAL;                                                         // e.g. 15 min
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
LigaTable::LigaTable() {
    // Configure time zone: CET with daylight-saving rules (Berlin style)
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");

    // Clear both internal snapshot buffers
    buf_[0].clear();
    buf_[1].clear();

    // Set active buffer index to 0
    active_.store(0, std::memory_order_relaxed);
}

/**
 * @brief Commit a new snapshot into the double-buffer system.
 *
 * This function is called by the writer task (e.g. LigaTask) after a new
 * snapshot (`LigaSnapshot`) has been fully constructed.
 *
 * Behavior:
 *  - Determines which buffer is currently active.
 *  - Selects the other (inactive) buffer as the target (`next`).
 *  - Copies the new snapshot into the inactive buffer.
 *  - Atomically switches the active buffer pointer to the new buffer
 *    using `memory_order_release`, ensuring the reader sees a fully
 *    constructed snapshot.
 *
 * @param s The fully built snapshot to publish.
 */
void LigaTable::commit(const LigaSnapshot& s) {
    uint8_t cur  = active_.load(std::memory_order_relaxed);                     // current active buffer index
    uint8_t next = 1 - cur;                                                     // choose the other buffer
    buf_[next]   = s;                                                           // copy snapshot into inactive buffer
    active_.store(next, std::memory_order_release);                             // publish switch with release semantics
}

/**
 * @brief Get the currently active snapshot from the double-buffer system.
 *
 * This function is called by the reader task (e.g. ReportTask) to obtain
 * a stable copy of the most recent snapshot.
 *
 * Behavior:
 *  - Loads the index of the active buffer using `memory_order_acquire`
 *    to synchronize with the writer’s `commit()`.
 *  - Copies the active buffer into the provided output parameter.
 *
 * @param[out] out Destination snapshot which receives the current active buffer.
 */
void LigaTable::get(LigaSnapshot& out) const {
    uint8_t idx = active_.load(std::memory_order_acquire);                      // read active buffer index with acquire semantics
    out         = buf_[idx];                                                    // copy active snapshot to output
}

void LigaTable::openLigaDBHealth() {
    if (WiFi.status() != WL_CONNECTED && !connect()) {
        ligaPrintln("OpenLigaDB WLAN down");
        return;
    }
    if (!waitForTime()) {
        ligaPrintln("OpenLigaDB Zeit nicht synchron - TLS kann scheitern");
    }
    ApiHealth h = checkOpenLigaDB(activeLeague, 50000);
    ligaPrintln("openLigaDB - Status: %s (http=%d, %d ms) %s", h.status.c_str(), h.httpCode, h.elapsedMs, h.detail.c_str());
}

void LigaTable::getNextMatch() {
    NextMatch nm;

    if (WiFi.status() != WL_CONNECTED && !connect()) {
        ligaPrintln("(getNextMatch)  WLAN down");
        return;
    }
    if (!waitForTime()) {
        ligaPrintln("(getNextMatch)  Zeit nicht synchron - Ergebnis evtl. falsch");
    }

    if (getNextUpcoming(activeLeague, nm)) {
        ligaPrint("(getNextMatch) Saison %d, Spieltag %d:", nm.season, nm.group);
        Serial.printf(" Nächstes Spiel (BL%d): %s\n", activeLeague, nm.dateUTC.c_str());
        ligaPrintln("(getNextMatch) Begegnung: %s (%s) vs. %s (%s)", nm.team1.c_str(), nm.team1Short.c_str(), nm.team2.c_str(),
                    nm.team2Short.c_str());
    } else {
        ligaPrintln("(getNextMatch) Kein zukünftiges Spiel gefunden (Saison evtl. vorbei oder API down).");
    }
}

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
bool LigaTable::connect() {
    if (WiFi.status() == WL_CONNECTED)                                          // already connected
        return true;

    WiFi.mode(WIFI_STA);                                                        // set station mode (client)
    WiFi.setSleep(false);                                                       // disable power-save mode to avoid TLS wake issues
    ligaPrint("Verbinde mit WLAN");                                             // log start of connection attempt

    WiFi.begin(ssid, password);                                                 // start WiFi connection
    uint32_t t0 = millis();

    while (WiFi.status() != WL_CONNECTED) {                                     // wait until connected
        delay(200);
        Serial.println('.');                                                    // print progress indicator
        if (millis() - t0 > 15000) {                                            // check for 15 s timeout
            ligaPrintln("\r\nWLAN-Timeout (15s)");
            return false;                                                       // fail after timeout
        }
    }
    return true;                                                                // success
}

/**
 * @brief Fetch the current league table (standings) from the provider endpoint.
 *
 * Uses: https:                                                                 //api.openligadb.de/getbltable/<leagueShortcut>/<season>
 * where leagueShortcut is "bl1" or "bl2" and season is the current season
 * obtained via getSeasonAndGroup(league, ...).
 *
 * @param out Compact snapshot to fill with table rows.
 * @return true on success, false on failure.
 *
 * @note OpenLigaDB proper does not always provide standings; this function
 *       assumes your provider exposes the above endpoint. If the endpoint
 *       returns no rows, consider computing standings locally from matches.
 */
bool LigaTable::fetchTable(LigaSnapshot& out) {
    // 0) Ensure connectivity/time
    if (WiFi.status() != WL_CONNECTED && !connect()) {
        ligaPrintln("OpenLigaDB WLAN down");
        return false;
    }
    if (!waitForTime()) {
        ligaPrintln("OpenLigaDB time not synced - TLS may fail");
    }

    // 1) Determine season & current matchday for the active league
    int season = 0, group = 0;
    if (!getSeasonAndGroup(activeLeague, season, group) || season <= 0) {
        ligaPrintln("fetchTable: cannot determine season for %s", leagueShortcut(activeLeague));
        return false;
    }

    // 2) Build URL: .../getbltable/<league>/<season>
    char url[128];
    snprintf(url, sizeof(url), apiURL, leagueShortcut(activeLeague), season);

    // 3) HTTP GET + JSON
    WiFiClientSecure client = makeSecureClient();
    HTTPClient       http;
    http.setReuse(false);
    http.setConnectTimeout(8000);
    http.setTimeout(8000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.useHTTP10(true);                                                       // avoid chunked
    http.addHeader("Connection", "close");
    http.setUserAgent("ESP32-Flap/1.0");
    http.addHeader("Accept", "application/json");
    http.addHeader("Accept-Encoding", "identity");

    DynamicJsonDocument doc(16384);                                             // Tabelle ist kleiner als Matchdaten
    if (!httpGetJsonRobust(http, client, url, doc)) {
        ligaPrintln("Table GET failed: %s", url);
        return false;
    }

    // 4) Accept either an array or an object with an array under common keys
    JsonArray arr;
    if (doc.is<JsonArray>()) {
        arr = doc.as<JsonArray>();
    } else if (doc.is<JsonObject>()) {
        JsonObject obj = doc.as<JsonObject>();
        if (obj["table"].is<JsonArray>())
            arr = obj["table"].as<JsonArray>();
        else if (obj["Table"].is<JsonArray>())
            arr = obj["Table"].as<JsonArray>();
        else if (obj["rows"].is<JsonArray>())
            arr = obj["rows"].as<JsonArray>();
    }

    if (arr.isNull() || arr.size() == 0) {
        ligaPrintln("Table endpoint returned no rows: %s", url);
        return false;
    }

    // 5) Build compact snapshot
    LigaSnapshot snap;
    snap.clear();
    snap.fetchedAtUTC = (uint32_t)(millis() / 1000);
    snap.season       = season;
    snap.matchday     = group;                                                  // keep in sync with your poll

    uint16_t rank = 1;
    for (JsonObject team : arr) {
        if (snap.teamCount >= LIGA_MAX_TEAMS)
            break;

        const char* teamName  = team["teamName"] | team["TeamName"] | "";
        const char* shortName = team["shortName"] | team["ShortName"] | "";

        int won  = team["won"] | team["Won"] | 0;
        int draw = team["draw"] | team["Draw"] | 0;
        int lost = team["lost"] | team["Lost"] | 0;
        int gf   = team["goals"] | team["Goals"] | 0;
        int ga   = team["opponentGoals"] | team["OpponentGoals"] | 0;

        int matches  = team["matches"] | team["Matches"] | (won + draw + lost);
        int goalDiff = team["goalDiff"] | team["GoalDiff"] | (gf - ga);
        int points   = team["points"] | team["Points"] | team["pts"] | 0;

        String dfb  = dfbCodeForTeamStrict(teamName);
        int    flap = flapForTeamStrict(teamName);

        LigaRow& r = snap.rows[snap.teamCount];
        r.pos      = (uint8_t)rank;
        r.sp       = (uint8_t)max(0, min(99, matches));
        r.diff     = (int8_t)max(-99, min(99, goalDiff));
        r.pkt      = (uint8_t)max(0, min(255, points));
        r.flap     = (uint8_t)max(0, min(255, flap));

        copy_str_bounded(r.team, sizeof(r.team), teamName);
        copy_str_bounded(r.shortName, sizeof(r.shortName), shortName);
        copy_str_bounded(r.dfb, sizeof(r.dfb), dfb.c_str());

        snap.teamCount++;
        rank++;
    }

    out = snap;
    return true;
}

/**
 * @brief Check for new goals in currently live matches and report them.
 *
 * This method queries the helper `collectNewGoalsAcrossLive()` for the
 * active league (`activeLeague`). It collects newly detected goals into
 * a temporary buffer of `LiveGoalEvent` structures. If at least one new
 * goal is found, each goal is printed to the serial console in a human-
 * readable format.
 *
 * Output format:
 *   [TOR] <goalTimeUTC>  <team1> vs <team2>  <minute>’  <scorer>  (<score1>:<score2>) [flags...]
 *
 * Flags:
 *   - [Elfer]     if goal was a penalty
 *   - [Eigentor]  if goal was an own goal
 *   - [n.V.]      if goal was in overtime
 *
 * Application logic (e.g. flap animation, buzzer) can be triggered in
 * the indicated section once a goal is detected.
 */

/*
void LigaTable::getGoal() {
   LiveGoalEvent evs[maxGoalsPerMatchday];
   size_t        n = collectNewGoalsAcrossLive(activeLeague, evs, maxGoalsPerMatchday);

   if (n > 0) {
       for (size_t i = 0; i < n; i++) {
           const LiveGoalEvent& g = evs[i];

           Serial.printf("[TOR] %s  %s vs %s  %d’  %s  (%d:%d) --> Tor für %s%s%s\n", g.goalTimeUTC.c_str(), g.team1.c_str(), g.team2.c_str(),
                         g.minute, g.scorer.c_str(), g.score1, g.score2, g.scoredFor.c_str(), g.isPenalty ? " [Elfer]" : "",
                         g.isOwnGoal ? " [Eigentor]" : "", g.isOvertime ? " [n.V.]" : "");
       }

       // Hier kannst du Animation, Sound usw. starten
   }
}

*/
/**
 * @brief Find the next upcoming match for a given league.
 *
 * This function determines the current season and matchday via
 * getSeasonAndGroup(), then looks ahead until it finds a match
 * that is not yet finished and scheduled in the future.
 *
 * @param league League selector (BL1, BL2).
 * @param nm     Output struct with information about the next match.
 * @return true  If a next upcoming match was found.
 * @return false If no upcoming match was found or on error.
 */
bool LigaTable::getNextUpcoming(League league, NextMatch& out) {
    char url[128];
    snprintf(url, sizeof(url), "https://api.openligadb.de/getnextmatchbyleagueshortcut/%s", leagueShortcut(league));

    WiFiClientSecure client = makeSecureClient();
    HTTPClient       http;
    JsonDocument     doc;

    if (!httpGetJsonRobust(http, client, url, doc)) {
        ligaPrintln("[getNextUpcoming] fetch failed: %s", url);
        return false;
    }

    if (!doc.is<JsonObject>()) {
        ligaPrintln("[getNextUpcoming] response malformed: %s", url);
        return false;
    }

    JsonObject  m       = doc.as<JsonObject>();
    const char* timeUtc = m["matchDateTimeUTC"] | "";
    const char* team1   = m["team1"]["teamName"] | "";
    const char* team2   = m["team2"]["teamName"] | "";

    if (strlen(timeUtc) == 0 || strlen(team1) == 0 || strlen(team2) == 0) {
        ligaPrintln("[getNextUpcoming] no valid next match in response: %s", url);
        return false;
    }

    out.dateUTC = String(timeUtc);
    out.team1   = String(team1);
    out.team2   = String(team2);

    ligaPrintln("[getNextUpcoming] next match: %s vs. %s at %s", out.team1.c_str(), out.team2.c_str(), out.dateUTC.c_str());

    return true;
}

bool LigaTable::getSeasonAndGroup(League league, int& seasonOut, int& groupOut) {
    if (!waitForTime()) {
        ligaPrintln("[getSeasonAndGroup] Systemzeit nicht synchronisiert, Heuristik unsicher!");
    }

    // 1) HTTP GET /getcurrentgroup
    char url[128];
    snprintf(url, sizeof(url), "https://api.openligadb.de/getcurrentgroup/%s", leagueShortcut(league));

    WiFiClientSecure client = makeSecureClient();
    HTTPClient       http;
    http.setReuse(false);
    http.setTimeout(6000);

    if (!http.begin(client, url)) {
        ligaPrintln("[getSeasonAndGroup] http.begin failed (league=%s)", leagueShortcut(league));
        return false;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        ligaPrintln("[getSeasonAndGroup] GET %s -> %d", url, code);
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();

    ligaPrintln("[getSeasonAndGroup] response len=%d", body.length());

    DynamicJsonDocument  doc(1024);
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
        ligaPrintln("[getSeasonAndGroup] JSON parse failed: %s", err.c_str());
        return false;                                                           // ❌ kein Fallback mehr
    }

    JsonObject obj = doc.as<JsonObject>();
    if (!obj.containsKey("groupOrderID")) {
        ligaPrintln("[getSeasonAndGroup] groupOrderID missing in response");
        return false;
    }

    int group = obj["groupOrderID"] | 0;
    if (group <= 0) {
        ligaPrintln("[getSeasonAndGroup] invalid groupOrderID=%d", group);
        return false;
    }

    // 2) Saison heuristisch bestimmen (aktuelles Jahr oder Jahr-1)
    time_t     now   = time(nullptr);
    struct tm* utc   = gmtime(&now);
    int        year  = utc->tm_year + 1900;
    int        month = utc->tm_mon + 1;

    int season = (month >= 7) ? year : year - 1;

    // 3) Optional: Verifikation über /getlastchangedate
    char verifyUrl[160];
    snprintf(verifyUrl, sizeof(verifyUrl), "https://api.openligadb.de/getlastchangedate/%s/%d/%d", leagueShortcut(league), season, group);

    if (http.begin(client, verifyUrl)) {
        int vcode = http.GET();
        if (vcode == HTTP_CODE_OK) {
            String vbody = http.getString();
            vbody.trim();
            if (vbody.length() > 0 && vbody.startsWith("\"")) {
                ligaPrintln("[getSeasonAndGroup] verification ok (season=%d, group=%d)", season, group);
            } else {
                ligaPrintln("[getSeasonAndGroup] verification empty for season=%d, trying year-1", season);
                season -= 1;
            }
        } else {
            ligaPrintln("[getSeasonAndGroup] verify GET %s -> %d", verifyUrl, vcode);
        }
        http.end();
    }

    // 4) Ergebnis zurückgeben
    seasonOut = season;
    groupOut  = group;

    ligaPrintln("[getSeasonAndGroup] final season=%d, group=%d", season, group);
    return true;
}
