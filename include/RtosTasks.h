// #################################################################################################################
//
//  ██████  ████████  ██████  ███████     ████████  █████  ███████ ██   ██
//  ██   ██    ██    ██    ██ ██             ██    ██   ██ ██      ██  ██
//  ██████     ██    ██    ██ ███████        ██    ███████ ███████ █████
//  ██   ██    ██    ██    ██      ██        ██    ██   ██      ██ ██  ██
//  ██   ██    ██     ██████  ███████        ██    ██   ██ ███████ ██   ██
//
//
// ################################################################################################## by Achim ####
// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=ANSI%20Regular&t=RTOS%20TASK
//

#ifndef RtosTasks_h
#define RtosTasks_h

void remoteControl(void* pvParameters);                                         // free RTOS Task for receiving remote control codes
void ligaTask(void* pvParameters);                                              // free RTOS Task for fetching liga data
void webServerTask(void* pvParameters);                                         // free RTOS Task to provide web server
void parserTask(void* pvParameters);                                            // free RTOS Task for parsing remote control codes
void twinRegister(void* pvParameters);                                          // free RTOS Task for Registry
void reportTask(void* pvParameters);                                            // free RTOS Task for Report Task
void statisticTask(void* param);                                                // free RTOS Task for Statistics Task
void slaveTwinTask(void* pvParameters);                                         // free RTOS Task for Twin 0...n
#endif                                                                          // RtosTasks_h
