// ###################################################################################################
//
//    ██      ██  ██████   █████      ██   ██ ███████ ██      ██████  ███████ ██████
//    ██      ██ ██       ██   ██     ██   ██ ██      ██      ██   ██ ██      ██   ██
//    ██      ██ ██   ███ ███████     ███████ █████   ██      ██████  █████   ██████
//    ██      ██ ██    ██ ██   ██     ██   ██ ██      ██      ██      ██      ██   ██
//    ███████ ██  ██████  ██   ██     ██   ██ ███████ ███████ ██      ███████ ██   ██
//
//
// ################################################################################################## by Achim ####
// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=ANSI%20Regular&t=LIGA%20HELPER
/*

    diverse helper for liga task

*/
#ifndef LigaHelper_h
#define LigaHelper_h

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>

#define maxGoalsPerMatchday 40                                                  // for MatchState buffer s_state

// remember for each match (hightes seen goalID)
struct MatchState {
    uint8_t league;                                                             // 1=BL1, 2=BL2
    int     matchID;                                                            // id of match
    int     lastGoalID;                                                         // id of goal
};

// -----------------------------------------------------------------------------
// Match helpers
// -----------------------------------------------------------------------------

MatchState* stateFor(uint8_t league, int matchID);                              // helper for match goals

// -----------------------------------------------------------------------------
// Time / Date helpers
// -----------------------------------------------------------------------------

bool   parseIsoZ(const String& s, struct tm& out);                              // Parse an ISO-8601 UTC timestamp string into a struct tm.
time_t timegm_portable(struct tm* tm_utc);                                      // Portable replacement for timegm(), converts UTC struct tm to time_t.
time_t toUtcTimeT(const String& iso);                                           // Convert an ISO-8601 UTC string into a UTC time_t.
String nowUTC_ISO();                                                            // Get the current UTC time as ISO-8601 string.

// -----------------------------------------------------------------------------
// JSON access helpers (case-robust key lookup)
// -----------------------------------------------------------------------------

const char* strOr(JsonObjectConst o, const char* def, const char* k1, const char* k2 = nullptr, const char* k3 = nullptr,
                  const char* k4 = nullptr);                                    // Return a string value from JSON by trying multiple keys.
int         intOr(JsonObjectConst o, int def, const char* k1, const char* k2 = nullptr,
                  const char* k3 = nullptr);                                    // Return an integer value from JSON by trying multiple keys.
bool        boolOr(JsonObjectConst o, bool def, const char* k1, const char* k2 = nullptr,
                   const char* k3 = nullptr);                                   // Return a boolean value from JSON by trying multiple keys.

// -----------------------------------------------------------------------------
// Network / HTTP helpers
// -----------------------------------------------------------------------------

WiFiClientSecure makeSecureClient();                                            // Create and configure a secure WiFi client (TLS).
bool             waitForTime(uint32_t maxMs = 15000);                           // Wait until system time (NTP) is valid, up to maxMs.

#endif                                                                          // LigaHelper_h
