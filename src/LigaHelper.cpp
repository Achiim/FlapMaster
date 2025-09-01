// ###################################################################################################
//
//    ██      ██  ██████   █████      ██   ██ ███████ ██      ██████  ███████ ██████
//    ██      ██ ██       ██   ██     ██   ██ ██      ██      ██   ██ ██      ██   ██
//    ██      ██ ██   ███ ███████     ███████ █████   ██      ██████  █████   ██████
//    ██      ██ ██    ██ ██   ██     ██   ██ ██      ██      ██      ██      ██   ██
//    ███████ ██  ██████  ██   ██     ██   ██ ███████ ███████ ██      ███████ ██   ██
//
//
// ################################################################################################## by Achim ####
// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=ANSI%20Regular&t=LIGA%20HELPER
/*

    diverse helper for liga task

*/
#include <Arduino.h>
#include <time.h>
#include <WiFi.h>
#include "FlapTasks.h"
#include "secret.h"                                                             // const char* ssid, const char* password
#include "cert.all"                                                             // certificate for https with OpenLigaDB
#include "Liga.h"
#include "LigaHelper.h"

// 1. Bundesliga
static const DfbMap DFB1[] = {{"FC Bayern München", "FCB", 1},     {"Borussia Dortmund", "BVB", 7}, {"RB Leipzig", "RBL", 13},
                              {"Bayer 04 Leverkusen", "B04", 2},   {"1. FSV Mainz 05", "M05", 8},   {"Borussia Mönchengladbach", "BMG", 14},
                              {"Eintracht Frankfurt", "SGE", 3},   {"VfL Wolfsburg", "WOB", 9},     {"1. FC Union Berlin", "FCU", 15},
                              {"SC Freiburg", "SCF", 4},           {"TSG Hoffenheim", "TSG", 10},   {"VfB Stuttgart", "VFB", 16},
                              {"SV Werder Bremen", "SVW", 5},      {"FC Augsburg", "FCA", 11},      {"1. FC Köln", "KOE", 17},
                              {"1. FC Heidenheim 1846", "HDH", 6}, {"Hamburger SV", "HSV", 12},     {"FC St. Pauli", "STP", 18}};

// 2. Bundesliga
static const DfbMap DFB2[] = {{"Hertha BSC", "BSC", 19},           {"VfL Bochum", "BOC", 25},         {"Eintracht Braunschweig", "EBS", 30},
                              {"SV Darmstadt 98", "SVD", 20},      {"Fortuna Düsseldorf", "F95", 26}, {"SV 07 Elversberg", "ELV", 31},
                              {"SpVgg Greuther Fürth", "SGF", 21}, {"Hannover 96", "H96", 27},        {"1. FC Kaiserslautern", "FCK", 32},
                              {"Karlsruher SC", "KSC", 22},        {"1. FC Magdeburg", "FCM", 28},    {"1. FC Nürnberg", "FCN", 33},
                              {"SC Paderborn 07", "SCP", 23},      {"Holstein Kiel", "KSV", 29},      {"FC Schalke 04", "S04", 34},
                              {"Preußen Münster", "PRM", 24},      {"Arminia Bielefeld", "DSC", 35},  {"Dynamo Dresden", "DYN", 0}};

static MatchState s_state[maxGoalsPerMatchday];                                 // enough for one match day
static size_t     s_stateN = 0;

// -------------------------------------------------------------

/**
 * @brief Get or create a per-(league, matchID) state entry.
 *
 * Searches the internal state array for an entry matching the given league
 * and matchID. If found, returns it; otherwise creates a new zero - initialized
 * entry(if capacity allows) and returns it.Returns nullptr on overflow.
 * @param league League identifier(1 = BL1, 2 = BL2).
 * @param matchID Unique match identifier.
 * @return Pointer to the state entry or nullptr if no space is left.
 */
MatchState* stateFor(uint8_t league, int matchID) {
    // search for an existing entry
    for (size_t i = 0; i < s_stateN; ++i) {
        if (s_state[i].league == league && s_state[i].matchID == matchID) {
            return &s_state[i];
        }
    }
    // create new entry if capacity allows
    if (s_stateN < maxGoalsPerMatchday) {
        s_state[s_stateN] = {league, matchID, 0, 0, 0};                         // init with scores
        return &s_state[s_stateN++];
    }
    return nullptr;
}

