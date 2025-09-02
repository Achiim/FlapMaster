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
#ifndef Liga_h
#define Liga_h

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>                                                        // v7 (JsonDocument)
#include <atomic>
#include <stdint.h>
#include <string.h>
#include "TracePrint.h"

// --- Configuration -----------------------------------------------------------
#define LIGA_MAX_TEAMS 18
#define POLL_10MIN_BEFORE_KICKOFF 60 * 1000                                     // 60 seconds
#define POLL_DURING_GAME 20 * 1000                                              // 20 seconds
#define POLL_NORMAL 15 * 60 * 1000                                              // 15 minutes

// --- Chooseable league -------------------------------------------------------
enum class League : uint8_t { BL1 = 1, BL2 = 2 };
extern League activeLeague;

/** @brief Return OpenLigaDB league shortcut string for the given enum. */
static inline const char* leagueShortcut(League lg) {
    return (lg == League::BL2) ? "bl2" : "bl1";
}

// to be used for dynamic polling
static String         s_lastChange;                                             // dein vorhandener ISO-String
static time_t         s_lastChangeEpoch      = 0;                               // Epoch zum schnellen Vergleich
static time_t         s_nextKickoffEpoch     = 0;                               // nächster Anstoß (Epoch UTC)
static const uint32_t KICKOFF_REFRESH_MS     = 30000;                           // 30 Seconds
static uint32_t       s_lastKickoffRefreshMs = KICKOFF_REFRESH_MS;              // wann zuletzt berechnet

// ----- Datatyps -----
struct DfbMap {
    const char* key;                                                            // team name
    const char* code;                                                           // 3-char DFB abbriviation
    const int   flap;                                                           // falp number
};

/// One goal event detected for a live match.
struct LiveGoalEvent {
    int    matchID = 0;
    String kickOffUTC;                                                          // kickoff timestamp (UTC)
    String goalTimeUTC;                                                         // event timestamp (from lastUpdate or similar)
    int    minute = -1;                                                         // match minute
    int    score1 = 0, score2 = 0;                                              // score after this goal (team1:team2)
    String team1, team2, scorer;                                                // home, away, goal scorer name
    bool   isPenalty  = false;                                                  // true if penalty
    bool   isOwnGoal  = false;                                                  // true if own goal
    bool   isOvertime = false;                                                  // true if extra time (n.V.)
    String scoredFor;                                                           // resolved team name that gets credit for the goal
};

// ---------- Liga Table row (ASCII-only snapshot) ----------
struct LigaRow {
    uint8_t pos;                                                                // 1..18
    uint8_t sp;                                                                 // matches played
    int8_t  diff;                                                               // goal difference
    uint8_t pkt;                                                                // points
    char    team[32];                                                           // ASCII only (pretransliterated)
    char    shortName[16];                                                      // ASCII only
    char    dfb[4];                                                             // ASCII (3-letter + NUL)
    uint8_t flap;                                                               // flap
};

struct LigaSnapshot {
    uint16_t season;                                                            // 0 if unknown
    uint8_t  matchday;                                                          // 0 if unknown
    uint32_t nextKickoffUTC;                                                    // 0 if unknown
    uint32_t fetchedAtUTC;                                                      // epoch-ish
    uint8_t  teamCount;                                                         // <= 18
    LigaRow  rows[LIGA_MAX_TEAMS];
    void     clear() {                                                          // clear snapshot
        season         = 0;
        matchday       = 0;
        nextKickoffUTC = 0;
        fetchedAtUTC   = 0;
        teamCount      = 0;
        for (uint8_t i = 0; i < LIGA_MAX_TEAMS; i++) {
            rows[i].pos = rows[i].sp = rows[i].pkt = 0;
            rows[i].diff                           = 0;
            rows[i].team[0] = rows[i].shortName[0] = rows[i].dfb[0] = '\0';
        }
    }
};

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

struct SeasonGroup {
    int      season      = -1;
    int      group       = -1;
    bool     valid       = false;
    uint32_t fetchedAtMs = 0;
};

class LigaTable {
   public:
    // Constructor for LigaTable
    LigaTable();

    bool connect();                                                             // connect to external data provider for liga data
    bool fetchTable(LigaSnapshot& out);                                         // get data from external provider
    bool disconnect();                                                          // disconnect from external data provider for liga data
    bool pollLastChange(League league, int& seasonOut, int& matchdayOut);       // get last change date/time of liga
    bool getSeasonAndGroup(League league, int& outSeason, int& outGroup);       // get saison and Spieltag
    // bool     getLastChange(League league, int season, int group, String& out);
    // void     getNextMatch();                                                    // get next match date, time and opponents
    void openLigaDBHealth();                                                    // health check
    // void     getGoalsLive();                                                    // get goal events
    // size_t   getGoalsLive(League league, LiveGoalEvent* out, size_t maxOut);    // get goal events
    void     get(LigaSnapshot& out) const;                                      // get snapshot from openLigaDB
    void     commit(const LigaSnapshot& s);                                     // release snahpshot to be accessed by reporting
    uint32_t decidePollMs();                                                    // get time to wait until poll openLigaDB again
    // size_t   collectNewGoalsAcrossLive(League league, LiveGoalEvent* out, size_t maxOut);
    int collectLiveMatches(League league, LiveGoalEvent* out, size_t maxCount);
    int fetchGoalsForLiveMatch(int matchId, const String& sinceUtc, LiveGoalEvent* out, size_t maxCount);

   private:
    LigaSnapshot         buf_[2];                                               // two buffer to bw switched
    std::atomic<uint8_t> active_;                                               // signalize writing  to snapshot
    SeasonGroup          _lastSG;                                               // last fetched season and group
    int                  currentSeason_   = 0;                                  // Season 2025
    int                  currentMatchDay_ = 0;                                  // Matchday 1...34

    // private member functions
    // bool        httpGetJsonWith(HTTPClient& http, WiFiClientSecure& client, const char* url, JsonDocument& doc);
    bool httpGetJsonRobust(HTTPClient& http, WiFiClientSecure& client, const String& url, JsonDocument& doc, int maxRetry = 2);
    // bool        loadCurrentMatchday(JsonDocument& outDoc, League league, int* outSeason, int* outGroupOrderId);
    // bool      getNextUpcoming(League league, NextMatch& nm);
    void      refreshNextKickoffEpoch(League league);
    ApiHealth checkOpenLigaDB(League league, unsigned timeoutMs = 5000);
    // int         currentSeasonFromDate();
    SeasonGroup lastSeasonGroup() const;
    time_t      bestFutureKickoffInGroup(League league, int season, int group);

    // Liga trace
    template <typename... Args>
    void ligaPrint(const Args&... args) {                                       // standard parserPrint
        tracePrint("[FLAP  -  LIGA  ] ", args...);
    }

    template <typename... Args>
    void ligaPrintln(const Args&... args) {                                     // standard parserPrint with new line
        tracePrintln("[FLAP  -  LIGA  ] ", args...);
    }
};

#endif                                                                          // Liga_h
