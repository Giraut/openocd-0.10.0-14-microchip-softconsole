#include <stdio.h>

#include <hidapi.h>

// Revisions A and B were named after I gave the files names 1 and 2 postfix
// this can be cleaned up to be more consistent with the current naming
#include "microsemi_efp6_client1.h"

#define EFP6_1_MICROSEMI_VID 0x1514
#define EFP6_1_PID           0x200a

// This could be (and previously it was) a implementation of the fp_implementation_api
// however then having required headers and C files meant that changing the API 
// required to change the code at 7 locations which was allowing me to make more 
// errors. When needed this could be implemented as fully working implementation
// if people refuse to upgrade and will demand working revision A

void eFP6_1_warn(void)
{
    struct hid_device_info* devs;

    devs = hid_enumerate(EFP6_1_MICROSEMI_VID, EFP6_1_PID);

    for(struct hid_device_info* cur_dev = devs; cur_dev; cur_dev = cur_dev->next) {
        printf("********************************************************************\r\n");
        printf("*                          Warning!                                *\r\n");
        printf("* Embedded FlashPro6 detected with an older unsupported firmware!  *\r\n");
        printf("* Read the SoftConsole release notes for update instructions.      *\r\n");
        printf("********************************************************************\r\n");

        break; // No need to display this message 5 times if there are 5 old eFP6
    }

    hid_free_enumeration(devs);
}
