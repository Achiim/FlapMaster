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
#define LIGA_MAX_TEAMS 18

#define POLL_GET_ALL_CHANGES (10 * 1000)                                        // 10 seconds
#define POLL_DURING_GAME (30 * 1000)                                            // 30 seconds
#define POLL_10MIN_BEFORE_KICKOFF (2 * 60 * 1000)                               // 2 minutes
#define POLL_NORMAL (5 * 60 * 1000)                                             // 30 minutes
// ==== defines ====

// ==== enums ====
// --- Chooseable league -------------------------------------------------------
enum class League : uint8_t { BL1 = 1, BL2 = 2 };
extern League activeLeague;

/** @brief Return OpenLigaDB league shortcut string for the given enum. */
static inline const char* leagueShortcut(League lg) {
    return (lg == League::BL2) ? "bl2" : "bl1";
}

enum PollScope {
    CHECK_FOR_CHANGES,                                                          // only check for changes on openLigaDB
    FETCH_TABLE,                                                                // fetch actualized table
    FETCH_CURRENT_MATCHDAY,                                                     // fetch actual matchday
    FETCH_CURRENT_SEASON,                                                       // fetch actuel season
    FETCH_NEXT_KICKOFF,                                                         // fetch next kickoff (don't fetch during game is live)
    FETCH_LIVE_MATCHES,                                                         //
    FETCH_GOALS,                                                                //
    FETCH_NEXT_MATCH,                                                           //
    SHOW_NEXT_KICKOFF,                                                          // show next kickoff from stored data
    CALC_LEADER_CHANGE,                                                         // calculate leader change from table
    CALC_RELEGATION_GHOST_CHANGE,                                               // calculate relegation ghost change from table
    CALC_RED_LANTERN_CHANGE,                                                    // calculate red lantern change from table
    CALC_GOALS                                                                  // calculate goals from table
};

// global Poll Modes
enum PollMode {
    POLL_MODE_RELAXED,                                                          // relaxed poll wide outside of live activity
    POLL_MODE_REACTIVE,                                                         // something was changed in openLigaDB data -> get it
    POLL_MODE_PRELIVE,                                                          // shortly befor next live game
    POLL_MODE_LIVE                                                              // during live games until it's over (2h past kickoff)
};

// global Poll Scopes for actual PollCycle for Poll Manager
const PollScope relaxedCycle[]  = {FETCH_CURRENT_SEASON, FETCH_CURRENT_MATCHDAY, FETCH_NEXT_KICKOFF, CHECK_FOR_CHANGES};
const PollScope reactiveCycle[] = {FETCH_TABLE, SHOW_NEXT_KICKOFF, CHECK_FOR_CHANGES}; // don't ask for nextKickoff during live games
const PollScope preLiveCycle[]  = {SHOW_NEXT_KICKOFF, CHECK_FOR_CHANGES};       // don't ask for nextKickoff during live games
const PollScope liveCycle[]     = {FETCH_TABLE, SHOW_NEXT_KICKOFF, CALC_LEADER_CHANGE, CALC_RELEGATION_GHOST_CHANGE, FETCH_GOALS, CHECK_FOR_CHANGES};
// ==== enums ====

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
    char    team[32];                                                           // ASCII only (pretransliterated)
    char    dfb[4];                                                             // ASCII (3-letter + NUL)
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
            rows[i].diff = rows[i].og = rows[i].g = 0;
            rows[i].w = rows[i].l = rows[i].d = 0;
            rows[i].team[0] = rows[i].dfb[0] = '\0';
        }
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

// HTTP request and evaluation
extern std::string jsonBuffer;                                                  // buffer for deserialization in event-handlers

// Poll-Manager Control
extern bool             currentMatchdayChanged;                                 // actuel state of openLigaDB matchday data
extern const PollScope* activeCycle;                                            // PollScope list for active cycle
extern size_t           activeCycleLength;                                      // number of PollSopes in activeCycle
extern PollMode         currentPollMode;                                        // current poll mode of poll mananger
extern PollMode         nextPollMode;                                           // poll mode for next PollCycle

// openLigaDB data
extern LigaSnapshot snap[2];                                                    // current and previus table snapshot
extern uint8_t      snapshotIndex;                                              // 0 oder 1

extern int         ligaSeason;                                                  // global actual Season
extern int         ligaMatchday;                                                // global actual Matchday
extern std::string nextKickoffString;
extern double      diffSecondsUntilKickoff;
extern time_t      currentNextKickoffTime;
extern time_t      previousNextKickoffTime;
extern bool        nextKickoffChanged;
extern bool        nextKickoffFarAway;
extern std::string dateTimeBuffer;                                              // temp buffer to request
extern String      currentLastChangeOfMatchday;                                 // actual change date
extern String      previousLastChangeOfMatchday;                                // old change date
// ==== global Variables ====

// global Funtions
bool        initLigaTask();
int         getCurrentSeason();
bool        connectToWifi();
bool        waitForTime(uint32_t maxMs = 15000, bool report = false);           // Wait until system time (NTP) is valid, up to maxMs.
void        printTime(const char* label);
uint32_t    getPollDelay(PollMode mode);
void        selectPollCycle(PollMode mode);
const char* pollModeToString(PollMode mode);

bool openLigaDBHealthCheck();
bool checkForMatchdayChanges();
void showNextKickoff();

class LigaTable {
   public:
    // Constructor for LigaTable
    LigaTable();

    // public member functions
    bool pollForChanges();
    bool pollTable();                                                           // get Bundesligatabelle
    bool pollCurrentMatchday();
    bool pollNextKickoff();

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
