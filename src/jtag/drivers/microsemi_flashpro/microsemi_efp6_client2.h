#ifndef EFP6CLIENT2
#define EFP6CLIENT2


#include <stdint.h>

#include <string.h>
#include <wchar.h>
#include <locale.h>
#include <stdbool.h>

#include "microsemi_fp_implementations.h"

//#define EFP6_2_PACKET_SIZE_STATS
#define EFP6_2_PACKET_SIZE_STATS_FREQUENCY 128

#define EFP6_2_MAX_SERIAL_STRING           126  // https://stackoverflow.com/questions/7193645/how-long-is-the-string-of-manufacturer-of-a-usb-device

#define EFP6_2_READ_TIMEOUT 3500

// Internal functions
int  eFP6_2_getCm3Version(void);
int  eFP6_2_enableJtagPort(void);
int  eFP6_2_disableJtagPort(void);
int  eFP6_2_setLed(uint8_t status);


// API functions
int  eFP6_2_enumerate(const char* partialPortName);
int  eFP6_2_open(void);
int  eFP6_2_close(void);

int  eFP6_2_setTckFrequency(int32_t tckFreq);
int  eFP6_2_readTckFrequency(void);

int  eFP6_2_setTrst(struct reset_command *cmd);
int  eFP6_2_runtest(struct runtest_command *cmd);
int  eFP6_2_jtagGotoState(struct statemove_command *cmd);
int  eFP6_2_executeScan(struct scan_command *cmd);


extern fp_implementation_api efp6implementation2;

#endif