#include <Arduino.h>
#include <time.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>                                                        // v7 (JsonDocument)
#include "Liga.h"
#include "secret.h"                                                             // const char* ssid, const char* password
#include "cert.all"                                                             // certificate for https with OpenLigaDB

// ===== DFB/DFL 3-Letter Mapping (strict; kein Fallback/keine Heuristik) =====
static String normKey(String s) {
    s.trim();
    s.replace("\xC2\xA0", " ");                                                 // NBSP -> Space
    s.replace("ä", "ae");
    s.replace("ö", "oe");
    s.replace("ü", "ue");
    s.replace("Ä", "AE");
    s.replace("Ö", "OE");
    s.replace("Ü", "UE");
    s.replace("ß", "ss");
    s.replace("-", " ");
    s.replace(".", " ");
    while (s.indexOf("  ") >= 0)
        s.replace("  ", " ");
    s.toUpperCase();
    return s;
}

struct DfbMap {
    const char* key;
    const char* code;
};

// 1. Bundesliga
static const DfbMap DFB1[] = {
    {"FC BAYERN MUENCHEN", "FCB"},   {"BORUSSIA DORTMUND", "BVB"}, {"RB LEIPZIG", "RBL"},
    {"BAYER 04 LEVERKUSEN", "B04"},  {"1 FSV MAINZ 05", "M05"},    {"BORUSSIA MOENCHENGLADBACH", "BMG"},
    {"EINTRACHT FRANKFURT", "SGE"},  {"VFL WOLFSBURG", "WOB"},     {"1 FC UNION BERLIN", "FCU"},
    {"SC FREIBURG", "SCF"},          {"TSG HOFFENHEIM", "TSG"},    {"VFB STUTTGART", "VFB"},
    {"SV WERDER BREMEN", "SVW"},     {"FC AUGSBURG", "FCA"},       {"1 FC KOELN", "KOE"},
    {"1 FC HEIDENHEIM 1846", "HDH"}, {"HAMBURGER SV", "HSV"},      {"FC ST PAULI", "STP"},
};

// 2. Bundesliga (ergänzbar – strikt nach Namen)
static const DfbMap DFB2[] = {
    {"HERTHA BSC", "BSC"},          {"VFL BOCHUM 1848", "BOC"}, {"EINTRACHT BRAUNSCHWEIG", "EBS"}, {"SV DARMSTADT 98", "SVD"},
    {"FORTUNA DUESSELDORF", "F95"}, {"SV ELVERSBERG", "ELV"},   {"SPVGG GREUTHER FUERTH", "SGF"},  {"HANNOVER 96", "H96"},
    {"1 FC KAISERSLAUTERN", "FCK"}, {"KARLSRUHER SC", "KSC"},   {"1 FC MAGDEBURG", "FCM"},         {"1 FC NUERNBERG", "FCN"},
    {"SC PADERBORN 07", "SCP"},     {"HOLSTEIN KIEL", "KSV"},   {"FC SCHALKE 04", "S04"},          {"SC PREUSSEN MUENSTER", "PRM"},
    // ggf. weitere Vereine später ergänzen (Rostock, Osnabrück, Wiesbaden, Sandhausen, Ulm …)
};

static String dfbCodeForTeamStrict(const String& teamName) {
    String key = normKey(teamName);
    for (auto& e : DFB1)
        if (key == e.key)
            return e.code;
    for (auto& e : DFB2)
        if (key == e.key)
            return e.code;
    return "";                                                                  // strikt: kein Treffer => leeres Kürzel
}

