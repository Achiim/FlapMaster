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

// 1. Bundesliga
static const DfbMap DFB1[] = {{"FC Bayern München", "FCB", 1},     {"Borussia Dortmund", "BVB", 7}, {"RB Leipzig", "RBL", 13},
                              {"Bayer 04 Leverkusen", "B04", 2},   {"1. FSV Mainz 05", "M05", 8},   {"Borussia Mönchengladbach", "BMG", 14},
                              {"Eintracht Frankfurt", "SGE", 3},   {"VfL Wolfsburg", "WOB", 9},     {"1. FC Union Berlin", "FCU", 15},
                              {"SC Freiburg", "SCF", 4},           {"TSG Hoffenheim", "TSG", 10},   {"VfB Stuttgart", "VFB", 16},
                              {"SV Werder Bremen", "SVW", 5},      {"FC Augsburg", "FCA", 11},      {"1. FC Köln", "KOE", 17},
                              {"1. FC Heidenheim 1846", "HDH", 6}, {"Hamburger SV", "HSV", 12},     {"FC St. Pauli", "STP", 18}};

// 2. Bundesliga (ergänzbar – strikt nach Namen)
static const DfbMap DFB2[] = {{"Hertha BSC", "BSC", 19},           {"VfL Bochum 1848", "BOC", 25},    {"Eintracht Braunschweig", "EBS", 30},
                              {"SV Darmstadt 98", "SVD", 20},      {"Fortuna Düsseldorf", "F95", 26}, {"SV 07 Elversberg", "ELV", 31},
                              {"SpVgg Greuther Fürth", "SGF", 21}, {"Hannover 96", "H96", 27},        {"1. FC Kaiserslautern", "FCK", 32},
                              {"Karlsruher SC", "KSC", 22},        {"1. FC Magdeburg", "FCM", 28},    {"1. FC Nürnberg", "FCN", 33},
                              {"SC Paderborn 07", "SCP", 23},      {"Holstein Kiel", "KSV", 29},      {"FC Schalke 04", "S04", 34},
                              {"SC Preußen Münster", "PRM", 24},   {"Arminia Bielefeld", "DSC", 35},  {"Dynamo Dresden", "DYN", 0}};

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

