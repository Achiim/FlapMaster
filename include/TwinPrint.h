// #####################################################################################################
//
//  ████████ ██     ██ ██ ███    ██     ██████  ██████  ██ ███    ██ ████████
//     ██    ██     ██ ██ ████   ██     ██   ██ ██   ██ ██ ████   ██    ██
//     ██    ██  █  ██ ██ ██ ██  ██     ██████  ██████  ██ ██ ██  ██    ██
//     ██    ██ ███ ██ ██ ██  ██ ██     ██      ██   ██ ██ ██  ██ ██    ██
//     ██     ███ ███  ██ ██   ████     ██      ██   ██ ██ ██   ████    ██
//
////
//   //// ################################################################################ by Achim ####
// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=ANSI%20Regular&t=Twin%20Print
//

#ifndef TwinPrint_h
#define TwinPrint_h

#include <sstream>

/*
// ----------------------------
template <typename T>
void appendToStream(std::ostringstream& stream, T value) {
    stream << value;
}

// ----------------------------
template <typename T, typename... Args>
void appendToStream(std::ostringstream& stream, T value, Args... args) {
    stream << value;
    appendToStream(stream, args...);
}
*/
// ----------------------------
template <typename... Args>
void twinPrint(Args... args) {                                                  // first trace print expects follow up lines by Serial.print(ln)
    std::ostringstream stream;
    appendToStream(stream, args...);
    Serial.print("[I²C TWIN 0x");
    Serial.print(slaveAddress, HEX);
    Serial.print("  ] ");
    Serial.print(stream.str().c_str());
}

// ----------------------------
template <typename... Args>
void twinPrintln(Args... args) {                                                // standalone one line trace message for Twin 0x
    std::ostringstream stream;
    appendToStream(stream, args...);
    Serial.print("[I²C TWIN 0x");
    Serial.print(slaveAddress, HEX);
    Serial.print("  ] ");
    Serial.println(stream.str().c_str());
}

template <typename... Args>
void twinPrint(Args... args) {                                                  // first trace print expects follow up lines by Serial.print(ln)
    char buf[128];
    int  n = snprintf(buf, sizeof(buf), "[I²C TWIN 0x%X] %s %d %f", slaveAddress, str1, val, fval);
    if (n > 0)
        Serial.println(buf);
}
#endif                                                                          // TrinPrint_h