// -------------------------------------------------------------

/**
 * @brief Parse an ISO-8601 UTC timestamp string into a struct tm.
 *
 * This function expects an input string in the form:
 *   "YYYY-MM-DDTHH:MM:SSZ"  or with a trailing "+00:00"
 * (e.g. "2025-08-29T18:30:00Z").
 *
 * It extracts year, month, day, hour, minute and second
 * and fills the provided `struct tm` accordingly.
 *
 * Notes:
 * - The function assumes UTC ("Z" or "+00:00"), no timezone conversion is applied.
 * - On success, fields in `out` are populated and the function returns true.
 * - On failure (string too short), the function returns false.
 *
 * @param s   Input ISO-8601 string (Arduino String).
 * @param out Reference to a `struct tm` to fill with parsed values.
 * @return    True if parsing was successful, false otherwise.
 */
bool parseIsoZ(const String& s, struct tm& out) {
    if (s.length() < 19)                                                        // input must be at least "YYYY-MM-DDTHH:MM:SS"
        return false;

    // reset struct tm
    memset(&out, 0, sizeof(out));

    // parse year, month, day
    out.tm_year = s.substring(0, 4).toInt() - 1900;                             // tm_year = years since 1900
    out.tm_mon  = s.substring(5, 7).toInt() - 1;                                // tm_mon  = 0-based month
    out.tm_mday = s.substring(8, 10).toInt();

    // parse hour, minute, second
    out.tm_hour = s.substring(11, 13).toInt();
    out.tm_min  = s.substring(14, 16).toInt();
    out.tm_sec  = s.substring(17, 19).toInt();

    return true;
}

// -------------------------------------------------------------

/**
 * @brief Portable implementation of timegm() for systems without it.
 *
 * This function converts a broken-down UTC time (`struct tm* tm_utc`)
 * into a `time_t` value (seconds since epoch, UTC).
 *
 * Since the standard `mktime()` interprets the input `struct tm` as a
 * *local* time, this implementation compensates for the current local
 * time zone offset (including daylight saving time) to produce the
 * correct UTC-based timestamp.
 *
 * Algorithm:
 *  - Use mktime() on the input to get a "local" time_t.
 *  - Compute the offset between local time and UTC using localtime_r()
 *    and gmtime_r() on the same time_t value.
 *  - Subtract this offset from the local result to obtain the UTC epoch.
 *
 * @param tm_utc Pointer to a `struct tm` representing a UTC calendar time.
 * @return       The corresponding UTC timestamp as time_t.
 */
time_t timegm_portable(struct tm* tm_utc) {
    // mktime() interprets the input as local time → create local time_t first
    time_t t_local = mktime(tm_utc);

    // determine the current timezone offset (including DST)
    struct tm lt, gt;
    localtime_r(&t_local, &lt);                                                 // same second as local calendar time
    gmtime_r(&t_local, &gt);                                                    // same second as UTC calendar time

    // mktime(localtime(t)) == t_local
    // mktime(gmtime(t))    == t_local - offset
    time_t t_again_local = mktime(&lt);
    time_t t_again_utc   = mktime(&gt);
    time_t offset        = t_again_local - t_again_utc;                         // seconds east of UTC (e.g. +3600 in CET)

    // "UTC interpretation" = local result minus offset
    return t_local - offset;
}

// -------------------------------------------------------------

/**
 * @brief Convert an ISO-8601 UTC string into a UTC time_t.
 *
 * This function takes an ISO-8601 datetime string (e.g. "2025-08-29T18:30:00Z"
 * or "...+00:00"), normalizes it to the form "...Z", parses it into a
 * `struct tm` using parseIsoZ(), and then converts it to a UTC `time_t`
 * using the portable timegm() implementation.
 *
 * @param iso ISO-8601 string, expected in UTC ("...Z" or "...+00:00").
 * @return    Corresponding UTC timestamp as time_t, or 0 on parse error.
 */