// ===== UTF-8-Ausrichtung (Umlaute korrekt, exakt width Spalten) =====
static void printUtf8Cell(const char* s, int width) {
    const uint8_t* p   = (const uint8_t*)s;
    const uint8_t* end = p;
    int            cps = 0;
    while (*p && cps < width) {
        uint8_t c = *p;
        if ((c & 0x80) == 0x00)
            p += 1;
        else if ((c & 0xE0) == 0xC0)
            p += 2;
        else if ((c & 0xF0) == 0xE0)
            p += 3;
        else if ((c & 0xF8) == 0xF0)
            p += 4;
        else
            p += 1;
        cps++;
        end = p;                                                                // end nach dem Vorrücken setzen
    }
    Serial.write((const char*)s, (size_t)(end - (const uint8_t*)s));
    for (int i = cps; i < width; ++i)
        Serial.write(' ');
}

// Kompakte Spaltenbreiten
constexpr int W_POS   = 3;                                                      // 1..18
constexpr int W_TEAM  = 24;                                                     // z. B. „Borussia Mönchengladbach“
constexpr int W_SHORT = 10;                                                     // „Union Berlin“ ggf. gekürzt
constexpr int W_DFB   = 3;                                                      // 3-Letter
constexpr int W_SP    = 2;                                                      // Spiele (1..34)
constexpr int W_DIFF  = 4;                                                      // -99..+99
constexpr int W_PKT   = 3;                                                      // 0..99

static void printBorder() {
    auto dash = [](int w) {
        for (int i = 0; i < w + 2; i++)
            Serial.write('-');
        Serial.write('+');
    };
    Serial.write('+');
    dash(W_POS);
    dash(W_TEAM);
    dash(W_SHORT);
    dash(W_DFB);
    dash(W_SP);
    dash(W_DIFF);
    dash(W_PKT);
    Serial.write('\r');
    Serial.write('\n');
}

static void printHeader() {
    printBorder();
    Serial.print("| ");
    printUtf8Cell("Pos", W_POS);
    Serial.print(" | ");
    printUtf8Cell("teamName", W_TEAM);
    Serial.print(" | ");
    printUtf8Cell("shortName", W_SHORT);
    Serial.print(" | ");
    printUtf8Cell("DFB", W_DFB);
    Serial.print(" | ");
    printUtf8Cell("Sp", W_SP);
    Serial.print(" | ");
    printUtf8Cell("Diff", W_DIFF);
    Serial.print(" | ");
    printUtf8Cell("Pkt", W_PKT);
    Serial.println(" |");
    printBorder();
}

// ===== LigaTable: Implementierung =====
LigaTable::LigaTable() {
    // Berlin: CET/CEST
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov"); // synch time
}

bool LigaTable::connect() {
    if (WiFi.status() == WL_CONNECTED)
        return true;
    WiFi.mode(WIFI_STA);
    ligaPrint("Verbinde mit WLAN");
    WiFi.begin(ssid, password);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(200);
        Serial.print('.');
        if (millis() - t0 > 15000) {
            ligaPrintln("\r\nWLAN-Timeout (15s)");
            return false;
        }
    }
    return true;
}

