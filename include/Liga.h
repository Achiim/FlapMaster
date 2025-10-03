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
#include "TracePrint.h"

// ==== defines ====
#define flapUserAgent "Liga Flap Display V1.0 ESP32"
#define LIGA1_MAX_TEAMS 18
#define LIGA2_MAX_TEAMS 18
#define LIGA3_MAX_TEAMS 20

#define MAX_TEAMNAME_LENGTH 32                                                  // max. length of team name (ASCII only)
#define MAX_DFB_SHORT 4                                                         // max. length of DFB short name (3-letter + NUL)

#define MAX_MATCHES_PER_MATCHDAY 10                                             // max. number of matches per matchday to track
#define MAX_GOALS_PER_MATCHDAY 50                                               // max. number of goals per matchday to track
#define MAX_MATCH_DURATION 150 * 60                                             // 2,5 hours in seconds
#define TEN_MINUTES_BEFORE_MATCH 10 * 10 * 60                                   // 10 minutes in seconds

#define POLL_NOWAIT (0)                                                         // no wait at all
#define POLL_GET_ALL_CHANGES (1 * 1000)                                         // 1 seconds
#define POLL_DURING_GAME (20 * 1000)                                            // 20 seconds
#define POLL_10MIN_BEFORE_KICKOFF (1 * 60 * 1000)                               // 1 minutes
#define POLL_NORMAL (60 * 60 * 1000)                                            // 60 minutes

// ==== defines ====

// ==== enums ====
// --- Chooseable league -------------------------------------------------------
enum class League : uint8_t { BL1 = 1, BL2 = 2, BL3 = 3 };                      // Bundesliga 1, 2, 3
extern League activeLeague;

/** @brief Return OpenLigaDB league shortcut string for the given enum. */
static inline const char* leagueShortcut(League lg) {
    if (lg == League::BL3)
        return "bl3";
    if (lg == League::BL2)
        return "bl2";
    if (lg == League::BL1)
        return "bl1";
    return "bl1";                                                               // default
}

/** @brief Return OpenLigaDB league name string for the given enum. */
static inline const char* leagueName(League lg) {
    if (lg == League::BL3)
        return "3. Bundesliga";
    if (lg == League::BL2)
        return "2. Bundesliga";
    if (lg == League::BL1)
        return "1. Bundesliga";
    return "-";                                                                 // default
}

enum PollScope {
    CHECK_FOR_CHANGES,                                                          // only check for changes on openLigaDB
    FETCH_TABLE,                                                                // fetch actualized table
    FETCH_CURRENT_MATCHDAY,                                                     // fetch actual matchday
    CALC_CURRENT_SEASON,                                                        // calculate actuel season
    FETCH_NEXT_KICKOFF,                                                         // fetch next kickoff (don't fetch during game is live)
    FETCH_LIVE_MATCHES,                                                         // fetch live matches, that are in future or kickoff was 2,5 hours ago
    FETCH_LIVE_GOALS,                                                           // fetch goals for live matches by mathchID
    FETCH_NEXT_MATCH_LIST,                                                      // fetch list of next matches with nearest kickoff
    SHOW_NEXT_KICKOFF,                                                          // show next kickoff from stored data
    CALC_LIVE_TABLE,                                                            // calculate table changes from old and new table
    CALC_LEADER_CHANGE,                                                         // calculate leader change from table
    CALC_RELEGATION_GHOST_CHANGE,                                               // calculate relegation ghost change from table
    CALC_RED_LANTERN_CHANGE,                                                    // calculate red lantern change from table
    CALC_GOALS                                                                  // calculate goals from table
};

// global Poll Modes
enum PollMode {
    POLL_MODE_NONE,                                                             // no polling at all
    POLL_MODE_ONCE,                                                             // poll only once and then switch to relaxed
    POLL_MODE_RELAXED,                                                          // relaxed poll wide outside of live activity
    POLL_MODE_REACTIVE,                                                         // something was changed in openLigaDB data -> get it
    POLL_MODE_PRELIVE,                                                          // shortly befor next live game
    POLL_MODE_LIVE                                                              // during live games until it's over (2h past kickoff)
};

// global Poll Scopes for actual PollCycle for Poll-Manager
const PollScope onceCycle[] = {
    // do it only once to initiate data
    CALC_CURRENT_SEASON,                                                        // initialize season
    FETCH_CURRENT_MATCHDAY,                                                     // initialize matchday
    CHECK_FOR_CHANGES                                                           // request openLigaDB for changes at current matchday
};

