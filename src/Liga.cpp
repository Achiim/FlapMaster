#include <Arduino.h>
#include <time.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>                                                        // v7 (JsonDocument)
#include "FlapTasks.h"
#include "Liga.h"
#include "secret.h"                                                             // const char* ssid, const char* password
#include "cert.all"                                                             // certificate for https with OpenLigaDB

struct DfbMap {
    const char* key;
    const char* code;
};

// ----- Datentypen -----
struct LiveGoalEvent {
    int    matchID = 0;
    String kickOffUTC;                                                          // Spiel-Anstoß (UTC)
    String goalTimeUTC;                                                         // Zeitpunkt des Goals (aus lastUpdate / Konstrukt)
    int    minute = -1;
    int    score1 = 0, score2 = 0;
    String team1, team2, scorer;
    bool   isPenalty = false, isOwnGoal = false, isOvertime = false;
};

// Merker pro Spiel (höchste gesehene goalID)
struct MatchState {
    int matchID;
    int lastGoalID;
};

// 1. Bundesliga
static const DfbMap DFB1[] = {{"FC Bayern München", "FCB"},     {"Borussia Dortmund", "BVB"}, {"RB Leipzig", "RBL"},
                              {"Bayer 04 Leverkusen", "B04"},   {"1. FSV Mainz 05", "M05"},   {"Borussia Mönchengladbach", "BMG"},
                              {"Eintracht Frankfurt", "SGE"},   {"VfL Wolfsburg", "WOB"},     {"1. FC Union Berlin", "FCU"},
                              {"SC Freiburg", "SCF"},           {"TSG Hoffenheim", "TSG"},    {"VfB Stuttgart", "VFB"},
                              {"SV Werder Bremen", "SVW"},      {"FC Augsburg", "FCA"},       {"1. FC Köln", "KOE"},
                              {"1. FC Heidenheim 1846", "HDH"}, {"Hamburger SV", "HSV"},      {"FC St. Pauli", "STP"}};

// 2. Bundesliga (ergänzbar – strikt nach Namen)
static const DfbMap DFB2[] = {
    {"Hertha BSC", "BSC"},           {"VfL Bochum 1848", "BOC"},    {"Eintracht Braunschweig", "EBS"},
    {"SV Darmstadt 98", "SVD"},      {"Fortuna Düsseldorf", "F95"}, {"SV Elversberg", "ELV"},
    {"SpVgg Greuther Fürth", "SGF"}, {"Hannover 96", "H96"},        {"1. FC Kaiserslautern", "FCK"},
    {"Karlsruher SC", "KSC"},        {"1. FC Magdeburg", "FCM"},    {"1. FC Nürnberg", "FCN"},
    {"SC Paderborn 07", "SCP"},      {"Holstein Kiel", "KSV"},      {"FC Schalke 04", "S04"},
    {"SC Preußen Münster", "PRM"}
    // ggf. später ergänzen (Hansa Rostock, VfL Osnabrück, SV Wehen Wiesbaden, …)
};

// Merker für adaptives Polling
static String         s_lastChange;                                             // dein vorhandener ISO-String
static time_t         s_lastChangeEpoch      = 0;                               // Epoch zum schnellen Vergleich
static time_t         s_nextKickoffEpoch     = 0;                               // nächster Anstoß (Epoch UTC)
static uint32_t       s_lastKickoffRefreshMs = 0;                               // wann zuletzt berechnet
static const uint32_t KICKOFF_REFRESH_MS     = 300000;                          // 5 min

static MatchState s_state[40];                                                  // reicht für einen Spieltag
static size_t     s_stateN = 0;

// ----- Hilfen -----
static MatchState* stateFor(int id) {
    for (size_t i = 0; i < s_stateN; i++)
        if (s_state[i].matchID == id)
            return &s_state[i];
    if (s_stateN < 40) {
        s_state[s_stateN] = {id, 0};
        return &s_state[s_stateN++];
    }
    return nullptr;
}

