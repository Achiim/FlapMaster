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

/**
 * @brief Perform a robust HTTP GET and parse JSON response.
 *
 * This helper handles several problematic cases commonly encountered on ESP32:
 *  - Unknown or zero Content-Length (CL=0 or -1).
 *  - Chunked transfer encoding.
 *  - Large vs. small responses (switch between buffer vs. stream).
 *  - Automatic retries on failure (up to @p maxRetry).
 *
 * Behavior:
 *  - Opens a TLS-secured connection with @p client and performs GET on @p url.
 *  - If Content-Length is small (<8192 bytes), downloads into a String buffer
 *    and then deserializes into @p doc.
 *  - If Content-Length is large or unknown, deserializes directly from the stream.
 *  - Returns true if JSON parsing succeeds, or if parsing fails only with
 *    `DeserializationError::IncompleteInput` (treated as usable).
 *  - On other parse errors or HTTP errors, retries (with 200 ms delay) up to @p maxRetry.
 *
 * @param http     Reference to an HTTPClient instance (reused by caller).
 * @param client   Reference to a WiFiClientSecure (TLS) instance.
 * @param url      Target URL to fetch JSON from.
 * @param doc      Destination JsonDocument to populate.
 * @param maxRetry Maximum retry attempts (default is caller-defined).
 * @return true    If JSON was successfully parsed or usable despite IncompleteInput.
 * @return false   If all retries failed or an unrecoverable error occurred.
 */
bool LigaTable::httpGetJsonRobust(HTTPClient& http, WiFiClientSecure& client, const String& url, JsonDocument& doc, int maxRetry) {
    for (int attempt = 0; attempt < maxRetry; attempt++) {
        // Try to open a secure connection
        if (!http.begin(client, url)) {
            #ifdef ERRORVERBOSE
                TraceScope trace;
                {
                ligaPrintln("http.begin() failed @ %s", url.c_str());
                }
            #endif
            return false;                                                       // unrecoverable, do not retry
        }
        int code = http.GET();                                                  // Perform the GET request
        if (code != HTTP_CODE_OK) {
            #ifdef ERRORVERBOSE
                TraceScope trace;
                {
                ligaPrintln("HTTP GET %s -> %d", url.c_str(), code);            // Server responded with an error → retry
                }
            #endif
            http.end();
            delay(200);                                                         // short back-off before retry
            continue;
        }
        // --- Evaluate Content-Length ---
        int                  len = http.getSize();                              // -1 = unknown length
        DeserializationError err;
        if (len > 0 && len < 8192) {                                            // Small response: safer to buffer into String before parsing
            String payload = http.getString();
            err            = deserializeJson(doc, payload);
        } else {
            Stream* s = http.getStreamPtr();                                    // Large or unknown size: stream directly into ArduinoJson
            if (!s) {
                #ifdef ERRORVERBOSE
                    TraceScope trace;
                    {
                    ligaPrintln("HTTP stream null @ %s", url.c_str());
                    }
                #endif
                http.end();
                continue;                                                       // retry with new connection
            }
            err = deserializeJson(doc, *s);
        }
        http.end();                                                             // always free connection after use
        if (!err) {
            return true;                                                        // Parsed successfully
        }
        if (err == DeserializationError::IncompleteInput) {
            #ifdef ERRORVERBOSE                                                 // Sometimes OpenLigaDB cuts JSON → still usable
                TraceScope trace;
                {
                ligaPrintln("Warning: JSON incomplete @ %s, data may still be usable", url.c_str());
                }
            #endif
            return true;
        }

        #ifdef ERRORVERBOSE                                                     // Any other parse error: log and retry
            TraceScope trace;
            {
            ligaPrintln("JSON parse error @ %s: %s", url.c_str(), err.c_str());
            }
        #endif
        delay(200);
    }
    return false;                                                               // all retries failed
}

