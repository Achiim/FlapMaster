// #################################################################################################################
//  ███    ███  █████  ███████ ████████ ███████ ██████      ██████  ██████  ██ ███    ██ ████████
//  ████  ████ ██   ██ ██         ██    ██      ██   ██     ██   ██ ██   ██ ██ ████   ██    ██
//  ██ ████ ██ ███████ ███████    ██    █████   ██████      ██████  ██████  ██ ██ ██  ██    ██
//  ██  ██  ██ ██   ██      ██    ██    ██      ██   ██     ██      ██   ██ ██ ██  ██ ██    ██
//  ██      ██ ██   ██ ███████    ██    ███████ ██   ██     ██      ██   ██ ██ ██   ████    ██
//
//
// ################################################################################################## by Achim ####
// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=ANSI%20Regular&t=Master%20PRINT
//

#ifndef MasterPrint_h
#define MasterPrint_h

#include <Arduino.h>

/*
    Examples of usage:

    masterPrint("");                                                            // → [I²C FLAP MASTER]
    masterPrint("Hello");                                                       // → [I²C FLAP MASTER] Hello
    masterPrint("Wert=",  42);                                                  // → [I²C FLAP MASTER] Wert=42
    masterPrint("A=", a, " B=", b, " C=", c);                                   // → [I²C FLAP MASTER] A=123 B=456 C=789
    masterPrint("%s %d %f", str1, val, fval);                                   // → [I²C FLAP MASTER] Achim 10 3.14

*/

/// masterPrint mit beliebig vielen Argumenten: Prefix + args
template <typename... Args>
void masterPrint(const Args&... args) {
    Serial.print("[I²C FLAP MASTER] ");                                         // 1) print Prefix every time
    (Serial.print(args), ...);                                                  // 2) print all arguments
}

/// masterPrintln mit beliebig vielen Argumenten: Prefix + args + Zeilenumbruch
template <typename... Args>
void masterPrintln(const Args&... args) {
    Serial.print("[I²C FLAP MASTER] ");                                         // 1) print Prefix every time
    (Serial.print(args), ...);                                                  // 2) print all arguments
    Serial.println();                                                           // 3) abschließenden Newline
}

// ----------------------------
template <typename... Args>
void masterPrint(const char* fmt, const Args&... args) {
    Serial.print("[I²C FLAP MASTER] ");                                         // 1) print Prefix every time
    char buf[128];                                                              // 2) format agruments (fmt=="" → buf=="" → n==0)
    snprintf(buf, sizeof(buf), fmt, args...);
    Serial.print(buf);                                                          // 3) print every time
}

// ----------------------------
template <typename... Args>
void masterPrintln(const char* fmt, const Args&... args) {
    Serial.print("[I²C FLAP MASTER] ");                                         // 1) print Prefix every time
    char buf[128];                                                              // 2) format agruments (fmt=="" → buf=="" → n==0)
    snprintf(buf, sizeof(buf), fmt, args...);
    Serial.println(buf);                                                        // 3) print every time with new line
}

#endif                                                                          // MasterPrint_h