static bool parseIsoZ(const String& s, struct tm& out) {                        // "YYYY-MM-DDTHH:MM:SSZ" (oder +00:00)
    if (s.length() < 19)
        return false;
    memset(&out, 0, sizeof(out));
    out.tm_year = s.substring(0, 4).toInt() - 1900;
    out.tm_mon  = s.substring(5, 7).toInt() - 1;
    out.tm_mday = s.substring(8, 10).toInt();
    out.tm_hour = s.substring(11, 13).toInt();
    out.tm_min  = s.substring(14, 16).toInt();
    out.tm_sec  = s.substring(17, 19).toInt();
    return true;
}

// Portable Ersatz für timegm(): interpretiert struct tm als **UTC**.
static time_t timegm_portable(struct tm* tm_utc) {
    // mktime() interpretiert tm als **lokale** Zeit → erst lokales time_t bilden
    time_t t_local = mktime(tm_utc);

    // Jetzt den aktuellen TZ-Offset (inkl. DST) bestimmen:
    struct tm lt, gt;
    localtime_r(&t_local, &lt);                                                 // gleiche Sekunde als lokale Kalenderzeit
    gmtime_r(&t_local, &gt);                                                    // gleiche Sekunde als UTC-Kalenderzeit

    // mktime(localtime(t)) == t_local; mktime(gmtime(t)) == t_local - offset
    time_t t_again_local = mktime(&lt);
    time_t t_again_utc   = mktime(&gt);
    time_t offset        = t_again_local - t_again_utc;                         // Sekunden Ost von UTC (z. B. +3600 in CET)

    // „UTC-Interpretation“ = lokales Ergebnis MINUS Offset
    return t_local - offset;
}

static time_t toUtcTimeT(const String& iso) {
    String t = iso;
    if (t.endsWith("+00:00"))
        t = t.substring(0, 19) + "Z";
    if (t.length() >= 19 && t[19] != 'Z')
        t = t.substring(0, 19) + "Z";
    struct tm tm;
    if (!parseIsoZ(t, tm))
        return 0;
    // mktime erwartet lokale Zeit; wir wollen UTC: nutze timegm-Äquivalent
    return timegm_portable(&tm);
}

static String nowUTC_ISO() {
    time_t    t = time(nullptr);
    struct tm tm;
    gmtime_r(&t, &tm);
    char buf[25];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return String(buf);
}

// ----- Kernfunktion -----
// foreward declaration
static bool httpGetJson(const char* url, JsonDocument& doc);