/**
 * @brief Poll the "last change" timestamp of the actual matchday.
 *
 * This function checks the "last change" timestamps of the current matchday.
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
    // 1) get season + matchday
    if (!getSeasonAndGroup(league, season, group)) {
        #ifdef ERRORVERBOSE
            TraceScope trace;
            {
            ligaPrintln("(pollLastChange) getSeasonAndGroup failed");
            }
        #endif
        return false;
    }

    seasonOut        = season;
    matchdayOut      = group;
    currentSeason_   = season;
    currentMatchDay_ = group;

    // 2) LastChange für aktuellen Spieltag
    auto fetchLc = [&](int s, int g) -> String {
        char url[128];
        snprintf(url, sizeof(url), "https://api.openligadb.de/getlastchangedate/%s/%d/%d", leagueShortcut(league), s, g);

        WiFiClientSecure client = makeSecureClient();
        HTTPClient       http;
        http.setReuse(false);
        http.setTimeout(6000);

        if (!http.begin(client, url)) {
            #ifdef ERRORVERBOSE
                TraceScope trace;
                {
                ligaPrintln("(pollLastChange) http.begin failed: %s", url);
                }
            #endif
            return "";
        }

        int code = http.GET();
        if (code != HTTP_CODE_OK) {
            #ifdef ERRORVERBOSE
                TraceScope trace;
                {
                ligaPrintln("(pollLastChange) GET %s -> %d", url, code);
                }
            #endif
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

    String lcCur = fetchLc(season, group);

    if (lcCur.isEmpty())
        return false;

    time_t tCur   = lcCur.isEmpty() ? 0 : toUtcTimeT(lcCur);
    String latest = lcCur;

    if (latest.isEmpty() || latest == s_lastChange)
        return false;

    s_lastChange      = latest;
    s_lastChangeEpoch = toUtcTimeT(latest);

    #ifdef LIGAVERBOSE
        struct tm tmLoc;
        localtime_r(&s_lastChangeEpoch, &tmLoc);                                // convert UTC epoch → local time
        char buf[32];
        strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M:%S %Z", &tmLoc);
        TraceScope trace;
        {
        ligaPrintln("last change of actual matchday at \"%s\" (local %s)", latest.c_str(), buf);
        }
    #endif
    return true;
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
 * @brief Refresh the cached timestamp of the next scheduled kickoff.
 *
 * Logic:
 *  - Rate-limited by KICKOFF_REFRESH_MS.
 *  - Uses last stored Season/Group (from getSeasonAndGroup()).
 *  - First checks the current group (matchday).
 *  - If no future kickoff is found and group < 34, also checks group+1.
 *  - Stores the earliest future kickoff into s_nextKickoffEpoch,
 *    or 0 if none could be found.
 *
 * @param league League selector (e.g. BL1, BL2).
 */
void LigaTable::refreshNextKickoffEpoch(League league) {
    // --- Rate limiting ---
    uint32_t nowMs = millis();
    if (nowMs - s_lastKickoffRefreshMs < KICKOFF_REFRESH_MS) {
        #ifdef LIGAVERBOSE
            TraceScope trace;
            {
            ligaPrintln("[kickoff] throttled, next in %u ms", (unsigned)(KICKOFF_REFRESH_MS - (nowMs - s_lastKickoffRefreshMs)));
            }
        #endif
        return;
    }
    s_lastKickoffRefreshMs = nowMs;

    // --- Require valid season/group ---
    SeasonGroup sg = lastSeasonGroup();
    if (!sg.valid) {
        #ifdef ERRORVERBOSE
            TraceScope trace;
            {
            ligaPrintln("[kickoff] invalid season/group %d/%d", sg.season, sg.group);
            }
        #endif
        s_nextKickoffEpoch = 0;
        return;
    }

    // --- Check current group ---
    time_t best = bestFutureKickoffInGroup(league, sg.season, sg.group);

    // --- If empty and not at last group, check next group ---
    if (best == 0 && sg.group < 34) {
        best = bestFutureKickoffInGroup(league, sg.season, sg.group + 1);
    }

    // --- Store result and log ---

    if (best) {
        s_nextKickoffEpoch = best;
        #ifdef LIGAVERBOSE
            struct tm tmLoc;
            localtime_r(&best, &tmLoc);                                         // convert epoch → local time
            char buf[32];
            strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M:%S %Z", &tmLoc);         // print with TZ name
            TraceScope trace;
            {
            ligaPrintln("next kickoff = %s (epoch=%ld)", buf, (long)best);
            }
        #endif

    } else {
        s_nextKickoffEpoch = 0;
        #ifdef LIGAVERBOSE
            TraceScope trace;
            {
            ligaPrintln("no upcoming kickoff on group %d%s", sg.group, (sg.group < 34 ? " or group+1" : ""));
            }
        #endif
    }
}

