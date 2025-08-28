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

class LigaTable {
   public:
    bool     connect();                                                         // connect to external data provider for liga data
    bool     fetchTable();                                                      // get data from external provider
    bool     disconnect();                                                      // disconnect from external data provider for liga data
    void     getNextMatch();
    void     openLigaDBHealth();
    void     getGoal();
    void     tableChanged();
    bool     pollLastChange();
    bool     getSeasonAndGroup(int& season, int& group);
    uint32_t decidePollMs();

    // Constructor for LigaTable
    LigaTable();

   private:
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