// Kleine Helper, um Keys case-robust zu lesen
static const char* strOr(JsonObjectConst o, const char* def, const char* k1, const char* k2 = nullptr, const char* k3 = nullptr,
                         const char* k4 = nullptr) {
    if (k1 && !o[k1].isNull())
        return o[k1];
    if (k2 && !o[k2].isNull())
        return o[k2];
    if (k3 && !o[k3].isNull())
        return o[k3];
    if (k4 && !o[k4].isNull())
        return o[k4];
    return def;
}
static int intOr(JsonObjectConst o, int def, const char* k1, const char* k2 = nullptr, const char* k3 = nullptr) {
    if (k1 && !o[k1].isNull())
        return o[k1].as<int>();
    if (k2 && !o[k2].isNull())
        return o[k2].as<int>();
    if (k3 && !o[k3].isNull())
        return o[k3].as<int>();
    return def;
}
static bool boolOr(JsonObjectConst o, bool def, const char* k1, const char* k2 = nullptr, const char* k3 = nullptr) {
    if (k1 && !o[k1].isNull())
        return o[k1].as<bool>();
    if (k2 && !o[k2].isNull())
        return o[k2].as<bool>();
    if (k3 && !o[k3].isNull())
        return o[k3].as<bool>();
    return def;
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

static bool httpGetJsonWith(HTTPClient& http, WiFiClientSecure& client, const char* url, JsonDocument& doc) {
    (void)waitForTime();

    http.setReuse(true);
    http.setConnectTimeout(8000);
    http.setTimeout(8000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.useHTTP10(true);                                                       // WICHTIG: 1.0 erzwingen -> kein chunked
    http.addHeader("Connection", "close");                                      // optional, macht’s explizit:
    http.setUserAgent("ESP32-Flap/1.0");
    http.addHeader("Accept", "application/json");
    http.addHeader("Accept-Encoding", "identity");
    const char* hdrs[] = {"Content-Type", "Content-Encoding", "Transfer-Encoding"};
    http.collectHeaders(hdrs, 3);

    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!http.begin(client, url)) {
            Serial.printf("HTTP begin failed: %s  (freeHeap=%u)\n", url, (unsigned)ESP.getFreeHeap());
            http.end();
            delay(150);
            continue;
        }

        int code = http.GET();
        if (code != HTTP_CODE_OK) {
            Serial.printf("GET %s -> %d (%s)\n", url, code, http.errorToString(code).c_str());
            http.end();
            if (code < 0)
                delay(150);                                                     // bei Transportfehler kurz retry
            continue;
        }

        Stream* s = http.getStreamPtr();
        if (s && s->peek() == 0xEF) {
            uint8_t bom[3];
            s->readBytes(bom, 3);
        }

        DeserializationError err = deserializeJson(doc, *s);
        http.end();
        if (err) {
            Serial.printf("JSON parse error @ %s: %s\n", url, err.c_str());

            const String ct = http.header("Content-Type");
            const String ce = http.header("Content-Encoding");
            const String te = http.header("Transfer-Encoding");
            const int    cl = http.header("Content-Length").toInt();
            Serial.printf("Bad JSON. CT=%s CE=%s TE=%s CL=%d\n", ct.c_str(), ce.c_str(), te.c_str(), cl);

            // Vorsichtig ersten Teil lesen (diagnostisch)
            WiFiClient* s = (WiFiClient*)http.getStreamPtr();
            char        peekBuf[96]{};
            size_t      n = s ? s->readBytes(peekBuf, sizeof(peekBuf) - 1) : 0;
            if (n) {
                Serial.print("Body[0..n]: ");
                for (size_t i = 0; i < n; i++)
                    Serial.write(isprint((unsigned char)peekBuf[i]) ? peekBuf[i] : '.');
                Serial.println();
            }

            return false;
        }
        return true;
    }
    return false;
}

// ----- Kernfunktion -----

// ---- Robuster Loader: ermittelt Season & Group und lädt den Spieltag ----
static bool loadCurrentBL1Matchday(JsonDocument& outDoc, int* outSeason, int* outGroupOrderId) {
    WiFiClientSecure client = makeSecureClient();
    HTTPClient       http;

    int season       = 0;
    int groupOrderID = 0;

    // --- 1) aktuelle Gruppe holen (kann Zahl/Strings oder Objekt/Array sein) ---
    {
        JsonDocument gdoc;
        if (httpGetJsonWith(http, client, "https://api.openligadb.de/getcurrentgroup/bl1", gdoc)) {
            if (gdoc.is<JsonObject>()) {
                auto g       = gdoc.as<JsonObject>();
                season       = g["leagueSeason"] | g["LeagueSeason"] | 0;
                groupOrderID = g["groupOrderID"] | g["GroupOrderID"] | 0;
            } else if (gdoc.is<JsonArray>() && gdoc.as<JsonArray>().size() > 0 && gdoc.as<JsonArray>()[0].is<JsonObject>()) {
                auto g       = gdoc.as<JsonArray>()[0].as<JsonObject>();
                season       = g["leagueSeason"] | g["LeagueSeason"] | 0;
                groupOrderID = g["groupOrderID"] | g["GroupOrderID"] | 0;
            } else if (gdoc.is<int>()) {
                groupOrderID = gdoc.as<int>();
            } else if (gdoc.is<const char*>()) {
                groupOrderID = atoi(gdoc.as<const char*>());
            } else {
                Serial.println("[collector] getcurrentgroup: unbekanntes Format");
            }
        } else {
            Serial.println("[collector] getcurrentgroup fehlgeschlagen");
        }
    }

    // --- 2) falls Season fehlt, aus grobem Endpoint bestimmen ---
    if (groupOrderID > 0 && season == 0) {
        JsonDocument tmp;
        if (httpGetJsonWith(http, client, "https://api.openligadb.de/getmatchdata/bl1", tmp)) {
            JsonArray matches = tmp.as<JsonArray>();
            if (!matches.isNull() && matches.size() > 0) {
                season = matches[0]["leagueSeason"] | matches[0]["LeagueSeason"] | 0;
            }
        }
    }

    // --- 3) exakten Spieltag laden ---
    if (groupOrderID > 0 && season > 0) {
        char url[160];
        snprintf(url, sizeof(url), "https://api.openligadb.de/getmatchdata/bl1/%d/%d", season, groupOrderID);
        Serial.printf("[collector] GET %s\n", url);
        if (httpGetJsonWith(http, client, url, outDoc)) {
            if (outSeason)
                *outSeason = season;
            if (outGroupOrderId)
                *outGroupOrderId = groupOrderID;
            return true;
        }
        Serial.println("[collector] getmatchdata/<season>/<group> fehlgeschlagen (Primärpfad)");
    } else {
        Serial.println("[collector] getcurrentgroup: season/groupOrderID nicht bestimmbar");
    }

    // Optional: dein vorhandener Fallback kann hier bleiben
    return false;
}