bool LigaTable::fetch() {
    if (WiFi.status() != WL_CONNECTED)
        if (!connect())
            return false;

    WiFiClientSecure client;
    client.setCACert(OPENLIGA_CA);                                              // use certificate
    HTTPClient http;
    http.setReuse(false);
    http.setConnectTimeout(8000);
    http.setTimeout(8000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.useHTTP10(true);                                                       // vermeidet chunked
    http.setUserAgent("ESP32-Flap/1.0");
    http.addHeader("Accept", "application/json");
    http.addHeader("Accept-Encoding", "identity");
    const char* hdrs[] = {"Content-Type", "Content-Encoding", "Transfer-Encoding"};
    http.collectHeaders(hdrs, 3);

    if (!http.begin(client, apiURL)) {
        ligaPrintln("http.begin() fehlgeschlagen");
        return false;
    }
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        ligaPrintln("Fehler beim Abrufen: %d\r\n", code);
        http.end();
        return false;
    }

    // komplett einlesen (funktioniert auch bei chunked)
    int    len = http.getSize();
    String payload;
    if (len > 0) {
        payload.reserve(len);
        WiFiClient* s = http.getStreamPtr();
        while (len > 0) {
            uint8_t buf[256];
            int     n = s->read(buf, (len > 256) ? 256 : len);
            if (n <= 0) {
                delay(1);
                continue;
            }
            payload.concat(String((const char*)buf).substring(0, n));           // simpel, kostet etwas CPU/RAM
            len -= n;
        }
    } else {
        payload = http.getString();                                             // Fallback (chunked)
    }

    if (payload.isEmpty()) {
        const size_t MAX_BYTES = 65536;
        Stream&      s         = http.getStream();
        uint32_t     t0        = millis();
        while ((millis() - t0) < 10000) {
            while (s.available()) {
                payload += (char)s.read();
                if (payload.length() >= MAX_BYTES)
                    break;
            }
            const size_t MAX_BYTES = 65536;
            Stream&      s         = http.getStream();
            uint32_t     t0 = millis(), last = t0;
            size_t       lastSize = payload.length();
            while (millis() - t0 < 10000) {                                     // 10 s Gesamt-Timeout
                while (s.available()) {
                    char c = (char)s.read();
                    payload += c;
                    if (payload.length() >= MAX_BYTES)
                        break;
                    last = millis();
                }
                // Kein weiterer Fortschritt seit 250 ms? -> fertig.
                if (millis() - last > 250)
                    break;
                delay(1);
                if (payload.length() >= MAX_BYTES)
                    break;
            }
            delay(1);
            if (payload.length() >= MAX_BYTES)
                break;
        }
    }

    // BOM + Whitespace
    if (payload.length() >= 3 && (uint8_t)payload[0] == 0xEF && (uint8_t)payload[1] == 0xBB && (uint8_t)payload[2] == 0xBF)
        payload.remove(0, 3);
    payload.trim();

    if (!payload.startsWith("[") && !payload.startsWith("{")) {
        ligaPrintln("Antwort ist kein JSON - bitte apiURL prüfen");
        http.end();
        return false;
    }

    JsonDocument         doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        ligaPrintln(String("Fehler beim JSON-Parsen: ") + err.c_str());
        http.end();
        return false;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull()) {
        ligaPrintln("Unerwartetes JSON-Format – Array erwartet");
        http.end();
        return false;
    }

    // Ausgabe
    Serial.println(F("\r\nBundesliga-Tabelle (OpenLigaDB):"));
    printHeader();

    uint16_t rank = 1;
    for (JsonObject team : arr) {
        const char* teamName  = team["teamName"] | team["TeamName"] | "";
        const char* shortName = team["shortName"] | team["ShortName"] | "";

        int won  = team["won"] | 0;
        int draw = team["draw"] | 0;
        int lost = team["lost"] | 0;
        int gf   = team["goals"] | team["Goals"] | 0;
        int ga   = team["opponentGoals"] | team["OpponentGoals"] | 0;

        int matches  = team["matches"] | (won + draw + lost);
        int goalDiff = team["goalDiff"] | team["GoalDiff"] | (gf - ga);
        int points   = team["points"] | team["Points"] | team["pts"] | 0;

        String dfb = dfbCodeForTeamStrict(teamName);                            // strikt: ggf. ""

        Serial.print("| ");
        Serial.printf("%*u", W_POS, rank);
        Serial.print(" | ");
        printUtf8Cell(teamName, W_TEAM);
        Serial.print(" | ");
        printUtf8Cell(shortName, W_SHORT);
        Serial.print(" | ");
        printUtf8Cell(dfb.c_str(), W_DFB);
        Serial.print(" | ");
        Serial.printf("%*d", W_SP, matches);
        Serial.print(" | ");
        Serial.printf("%*d", W_DIFF, goalDiff);
        Serial.print(" | ");
        Serial.printf("%*d", W_PKT, points);
        Serial.println(" |");

        rank++;
    }
    printBorder();

    http.end();
    client.stop();                                                              // explizit schließen
    return true;
}