/**
 * @brief Find the earliest future kickoff timestamp within one specific matchday.
 *
 * This helper calls the endpoint:
 *   /getmatchdata/<league>/<season>/<group>
 * and scans all matches in that group. It extracts kickoff timestamps
 * ("matchDateTimeUTC" or "matchDateTime") and returns the earliest one
 * that is strictly greater than "now".
 *
 * @param league League selector (e.g. BL1, BL2).
 * @param season Season year (e.g. 2025).
 * @param group  Matchday number (1..34).
 * @return time_t Earliest future kickoff epoch in seconds, or 0 if none found.
 */
time_t LigaTable::bestFutureKickoffInGroup(League league, int season, int group) {
    // --- JSON filter: root = array, element schema at [0] ---
    StaticJsonDocument<256> filter;
    {
        JsonArray  farr        = filter.to<JsonArray>();
        JsonObject el          = farr.createNestedObject();
        el["matchDateTimeUTC"] = true;
        el["MatchDateTimeUTC"] = true;
        el["matchDateTime"]    = true;
        el["MatchDateTime"]    = true;
    }

    // Build URL: /getmatchdata/<league>/<season>/<group>
    char url[160];
    snprintf(url, sizeof(url), "https://api.openligadb.de/getmatchdata/%s/%d/%d", leagueShortcut(league), season, group);

    // HTTP client
    WiFiClientSecure client = makeSecureClient();
    HTTPClient       http;
    http.setReuse(false);
    http.setConnectTimeout(8000);
    http.setTimeout(8000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.useHTTP10(true);                                                       // avoid chunked encoding
    http.addHeader("Accept", "application/json");
    http.addHeader("Accept-Encoding", "identity");
    http.addHeader("Connection", "close");
    http.setUserAgent("ESP32-Flap/1.0");

    if (!http.begin(client, url)) {
        #ifdef ERRORVERBOSE
            TraceScope trace;
            {
            ligaPrintln("[kickoff] http.begin failed: %s", url);
            }
        #endif
        return 0;
    }
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        #ifdef ERRORVERBOSE
            TraceScope trace;
            {
            ligaPrintln("[kickoff] GET %s -> %d", url, code);
            }
        #endif
        http.end();
        return 0;
    }

    // Parse with filter
    DynamicJsonDocument  doc(4096);
    DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();

    if (err || !doc.is<JsonArray>()) {
        #ifdef ERRORVERBOSE
            TraceScope trace;
            {
            ligaPrintln("[kickoff] JSON %s on group %d", err ? err.c_str() : "unexpected shape", group);
            }
        #endif
        return 0;
    }

    // Iterate matches and find earliest future kickoff
    const time_t nowT = time(nullptr);
    time_t       best = 0;

    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject m : arr) {
        const char* raw = m["matchDateTimeUTC"] | m["MatchDateTimeUTC"] | m["matchDateTime"] | m["MatchDateTime"] | "";
        if (!*raw)
            continue;

        // Normalize to "...Z" format
        String iso = String(raw);
        if (iso.endsWith("+00:00"))
            iso = iso.substring(0, 19) + "Z";
        if (iso.length() >= 19 && iso[19] != 'Z')
            iso = iso.substring(0, 19) + "Z";

        time_t t = toUtcTimeT(iso);
        if (t == 0)
            continue;
        if (t > nowT && (best == 0 || t < best))
            best = t;
    }
    return best;
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

    time_t nowT = time(nullptr);

    // Case 1: less than 10 minutes until kickoff → fast polling
    if (s_nextKickoffEpoch && (s_nextKickoffEpoch - nowT) <= 10 * 60) {
        return POLL_10MIN_BEFORE_KICKOFF;                                       // e.g. 60 s
    }

    // Case 2: match is ongoing (kickoff already happened but <3h ago)
    if (s_nextKickoffEpoch && nowT >= s_nextKickoffEpoch && nowT <= s_nextKickoffEpoch + 3 * 60 * 60) {
        return POLL_DURING_GAME;                                                // e.g. 20 s
    }

    // Case 3: no imminent kickoff and no game in progress → relaxed polling
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
 * @brief Atomically publish a new table snapshot (double buffer) and
 *        remember the previously active snapshot.
 *
 * Thread-safety:
 *  - Readers call activeSnapshot()/previousSnapshot() which use acquire loads.
 *  - This function copies @p s into the inactive buffer, updates `previous_`
 *    to the old active index, then flips `active_` with release semantics.
 *
 * @param s Fresh snapshot to publish.
 */
