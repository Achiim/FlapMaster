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

enum PollScope { CHECK_FOR_CHANGES, FETCH_TABLE, FETCH_NEXT_MATCH, FETCH_CURRENT_MATCHDAY, FETCH_NEXT_KICKOFF, FETCH_LIVE_MATCHES, FETCH_GOALS };

// global Variables
extern std::string currentLastChange;                                           // actuel change date
extern std::string previousLastChange;                                          // old change date
extern std::string dateTimeBuffer;                                              // openLigaDB does not send JSON just string

extern String      lastMatchChecksum;
extern std::string jsonBuffer;

extern String currentChecksum;
extern String currentGroupName;

extern int ligaSeason;                                                          // global actual Season
extern int ligaMatchday;                                                        // global actual Matchday

// global Funtions
bool initLigaTask();
int  getCurrentSeason();
bool connectToWifi();
bool waitForTime(uint32_t maxMs = 15000, bool report = false);                  // Wait until system time (NTP) is valid, up to maxMs.
void printTime(const char* label);

bool openLigaDBHealthCheck();
void pollNextMatch();

class LigaTable {
   public:
    // Constructor for LigaTable
    LigaTable();

    // public member functions
    bool pollForChanges(bool& outchanged);
    bool pollTable();                                                           // get Bundesligatabelle
    bool pollCurrentMatchday();

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
