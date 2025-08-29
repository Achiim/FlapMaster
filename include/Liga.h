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
#include <atomic>
#include <stdint.h>
#include <string.h>
#include "TracePrint.h"

// --- Configuration -----------------------------------------------------------
#define LIGA_MAX_TEAMS 18

// ---------- DATA (ASCII-only snapshot) ----------
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
    LigaRow  rows[18];
    void     clear() {
        season         = 0;
        matchday       = 0;
        nextKickoffUTC = 0;
        fetchedAtUTC   = 0;
        teamCount      = 0;
        for (uint8_t i = 0; i < 18; i++) {
            rows[i].pos = rows[i].sp = rows[i].pkt = 0;
            rows[i].diff                           = 0;
            rows[i].team[0] = rows[i].shortName[0] = rows[i].dfb[0] = '\0';
        }
    }
};

class LigaTable {
   public:
    bool     connect();                                                         // connect to external data provider for liga data
    bool     fetchTable(LigaSnapshot& out);                                     // get data from external provider
    bool     disconnect();                                                      // disconnect from external data provider for liga data
    void     getNextMatch();                                                    // get next match date, time and opponents
    void     openLigaDBHealth();                                                // health check
    void     getGoal();                                                         // get goal events
    bool     pollLastChange(int* seasonOut = nullptr, int* matchdayOut = nullptr); // get last change date/time of liga
    bool     getSeasonAndGroup(int& season, int& group);                        // get saison and Spieltag
    bool     hasSnapshot() const;                                               // snapshot of ligaTable is ready
    void     get(LigaSnapshot& out) const;                                      // get snapshot from openLigaDB
    void     commit(const LigaSnapshot& s);                                     // release snahpshot to be accessed by reporting
    uint32_t decidePollMs();                                                    // get time to wait until poll openLigaDB again

    // Constructor for LigaTable
    LigaTable();

   private:
    LigaSnapshot         buf_[2];                                               // two buffer to bw switched
    std::atomic<uint8_t> active_;                                               // signalize writing  to snapshot
    int                  _currentSeason   = 0;
    int                  _currentMatchDay = 0;                                  // Matchday

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
