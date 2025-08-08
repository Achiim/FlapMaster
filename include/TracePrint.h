#ifndef TracePrint_h
#define TracePrint_h

/*
Usage examples:

        int counter       = 42;
        float temperature = 23.5f;
        const char* msg   = "Hello, World!";
        bool flag         = true;
        char c            = 'X';
        long bigValue     = 123456789L;

        // 1) Einfaches Anhängen mehrerer Werte
        tracePrint("DBG: ", "counter=", counter, ", flag=", flag);

        // 2) Mit endl
        tracePrintln("INF: ", "Startup complete");

        // 3) Floating-Point
        tracePrint("TMP: ", temperature, "°C");

        // 4) Einzelner String
        tracePrintln("MSG: ", msg);

        // 5) Char
        tracePrint("CHR: ", "Char is ", c);

        // 6) Long-Wert
        tracePrintln("VAL: ", bigValue);

        // 7) Mehrere Argumente
        tracePrint("MIX: ", "x=", counter, ", y=", bigValue, ", ok=", flag);

        // 8) Formatierte Ausgabe (printf-like)
        tracePrintf("FMT: ", "counter=%d, temp=%.1f°C", counter, temperature);

        // 9) Prefix nur
        tracePrintln("NOTE: ");

        // 10) Nur formatierte Ausgabe (mit Prefix)
        tracePrint("FMT2: ", "msg='%s'", msg);

        // 11) Kombi: variadisch und endl
        tracePrintln("DBG2: ", "c=", c, ", flag=", flag, ", tmp=", temperature);

        // 12) Nur Prefix und endl
        tracePrintln("EMPTY: ");

        // 13) Negativer Wert
        tracePrint("NEG: ", "counter=", -counter);

        // 14) Hexadezimal mit printf
        tracePrintf("HEX: ", "0x%X", counter);

        // 15) Zweifach formatierte Werte
        tracePrintf("FP: ", "temp1=%.2f, temp2=%.3f", temperature, temperature/2);

        // 16) Boolean als Text
        tracePrintln("BOOL: ", (flag ? "true" : "false"));

        // 17) Kombination mit Literal-Strings
        tracePrint("LIT: ", "Status: ", msg, " [OK]");

        // 18) Großes Mix
        tracePrintln("ALL: ",
            "c=", c, ", cnt=", counter, ", big=", bigValue,
            ", tmp=", temperature, ", msg=", msg);

        // 19) printf mit mehreren Formaten
        tracePrintf("MULTI: ", "i=%d, l=%ld, f=%.1f, c=%c", counter, bigValue, temperature, c);

        // 20) Laufende Nummern
        for (int i = 0; i < 3; ++i) {
            tracePrintf("LOOP: ", "i=%d\n", i);
        }

*/

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
