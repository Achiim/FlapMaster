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

// ===== DFB/DFL 3-Letter Mapping (strict; kein Fallback/keine Heuristik) =====
static String normKey(String s) {
    s.trim();
    s.replace("\xC2\xA0", " ");                                                 // NBSP -> Space
    s.replace("ä", "ae");
    s.replace("ö", "oe");
    s.replace("ü", "ue");
    s.replace("Ä", "AE");
    s.replace("Ö", "OE");
    s.replace("Ü", "UE");
    s.replace("ß", "ss");
    s.replace("-", " ");
    s.replace(".", " ");
    while (s.indexOf("  ") >= 0)
        s.replace("  ", " ");
    s.toUpperCase();
    return s;
}

struct DfbMap {
    const char* key;
    const char* code;
};

// 1. Bundesliga
static const DfbMap DFB1[] = {
    {"FC BAYERN MUENCHEN", "FCB"},   {"BORUSSIA DORTMUND", "BVB"}, {"RB LEIPZIG", "RBL"},
    {"BAYER 04 LEVERKUSEN", "B04"},  {"1 FSV MAINZ 05", "M05"},    {"BORUSSIA MOENCHENGLADBACH", "BMG"},
    {"EINTRACHT FRANKFURT", "SGE"},  {"VFL WOLFSBURG", "WOB"},     {"1 FC UNION BERLIN", "FCU"},
    {"SC FREIBURG", "SCF"},          {"TSG HOFFENHEIM", "TSG"},    {"VFB STUTTGART", "VFB"},
    {"SV WERDER BREMEN", "SVW"},     {"FC AUGSBURG", "FCA"},       {"1 FC KOELN", "KOE"},
    {"1 FC HEIDENHEIM 1846", "HDH"}, {"HAMBURGER SV", "HSV"},      {"FC ST PAULI", "STP"},
};

