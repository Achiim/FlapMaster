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
#include <Arduino.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include "TracePrint.h"

#ifndef FlapFile_h
    #define FlapFile_h
/**
 * @brief Flap File class
 * provides all functionality to handle files on SPIFFS
 */
class FlapFile {
   public:
    // ----------------------------
    // constructor
    FlapFile();

    // ----------------------------
    // public functions
    bool available();                                                           // check ich filesystem is available
    bool saveFile(const char* filename, JsonDocument& doc);                     // store a file
    bool readFile(const char* filename, JsonDocument& doc);                     // read a file

    // ----------------------------
    // Trace functions
    template <typename... Args>                                                 // Registry trace
    void filePrint(const Args&... args) const {
        tracePrint("[FLAP-FILESYSTEM] ", args...);
    }
    template <typename... Args>                                                 // Registry trace with new line
    void filePrintln(const Args&... args) const {
        tracePrintln("[FLAP-FILESYSTEM] ", args...);
    }

   private:
    // ----------------------------
    // privat functions
    String formatSize(size_t bytes);                                            // Format Bytes to Mb, Kb, B
    String isoTimestamp();                                                      // Format time stamp
    void   listPartitions();                                                    // list ESP32 partitions
};

#endif                                                                          // FlapFile_h