const PollScope relaxedCycle[] = {
    CALC_CURRENT_SEASON,                                                        // initialize season
    FETCH_CURRENT_MATCHDAY,                                                     // initialize matchday
    FETCH_TABLE,                                                                // get actual table from openLigaDB first time
    FETCH_NEXT_MATCH_LIST,                                                      // fetch list of next matches with nearest kickoff
    FETCH_NEXT_KICKOFF,                                                         // fetch next kickoff
    CHECK_FOR_CHANGES                                                           // request openLigaDB for changes at current matchday
};

const PollScope reactiveCycle[] = {
    FETCH_LIVE_MATCHES,                                                         // are there actual live matches? to get into live Poll immediately
    FETCH_NEXT_MATCH_LIST,                                                      // fetch list of next matches with nearest kickoff
    FETCH_TABLE,                                                                // get actual table from openLigaDB first time
    CHECK_FOR_CHANGES                                                           // request openLigaDB for changes at current matchday
};

const PollScope preLiveCycle[] = {
    FETCH_LIVE_MATCHES,                                                         // are there actual live matches? to get into live Poll immediately
    SHOW_NEXT_KICKOFF,                                                          // show actual kickoff from stored data
    CHECK_FOR_CHANGES                                                           // request openLigaDB for changes at current matchday
};

// don't ask for nextKickoff during live games, you will get kickoff from next live matches
const PollScope liveCycle[] = {
    FETCH_LIVE_GOALS,                                                           // get goals from live matches
    CALC_LIVE_TABLE,                                                            // calculate table changes from old and new table
    CALC_LEADER_CHANGE,                                                         // calculate leader change from goals
    CALC_RELEGATION_GHOST_CHANGE,                                               // calculate relegation ghost change from goals
    CALC_RED_LANTERN_CHANGE,                                                    // calculate red lantern change from goals
    FETCH_NEXT_MATCH_LIST,                                                      // fetch list of next matches with nearest kickoff
    FETCH_NEXT_KICKOFF                                                          // fetch next kickoff
};

// ==== enums ====
extern int ligaMaxTeams;                                                        // max. number of teams in selected league

// ==== Structures / Data Types ====
// ---------- Liga Table row (ASCII-only snapshot) ----------
struct LigaRow {
    uint8_t pos;                                                                // 1..18
    uint8_t sp;                                                                 // matches played
    uint8_t pkt;                                                                // points
    int8_t  diff;                                                               // goal difference
    uint8_t og;                                                                 // opponentGoals
    uint8_t g;                                                                  // goals
    uint8_t w;                                                                  // matches won
    uint8_t l;                                                                  // matches lost
    uint8_t d;                                                                  // matches drawn
    uint8_t flap;                                                               // number on flap display
    char    team[MAX_TEAMNAME_LENGTH];                                          // ASCII only (pretransliterated)
    char    dfb[MAX_DFB_SHORT];                                                 // ASCII (3-letter + NUL)
};

struct LigaSnapshot {
    uint16_t season;                                                            // 0 if unknown
    uint8_t  matchday;                                                          // 0 if unknown
    uint32_t nextKickoffUTC;                                                    // 0 if unknown
    uint32_t fetchedAtUTC;                                                      // epoch-ish
    uint8_t  teamCount;                                                         // <= 20
    LigaRow  rows[LIGA3_MAX_TEAMS];
    void     clear() {                                                          // clear snapshot
        season         = 0;
        matchday       = 0;
        nextKickoffUTC = 0;
        fetchedAtUTC   = 0;
        teamCount      = 0;
        for (uint8_t i = 0; i < ligaMaxTeams; i++) {
            rows[i].pos = rows[i].sp = rows[i].pkt = 0;
            rows[i].diff = rows[i].og = rows[i].g = 0;
            rows[i].w = rows[i].l = rows[i].d = 0;
            rows[i].team[0] = rows[i].dfb[0] = '\0';
        }
    }
};
// ==== Live Goal structure ====
struct LiveMatchGoalInfo {
    uint32_t goalID;                                                            // unique ID of goal in openLigaDB
    uint32_t matchID;                                                           // matchID this goal belongs to
    uint8_t  goalMinute;                                                        // minute a goal was scored
    uint8_t  scoreTeam1;                                                        // actual score of team 1
    uint8_t  scoreTeam2;                                                        // actual score of team 2

    int8_t goalsTeam1Delta;                                                     // change of goals for Team 1
    int8_t goalsTeam2Delta;                                                     // change of goals for Team 2
    int8_t pointsTeam1Delta;                                                    // change of points for Team 1
    int8_t pointsTeam2Delta;                                                    // change of points for Team 2

    String result;                                                              // result string like "1:0" after this goal
    String scoringTeam;                                                         // team that scored the goal
    String scoringPlayer;                                                       // player who scored the goal