// Sammelt ALLE neuen Tore über alle aktuell laufenden BL1-Spiele.
// Rückgabe = Anzahl neuer Tore; schreibt sie in 'out[0..n)'.
size_t collectNewGoalsAcrossLiveBL1(LiveGoalEvent* out, size_t maxOut) {
    size_t outN = 0;
    // 1) aktuellen Spieltag laden
    JsonDocument doc;
    if (!httpGetJson("https://api.openligadb.de/getmatchdata/bl1", doc))
        return 0;
    JsonArray matches = doc.as<JsonArray>();
    if (matches.isNull() || matches.size() == 0)
        return 0;

    const String nowIso = nowUTC_ISO();
    const time_t nowT   = toUtcTimeT(nowIso);

    for (JsonObject m : matches) {
        // a) Status / Zeiten
        bool finished = m["matchIsFinished"] | m["MatchIsFinished"] | false;
        if (finished)
            continue;

        String kick = m["matchDateTimeUTC"] | m["MatchDateTimeUTC"] | m["matchDateTime"] | "";
        if (kick.length() == 0)
            continue;
        time_t kickT = toUtcTimeT(kick);
        if (kickT == 0)
            continue;

        // "läuft": Anstoß <= jetzt und nicht fertig (mit Pufferfenster 3h)
        if (nowT + 0 < kickT)
            continue;                                                           // noch nicht angepfiffen
        if (nowT > kickT + 3 * 3600)
            continue;                                                           // sehr alt -> ignorieren (Sicherung)

        // b) Goals auswerten
        int mid = m["matchID"] | 0;
        if (mid <= 0)
            continue;
        MatchState* st = stateFor(mid);
        if (!st)
            continue;

        int maxSeen = st->lastGoalID;

        JsonArray goals = m["goals"].as<JsonArray>();
        if (!goals.isNull()) {
            for (JsonObject g : goals) {
                int gid = g["goalID"] | 0;
                if (gid > st->lastGoalID && outN < maxOut) {
                    LiveGoalEvent ev;
                    ev.matchID     = mid;
                    ev.kickOffUTC  = kick;
                    ev.minute      = g["matchMinute"] | -1;
                    ev.isPenalty   = g["isPenalty"] | false;
                    ev.isOwnGoal   = g["isOwnGoal"] | false;
                    ev.isOvertime  = g["isOvertime"] | false;
                    ev.score1      = g["scoreTeam1"] | 0;
                    ev.score2      = g["scoreTeam2"] | 0;
                    ev.scorer      = (const char*)(g["goalGetterName"] | "");
                    ev.goalTimeUTC = m["lastUpdateDateTime"] | "";              // OLGDB aktualisiert das beim Ereignis
                    JsonObject t1  = m["team1"].isNull() ? m["Team1"] : m["team1"];
                    JsonObject t2  = m["team2"].isNull() ? m["Team2"] : m["team2"];
                    ev.team1       = (const char*)(t1["teamName"] | t1["TeamName"] | "");
                    ev.team2       = (const char*)(t2["teamName"] | t2["TeamName"] | "");
                    out[outN++]    = ev;
                }
                if (gid > maxSeen)
                    maxSeen = gid;
            }
        }
        // c) höchsten gesehenen goalID-Stand merken
        if (maxSeen > st->lastGoalID)
            st->lastGoalID = maxSeen;
        if (outN >= maxOut)
            break;                                                              // Ausgabepuffer voll
    }
    return outN;
}

// Holt LastChange für eine (Saison, Gruppe)
static bool getLastChange(int season, int group, String& out) {
    char url[128];
    snprintf(url, sizeof(url), "https://api.openligadb.de/getlastchangedate/bl1/%d/%d", season, group);

    JsonDocument ldc;
    if (!httpGetJson(url, ldc))
        return false;

    // Entweder reiner String oder { "lastChangeDate": "..." }
    out = ldc.is<JsonObject>() ? (const char*)(ldc["lastChangeDate"] | "") : ldc.as<const char*>();
    out.trim();
    return out.length() > 0;
}

// Holt Season & aktuellen Spieltag aus dem "aktueller Spieltag"-Endpoint
bool LigaTable::getSeasonAndGroup(int& season, int& group) {
    JsonDocument doc;
    if (!httpGetJson("https://api.openligadb.de/getmatchdata/bl1", doc))
        return false;
    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull() || arr.size() == 0)
        return false;
    season = arr[0]["leagueSeason"] | arr[0]["LeagueSeason"] | 0;
    group  = arr[0]["group"]["groupOrderID"] | arr[0]["Group"]["GroupOrderID"] | 0;
    return season > 0 && group > 0;
}

