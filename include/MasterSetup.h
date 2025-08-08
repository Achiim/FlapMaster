// ###################################################################################################
//
//  ███    ███  █████  ███████ ████████ ███████ ██████      ███████ ███████ ████████ ██    ██ ██████
//  ████  ████ ██   ██ ██         ██    ██      ██   ██     ██      ██         ██    ██    ██ ██   ██
//  ██ ████ ██ ███████ ███████    ██    █████   ██████      ███████ █████      ██    ██    ██ ██████
//  ██  ██  ██ ██   ██      ██    ██    ██      ██   ██          ██ ██         ██    ██    ██ ██
//  ██      ██ ██   ██ ███████    ██    ███████ ██   ██     ███████ ███████    ██     ██████  ██
//
//
// ################################################################################################## by Achim ####
// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=ANSI%20Regular&t=MASTER%20SETUP
/*

*/
#ifndef MasterSetup_h
#define MasterSetup_h

void masterIntroduction();                                                      // welcome message
void masterAddressPool();                                                       // usable I2C addresses
void masterI2Csetup();                                                          // setup i2c for Master
void masterRemoteControl();                                                     // setup remote control
void masterSlaveControlObject();                                                // create control objects
void masterStartRtosTasks();                                                    // start all RTOS Tasks
void masterOutrodution();                                                       // setup finishc message
void createTwinTasks();                                                         // create Twins
void createStatisticTask();                                                     // create Statistic
void createReportTask();                                                        // create Report task
void createRemoteControlTask();                                                 // create Remote Control
void createRegisterTwinsTask();                                                 // create Registry
void createParserTask();                                                        // create remote creator task

#endif                                                                          // MasterSetup_h
