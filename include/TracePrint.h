#ifndef TracePrint_h
#define TracePrint_h

template <typename... Args>
void tracePrint(const char* prefix, const Args&... args) {
    Serial.print(prefix);
    (Serial.print(args), ...);
}

template <typename... Args>
void tracePrintln(const char* prefix, const Args&... args) {
    Serial.print(prefix);
    (Serial.print(args), ...);
    Serial.println();
}

template <typename... Args>
void tracePrintf(const char* prefix, const char* fmt, const Args&... args) {
    char buf[128];
    snprintf(buf, sizeof(buf), fmt, args...);
    Serial.print(prefix);
    Serial.print(buf);
}
template <typename... Args>
void tracePrint(const char* prefix, const char* fmt, const Args&... args) {
    char buf[128];
    snprintf(buf, sizeof(buf), fmt, args...);
    Serial.print(prefix);
    Serial.print(buf);
}

template <typename... Args>
void tracePrintln(const char* prefix, const char* fmt, const Args&... args) {
    char buf[128];
    snprintf(buf, sizeof(buf), fmt, args...);
    Serial.print(prefix);
    Serial.println(buf);
}

#endif                                                                          // TracePrint_h