void LigaTable::commit(const LigaSnapshot& s) {
    const uint8_t cur  = active_.load(std::memory_order_acquire);               // current active index
    const uint8_t next = 1U - cur;                                              // inactive buffer index

    buf_[next] = s;                                                             // copy into inactive buffer

    // publish ordering: set previous_ to old active, then flip active_ to new
    previous_.store(cur, std::memory_order_relaxed);
    active_.store(next, std::memory_order_release);
}

// -----------------------------------------------------------------------------
// --- Snapshpt helpers ---
// -----------------------------------------------------------------------------
/** @brief Get a const reference to the currently active snapshot. */
LigaSnapshot& LigaTable::activeSnapshot() {
    return buf_[active_.load(std::memory_order_acquire)];
}

/** @brief Get a const reference to the previous snapshot (may equal active at startup). */
LigaSnapshot& LigaTable::previousSnapshot() {
    return buf_[previous_.load(std::memory_order_acquire)];
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
        #ifdef ERRORVERBOSE
            TraceScope trace;
            {
            ligaPrintln("OpenLigaDB WLAN down");
            }
        #endif
        return;
    }
    if (!waitForTime()) {
        #ifdef ERRORVERBOSE
            TraceScope trace;
            {
            ligaPrintln("OpenLigaDB Zeit nicht synchron - TLS kann scheitern");
            }
        #endif
    }
    ApiHealth h = checkOpenLigaDB(activeLeague, 50000);
    ligaPrintln("openLigaDB - Status: %s (http=%d, %d ms) %s", h.status.c_str(), h.httpCode, h.elapsedMs, h.detail.c_str());
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
    SeasonGroup sg = lastSeasonGroup();
    if (!sg.valid) {
        ligaPrintln("fetchTable: no stored SeasonGroup available for %s", leagueShortcut(activeLeague));
        return false;
    }

    // 2) Build URL: .../getbltable/<league>/<season>
    char url[128];
    snprintf(url, sizeof(url), apiURL, leagueShortcut(activeLeague), sg.season);

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
    snap.season       = sg.season;
    snap.matchday     = sg.group;                                               // keep in sync with your poll

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
bool LigaTable::getSeasonAndGroup(League league, int& seasonOut, int& groupOut) {
    if (!waitForTime()) {
        #ifdef ERRORVERBOSE
            TraceScope trace;
            {
            ligaPrintln("system time not synchronized, heuristik unsecure!");
            }
        #endif
        return false;
    }

    // --- API GET /getcurrentgroup ---
    char url[128];
    snprintf(url, sizeof(url), "https://api.openligadb.de/getcurrentgroup/%s", leagueShortcut(league));

    WiFiClientSecure client = makeSecureClient();
    HTTPClient       http;
    http.setReuse(false);
    http.setTimeout(6000);

    if (!http.begin(client, url)) {
        #ifdef ERRORVERBOSE
            TraceScope trace;
            {
            ligaPrintln("[getSeasonAndGroup] http.begin failed (league=%s)", leagueShortcut(league));
            }
        #endif
        return false;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        #ifdef ERRORVERBOSE
            TraceScope trace;
            {
            ligaPrintln("[getSeasonAndGroup] GET %s -> %d", url, code);
            }
        #endif
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();

    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, body)) {
        #ifdef ERRORVERBOSE
            TraceScope trace;
            {
            ligaPrintln("[getSeasonAndGroup] JSON parse failed");
            }
        #endif
        return false;
    }

    JsonObject obj   = doc.as<JsonObject>();
    int        group = obj["groupOrderID"] | 0;
    if (group <= 0) {
        #ifdef ERRORVERBOSE
            TraceScope trace;
            {
            ligaPrintln("[getSeasonAndGroup] invalid groupOrderID=%d", group);
            }
        #endif
        return false;
    }

    // --- Saison heuristisch bestimmen ---
    time_t     now    = time(nullptr);
    struct tm* utc    = gmtime(&now);
    int        year   = utc ? (utc->tm_year + 1900) : 1970;
    int        month  = utc ? (utc->tm_mon + 1) : 1;
    int        season = (month >= 7) ? year : year - 1;

    // --- Verifikation /getlastchangedate ---
    char verifyUrl[160];
    snprintf(verifyUrl, sizeof(verifyUrl), "https://api.openligadb.de/getlastchangedate/%s/%d/%d", leagueShortcut(league), season, group);

    if (http.begin(client, verifyUrl)) {
        int vcode = http.GET();
        if (vcode == HTTP_CODE_OK) {
            String vbody = http.getString();
            vbody.trim();
            if (vbody.length() > 0 && vbody.startsWith("\"")) {
                // maybe trace
            } else {
                #ifdef LIGAVERBOSE
                    TraceScope trace;
                    {
                    ligaPrintln("[getSeasonAndGroup]  verification empty for season=%d, trying year-1", season);
                    }
                #endif
                season -= 1;
            }
        }
        http.end();
    }

    // --- Outputs ---
    seasonOut = season;
    groupOut  = group;

    if (_lastSG.season != season || _lastSG.group != group) {                   // report only change of season and/or group
    #ifdef LIGAVERBOSE
        TraceScope trace;
        {
        ligaPrintln("actual league=%s season=%d, matchday=%d (stored)", leagueShortcut(league), season, group);
        }
    #endif
    }

    // --- Speichern in Struct ---
    _lastSG.season      = season;
    _lastSG.group       = group;
    _lastSG.valid       = (season > 0 && group > 0);
    _lastSG.fetchedAtMs = millis();
    return _lastSG.valid;
}