// 2. Bundesliga (ergänzbar – strikt nach Namen)
static const DfbMap DFB2[] = {
    {"HERTHA BSC", "BSC"},          {"VFL BOCHUM 1848", "BOC"}, {"EINTRACHT BRAUNSCHWEIG", "EBS"}, {"SV DARMSTADT 98", "SVD"},
    {"FORTUNA DUESSELDORF", "F95"}, {"SV ELVERSBERG", "ELV"},   {"SPVGG GREUTHER FUERTH", "SGF"},  {"HANNOVER 96", "H96"},
    {"1 FC KAISERSLAUTERN", "FCK"}, {"KARLSRUHER SC", "KSC"},   {"1 FC MAGDEBURG", "FCM"},         {"1 FC NUERNBERG", "FCN"},
    {"SC PADERBORN 07", "SCP"},     {"HOLSTEIN KIEL", "KSV"},   {"FC SCHALKE 04", "S04"},          {"SC PREUSSEN MUENSTER", "PRM"},
    // ggf. weitere Vereine später ergänzen (Rostock, Osnabrück, Wiesbaden, Sandhausen, Ulm …)
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

// Merker
static String s_lastChange;                                                     // z.B. "2025-08-24T17:24:08.0000000Z"

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

// Dein vorhandener HTTPS-JSON-Helper (gleiche TLS-Konfig wie in fetch())
static bool httpGetJson(const char* url, JsonDocument& doc);

// ----- Kernfunktion -----
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
bool LigaTable::pollLastChange() {
    int season = 0, group = 0;
    if (!getSeasonAndGroup(season, group))
        return false;

    String lcCur, lcPrev;
    if (!getLastChange(season, group, lcCur))
        lcCur = "";
    if (group > 1 && !getLastChange(season, group - 1, lcPrev))
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
    s_lastChange = latest;
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
    String key = normKey(teamName);
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

// ===== UTF-8-Ausrichtung (Umlaute korrekt, exakt width Spalten) =====
static void printUtf8Cell(const char* s, int width) {
    const uint8_t* p   = (const uint8_t*)s;
    const uint8_t* end = p;
    int            cps = 0;
    while (*p && cps < width) {
        uint8_t c = *p;
        if ((c & 0x80) == 0x00)
            p += 1;
        else if ((c & 0xE0) == 0xC0)
            p += 2;
        else if ((c & 0xF0) == 0xE0)
            p += 3;
        else if ((c & 0xF8) == 0xF0)
            p += 4;
        else
            p += 1;
        cps++;
        end = p;                                                                // end nach dem Vorrücken setzen
    }
    Serial.write((const char*)s, (size_t)(end - (const uint8_t*)s));
    for (int i = cps; i < width; ++i)
        Serial.write(' ');
}

// Kompakte Spaltenbreiten
constexpr int W_POS   = 3;                                                      // 1..18
constexpr int W_TEAM  = 24;                                                     // z. B. „Borussia Mönchengladbach“
constexpr int W_SHORT = 10;                                                     // „Union Berlin“ ggf. gekürzt
constexpr int W_DFB   = 3;                                                      // 3-Letter
constexpr int W_SP    = 2;                                                      // Spiele (1..34)
constexpr int W_DIFF  = 4;                                                      // -99..+99
constexpr int W_PKT   = 3;                                                      // 0..99

static void printBorder() {
    auto dash = [](int w) {
        for (int i = 0; i < w + 2; i++)
            Serial.write('-');
        Serial.write('+');
    };
    Serial.write('+');
    dash(W_POS);
    dash(W_TEAM);
    dash(W_SHORT);
    dash(W_DFB);
    dash(W_SP);
    dash(W_DIFF);
    dash(W_PKT);
    Serial.write('\r');
    Serial.write('\n');
}

static void printHeader() {
    printBorder();
    Serial.print("| ");
    printUtf8Cell("Pos", W_POS);
    Serial.print(" | ");
    printUtf8Cell("teamName", W_TEAM);
    Serial.print(" | ");
    printUtf8Cell("shortName", W_SHORT);
    Serial.print(" | ");
    printUtf8Cell("DFB", W_DFB);
    Serial.print(" | ");
    printUtf8Cell("Sp", W_SP);
    Serial.print(" | ");
    printUtf8Cell("Diff", W_DIFF);
    Serial.print(" | ");
    printUtf8Cell("Pkt", W_PKT);
    Serial.println(" |");
    printBorder();
}

// ===== LigaTable: Implementierung =====
LigaTable::LigaTable() {
    // Berlin: CET/CEST
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov"); // synch time
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
    ligaPrintln("openLigaDB - Status: %s (http=%d, %d ms) %s\n", h.status.c_str(), h.httpCode, h.elapsedMs, h.detail.c_str());
}

void LigaTable::getNextMatch() {
    NextMatch nm;

    if (WiFi.status() != WL_CONNECTED && !connect()) {
        Serial.println("[NextMatch] WLAN down");
        return;
    }
    if (!waitForTime()) {
        Serial.println("[NextMatch] Zeit nicht synchron - Ergebnis evtl. falsch");
    }

    if (getNextUpcomingBL1(nm)) {
        Serial.printf("Saison %d, Spieltag %d:\n", nm.season, nm.group);
        Serial.printf("Nächstes Spiel (BL1): %s\n", nm.dateUTC.c_str());
        Serial.printf("Begegnung: %s (%s) vs. %s (%s)\n", nm.team1.c_str(), nm.team1Short.c_str(), nm.team2.c_str(), nm.team2Short.c_str());
    } else {
        Serial.println("Kein zukünftiges Spiel gefunden (Saison evtl. vorbei oder API down).");
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

bool LigaTable::fetchTable() {
    if (WiFi.status() != WL_CONNECTED && !connect()) {
        ligaPrintln("OpenLigaDB WLAN down");
        return false;
    }
    if (!waitForTime()) {
        ligaPrintln("OpenLigaDB Zeit nicht synchron - TLS kann scheitern");
    }
    WiFiClientSecure client = makeSecureClient();
    HTTPClient       http;
    http.setReuse(false);
    http.setConnectTimeout(8000);
    http.setTimeout(8000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.useHTTP10(true);                                                       // vermeidet chunked
    http.setUserAgent("ESP32-Flap/1.0");
    http.addHeader("Accept", "application/json");
    http.addHeader("Accept-Encoding", "identity");
    const char* hdrs[] = {"Content-Type", "Content-Encoding", "Transfer-Encoding"};
    http.collectHeaders(hdrs, 3);

    if (!http.begin(client, apiURL)) {
        ligaPrintln("http.begin() fehlgeschlagen");
        return false;
    }
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        ligaPrintln("Fehler beim Abrufen: %d\r\n", code);
        http.end();
        return false;
    }

    // komplett einlesen (funktioniert auch bei chunked)
    int    len = http.getSize();
    String payload;
    if (len > 0) {
        payload.reserve(len);
        WiFiClient* s = http.getStreamPtr();
        while (len > 0) {
            uint8_t buf[256];
            int     n = s->read(buf, (len > 256) ? 256 : len);
            if (n <= 0) {
                delay(1);
                continue;
            }
            payload.concat(String((const char*)buf).substring(0, n));           // simpel, kostet etwas CPU/RAM
            len -= n;
        }
    } else {
        payload = http.getString();                                             // Fallback (chunked)
    }

    if (payload.isEmpty()) {
        const size_t MAX_BYTES = 65536;
        Stream&      s         = http.getStream();
        uint32_t     t0        = millis();
        while ((millis() - t0) < 10000) {
            while (s.available()) {
                payload += (char)s.read();
                if (payload.length() >= MAX_BYTES)
                    break;
            }
            const size_t MAX_BYTES = 65536;
            Stream&      s         = http.getStream();
            uint32_t     t0 = millis(), last = t0;
            size_t       lastSize = payload.length();
            while (millis() - t0 < 10000) {                                     // 10 s Gesamt-Timeout
                while (s.available()) {
                    char c = (char)s.read();
                    payload += c;
                    if (payload.length() >= MAX_BYTES)
                        break;
                    last = millis();
                }
                // Kein weiterer Fortschritt seit 250 ms? -> fertig.
                if (millis() - last > 250)
                    break;
                delay(1);
                if (payload.length() >= MAX_BYTES)
                    break;
            }
            delay(1);
            if (payload.length() >= MAX_BYTES)
                break;
        }
    }

    // BOM + Whitespace
    if (payload.length() >= 3 && (uint8_t)payload[0] == 0xEF && (uint8_t)payload[1] == 0xBB && (uint8_t)payload[2] == 0xBF)
        payload.remove(0, 3);
    payload.trim();

    if (!payload.startsWith("[") && !payload.startsWith("{")) {
        ligaPrintln("Antwort ist kein JSON - bitte apiURL prüfen");
        http.end();
        return false;
    }

    JsonDocument         doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        ligaPrintln(String("Fehler beim JSON-Parsen: ") + err.c_str());
        http.end();
        return false;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull()) {
        ligaPrintln("Unerwartetes JSON-Format - Array erwartet");
        http.end();
        return false;
    }

    // Ausgabe
    Serial.println(F("\r\nBundesliga-Tabelle (OpenLigaDB):"));
    printHeader();

    uint16_t rank = 1;
    for (JsonObject team : arr) {
        const char* teamName  = team["teamName"] | team["TeamName"] | "";
        const char* shortName = team["shortName"] | team["ShortName"] | "";

        int won  = team["won"] | 0;
        int draw = team["draw"] | 0;
        int lost = team["lost"] | 0;
        int gf   = team["goals"] | team["Goals"] | 0;
        int ga   = team["opponentGoals"] | team["OpponentGoals"] | 0;

        int matches  = team["matches"] | (won + draw + lost);
        int goalDiff = team["goalDiff"] | team["GoalDiff"] | (gf - ga);
        int points   = team["points"] | team["Points"] | team["pts"] | 0;

        String dfb = dfbCodeForTeamStrict(teamName);                            // strikt: ggf. ""

        Serial.print("| ");
        Serial.printf("%*u", W_POS, rank);
        Serial.print(" | ");
        printUtf8Cell(teamName, W_TEAM);
        Serial.print(" | ");
        printUtf8Cell(shortName, W_SHORT);
        Serial.print(" | ");
        printUtf8Cell(dfb.c_str(), W_DFB);
        Serial.print(" | ");
        Serial.printf("%*d", W_SP, matches);
        Serial.print(" | ");
        Serial.printf("%*d", W_DIFF, goalDiff);
        Serial.print(" | ");
        Serial.printf("%*d", W_PKT, points);
        Serial.println(" |");

        rank++;
    }
    printBorder();

    http.end();
    client.stop();                                                              // explizit schließen
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

void LigaTable::tableChanged() {
    fetchTable();                                                               // get table
}