time_t toUtcTimeT(const String& iso) {
    String t = iso;

    // normalize to "YYYY-MM-DDTHH:MM:SSZ"
    if (t.endsWith("+00:00"))
        t = t.substring(0, 19) + "Z";
    if (t.length() >= 19 && t[19] != 'Z')
        t = t.substring(0, 19) + "Z";

    // parse ISO string into struct tm
    struct tm tm;
    if (!parseIsoZ(t, tm))
        return 0;

    // mktime() assumes local time, we need UTC → use portable timegm()
    return timegm_portable(&tm);
}

// -------------------------------------------------------------

/**
 * @brief Get the current UTC time as ISO-8601 string.
 *
 * This function queries the current system time, converts it to UTC using
 * gmtime_r(), and formats it into the ISO-8601 string form
 * "YYYY-MM-DDTHH:MM:SSZ".
 *
 * @return ISO-8601 UTC string of the current time.
 */
String nowUTC_ISO() {
    time_t    t = time(nullptr);                                                // current epoch time
    struct tm tm;
    gmtime_r(&t, &tm);                                                          // convert to UTC broken-down time

    char buf[25];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return String(buf);
}

// -------------------------------------------------------------

/**
 * @brief Return a string value from a JSON object by trying multiple keys.
 *
 * This helper allows robust access to JSON fields where the same data
 * may appear with different key casings (e.g. "teamName" vs. "TeamName").
 * It tries up to four candidate keys and returns the first non-null value.
 *
 * @param o   JSON object to read from.
 * @param def Default string to return if none of the keys exist.
 * @param k1  Primary key name.
 * @param k2  Optional secondary key name (default: nullptr).
 * @param k3  Optional third key name (default: nullptr).
 * @param k4  Optional fourth key name (default: nullptr).
 * @return    C-string value from JSON or the default string if not found.
 */
const char* strOr(JsonObjectConst o, const char* def, const char* k1, const char* k2, const char* k3, const char* k4) {
    // try keys in order, return the first found
    if (k1 && !o[k1].isNull())
        return o[k1];
    if (k2 && !o[k2].isNull())
        return o[k2];
    if (k3 && !o[k3].isNull())
        return o[k3];
    if (k4 && !o[k4].isNull())
        return o[k4];
    // nothing found → return default
    return def;
}

// -------------------------------------------------------------

/**
 * @brief Return an integer value from a JSON object by trying multiple keys.
 *
 * Similar to strOr(), but casts the first existing key's value to int.
 *
 * @param o   JSON object to read from.
 * @param def Default integer to return if none of the keys exist.
 * @param k1  Primary key name.
 * @param k2  Optional secondary key name (default: nullptr).
 * @param k3  Optional third key name (default: nullptr).
 * @return    Integer value from JSON or the default if not found.
 */
int intOr(JsonObjectConst o, int def, const char* k1, const char* k2, const char* k3) {
    // try keys in order, return the first found
    if (k1 && !o[k1].isNull())
        return o[k1].as<int>();
    if (k2 && !o[k2].isNull())
        return o[k2].as<int>();
    if (k3 && !o[k3].isNull())
        return o[k3].as<int>();
    // nothing found → return default
    return def;
}

// -------------------------------------------------------------

/**
 * @brief Return a boolean value from a JSON object by trying multiple keys.
 *
 * Similar to strOr() and intOr(), but casts the first existing key's value to bool.
 *
 * @param o   JSON object to read from.
 * @param def Default boolean to return if none of the keys exist.
 * @param k1  Primary key name.
 * @param k2  Optional secondary key name (default: nullptr).
 * @param k3  Optional third key name (default: nullptr).
 * @return    Boolean value from JSON or the default if not found.
 */
bool boolOr(JsonObjectConst o, bool def, const char* k1, const char* k2, const char* k3) {
    // try keys in order, return the first found
    if (k1 && !o[k1].isNull())
        return o[k1].as<bool>();
    if (k2 && !o[k2].isNull())
        return o[k2].as<bool>();
    if (k3 && !o[k3].isNull())
        return o[k3].as<bool>();
    // nothing found → return default
    return def;
}