SeasonGroup LigaTable::lastSeasonGroup() const {
    return _lastSG;                                                             // gibt eine Kopie zurück
}

/**
 * @brief Collect all currently live matches (robust version).
 *
 * Reads stored Season/Group (set by getSeasonAndGroup), fetches current
 * matchday, and copies all 'live && !finished' matches into @p out.
 * Uses full body String (instead of stream) to avoid IncompleteInput errors.
 *
 * @param league    League shortcut enum.
 * @param out       Caller-provided array of LiveGoalEvent (stubs).
 * @param maxCount  Max elements @p out can hold.
 * @return int      Number of live matches written to @p out (>=0).
 */
/**
 * @brief Collect all currently live matches (robust + filtered).
 *
 * Uses a JSON filter tailored for an array-of-matches response so that only
 * a handful of fields are materialized (small RAM footprint). The filter is
 * *applied* during deserialization. The function keeps OpenLigaDB order and
 * writes up to @p maxCount entries into @p out.
 */
int LigaTable::collectLiveMatches(League league, LiveGoalEvent* out, size_t maxCount) {
    if (!out || maxCount == 0)
        return 0;

    SeasonGroup sg = lastSeasonGroup();
    if (!sg.valid) {
        #ifdef ERRORVERBOSE
            TraceScope trace;
            {
            ligaPrintln("(collectLiveMatches)no stored Season/Group - call getSeasonAndGroup first");
            }
        #endif
        return 0;
    }

    // --- Build endpoint for current matchday ---
    char url[160];
    snprintf(url, sizeof(url), "https://api.openligadb.de/getmatchdata/%s/%d/%d", leagueShortcut(league), sg.season, sg.group);

    WiFiClientSecure client = makeSecureClient();
    HTTPClient       http;
    http.setReuse(false);
    http.setConnectTimeout(8000);
    http.setTimeout(8000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.useHTTP10(true);                                                       ///< avoid chunked
    http.addHeader("Accept", "application/json");
    http.addHeader("Accept-Encoding", "identity");                              ///< avoid gzip
    http.addHeader("Connection", "close");
    http.setUserAgent("ESP32-Flap/1.0");

    if (!http.begin(client, url)) {
        #ifdef ERRORVERBOSE
            TraceScope trace;
            {
            ligaPrintln("(collectLiveMatches) http.begin failed: %s", url);
            }
        #endif
        return 0;
    }
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        #ifdef ERRORVERBOSE
            {
            TraceScope trace;
            ligaPrintln("(collectLiveMatches) GET %s -> %d", url, code);
            }
        #endif
        http.end();
        return 0;
    }
    int expected = http.getSize();                                              // -1 wenn unbekannt
    // ligaPrintln("(collectLiveMatches) HTTP %d, Content-Length=%d", code, expected);

    // Warte bis Daten anliegen (oder Timeout erreicht)
    WiFiClient&    stream      = http.getStream();
    uint32_t       t0          = millis();
    const uint32_t waitTimeout = 8000;                                          // ms, muss zur setTimeout-Einstellung passen
    while (stream.connected() && !stream.available() && (millis() - t0) < waitTimeout) {
        delay(1);
    }
    int avail = stream.available();
    // ligaPrintln("(collectLiveMatches) stream.available()=%d", avail);

    // --- Build a correct FILTER for an array-of-matches ---
    // Root must be an ARRAY; define element schema at index 0.
    StaticJsonDocument<512> filter;
    JsonArray               farr = filter.to<JsonArray>();
    JsonObject              el   = farr.createNestedObject();                   // == filter[0]

    // IDs & flags (support both casings seen in OpenLigaDB)
    el["matchID"]         = true;
    el["MatchID"]         = true;
    el["matchIsLive"]     = true;
    el["MatchIsLive"]     = true;
    el["matchIsFinished"] = true;
    el["MatchIsFinished"] = true;

    // Kickoff (both UTC and local date-time variants)
    el["matchDateTimeUTC"] = true;
    el["MatchDateTimeUTC"] = true;
    el["matchDateTime"]    = true;
    el["MatchDateTime"]    = true;

    // Teams (both lowercase and PascalCase trees)
    el["team1"]["teamName"] = true;
    el["Team1"]["TeamName"] = true;
    el["team2"]["teamName"] = true;
    el["Team2"]["TeamName"] = true;

    // --- Parse with FILTER APPLIED ---
    DynamicJsonDocument  doc(4096);                                             // mit Filter reichen 4  KB
    DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();

    if (err) {
        #ifdef ERRORVERBOSE
            {
            TraceScope trace;
            ligaPrintln("(collectLiveMatches) JSON parse error: %s", err.c_str());
            }
        #endif
        return 0;
    }

    // --- Iterate over filtered matches and collect live ones ---
    size_t    count         = 0;
    JsonArray arr           = doc.as<JsonArray>();
    int       total         = 0;
    int       totalLive     = 0;
    int       totalFinished = 0;

    for (JsonObject m : arr) {
        total++;

        bool live     = m["matchIsLive"] | m["MatchIsLive"] | false;
        bool finished = m["matchIsFinished"] | m["MatchIsFinished"] | false;

        if (live)
            totalLive++;
        if (finished)
            totalFinished++;

        if (!(live && !finished))
            continue;
        if (count >= maxCount) { /* optional: Log "buffer full" */
            break;
        }

        int         id = m["matchID"] | m["MatchID"] | 0;
        const char* t1 = m["team1"]["teamName"] | m["Team1"]["TeamName"] | "";
        const char* t2 = m["team2"]["teamName"] | m["Team2"]["TeamName"] | "";
        const char* ko = m["matchDateTimeUTC"] | m["MatchDateTimeUTC"] | m["matchDateTime"] | m["MatchDateTime"] | "";
        if (id <= 0 || !*t1 || !*t2 || !*ko)
            continue;

        LiveGoalEvent& e = out[count++];
        e                = LiveGoalEvent{};
        e.matchID        = id;
        e.kickOffUTC     = String(ko);
        e.team1          = String(t1);
        e.team2          = String(t2);

        #ifdef LIGAVERBOSE
            {
            TraceScope trace;
            ligaPrintln("LIVE: %s vs %s (Kickoff %s, MatchID=%d)", e.team1.c_str(), e.team2.c_str(), e.kickOffUTC.c_str(), e.matchID);
            }
        #endif
    }

    if (count == 0) {
        #ifdef LIGAVERBOSE
            {
            TraceScope trace;
            ligaPrintln("no live matches at the moment.");
            }
        #endif
    } else {
        #ifdef LIGAVERBOSE
            {
            TraceScope trace;
            ligaPrintln("total=%d, live=%d, finished=%d, selected=%d", total, totalLive, totalFinished, (int)count);
            }
        #endif
    }
    return (int)count;
}