    bool isOwnGoal;                                                             // own goal?
    bool isPenalty;                                                             // penalty?
    bool isOvertime;                                                            // overtime?
    bool liveTableActualized;                                                   // ligaLiveTable actualized
    void clear() {                                                              // clear MachInfo
        goalID     = 0;
        matchID    = 0;
        goalMinute = 0;
        scoreTeam1 = 0;
        scoreTeam2 = 0;

        goalsTeam1Delta  = 0;
        goalsTeam2Delta  = 0;
        pointsTeam1Delta = 0;
        pointsTeam2Delta = 0;

        result        = "";
        scoringTeam   = "";
        scoringPlayer = "";

        isOwnGoal           = false;
        isPenalty           = false;
        isOvertime          = false;
        liveTableActualized = false;
    }
};

// ==== Live Match structure ====
struct MatchInfo {
    uint32_t    matchID;
    time_t      kickoff;
    std::string team1;
    std::string team2;
    void        clear() {                                                       // clear MachInfo
        matchID = 0;
        kickoff = 0;
        team1   = "";
        team2   = "";
    }
};

// ==== Structures / Data Types ====
struct DfbMap {
    const char* key;                                                            // team name
    const char* code;                                                           // 3-char DFB abbriviation
    const int   flap;                                                           // falp number
};

// ==== global Variables ====
// 1. Bundesliga
static const DfbMap DFB1[] PROGMEM = {{"FC Bayern München", "FCB", 1},     {"Borussia Dortmund", "BVB", 7}, {"RB Leipzig", "RBL", 13},
                                      {"Bayer 04 Leverkusen", "B04", 2},   {"1. FSV Mainz 05", "M05", 8},   {"Borussia Mönchengladbach", "BMG", 14},
                                      {"Eintracht Frankfurt", "SGE", 3},   {"VfL Wolfsburg", "WOB", 9},     {"1. FC Union Berlin", "FCU", 15},
                                      {"SC Freiburg", "SCF", 4},           {"TSG Hoffenheim", "TSG", 10},   {"VfB Stuttgart", "VFB", 16},
                                      {"SV Werder Bremen", "SVW", 5},      {"FC Augsburg", "FCA", 11},      {"1. FC Köln", "KOE", 17},
                                      {"1. FC Heidenheim 1846", "HDH", 6}, {"Hamburger SV", "HSV", 12},     {"FC St. Pauli", "STP", 18}};

// 2. Bundesliga (ergänzbar – strikt nach Namen)
static const DfbMap DFB2[] PROGMEM = {{"Hertha BSC", "BSC", 19},           {"VfL Bochum", "BOC", 25},         {"Eintracht Braunschweig", "EBS", 30},
                                      {"SV Darmstadt 98", "SVD", 20},      {"Fortuna Düsseldorf", "F95", 26}, {"SV 07 Elversberg", "ELV", 31},
                                      {"SpVgg Greuther Fürth", "SGF", 21}, {"Hannover 96", "H96", 27},        {"1. FC Kaiserslautern", "FCK", 32},
                                      {"Karlsruher SC", "KSC", 22},        {"1. FC Magdeburg", "FCM", 28},    {"1. FC Nürnberg", "FCN", 33},
                                      {"SC Paderborn 07", "SCP", 23},      {"Holstein Kiel", "KSV", 29},      {"FC Schalke 04", "S04", 34},
                                      {"Preußen Münster", "PRM", 24},      {"Arminia Bielefeld", "DSC", 35},  {"Dynamo Dresden", "DYN", 0}};

// HTTP request and evaluation
extern std::string jsonBuffer;                                                  // buffer for deserialization in event-handlers
extern bool        jsonBufferPrepared;                                          // flag is buffer space allready prepared
extern int         realJsonBufferSize;                                          // cunked buffers size cummulated

// Poll-Manager Control
extern bool             currentMatchdayChanged;                                 // actuel state of openLigaDB matchday data
extern const PollScope* activeCycle;                                            // PollScope list for active cycle
extern size_t           activeCycleLength;                                      // number of PollSopes in activeCycle
extern PollMode         currentPollMode;                                        // current poll mode of poll mananger
extern PollScope        currentPollScope;                                       // current poll scope of poll mananger
extern PollMode         nextPollMode;                                           // poll mode for next PollCycle
extern uint32_t         pollManagerDynamicWait;                                 // wait time according to current poll mode
extern uint32_t         pollManagerStartOfWaiting;                              // time_t when entering waiting state
extern bool             isSomeThingNew;                                         // flag that openLigaDB data has changed

