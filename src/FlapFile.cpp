// #################################################################################################################
//
//    ███████ ██       █████  ██████      ███████ ██ ██      ███████
//    ██      ██      ██   ██ ██   ██     ██      ██ ██      ██
//    █████   ██      ███████ ██████      █████   ██ ██      █████
//    ██      ██      ██   ██ ██          ██      ██ ██      ██
//    ██      ███████ ██   ██ ██          ██      ██ ███████ ███████
//
//
// ################################################################################################## by Achim ####
// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=ANSI%20Regular&t=flap%20FILE
//
/*

    File System for Flap Display

    Features:

*/

#include <FlapGlobal.h>
#include "FlapFile.h"
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <FS.h>
#include "esp_partition.h"

// -------------------
// Constructor
FlapFile::FlapFile() {
    if (!SPIFFS.begin(true)) {
        #ifdef FILEVERBOSE
            {
            TraceScope trace;
            filePrintln("SPIFFS could not be started");
            }
        #endif
        return;
    }

    size_t total = SPIFFS.totalBytes();
    size_t used  = SPIFFS.usedBytes();
    #ifdef FILEVERBOSE
        {
        TraceScope trace;
        filePrintln("Flap File System (SPIFFS) successfully started");
        filePrintln("Flap File System size: %s total, %s used, %s free", formatSize(total).c_str(), formatSize(used).c_str(),
        formatSize(total - used).c_str());
        }
    #endif

    listPartitions();                                                           // show all file system partitions
};

/**
 * @brief Save a JSON file with prepended metadata.
 *
 * This function takes an existing JsonDocument (`dataDoc`), wraps it into a new
 * document (`finalDoc`), and prepends a "_meta" object with metadata fields
 * such as file name, version, timestamp, description and author.
 * The combined JSON is then written to SPIFFS.
 *
 * @param filename Path to the JSON file in SPIFFS (e.g. "/TaskStatus.json")
 * @param dataDoc  The JsonDocument containing the actual data payload
 * @return true    If the file was saved successfully
 * @return false   If opening or writing the file failed
 */
bool FlapFile::saveFile(const char* filename, JsonDocument& dataDoc) {
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    StaticJsonDocument<2048> finalDoc;                                          ///< Final document with metadata + data

    // ------------------------------------------------------------
    // 1. Create metadata object first so it will be placed at top
    // ------------------------------------------------------------
    JsonObject meta = finalDoc.createNestedObject("_meta");                     ///< JSON header object
    #pragma GCC diagnostic pop
    meta["file"]        = filename;                                             ///< Name of the file in SPIFFS
    meta["version"]     = "1.0";                                                ///< Schema or file version
    meta["created"]     = isoTimestamp();                                       ///< Local MESZ/MEZ timestamp
    meta["description"] = "Flap Master Task Status Report";                     ///< Human readable description
    meta["author"]      = "ReportTask";                                         ///< File author/owner

    // ------------------------------------------------------------
    // 2. Copy all existing key/value pairs from input document
    // ------------------------------------------------------------
    for (JsonPair kv : dataDoc.as<JsonObject>()) {
        finalDoc[kv.key()] = kv.value();                                        ///< Copy each field into final document
    }

    // ------------------------------------------------------------
    // 3. Open file for writing in SPIFFS
    // ------------------------------------------------------------
    File file = SPIFFS.open(filename, FILE_WRITE);
    if (!file) {
        Serial.printf("Error: could not open %s for writing\n", filename);
        return false;
    }

    // ------------------------------------------------------------
    // 4. Serialize and pretty-print JSON into the file
    // ------------------------------------------------------------
    if (serializeJsonPretty(finalDoc, file) == 0) {
        Serial.printf("Error: failed to write JSON to %s\n", filename);
        file.close();
        return false;
    }

    // ------------------------------------------------------------
    // 5. Close file and report success
    // ------------------------------------------------------------
    file.close();
    Serial.printf("JSON saved successfully: %s\n", filename);
    return true;
}

// ---------------------------
// File open and read
// (ignores optional _meta section)
// ---------------------------
bool FlapFile::readFile(const char* filename, JsonDocument& doc) {
    File file = SPIFFS.open(filename, FILE_READ);
    if (!file) {
        filePrintln("Error: JSON-file not found");
        return false;
    }

    JsonDocument         tempDoc;
    DeserializationError err = deserializeJson(tempDoc, file);
    file.close();
    if (err) {
        filePrintln("Error while parsing JSON: %s", err.c_str());
        return false;
    }

    // Prüfen, ob _meta im Root-Objekt existiert
    if (tempDoc["_meta"].is<JsonObject>()) {
        for (JsonPair kv : tempDoc.as<JsonObject>()) {
            if (strcmp(kv.key().c_str(), "_meta") != 0) {
                doc[kv.key()] = kv.value();                                     // Nur Nicht-_meta übernehmen
            }
        }
    } else {
        doc.set(tempDoc);
    }

    return true;
}

// ----------------------------
// available
bool FlapFile::available() {
    if (!SPIFFS.begin(false)) {
        filePrintln("SPIFFS not available");
        return false;
    }
    return true;
}

// ---------------------------
// Format Byte Values
String FlapFile::formatSize(size_t bytes) {
    if (bytes < 1024) {
        return String(bytes) + " B";
    } else if (bytes < (1024 * 1024)) {
        return String(bytes / 1024.0, 2) + " kB";
    } else {
        return String(bytes / 1024.0 / 1024.0, 2) + " MB";
    }
}

// -----------------
// Format time stamp
String FlapFile::isoTimestamp() {
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);                                               // local time
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", &timeinfo);
    return String(buf);
}

// -----------------
// List ESP32 partitions
void FlapFile::listPartitions() {
    filePrintln("=== ESP32 Partition Table ===");
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it != NULL) {
        const esp_partition_t* p = esp_partition_get(it);
        filePrintln("Partition: %-12s | Type: 0x%02x | Subtype: 0x%02x | Offset: 0x%06x | Size: %s", p->label, p->type, p->subtype, p->address,
                    formatSize(p->size).c_str());
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);
    filePrintln("=== ESP32 Partition Table ====");
}