/**
 * @brief Fetch goal events for a single live match (lightweight per-match query).
 *
 * Calls /getmatchdata/{matchId} and parses only the minimal fields needed:
 * Goals[] (getter/minute/flags/score), Team1/Team2 names, and kickoff UTC.
 * If @p sinceUtc is non-empty, only goals with event time >= sinceUtc (UTC ISO)
 * are returned. The function writes into a caller-provided fixed buffer and
 * returns how many entries were filled (0..maxCount).
 *
 * @param matchId   OpenLigaDB MatchID (must be >0).
 * @param sinceUtc  Optional cutoff in ISO-8601 UTC ("" = no cutoff).
 * @param out       Output buffer (array) of LiveGoalEvent.
 * @param maxCount  Capacity of @p out.
 * @return int      Number of goals written (>=0). 0 means none/new goals.
 */
int LigaTable::fetchGoalsForLiveMatch(int matchId, const String& sinceUtc, LiveGoalEvent* out, size_t maxCount) {
    if (matchId <= 0 || !out || maxCount == 0)
        return 0;

    char url[96];
    snprintf(url, sizeof(url), "https://api.openligadb.de/getmatchdata/%d", matchId);

    WiFiClientSecure client = makeSecureClient();
    HTTPClient       http;
    http.setReuse(false);
    http.setConnectTimeout(8000);
    http.setTimeout(8000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.useHTTP10(true);                                                       // avoid chunked
    http.addHeader("Accept", "application/json");
    http.addHeader("Accept-Encoding", "identity");                              // avoid gzip
    http.addHeader("Connection", "close");
    http.setUserAgent("ESP32-Flap/1.0");

    if (!http.begin(client, url)) {
        ligaPrintln("[goals] http.begin failed: %s", url);
        return 0;
    }
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        ligaPrintln("[goals] GET %s -> %d", url, code);
        http.end();
        return 0;
    }

    // ---- Filter NUR die benötigten Felder (Object-Root) ----
    StaticJsonDocument<256> filter;
    // Teams (beide Schemata)
    filter["Team1"]["TeamName"] = true;
    filter["team1"]["teamName"] = true;
    filter["Team2"]["TeamName"] = true;
    filter["team2"]["teamName"] = true;
    // IDs & Kickoff
    filter["MatchID"]          = true;
    filter["matchID"]          = true;
    filter["MatchDateTimeUTC"] = true;
    filter["matchDateTimeUTC"] = true;
    filter["MatchDateTime"]    = true;
    filter["matchDateTime"]    = true;

    // Goals-Subschema einmal definieren, unter beiden Keys setzen
    StaticJsonDocument<192> goalSchema;
    goalSchema["GoalGetterName"]     = true;
    goalSchema["goalGetterName"]     = true;
    goalSchema["MatchMinute"]        = true;
    goalSchema["matchMinute"]        = true;
    goalSchema["IsPenalty"]          = true;
    goalSchema["isPenalty"]          = true;
    goalSchema["IsOwnGoal"]          = true;
    goalSchema["isOwnGoal"]          = true;
    goalSchema["IsOvertime"]         = true;
    goalSchema["isOvertime"]         = true;
    goalSchema["ScoreTeam1"]         = true;
    goalSchema["scoreTeam1"]         = true;
    goalSchema["ScoreTeam2"]         = true;
    goalSchema["scoreTeam2"]         = true;
    goalSchema["GoalDateTime"]       = true;
    goalSchema["goalDateTime"]       = true;
    goalSchema["LastUpdateDateTime"] = true;
    goalSchema["lastUpdateDateTime"] = true;
    goalSchema["CreatedAt"]          = true;
    goalSchema["createdAt"]          = true;

    filter["Goals"].set(goalSchema.as<JsonObject>());
    filter["goals"].set(goalSchema.as<JsonObject>());

    // ---- Body vollständig lesen (NUL-sicher), dann mit Filter parsen ----
    String body = http.getString();
    http.end();
    if (body.length() == 0 || body == "null")
        return 0;

    DynamicJsonDocument  doc(12288);                                            // 8–12 KB reichen i.d.R. mit Filter
    DeserializationError err = deserializeJson(doc, body.c_str(), body.length(), DeserializationOption::Filter(filter));
    if (err) {
        ligaPrintln("[goals] JSON error (id=%d): %s", matchId, err.c_str());
        return 0;
    }
    if (!doc.is<JsonObject>())
        return 0;

    JsonObject m = doc.as<JsonObject>();

    // Teams & Kickoff (beide Schreibweisen berücksichtigen)
    const char* t1 = m["Team1"]["TeamName"] | m["team1"]["teamName"] | "";
    const char* t2 = m["Team2"]["TeamName"] | m["team2"]["teamName"] | "";
    const char* ko = m["MatchDateTimeUTC"] | m["matchDateTimeUTC"] | m["MatchDateTime"] | m["matchDateTime"] | "";

    const bool useCutoff = (sinceUtc.length() > 0);
    time_t     cutoff    = 0;
    if (useCutoff)
        cutoff = toUtcTimeT(sinceUtc);

    // Goals-Array holen
    JsonArray gArr;
    if (m["Goals"].is<JsonArray>())
        gArr = m["Goals"].as<JsonArray>();
    else if (m["goals"].is<JsonArray>())
        gArr = m["goals"].as<JsonArray>();
    else
        return 0;

    size_t filled = 0;
    time_t prevS1 = 0, prevS2 = 0;                                              // optional für exaktere scoredFor-Logik (siehe Kommentar)

    for (JsonObject g : gArr) {
        if (filled >= maxCount)
            break;

        // Ereignis-Zeitstempel (UTC bevorzugt; sonst was da ist)
        const char* goalUtc =
            g["GoalDateTime"] | g["goalDateTime"] | g["LastUpdateDateTime"] | g["lastUpdateDateTime"] | g["CreatedAt"] | g["createdAt"] | ko;

        if (useCutoff) {
            time_t ts = goalUtc ? toUtcTimeT(String(goalUtc)) : 0;
            if (ts == 0 || ts < cutoff)
                continue;
        }

        LiveGoalEvent& e = out[filled++];
        e                = LiveGoalEvent{};
        e.matchID        = matchId;
        e.kickOffUTC     = String(ko);
        e.goalTimeUTC    = String(goalUtc);
        e.team1          = String(t1);
        e.team2          = String(t2);

        e.minute     = g["MatchMinute"] | g["matchMinute"] | -1;
        e.scorer     = String(g["GoalGetterName"] | g["goalGetterName"] | "");
        e.isPenalty  = g["IsPenalty"] | g["isPenalty"] | false;
        e.isOwnGoal  = g["IsOwnGoal"] | g["isOwnGoal"] | false;
        e.isOvertime = g["IsOvertime"] | g["isOvertime"] | false;
        e.score1     = g["ScoreTeam1"] | g["scoreTeam1"] | 0;
        e.score2     = g["ScoreTeam2"] | g["scoreTeam2"] | 0;

        // Credit – quick & safe:
        if (e.isOwnGoal) {
            e.scoredFor = (e.score1 >= e.score2) ? e.team1 : e.team2;
        } else {
            e.scoredFor = (e.score1 > e.score2) ? e.team1 : (e.score2 > e.score1 ? e.team2 : String(""));
        }

        // // Optional exakter: vorherigen Score pro Match verfolgen
        // if (filled == 1) { prevS1 = 0; prevS2 = 0; } // init
        // if (!e.isOwnGoal) {
        //   if (e.score1 == prevS1 + 1) e.scoredFor = e.team1;
        //   else if (e.score2 == prevS2 + 1) e.scoredFor = e.team2;
        // }
        // prevS1 = e.score1; prevS2 = e.score2;
    }

    return (int)filled;
}

