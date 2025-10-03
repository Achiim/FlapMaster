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
};

// ---------------------------
// File open and write
bool FlapFile::saveFile(const char* filename, StaticJsonDocument<2048>& doc) {
    File file = SPIFFS.open(filename, FILE_WRITE);
    if (!file) {
        filePrintln("file open error %s", filename);
        return false;
    }
    serializeJsonPretty(doc, file);
    file.close();

    filePrintln("file successfully stored %s", filename);
    return true;
}

// ---------------------------
// File open and read
bool FlapFile::readFile(const char* filename, StaticJsonDocument<2048>& doc) {
    File file = SPIFFS.open(filename, FILE_READ);
    if (!file) {
        filePrintln("Error: JSON-file not found");
        return false;
    }

    DeserializationError err = deserializeJson(doc, file);
    if (err) {
        String msg = "Error while parsing JSON: ";
        msg += err.c_str();                                                     // z. B. "InvalidInput", "NoMemory", "EmptyInput"
        filePrintln("%s", msg);
        return false;
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

String FlapFile::formatSize(size_t bytes) {
    if (bytes < 1024) {
        return String(bytes) + " B";
    } else if (bytes < (1024 * 1024)) {
        return String(bytes / 1024.0, 2) + " kB";
    } else {
        return String(bytes / 1024.0 / 1024.0, 2) + " MB";
    }
}
