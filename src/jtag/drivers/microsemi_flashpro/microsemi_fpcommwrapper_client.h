#ifndef FPCOMMWRAPPERCLIENT
#define FPCOMMWRAPPERCLIENT


#include <stdint.h>

#include <string.h>
#include <wchar.h>
#include <locale.h>
#include <stdbool.h>

#include "microsemi_fp_implementations.h"

/* Useful defines */
#define HZ_PER_KHZ                        1000        /* 1KHz = 1000Hz! :-) */

int  fpcommwrapper_enumerate(const char* partialPortName);
int  fpcommwrapper_open(void);
int  fpcommwrapper_close(void);

int  fpcommwrapper_setTckFrequency(int32_t tckFreq);
int  fpcommwrapper_readTckFrequency(void);

int  fpcommwrapper_setTrst(struct reset_command *cmd);
int  fpcommwrapper_jtagGotoState(struct statemove_command *cmd);
int  fpcommwrapper_runtest(struct runtest_command *cmd);
int  fpcommwrapper_executeScan(struct scan_command *cmd); 


extern fp_implementation_api fpcommwrapperImplementation;

#endif