// pollLastChange: vergleicht aktuellen und letzter Spieltag, nimmt den neueren Änderungszeitpunkt
bool LigaTable::pollLastChange(int* seasonOut, int* matchdayOut) {
    int season = 0, group = 0;
    if (!getSeasonAndGroup(season, group))
        return false;

    // Season/Matchday immer nach außen geben & intern merken
    if (seasonOut)
        *seasonOut = season;
    if (matchdayOut)
        *matchdayOut = group;
    _currentSeason   = season;                                                  // Saison
    _currentMatchDay = group;                                                   // match day

    String lcCur, lcPrev;
    if (!getLastChange(season, group, lcCur))                                   // last change of current matchday
        lcCur = "";
    if (group > 1 && !getLastChange(season, group - 1, lcPrev))                 // last change of previous matchday
        lcPrev = "";

    if (lcCur.isEmpty() && lcPrev.isEmpty())
        return false;

    // neueren Zeitstempel wählen (robust via Epoch-Vergleich)
    time_t tCur   = lcCur.isEmpty() ? 0 : toUtcTimeT(lcCur);
    time_t tPrev  = lcPrev.isEmpty() ? 0 : toUtcTimeT(lcPrev);
    String latest = (tPrev > tCur) ? lcPrev : lcCur;

    if (latest.isEmpty() || latest == s_lastChange)
        return false;

    ligaPrintln("LigaTable changed at %s please actualize", latest.c_str());

    time_t t = toUtcTimeT(latest);
    if (t > 0)
        s_lastChangeEpoch = t;                                                  // letzter Zeitpunkt des schnellen poll (während ein Spiel läuft)

    s_lastChange = latest;                                                      // letzter Zeitpunkt des normalen poll (wenn kein Spiel läuft)

    return true;
}

static WiFiClientSecure makeSecureClient() {
    WiFiClientSecure c;
    c.setCACert(OPENLIGA_CA);                                                   // certificat
    return c;
}

static bool waitForTime(uint32_t maxMs = 15000) {
    time_t   t  = time(nullptr);
    uint32_t t0 = millis();
    while (t < 1700000000 && (millis() - t0) < maxMs) {                         // ~2023-11-14
        delay(200);
        t = time(nullptr);
    }
    return t >= 1700000000;
}

static String dfbCodeForTeamStrict(const String& teamName) {
    String key = teamName;
    for (auto& e : DFB1)
        if (key == e.key)
            return e.code;
    for (auto& e : DFB2)
        if (key == e.key)
            return e.code;
    return "";                                                                  // strikt: kein Treffer => leeres Kürzel
}
struct NextMatch {
    int    season  = 0;
    int    group   = 0;
    int    matchID = 0;
    String dateUTC;
    String team1, team1Short;
    String team2, team2Short;
};

struct ApiHealth {
    bool   ok;
    int    httpCode;
    int    elapsedMs;
    String status;                                                              // "UP", "DNS_ERROR", "TCP/TLS_ERROR", "HTTP_ERROR", "JSON_ERROR", "TIMEOUT"
    String detail;                                                              // Zusatzinfo
};

// Datumsfeld aus Match holen und in „…Z“-Form bringen
static String matchUtc(const JsonObject& m) {
    String dt = m["matchDateTimeUTC"] | m["MatchDateTimeUTC"] | m["matchDateTime"] | "";
    if (!dt.length())
        return "";
    if (dt.endsWith("Z"))
        return dt;
    if (dt.endsWith("+00:00"))
        return dt.substring(0, 19) + "Z";
    if (dt.length() >= 19)
        return dt.substring(0, 19) + "Z";
    return dt;                                                                  // Fallback
}

// Nächstes Spiel in einem Match-Array finden (kleinstes Datum > nowIso)
static bool pickNextFromArray(JsonArray arr, const String& nowIso, NextMatch& out) {
    bool   have = false;
    String best;
    for (JsonObject m : arr) {
        String dt = matchUtc(m);
        if (!dt.length() || !(dt > nowIso))
            continue;
        if (!have || dt < best) {
            have           = true;
            best           = dt;
            out.dateUTC    = dt;
            out.matchID    = m["matchID"] | 0;
            JsonObject t1  = m["team1"].isNull() ? m["Team1"] : m["team1"];
            JsonObject t2  = m["team2"].isNull() ? m["Team2"] : m["team2"];
            out.team1      = (const char*)(t1["teamName"] | t1["TeamName"] | "");
            out.team1Short = (const char*)(t1["shortName"] | t1["ShortName"] | "");
            out.team2      = (const char*)(t2["teamName"] | t2["TeamName"] | "");
            out.team2Short = (const char*)(t2["shortName"] | t2["ShortName"] | "");
        }
    }
    return have;
}