// openLigaDB data
extern LigaSnapshot snap[2];                                                    // current and previus table snapshot
extern uint8_t      snapshotIndex;                                              // 0 oder 1

extern int               ligaSeason;                                            // global actual Season
extern int               ligaMatchday;                                          // global actual Matchday
extern int               liveMatchID;                                           // iD of current live match
extern char              lastScanTimestamp[32];                                 // last scan timestamp from openLigaDB
extern std::string       nextKickoffString;
extern double            diffSecondsUntilKickoff;
extern time_t            currentNextKickoffTime;
extern time_t            previousNextKickoffTime;
extern bool              nextKickoffChanged;
extern bool              nextKickoffFarAway;
extern bool              matchIsLive;
extern std::string       dateTimeBuffer;                                        // temp buffer to request
extern String            currentLastChangeOfMatchday;                           // actual change date
extern String            previousLastChangeOfMatchday;                          // old change date
extern int               ligaPlanMatchCount;                                    // number of planned matches in current matchday
extern int               ligaNextMatchCount;                                    // number of next matches in current matchday
extern int               ligaLiveMatchCount;                                    // number of live matches in current matchday
extern int               ligaFiniMatchCount;                                    // number of live matches in current matchday
extern int               liveGoalCount;                                         // number of finished live matches in current matchday
extern int               lastGoalID;                                            // last goal ID to detect new goals
extern MatchInfo         planMatches[MAX_MATCHES_PER_MATCHDAY];                 // max. 10 next-Spiele
extern MatchInfo         nextMatches[MAX_MATCHES_PER_MATCHDAY];                 // max. 10 next-Spiele
extern MatchInfo         liveMatches[MAX_MATCHES_PER_MATCHDAY];                 // max. 10 Live-Spiele
extern LiveMatchGoalInfo goalsInfos[MAX_GOALS_PER_MATCHDAY];                    // max. 50 goals per matchday

// ==== global Variables ====

// global Funtions
bool        initLigaTask();
int         calcCurrentSeason();
bool        connectToWifi();
void        configureTime();
bool        waitForTime(uint32_t maxMs = 15000, bool report = false);           // Wait until system time (NTP) is valid, up to maxMs.
void        printTime(const char* label);
uint32_t    getPollDelay(PollMode mode);
void        selectPollCycle(PollMode mode);
const char* pollModeToString(PollMode mode);
const char* pollScopeToString(PollScope scope);
bool        recalcLiveTable(LigaSnapshot& baseTable, LigaSnapshot& tempTable);  // recalculate table with live goals
void        printLigaLiveTable(LigaSnapshot& LiveTable);                        // print recalculated live table

void     processPollScope(PollScope scope);
PollMode determineNextPollMode();

bool openLigaDBHealthCheck();
bool checkForMatchdayChanges();
void showNextKickoff();

String dfbCodeForTeamStrict(const String& teamName);
int    flapForTeamStrict(const String& teamName);
bool   sendRequest(const String& url, String& response);

class LigaTable {
   public:
    // Constructor for LigaTable
    LigaTable();

    // public member functions
    bool pollForChanges();
    bool pollForTable();                                                        // get Bundesligatabelle
    bool pollForCurrentMatchday();                                              // get current matchday
    bool pollForNextKickoff();                                                  // get next kickoff
    void pollForLiveMatches();                                                  // get Bundesligatabelle
    bool pollForNextMatchList(int machdayOffset);                               // get list of next matches with nearest kickoff
    bool detectLeaderChange(const LigaSnapshot& oldSnap, const LigaSnapshot& newSnap, //
                            const LigaRow** oldLeaderOut, const LigaRow** newLeaderOut);
    bool detectRelegationGhostChange(const LigaSnapshot& oldSnap, const LigaSnapshot& newSnap, //
                                     const LigaRow** oldRGOut, const LigaRow** newRGOut);
    bool detectRedLanternChange(const LigaSnapshot& oldSnap, const LigaSnapshot& newSnap, //
                                const LigaRow** oldRLOut, const LigaRow** newRLOut);
    bool detectScoringTeams(const LigaSnapshot& oldSnap, const LigaSnapshot& newSnap, //
                            const LigaRow* scorers[], uint8_t& scorerCount);

    // Liga trace
    template <typename... Args>
    void ligaPrint(const Args&... args) {                                       // standard parserPrint
        tracePrint("[FLAP  -  LIGA  ] ", args...);
    }

    template <typename... Args>
    void ligaPrintln(const Args&... args) {                                     // standard parserPrint with new line
        tracePrintln("[FLAP  -  LIGA  ] ", args...);
    }

   private:
};

#endif                                                                          // Liga_h
