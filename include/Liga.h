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

#define POLL_GET_ALL_CHANGES 10 * 1000                                          // 10 seconds
#define POLL_DURING_GAME 30 * 1000                                              // 30 seconds
#define POLL_10MIN_BEFORE_KICKOFF 2 * 60 * 1000                                 // 2 minutes
#define POLL_NORMAL 3 * 60 * 1000                                               // 30 minutes

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
const PollScope relaxedCycle[]  = {FETCH_CURRENT_SEASON, FETCH_CURRENT_MATCHDAY, CHECK_FOR_CHANGES};
const PollScope reactiveCycle[] = {FETCH_TABLE, FETCH_NEXT_KICKOFF, CHECK_FOR_CHANGES}; // don't ask for nextKickoff during live games
const PollScope preLiveCycle[]  = {SHOW_NEXT_KICKOFF, CHECK_FOR_CHANGES};       // don't ask for nextKickoff during live games
const PollScope liveCycle[]     = {FETCH_TABLE, SHOW_NEXT_KICKOFF, CALC_LEADER_CHANGE, CALC_RELEGATION_GHOST_CHANGE, FETCH_GOALS, CHECK_FOR_CHANGES};

// global Variables
extern std::string currentLastChange;                                           // actuel change date
extern std::string previousLastChange;                                          // old change date
extern std::string dateTimeBuffer;                                              // openLigaDB does not send JSON just string

extern String      lastMatchChecksum;
extern std::string jsonBuffer;

extern String currentChecksum;

extern int              ligaSeason;                                             // global actual Season
extern int              ligaMatchday;                                           // global actual Matchday
extern const PollScope* activeCycle;
extern size_t           cycleLength;

extern PollMode mode;                                                           // global poll mode of poll mananger
extern PollMode nextmode;

extern time_t currentNextKickoffTime;
extern time_t previousNextKickoffTime;
extern bool   nextKickoffChanged;
extern bool   nextKickoffFarAway;

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
void buildPollCycle(PollMode mode, std::vector<PollScope>& outCycle);
void extendPollCycleOnChange(std::vector<PollScope>& cycle);

class LigaTable {
   public:
    // Constructor for LigaTable
    LigaTable();

    // public member functions
    bool pollForChanges(bool& outchanged);
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