static bool httpGetJson(const char* url, JsonDocument& doc) {
    WiFiClientSecure client = makeSecureClient();
    HTTPClient       http;
    http.setConnectTimeout(8000);
    if (!http.begin(client, url))
        return false;
    int code = http.GET();
    if (code != 200) {
        http.end();
        Serial.printf("GET -> %d (%s)\n", code, http.errorToString(code).c_str());
        return false;
    }
    String body = http.getString();
    http.end();
    body.trim();
    DeserializationError err = deserializeJson(doc, body);
    return !err;
}

// Hauptroutine: ermittelt Season & aktuellen Spieltag aus getmatchdata/bl1,
// sucht dort das nächste Spiel; wenn keins: lädt den nächsten Spieltag.
bool getNextUpcomingBL1(NextMatch& out) {
    const String nowIso = nowUTC_ISO();

    // 1) Aktuellen Spieltag samt Season holen
    JsonDocument curDoc;
    if (!httpGetJson("https://api.openligadb.de/getmatchdata/bl1", curDoc))
        return false;
    JsonArray curArr = curDoc.as<JsonArray>();
    if (curArr.isNull() || curArr.size() == 0)
        return false;

    int season   = curArr[0]["leagueSeason"] | curArr[0]["LeagueSeason"] | 0;
    int curGroup = curArr[0]["group"]["groupOrderID"] | curArr[0]["Group"]["GroupOrderID"] | 0;
    if (season == 0 || curGroup == 0)
        return false;

    out.season = season;
    out.group  = curGroup;

    // 2) Erst im aktuellen Spieltag suchen
    if (pickNextFromArray(curArr, nowIso, out))
        return true;

    // 3) Sonst nächsten Spieltag laden und dort suchen
    char url[96];
    snprintf(url, sizeof(url), "https://api.openligadb.de/getmatchdata/bl1/%d/%d", season, curGroup + 1);
    JsonDocument nextDoc;
    if (!httpGetJson(url, nextDoc))
        return false;
    JsonArray nextArr = nextDoc.as<JsonArray>();
    if (nextArr.isNull() || nextArr.size() == 0)
        return false;

    out.group = curGroup + 1;
    return pickNextFromArray(nextArr, nowIso, out);
}

static ApiHealth checkOpenLigaDB(unsigned timeoutMs = 5000) {
    const char*   host = "api.openligadb.de";
    const char*   url  = "https://api.openligadb.de/getcurrentgroup/bl1";
    unsigned long t0   = millis();

    // 1) DNS
    IPAddress ip;
    if (WiFi.hostByName(host, ip) != 1) {
        return {false, 0, int(millis() - t0), "DNS_ERROR", "hostByName failed"};
    }

    // 2) TLS + HTTP
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

    // 3) JSON prüfen
    body.trim();
    if (!(body.startsWith("{") || body.startsWith("["))) {
        return {false, 200, int(millis() - t0), "JSON_ERROR", "No JSON at top-level"};
    }

    JsonDocument         doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        return {false, 200, int(millis() - t0), "JSON_ERROR", err.c_str()};
    }

    // Pflichtfeld: groupOrderID muss existieren
    JsonObject g     = doc.is<JsonArray>() ? doc.as<JsonArray>()[0] : doc.as<JsonObject>();
    int        group = g["groupOrderID"] | g["GroupOrderID"] | 0;
    if (group <= 0) {
        return {false, 200, int(millis() - t0), "JSON_ERROR", "groupOrderID missing"};
    }

    return {true, 200, int(millis() - t0), "UP", "OK"};
}

// -------------------- Helpers (safe strings) --------------------
// sichere NUL-terminierende Kopie (falls du sie im Fetch nutzt)
static inline void copy_str_bounded(char* dst, size_t dst_sz, const char* src) {
    if (!dst || dst_sz == 0)
        return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0;
    for (; i + 1 < dst_sz && src[i] != '\0'; ++i)
        dst[i] = src[i];
    dst[i] = '\0';
}