// -------------------------------------------------------------

/**
 * @brief Create and configure a secure WiFi client.
 *
 * This helper initializes a `WiFiClientSecure` instance and
 * installs the CA certificate defined in `OPENLIGA_CA` so that
 * HTTPS connections to the OpenLigaDB API can be verified.
 *
 * @return A configured `WiFiClientSecure` instance.
 */
WiFiClientSecure makeSecureClient() {
    WiFiClientSecure c;
    c.setCACert(OPENLIGA_CA);                                                   // load root CA certificate for TLS verification
    return c;
}

// -------------------------------------------------------------

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
bool waitForTime(uint32_t maxMs) {
    time_t   t  = time(nullptr);
    uint32_t t0 = millis();

    // wait until epoch time is valid (>= ~2023-11-14) or timeout reached
    while (t < 1700000000 && (millis() - t0) < maxMs) {
        delay(200);
        t = time(nullptr);
    }
    return t >= 1700000000;
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
 * @brief Extract and normalize the UTC date string from a match object.
 *
 * Reads the date/time fields from the given JSON object and converts them
 * into a standardized ISO 8601 UTC format ending with "Z".
 *
 * - If already ends with 'Z', it is returned unchanged.
 * - If ends with "+00:00", the suffix is replaced with 'Z'.
 * - If at least 19 characters, the first 19 chars are kept and 'Z' appended.
 * - Otherwise, the string is returned as-is.
 *
 * @param m JSON object representing a match from OpenLigaDB.
 * @return Normalized UTC timestamp string (ISO 8601).
 */
String matchUtc(const JsonObject& m) {
    String dt = m["matchDateTimeUTC"] | m["MatchDateTimeUTC"] | m["matchDateTime"] | "";
    if (!dt.length())
        return "";
    if (dt.endsWith("Z"))
        return dt;
    if (dt.endsWith("+00:00"))
        return dt.substring(0, 19) + "Z";
    if (dt.length() >= 19)
        return dt.substring(0, 19) + "Z";
    return dt;                                                                  // fallback if format not recognized
}

/**
 * @brief Find the next scheduled match in a match array.
 *
 * Iterates through all matches in the given JSON array and selects the match
 * with the earliest kickoff time that is strictly later than the provided
 * reference timestamp (nowIso).
 *
 * @param arr     JSON array of matches as returned by OpenLigaDB.
 * @param nowIso  Current time in ISO 8601 UTC format (e.g. "2025-08-31T13:30:00Z").
 * @param out     Reference to NextMatch struct that will be filled with
 *                details of the next match (date, ID, teams).
 * @return true if a next match was found, false otherwise.
 */
bool pickNextFromArray(JsonArray arr, const String& nowIso, NextMatch& out) {
    bool   have = false;                                                        // flag: found a candidate
    String best;                                                                // best (earliest) datetime candidate

    // iterate through all match objects
    for (JsonObject m : arr) {
        String dt = matchUtc(m);                                                // normalize UTC datetime of this match
        if (!dt.length() || !(dt > nowIso))                                     // skip if empty or not in the future
            continue;

        // If first candidate OR earlier than current best, update "best"
        if (!have || dt < best) {
            have        = true;
            best        = dt;
            out.dateUTC = dt;
            out.matchID = m["matchID"] | 0;

            // Extract team info (case-insensitive keys)
            JsonObject t1  = m["team1"].isNull() ? m["Team1"] : m["team1"];
            JsonObject t2  = m["team2"].isNull() ? m["Team2"] : m["team2"];
            out.team1      = (const char*)(t1["teamName"] | t1["TeamName"] | "");
            out.team1Short = (const char*)(t1["shortName"] | t1["ShortName"] | "");
            out.team2      = (const char*)(t2["teamName"] | t2["TeamName"] | "");
            out.team2Short = (const char*)(t2["shortName"] | t2["ShortName"] | "");
        }
    }
    return have;                                                                // true if at least one future match was found
}

// -------------------- Helpers (safe strings) --------------------
// sichere NUL-terminierende Kopie (falls du sie im Fetch nutzt)
