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
#include "esp_sntp.h"

#define maxGoalsPerMatchday 30                                                  // for MatchState buffer s_state

// remember for each match (highest seen goalID + last scores)
struct MatchState {
    uint8_t league;                                                             // 1=BL1, 2=BL2
    int     matchID;                                                            // id of match
    int     lastGoalID;                                                         // id of last processed goal
    int     lastScore1;                                                         // last known score of team 1
    int     lastScore2;                                                         // last known score of team 2
};

// -----------------------------------------------------------------------------
// --- Match date helper ---
// -----------------------------------------------------------------------------
MatchState* stateFor(uint8_t league, int matchID);                              // helper for match goals
String      matchUtc(const JsonObject& m);                                      // Extract normalized UTC datetime

// -----------------------------------------------------------------------------
// Time / Date helpers
// -----------------------------------------------------------------------------

bool   parseIsoZ(const String& s, struct tm& out);                              // Parse an ISO-8601 UTC timestamp string into a struct tm.
time_t timegm_portable(struct tm* tm_utc);                                      // Portable replacement for timegm(), converts UTC struct tm to time_t.
time_t toUtcTimeT(const String& iso);                                           // Convert an ISO-8601 UTC string into a UTC time_t.
String nowUTC_ISO();                                                            // Get the current UTC time as ISO-8601 string.
bool   pickNextFromArray(JsonArray arr, const String& nowIso, NextMatch& out);
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
bool             waitForTime(uint32_t maxMs = 15000, bool report = false);      // Wait until system time (NTP) is valid, up to maxMs.

// -----------------------------------------------------------------------------
// --- Team mapping helpers ---
// -----------------------------------------------------------------------------
String dfbCodeForTeamStrict(const String& teamName);                            // Lookup strict DFB code for team
int    flapForTeamStrict(const String& teamName);                               // Lookup strict flap index for team

// -----------------------------------------------------------------------------
// --- String helpers ---
// -----------------------------------------------------------------------------
static inline void copy_str_bounded(char* dst, size_t dst_sz, const char* src) { // NUL-terminated copie
    if (!dst || dst_sz == 0)
        return;
    if (!src) {
        dst[0] = '\0';                                                          // NUL termination
        return;
    }
    size_t i = 0;
    for (; i + 1 < dst_sz && src[i] != '\0'; ++i)
        dst[i] = src[i];
    dst[i] = '\0';
}

// -----------------------------------------------------------------------------
// --- time helpers ---
// -----------------------------------------------------------------------------
static inline void printTime(const char* label) {
    time_t now = time(nullptr);
    if (now < 100000) {                                                         // kleiner Wert => keine gültige Zeit
        Liga->ligaPrintln("[%s] time not set (epoch=%ld)", label, (long)now);
    } else {
        char* ts              = ctime(&now);
        ts[strcspn(ts, "\n")] = '\0';                                           // remove newline from time string
        Liga->ligaPrintln("[%s] %s", label, ts);
    }
}

// -----------------------------------------------------------------------------
// --- API throttle helpers ---
// -----------------------------------------------------------------------------
static inline void ligaApiThrottle() {
    static uint32_t lastRequestMs = 0;
    const uint32_t  minSpacing    = HTTP_THROTTLE;                              // general break between requests

    uint32_t now = millis();
    if (lastRequestMs != 0 && (now - lastRequestMs) < minSpacing) {
        vTaskDelay(pdMS_TO_TICKS(minSpacing - (now - lastRequestMs)));
    }
    lastRequestMs = millis();
}

#endif                                                                          // LigaHelper_h