// ---- UTF-8 bounded print: schneidet an Zeichen-Grenzen & liest nie > maxBytes ----
static inline bool isUtf8Cont(uint8_t b) {
    return (b & 0xC0) == 0x80;
}
static inline size_t safe_strnlen_(const char* s, size_t maxb) {
    if (!s)
        return 0;
    size_t n = 0;
    while (n < maxb && s[n] != '\0')
        n++;
    return n;
}

static void printUtf8PaddedBounded(const char* s, size_t maxBytes, size_t cols) {
    const char* p = s ? s : "";
    size_t      n = safe_strnlen_(p, maxBytes);                                 // nie über den Feldpuffer hinaus
    size_t      i = 0, printed = 0;
    while (i < n && printed < cols) {
        uint8_t c    = (uint8_t)p[i];
        size_t  step = 1;
        if ((c & 0x80) == 0x00)
            step = 1;
        else if ((c & 0xE0) == 0xC0)
            step = 2;
        else if ((c & 0xF0) == 0xE0)
            step = 3;
        else if ((c & 0xF8) == 0xF0)
            step = 4;

        // validiere Fortsetzungsbytes innerhalb n
        if (i + step > n)
            step = 1;
        else {
            for (size_t k = 1; k < step; k++)
                if (!isUtf8Cont((uint8_t)p[i + k])) {
                    step = 1;
                    break;
                }
        }

        for (size_t k = 0; k < step && (i + k) < n; k++)
            Serial.write((uint8_t)p[i + k]);
        i += step;
        printed += 1;
    }
    for (; printed < cols; ++printed)
        Serial.write(' ');
}

static void printHeader() {
    Serial.println("┌─────┬──────────────────────────┬────────────┬─────┬────┬────────────┐");
    Serial.println("│ Pos │ Mannschaft               │ Name       │ DFB │ Sp │ Diff │ Pkt │");
    Serial.println("├─────┼──────────────────────────┼────────────┼─────┼────┼────────────┤");
}

static void printFooter() {
    Serial.println("└─────┴──────────────────────────┴────────────┴─────┴────┴────────────┘");
}

static void refreshNextKickoffEpoch() {
    if (millis() - s_lastKickoffRefreshMs < KICKOFF_REFRESH_MS)
        return;
    s_lastKickoffRefreshMs = millis();

    JsonDocument doc;                                                           // klein halten, kein Riesenfilter nötig
    if (!httpGetJson("https://api.openligadb.de/getmatchdata/bl1", doc))
        return;
    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull() || arr.size() == 0)
        return;

    time_t nowT = time(nullptr);
    time_t best = 0;
    for (JsonObject m : arr) {
        String dt = m["matchDateTimeUTC"] | m["MatchDateTimeUTC"] | m["matchDateTime"] | "";
        if (dt.length() == 0)
            continue;
        time_t t = toUtcTimeT(dt);
        if (t > nowT && (best == 0 || t < best))
            best = t;
    }
    if (best)
        s_nextKickoffEpoch = best;
}

uint32_t LigaTable::decidePollMs() {
    refreshNextKickoffEpoch();
    time_t nowT = time(nullptr);

    // 10 min vor Anstoß: schneller
    if (s_nextKickoffEpoch && (s_nextKickoffEpoch - nowT) <= 10 * 60) {
        return 5000;                                                            // 5 s
    }
    // Während Live-Phase: in den letzten 15 min gab es Änderungen
    if (s_lastChangeEpoch && (nowT - s_lastChangeEpoch) <= 15 * 60) {
        return 3000;                                                            // 3 s
    }
    // Sonst gemütlich
    return 60000;                                                               // 60 s
}

// ===== LigaTable: Constructor =====
LigaTable::LigaTable() {
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov"); // synch time .. Berlin: CET/CEST

    buf_[0].clear();                                                            // clear both snapshot buffer
    buf_[1].clear();
    active_.store(0, std::memory_order_relaxed);
}