/**
 * @brief Detect whether the table leader (row 0) changed between two snapshots.
 *
 * Compares team name (and, as a fallback, DFB short code) at position 0.
 *
 * @param oldSnap Previous snapshot.
 * @param newSnap New snapshot (about to become active).
 * @param out     Filled with change details when a change is detected.
 * @return true if the leader changed, false otherwise.
 */
static bool detectLeaderChange(const LigaSnapshot& oldSnap, const LigaSnapshot& newSnap, LeaderChange& out) {
    if (oldSnap.teamCount == 0 || newSnap.teamCount == 0)
        return false;

    const LigaRow& oldL = oldSnap.rows[0];
    const LigaRow& newL = newSnap.rows[0];

    // Compare by team string first
    if (strcmp(oldL.team, newL.team) == 0) {
        // Optional secondary check by DFB code (guards against spelling variants)
        if (strlen(oldL.dfb) == 0 || strlen(newL.dfb) == 0)
            return false;
        if (strcmp(oldL.dfb, newL.dfb) == 0)
            return false;
    }

    out.changed = true;
    out.oldTeam = String(oldL.team);
    out.newTeam = String(newL.team);
    return true;
}

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
 */
bool LigaTable::detectLeaderChange(const LigaSnapshot& oldSnap, const LigaSnapshot& newSnap, const LigaRow** oldLeaderOut,
                                   const LigaRow** newLeaderOut) {
    if (oldSnap.teamCount == 0 || newSnap.teamCount == 0) {
        #ifdef LIGAVERBOSE
            {
            TraceScope trace;
            ligaPrintln("no leader change: '%s' -> '%s'", newSnap.rows[0].team, newSnap.rows[0].team);
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
            TraceScope trace;
            {
            ligaPrintln("leader change: '%s' -> '%s' (pts %u→%u, diff %d→%d)", oldL.team, newL.team, oldL.pkt, newL.pkt, oldL.diff, newL.diff);
            }
        #endif

    return true;
}
