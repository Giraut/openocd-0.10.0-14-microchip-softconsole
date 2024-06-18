#ifndef FP_IMPLEMENTATIONS
#define FP_IMPLEMENTATIONS

#include <jtag/interface.h>

#define FP_LONGEST_PORT 256

// API to witch all client implementations (eFP6 and fpcommwrapper) need to adhere.
// Currently only 2 implementations are present microsemi_efp6_client2 which
// is eFP6 rev B and microsemi_fpcommwrapper_client. Previously there was
// eFP6 rev A implementation as well, but it was harder to keep in track with
// other changes and it was simplified. This approach should allows us to add
// support to newer revisions of eFP6 if needed, or any other non fpcommwrapper
// dongles we will need. 

// It is similar to the OpenOCDs parent structure, but simplified with small changes.
// From all the OpenOCD queue commands only subset is exposed in the hope we
// could keep using a trimmed down version like this and have easier time
// to maintain less commands which we understand better

typedef struct
{
  int  (*enumerate)(const char* partialPortName);
  int  (*open)(void);
  int  (*close)(void);

  int  (*setTckFrequency)(int32_t tckFreq);
  int  (*readTckFrequency)(void);

  int  (*setTrst)(      struct reset_command     *cmd);
  int  (*jtagGotoState)(struct statemove_command *cmd);
  int  (*runtest)(      struct runtest_command   *cmd);
  int  (*executeScan)(  struct scan_command      *cmd);

} fp_implementation_api;

#endif