size_t LigaTable::collectNewGoalsAcrossLiveBL1(LiveGoalEvent* out, size_t maxOut) {
    size_t outN = 0;

    if (WiFi.status() != WL_CONNECTED && !connect()) {
        Serial.println("[collector] WLAN down");
        return 0;
    }
    (void)waitForTime();

    JsonDocument doc;
    int          season = 0, groupOrderID = 0;
    if (!loadCurrentBL1Matchday(doc, &season, &groupOrderID)) {
        Serial.println("[collector] Spieltag konnte nicht geladen werden");
        return 0;
    }
    JsonArray matches = doc.as<JsonArray>();
    if (matches.isNull() || matches.size() == 0) {
        Serial.println("[FLAP  -  LIGA  ] keine Spiele im aktuellen Spieltag");
        return 0;
    }

    const time_t nowT = toUtcTimeT(nowUTC_ISO());

    for (JsonObject m : matches) {
        // Teams für Logs
        JsonObject  t1    = m["team1"].isNull() ? m["Team1"] : m["team1"];
        JsonObject  t2    = m["team2"].isNull() ? m["Team2"] : m["team2"];
        const char* name1 = strOr(t1, "Team1", "teamName", "TeamName");
        const char* name2 = strOr(t2, "Team2", "teamName", "TeamName");

        // Status/Zeit
        bool        finished = boolOr(m, false, "matchIsFinished", "MatchIsFinished");
        const char* kickC    = strOr(m, "", "matchDateTimeUTC", "MatchDateTimeUTC", "matchDateTime");
        if (!*kickC)
            continue;
        time_t kickT = toUtcTimeT(kickC);
        if (kickT == 0)
            continue;

        if (nowT < kickT) {
            Serial.printf("[FLAP  -  LIGA  ] %s vs %s -> Spiel noch nicht live: goals[] und results[] leer\n", name1, name2);
            continue;
        }

        int mid = intOr(m, 0, "matchID", "MatchID");
        if (mid <= 0)
            continue;

        MatchState* st = stateFor(mid);
        if (!st)
            continue;

        int maxSeen = st->lastGoalID;

        // --- Live-Goals einsammeln ---
        bool        anyNewGoalsForThisMatch = false;
        JsonArray   goals                   = !m["goals"].isNull() ? m["goals"].as<JsonArray>() : m["Goals"].as<JsonArray>();
        const char* lastUpd                 = strOr(m, "", "lastUpdateDateTime", "LastUpdateDateTime");

        if (!goals.isNull() && goals.size() > 0) {
            for (JsonObject g : goals) {
                int gid = intOr(g, 0, "goalID", "GoalID");
                if (gid <= st->lastGoalID)
                    continue;
                if (outN >= maxOut)
                    break;

                int         minute   = intOr(g, -1, "matchMinute", "MatchMinute");
                bool        penalty  = boolOr(g, false, "isPenalty", "IsPenalty");
                bool        owngoal  = boolOr(g, false, "isOwnGoal", "IsOwnGoal");
                bool        overtime = boolOr(g, false, "isOvertime", "IsOvertime", "IsOverTime");
                int         s1       = intOr(g, 0, "scoreTeam1", "ScoreTeam1");
                int         s2       = intOr(g, 0, "scoreTeam2", "ScoreTeam2");
                const char* scorer   = strOr(g, "", "goalGetterName", "GoalGetterName", "goalGetter", "GoalGetter");

                LiveGoalEvent& ev = out[outN++];
                ev.matchID        = mid;
                ev.kickOffUTC     = kickC;
                ev.minute         = minute;
                ev.isPenalty      = penalty;
                ev.isOwnGoal      = owngoal;
                ev.isOvertime     = overtime;
                ev.score1         = s1;
                ev.score2         = s2;
                ev.scorer         = scorer;
                ev.goalTimeUTC    = lastUpd;
                ev.team1          = name1;
                ev.team2          = name2;

                if (gid > maxSeen)
                    maxSeen = gid;
                anyNewGoalsForThisMatch = true;
            }
        } else if (!finished) {
            Serial.printf("[FLAP  -  LIGA  ] %s vs %s -> Live, aber noch keine Goals gemeldet\n", name1, name2);
        }

        // --- Fallback: Spiel beendet, aber keine Goals -> Endstand loggen ---
        if (finished && !anyNewGoalsForThisMatch) {
            JsonArray results = !m["matchResults"].isNull() ? m["matchResults"].as<JsonArray>() : m["MatchResults"].as<JsonArray>();
            int       endS1 = -1, endS2 = -1;
            if (!results.isNull()) {
                for (JsonObject r : results) {
                    int typeId = intOr(r, 0, "resultTypeID", "ResultTypeID");
                    if (typeId == 2) {                                          // Endstand
                        endS1 = intOr(r, -1, "pointsTeam1", "PointsTeam1");
                        endS2 = intOr(r, -1, "pointsTeam2", "PointsTeam2");
                        break;
                    }
                }
            }
            if (endS1 >= 0 && endS2 >= 0) {
                Serial.printf("[FLAP  -  LIGA  ] %s vs %s -> beendet: (%d:%d), keine Goals im Feed\n", name1, name2, endS1, endS2);
            } else {
                Serial.printf("[FLAP  -  LIGA  ] %s vs %s -> beendet, aber weder Goals noch Endstand vorhanden\n", name1, name2);
            }
        }

        // höchsten gesehenen goalID-Stand merken
        if (maxSeen > st->lastGoalID)
            st->lastGoalID = maxSeen;

        if (outN >= maxOut)
            break;                                                              // Ausgabepuffer voll
    }

    return outN;
}