// Writer (LigaTask) calls this after fully building a new snapshot.
void LigaTable::commit(const LigaSnapshot& s) {
    uint8_t cur  = active_.load(std::memory_order_relaxed);
    uint8_t next = 1 - cur;
    buf_[next]   = s;                                                           // full copy into the inactive buffer
    active_.store(next, std::memory_order_release);                             // Publish with release semantics so reader sees a fully written snapshot.
}

// Reader (ReportTask) obtains a copy of the currently active snapshot.
void LigaTable::get(LigaSnapshot& out) const {
    uint8_t idx = active_.load(std::memory_order_acquire);
    out         = buf_[idx];
}

// Useful for boot/first-run logic in ReportTask.
bool LigaTable::hasSnapshot() const {
    uint8_t idx = active_.load(std::memory_order_acquire);
    return buf_[idx].teamCount > 0;
}

void LigaTable::openLigaDBHealth() {
    if (WiFi.status() != WL_CONNECTED && !connect()) {
        ligaPrintln("OpenLigaDB WLAN down");
        return;
    }
    if (!waitForTime()) {
        ligaPrintln("OpenLigaDB Zeit nicht synchron - TLS kann scheitern");
    }
    ApiHealth h = checkOpenLigaDB(5000);
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

    if (getNextUpcomingBL1(nm)) {
        ligaPrint("(getNextMatch) Saison %d, Spieltag %d:", nm.season, nm.group);
        Serial.printf(" Nächstes Spiel (BL1): %s\n", nm.dateUTC.c_str());
        ligaPrintln("(getNextMatch) Begegnung: %s (%s) vs. %s (%s)", nm.team1.c_str(), nm.team1Short.c_str(), nm.team2.c_str(),
                    nm.team2Short.c_str());
    } else {
        ligaPrintln("(getNextMatch) Kein zukünftiges Spiel gefunden (Saison evtl. vorbei oder API down).");
    }
}

bool LigaTable::connect() {
    if (WiFi.status() == WL_CONNECTED)
        return true;
    WiFi.mode(WIFI_STA);
    ligaPrint("Verbinde mit WLAN");
    WiFi.begin(ssid, password);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(200);
        Serial.println('.');
        if (millis() - t0 > 15000) {
            ligaPrintln("\r\nWLAN-Timeout (15s)");
            return false;
        }
    }
    return true;
}

