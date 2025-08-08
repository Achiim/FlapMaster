// #################################################################################################################
//
//  ██████   █████  ██████  ███████ ███████ ██████
//  ██   ██ ██   ██ ██   ██ ██      ██      ██   ██
//  ██████  ███████ ██████  ███████ █████   ██████
//  ██      ██   ██ ██   ██      ██ ██      ██   ██
//  ██      ██   ██ ██   ██ ███████ ███████ ██   ██
//
// ################################################################################################## by Achim ####
// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=ANSI%20Regular&t=PARSER
//
#ifndef Parser_h
#define Parser_h

#include "TracePrint.h"
#include "RemoteControl.h"

class RemoteParser {                                                            // class to parse received remote control keys
   public:
    uint64_t      _receivedValue;                                               // raw date from remote
    ClickEvent    _receivedEvent;                                               // click-Type + key
    ClickEvent    _evt;                                                         // update after poll
    Key21         _receivedKey;                                                 // only pressed key
    Key21         _pendingKey             = Key21::NONE;                        // waiting for same click as pending key
    Key21         _lastKey                = Key21::UNKNOWN;                     // previous pressed Key21::key
    unsigned long _lastClickTime          = 0;                                  // last time key was pressed
    unsigned long _lastReceiveTime        = 0;                                  // last time doubble click was detected
    unsigned long _singleClickPendingTime = 0;                                  // time since single click was decteted
    bool          _waitingForSecondClick  = false;                              // we are still within the Threshold wile we are waiting for a double
    int           _repeatCount            = 0;                                  // counter for repeated keys

    // Constructor for RemoteParser
    RemoteParser();

    ClickEvent detect(Key21 receivedCode);                                      // parser vor remote control events
    ClickEvent poll();                                                          // regelmäßig (z. B. alle 10ms) im Loop/Task aufrufen
    void       handleQueueMessage();                                            // read from ParserQueue and filter
    void       analyseClickEvent();                                             // analyse if there is more than a single click
    void       dispatchToTwins();                                               // dispatch key stroke to twins for execution

    // Parser trace
    template <typename... Args>
    void parserPrint(const Args&... args) {                                     // standard parserPrint
        tracePrint("[FLAP - PARSER  ] ", args...);
    }

    template <typename... Args>
    void parserPrintln(const Args&... args) {                                   // standard parserPrint with new line
        tracePrintln("[FLAP - PARSER  ] ", args...);
    }

   private:
};

#endif                                                                          // Parser_h