// Holt LastChange für eine (Saison, Gruppe)
static bool getLastChange(int season, int group, String& out) {
    char             url[128];
    WiFiClientSecure client = makeSecureClient();                               // eine TLS-Session
    HTTPClient       http;                                                      // ein HTTPClient
    snprintf(url, sizeof(url), "https://api.openligadb.de/getlastchangedate/bl1/%d/%d", season, group);

    JsonDocument ldc;
    if (!httpGetJsonWith(http, client, url, ldc))
        return false;

    // Entweder reiner String oder { "lastChangeDate": "..." }
    out = ldc.is<JsonObject>() ? (const char*)(ldc["lastChangeDate"] | "") : ldc.as<const char*>();
    out.trim();
    return out.length() > 0;
}

// Holt Season & aktuellen Spieltag aus dem "aktueller Spieltag"-Endpoint
bool LigaTable::getSeasonAndGroup(int& season, int& group) {
    JsonDocument     doc;
    WiFiClientSecure client = makeSecureClient();                               // eine TLS-Session
    HTTPClient       http;                                                      // ein HTTPClient

    if (!httpGetJsonWith(http, client, "https://api.openligadb.de/getmatchdata/bl1", doc))
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

    if (latest.isEmpty() || latest == s_lastChange) {
        #ifdef LIGAVERBOSE
            {
            TraceScope trace;
            ligaPrintln("LigaTable changed at %s, nothing new", latest.c_str());
            }
        #endif
        return false;
    }

    #ifdef LIGAVERBOSE
        {
        TraceScope trace;
        ligaPrintln("LigaTable changed at %s please actualize", latest.c_str());
        }
    #endif

    time_t t = toUtcTimeT(latest);
    if (t > 0)
        s_lastChangeEpoch = t;                                                  // letzter Zeitpunkt des schnellen poll (während ein Spiel läuft)

    s_lastChange = latest;                                                      // letzter Zeitpunkt des normalen poll (wenn kein Spiel läuft)

    return true;
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

static int flapForTeamStrict(const String& teamName) {
    String key = teamName;
    for (auto& e : DFB1)
        if (key == e.key)
            return e.flap;
    for (auto& e : DFB2)
        if (key == e.key)
            return e.flap;
    return -1;                                                                  // strikt: kein Treffer => -1
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

// Hauptroutine: ermittelt Season & aktuellen Spieltag aus getmatchdata/bl1,
// sucht dort das nächste Spiel; wenn keins: lädt den nächsten Spieltag.
bool getNextUpcomingBL1(NextMatch& out) {
    const String     nowIso = nowUTC_ISO();
    WiFiClientSecure client = makeSecureClient();                               // eine TLS-Session
    HTTPClient       http;                                                      // ein HTTPClient

    // 1) Aktuellen Spieltag samt Season holen
    JsonDocument curDoc;
    if (!httpGetJsonWith(http, client, "https://api.openligadb.de/getmatchdata/bl1", curDoc))
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
    if (!httpGetJsonWith(http, client, url, nextDoc))
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
    s_lastKickoffRefreshMs  = millis();
    WiFiClientSecure client = makeSecureClient();                               // eine TLS-Session
    HTTPClient       http;                                                      // ein HTTPClient

    JsonDocument doc;                                                           // klein halten, kein Riesenfilter nötig
    if (!httpGetJsonWith(http, client, "https://api.openligadb.de/getmatchdata/bl1", doc))
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
        return POLL_10MIN_BEFORE_KICKOFF;                                       // 5 s
    }
    // Während Live-Phase: in den letzten 15 min gab es Änderungen
    if (s_lastChangeEpoch && (nowT - s_lastChangeEpoch) <= 15 * 60) {
        return POLL_DURING_GAME;                                                // 3 s
    }
    // Sonst gemütlich
    return POLL_NORMAL;                                                         // 15 min
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
    WiFi.setSleep(false);                                                       // vermeidet sporadische TLS-Fehler beim Aufwachen
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
    http.useHTTP10(true);                                                       // WICHTIG: 1.0 erzwingen -> kein chunked
    http.addHeader("Connection", "close");                                      // optional, macht’s explizit:
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

        // Map flap using your existing helper
        int flap = flapForTeamStrict(teamName);                                 // may return -1

        // Fill row
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

    // Output the result to caller without exposing any JSON object
    out = snap;

    http.end();
    client.stop();                                                              // explicitly close
    return true;
}

void LigaTable::getGoal() {
    static LiveGoalEvent evs[12];
    size_t               n = collectNewGoalsAcrossLiveBL1(evs, 12);
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