// --- Implementation: LigaTable::fetchTable (builds the snapshot) -------------
bool LigaTable::fetchTable(LigaSnapshot& out) {
    // Ensure WiFi is up (reuse your existing helpers)
    if (WiFi.status() != WL_CONNECTED && !connect()) {
        ligaPrintln("OpenLigaDB WLAN down");
        return false;
    }
    if (!waitForTime()) {
        // TLS may fail without a correct clock, but we try anyway
        ligaPrintln("OpenLigaDB time not synced - TLS may fail");
    }

    WiFiClientSecure client = makeSecureClient();
    HTTPClient       http;
    http.setReuse(false);
    http.setConnectTimeout(8000);
    http.setTimeout(8000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.useHTTP10(true);                                                       // avoid chunked if possible
    http.setUserAgent("ESP32-Flap/1.0");
    http.addHeader("Accept", "application/json");
    http.addHeader("Accept-Encoding", "identity");
    const char* hdrs[] = {"Content-Type", "Content-Encoding", "Transfer-Encoding"};
    http.collectHeaders(hdrs, 3);

    if (!http.begin(client, apiURL)) {
        ligaPrintln("http.begin() failed");
        return false;
    }

    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        ligaPrintln("HTTP GET failed: %d", code);
        http.end();
        return false;
    }

    // Parse JSON directly from the HTTP stream to avoid building a big String
    Stream* s = http.getStreamPtr();

    // Skip UTF-8 BOM if present
    if (s && s->peek() == 0xEF) {
        uint8_t bom[3];
        s->readBytes(bom, 3);
    }

    JsonDocument         doc;                                                   // dynamic document (ArduinoJson 7); scope-limited to this function
    DeserializationError err = deserializeJson(doc, *s);
    if (err) {
        ligaPrintln("JSON parse error: %s", err.c_str());
        http.end();
        return false;
    }

    // Accept either an array of teams or an object containing an array
    JsonArray arr;
    if (doc.is<JsonArray>()) {
        arr = doc.as<JsonArray>();
    } else if (doc.is<JsonObject>()) {
        JsonObject obj = doc.as<JsonObject>();
        // Try common keys used by OpenLigaDB variants
        if (obj["table"].is<JsonArray>())
            arr = obj["table"].as<JsonArray>();
        else if (obj["Table"].is<JsonArray>())
            arr = obj["Table"].as<JsonArray>();
        else if (obj["rows"].is<JsonArray>())
            arr = obj["rows"].as<JsonArray>();
    }

    if (arr.isNull()) {
        ligaPrintln("Unexpected JSON format - array of teams expected");
        http.end();
        return false;
    }

    // Build the compact snapshot
    LigaSnapshot snap;                                                          // local temp; we only expose the compact POD
    snap.clear();
    snap.fetchedAtUTC = (uint32_t)(millis() / 1000);

    // Take over season and matchday from poll
    snap.season   = Liga->_currentSeason;
    snap.matchday = Liga->_currentMatchDay;

    uint16_t rank = 1;
    for (JsonObject team : arr) {
        if (snap.teamCount >= LIGA_MAX_TEAMS)
            break;                                                              // safety bound

        const char* teamName  = team["teamName"] | team["TeamName"] | "";
        const char* shortName = team["shortName"] | team["ShortName"] | "";

        // Numeric fields with robust fallbacks
        int won  = team["won"] | team["Won"] | 0;
        int draw = team["draw"] | team["Draw"] | 0;
        int lost = team["lost"] | team["Lost"] | 0;
        int gf   = team["goals"] | team["Goals"] | 0;
        int ga   = team["opponentGoals"] | team["OpponentGoals"] | 0;

        int matches  = team["matches"] | team["Matches"] | (won + draw + lost);
        int goalDiff = team["goalDiff"] | team["GoalDiff"] | (gf - ga);
        int points   = team["points"] | team["Points"] | team["pts"] | 0;

        // Map DFB code using your existing helper
        String dfb = dfbCodeForTeamStrict(teamName);                            // may return ""

        // Fill row
        LigaRow& r = snap.rows[snap.teamCount];
        r.pos      = (uint8_t)rank;
        r.sp       = (uint8_t)max(0, min(99, matches));
        r.diff     = (int8_t)max(-99, min(99, goalDiff));
        r.pkt      = (uint8_t)max(0, min(255, points));

        copy_str_bounded(r.team, sizeof(r.team), teamName);
        copy_str_bounded(r.shortName, sizeof(r.shortName), shortName);
        copy_str_bounded(r.dfb, sizeof(r.dfb), dfb.c_str());

        snap.teamCount++;
        rank++;
    }

    // Output the result to caller without exposing any JSON object
    out = snap;

    http.end();
    client.stop();                                                              // explicitly close
    return true;
}

void LigaTable::getGoal() {
    LiveGoalEvent evs[12];
    size_t        n = collectNewGoalsAcrossLiveBL1(evs, 12);
    if (n > 0) {
        // Mindestens ein Tor in laufenden Spielen!
        for (size_t i = 0; i < n; i++) {
            Serial.printf("[TOR] %s  %s vs %s  %d’  %s  (%d:%d)%s%s%s\n", evs[i].goalTimeUTC.c_str(), evs[i].team1.c_str(), evs[i].team2.c_str(),
                          evs[i].minute, evs[i].scorer.c_str(), evs[i].score1, evs[i].score2, evs[i].isPenalty ? " [Elfer]" : "",
                          evs[i].isOwnGoal ? " [Eigentor]" : "", evs[i].isOvertime ? " [n.V.]" : "");
        }
        // -> hier: Flap-Animation, Buzzer, etc.
    }
}