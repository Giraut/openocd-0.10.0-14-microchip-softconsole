#include <stdio.h>

#include <jtag/interface.h>
#include <hidapi.h>

// Revisions A and B were named after I gave the files names 1 and 2 postfix
// this can be cleaned up and renamed to be more consistent with the current naming
#include "microsemi_efp6_client2.h"
#include "microsemi_efp6_api2.h"

#include "microsemi_missing_implementation.h"


// ---------------  Global variables for the programmer ------------------------

// The eFP6_2_close could happen asynchronously, therefore volatile
// it's not thread-safe but it's not used in multiple threads 
// however single threaded application can have asynchronous things happening
// like responding to a linux signal
volatile bool busy = false; 

hid_device* efp6Handle;

wchar_t eFP6_2_SerialNumber[EFP6_2_MAX_SERIAL_STRING];

__attribute__((aligned(8)))
uint8_t usbInBuffer[ EFP6_2_MAX_USB_BUFFER_BYTE_SIZE];

__attribute__((aligned(8)))
uint8_t usbOutBuffer[EFP6_2_MAX_USB_BUFFER_BYTE_SIZE];

uint8_t  packetsInBuffer = 0;
uint32_t bytesInBuffer   = EFP6_2_PACKET_OFFSET;

// If enabled in the header it will do extra statistic calculations
#ifdef EFP6_2_PACKET_SIZE_STATS  
uint32_t mhcp_efp6_stats_total = 0;
uint32_t mhcp_efp6_stats_max   = 0;
uint32_t number_of_hid_writes  = 0;
#endif


fp_implementation_api efp6implementation2 = {
  .enumerate        = eFP6_2_enumerate,
  .open             = eFP6_2_open,
  .close            = eFP6_2_close,

  .setTckFrequency  = eFP6_2_setTckFrequency,
  .readTckFrequency = eFP6_2_readTckFrequency,

  .setTrst          = eFP6_2_setTrst,
  .jtagGotoState    = eFP6_2_jtagGotoState,
  .runtest          = eFP6_2_runtest,
  .executeScan      = eFP6_2_executeScan
};


// ---------------------------- Programmer - Private ---------------------------


__attribute__((always_inline)) inline
void AddUsbPacketHeader(uint16_t packet_type, uint32_t target_address, uint32_t packet_length)
{
    uint16_t *casted = (uint16_t *)&usbInBuffer[bytesInBuffer];
    *(casted++) = EFP6_2_PACKET_START_CODE;
    *(casted++) = packet_type;

    *(casted++) = target_address >> 16;     // High 16-bits of the 32-bit target address
    *(casted++) = target_address & 0xfffff; // Low  16-bits of the 32-bit target address

    *(casted++) = packet_length  >> 16;     // High 16-bits of the 32-bit packet length
    *(casted)   = packet_length  & 0xfffff; // Low  16-bits of the 32-bit packet length

    // Still skipping CRC - can be enabled if really needed, so far we were just setting it to 0xDEAD
    // and the firmware/IP ignores it anyway, so I'm considering it just as a bloat part of the packets
    // casted = (uint16_t *)&usbInBuffer[bytesInBuffer + packet_length + EFP6_2_PACKET_HEADER_TOTAL_LENGTH - 2];
    // *casted = packet_crc;
}


__attribute__((always_inline)) inline
void AddUsbPacketHeaderSimpler(uint16_t packet_type, uint32_t packet_length)
{
    // Similar as AddUsbPacketHeader, but even more simplified, it assumes:
    // - the target address is 0
    // - the length is under 65536
    // - the CRC is ignored in the firmware/IP and doesn't have to be set
    // - that we correctly zeroized the buffer previously and all the skipped writes will have 0 in the buffer anyway

    uint16_t *casted = (uint16_t *)&usbInBuffer[bytesInBuffer];
    *(casted++) = EFP6_2_PACKET_START_CODE;
    *(casted++) = packet_type;

    casted++;                               // Skipping high 16-bits of the 32-bit target address
    casted++;                               // Skipping low  16-bits of the 32-bit target address

    casted++;                               // Skipping High 16-bits of the 32-bit packet length
    *(casted)   = packet_length  & 0xfffff; // Low  16-bits of the 32-bit packet length

    // Skipping CRC altogether
}


__attribute__((always_inline)) inline
int FlushUsbBuffer(void)
{
    int error;

    // __MINGW32__ is detecting the Windows builds, for Windows it uses separate slightly bigger buffer
    // so it can prepone the payload with one 0. In essence the REPORT ID is optional, but not on Windows
    // so it has to be set to 0 to skip it and we need to send 1024 payload on Windows we have to send 1025 
    // instead. Cleaner it would be to put it into hidapi and then treat the API the same and I did it for 
    // a moment and the build can be changed to use different fork, but then having to maintain it could
    // be hassle so I reverted to a ugly fix on the host side done here, instead of having it the library.
    // There are numerous issues around the 0 on Windows on the signal11 repository, and it looks like
    // the libusb (maintained fork) shares the same problem
    // https://stackoverflow.com/a/31668028/4535300
    #ifdef __MINGW32__
    static uint8_t usbInBuffer2[ EFP6_2_MAX_USB_BUFFER_BYTE_SIZE+1];
    usbInBuffer2[0]=0;
    #endif


    #ifdef EFP6_2_PACKET_SIZE_STATS
        // Collect statistics and display them every x packets
        if (mhcp_efp6_stats_max < bytesInBuffer) mhcp_efp6_stats_max = bytesInBuffer;
        mhcp_efp6_stats_total += bytesInBuffer;

        if ((number_of_hid_writes+1)%EFP6_2_PACKET_SIZE_STATS_FREQUENCY==0) {
            printf("hid write stats: max_total=%d avr_from_last_update=%d\r\n", mhcp_efp6_stats_max, mhcp_efp6_stats_total/MCHP_EFP6_PACKET_SIZE_STATS_FREQUENCY);
            mhcp_efp6_stats_total=0;
        }
        number_of_hid_writes++;
    #endif

    // Write to HID
    usbInBuffer[0] = packetsInBuffer;

    #ifdef __MINGW32__
    memcpy(usbInBuffer2+1, usbInBuffer, EFP6_2_MAX_USB_BUFFER_BYTE_SIZE);
    error = hid_write(efp6Handle, usbInBuffer2, EFP6_2_MAX_USB_BUFFER_BYTE_SIZE+1);
    #else
    error = hid_write(efp6Handle, usbInBuffer, EFP6_2_MAX_USB_BUFFER_BYTE_SIZE);
    #endif
    if (error < 0)
    {
        LOG_ERROR("Embedded FlashPro6 (revision B) failed to send the data. Programmer device reset is required.  Err = %d", error);
        return error;
    }
    memset(usbInBuffer, 0, bytesInBuffer); // Only clean section we polluted

    // Read from HID
    error = hid_read_timeout(efp6Handle, usbOutBuffer, EFP6_2_MAX_USB_BUFFER_BYTE_SIZE, EFP6_2_READ_TIMEOUT);
    if (error < (int)EFP6_2_MAX_USB_BUFFER_BYTE_SIZE)
    {
        LOG_ERROR("Failed to read data from USB buffer.  Programmer reset is required.  Err = %d", error);
        return error;
    }

    // Post write/read tasks
    bytesInBuffer   = EFP6_2_PACKET_OFFSET;  // Because the first 32-bits will be number of packets anyway
    packetsInBuffer = 0;

    return EFP6_2_RESPONSE_OK;
}


__attribute__((always_inline)) inline
void AddPacketToUsbBuffer(uint16_t opcode, const uint8_t* buf, uint32_t packetLength, uint32_t paddingBytes, uint32_t targetAddress)
{
    if (buf != NULL)
    {
        // Populate the USB buffer only when buf is non NULL
        // When buf is NULL populating the buffer with 0 can be skipped because we zeroize buffer in the flush anyway
        memcpy(&usbInBuffer[bytesInBuffer + EFP6_2_PACKET_HEADER_PREAMBLE_LENGTH], buf, packetLength);
    }

    // Zero padding can be skipped here as well as we still have the zeros here from the 
    // initial zero-ization, or from the previous flush

    if (targetAddress != 0) 
    {
        AddUsbPacketHeader(opcode, targetAddress, packetLength + paddingBytes);
    }
    else 
    {
        // Using simpler packet header handler because no target address
        AddUsbPacketHeaderSimpler(opcode, packetLength + paddingBytes);
    }


    bytesInBuffer += EFP6_2_PACKET_HEADER_TOTAL_LENGTH + packetLength + paddingBytes;

    packetsInBuffer++;
}



__attribute__((always_inline)) inline
int ConstructAndSendPacket(uint16_t opcode, uint8_t* buf, uint32_t packetLength, uint32_t targetAddress)
{
    int error = EFP6_2_RESPONSE_OK;
    AddPacketToUsbBuffer(opcode, buf, packetLength, 0, targetAddress);
    error = FlushUsbBuffer();
    
    return error;
}


__attribute__((always_inline)) inline
int retrieveData(uint8_t* buffer, uint32_t bit_length)
{
    int error;
    uint32_t byte_length;

    byte_length = (bit_length + 7) / 8;

    error = ConstructAndSendPacket(EFP6_2_READ_TDO_BUFFER_COMMAND_OPCODE, NULL, 0, 0);
    
    if (error != EFP6_2_RESPONSE_OK) return error;

    // printf("Getting %d bytes \r\n", byte_length);
    // for (int i=0; i< byte_length; i++)
    // {
    //     if (i%4==0) printf(" ");
    //     printf("%02x ", usbOutBuffer[i]);
    // }
    // printf("\r\n");

    memcpy(buffer, usbOutBuffer, byte_length);
    
    return error;
}


// ------------------ API - Programmer - Public ------------------------------
// TODO: not fully corrent, as the API changed a lot some commands are now
// 'private' so they should be moved above this line.
// For example eFP6_2_drScan and eFP6_2_irScan are not part of the public API anymore
// as now they are called from public eFP6_2_executeScan API call



int eFP6_2_enumerate(const char* partialPortName)
{
    struct hid_device_info* devs;
    uint32_t foundDevices = 0;

    // Convert a char* to a wchar_t*
    const size_t portStringSize = strlen(partialPortName);
    wchar_t* wefp6port[portStringSize + 1];    
    mbstowcs(wefp6port, partialPortName, portStringSize + 1);

    devs = hid_enumerate(EFP6_2_MICROSEMI_VID, EFP6_2_PID);

    for(struct hid_device_info* cur_dev = devs; cur_dev; cur_dev = cur_dev->next) {
        // printf("Embedded FlashPro6 (revision B) found (USB_ID=%04hx:%04hx path=%s serial_number=%ls release_number=%d manufacturer_string=%ls product_string=%ls)\r\n", cur_dev->vendor_id, cur_dev->product_id, cur_dev->path, cur_dev->serial_number, cur_dev->release_number, cur_dev->manufacturer_string, cur_dev->product_string);
        // LOG_INFO("%s is_ir_scan=%d end_state=%d num_fields=%d", __FUNCTION__, cmd->ir_scan, cmd->end_state, cmd->num_fields);
        LOG_INFO("Embedded FlashPro6 (revision B) found (USB_ID=%04hx:%04hx path=%s)", cur_dev->vendor_id, cur_dev->product_id, cur_dev->path);

        if (0 == portStringSize)
        {
            // When no port was given, use any port
            foundDevices++;
            wcscpy(eFP6_2_SerialNumber, cur_dev->serial_number);        
        }
        else 
        {
            // if (0 == wcsncasecmp(cur_dev->serial_number, wefp6port, portStringSize))
            // Not using the case insensitive version because it should be part of the <wchar.h> yet it gave me undefined symbol error
            if (0 == wcsncmp(cur_dev->serial_number, wefp6port, portStringSize))
            {
                // We found a match, by using the portStringSize it's possible to match just substring of the port
                foundDevices++;
                wcscpy(eFP6_2_SerialNumber, cur_dev->serial_number);
                LOG_INFO("The device at %s with port %s matched with the port checker", cur_dev->path, cur_dev->serial_number);
                // We can't claim that it will be used, because if multiple ports/serial_numbers will match, then only the last one will be used
            }
            else 
            {
                LOG_INFO("The device at %s with port %s will be ignored because it didn't match the port checker", cur_dev->path, cur_dev->serial_number);
            }

        }
    }

    hid_free_enumeration(devs);

    if (0 == foundDevices)
    {
        LOG_INFO("No Embedded FlashPro6 (revision B) devices found");
    }
    return foundDevices;
}


int eFP6_2_open(void)
{
    int error;

    memset(usbInBuffer,  0, EFP6_2_MAX_USB_BUFFER_BYTE_SIZE);
    memset(usbOutBuffer, 0, EFP6_2_MAX_USB_BUFFER_BYTE_SIZE);

    // When given NULL use whatever was detected in the enumeration stage
    efp6Handle = hid_open(EFP6_2_MICROSEMI_VID, EFP6_2_PID, eFP6_2_SerialNumber);

    if (efp6Handle == NULL) {
        LOG_ERROR("Unable to open Embedded FlashPro6 (revision B) device");
        return EFP6_2_RESPONSE_DEVICE_OPEN_ERROR;
    }

    error = hid_set_nonblocking(efp6Handle, 0);
    if (error != EFP6_2_RESPONSE_OK)
    {
        LOG_ERROR("Embedded FlashPro6 (revision B) hid_set_nonblocking failed");
        return error;
    }
   
    error = eFP6_2_getCm3Version();
    if (error != EFP6_2_RESPONSE_OK) return error;

    eFP6_2_setLed(EFP6_2_SOLID_GREEN);

    error = eFP6_2_enableJtagPort();
    if (error != EFP6_2_RESPONSE_OK) return error;

    return EFP6_2_RESPONSE_OK;
}


int eFP6_2_close(void)
{
    if (busy)
    {
        // Only explain the things if it's needed, if we are not busy, we will not be waiting for the command to finish
        LOG_INFO("Embedded FlashPro6 (revision B) waiting for the current command to finish");
    }

    while (busy);
    
    LOG_INFO("Embedded FlashPro6 (revision B): closing the device");

    eFP6_2_disableJtagPort();

    eFP6_2_setLed(EFP6_2_OFF);

    hid_close(efp6Handle);

    return EFP6_2_RESPONSE_OK;
}


int eFP6_2_getCm3Version(void)
{
    int error;

    busy = true;

    error = ConstructAndSendPacket(EFP6_2_CM3_FW_VERSION_OPCODE, NULL, 0, 0);
    if (error != EFP6_2_RESPONSE_OK)
        LOG_ERROR("Embedded FlashPro6 (revision B) failed to read firmware version number");
    else
        LOG_INFO("Embedded FlashPro6 (revision B) CM3 firmware version: %X.%X", usbOutBuffer[1], usbOutBuffer[0]);

    busy = false;

    return error;
}


int eFP6_2_setTckFrequency(int32_t tckFreq)
{
    uint8_t payload_buffer[EFP6_2_PAYLOAD_ATOMIC_PACKET_LENGTH];
    memset(payload_buffer, 0, EFP6_2_PAYLOAD_ATOMIC_PACKET_LENGTH);

    // Tested on Anton's iCicle :) to run at 13MHz, so using 12MHz to have some margin 

    const int defaultTckMHz = 12;

    busy = true;

    // This is Anton's condition to check for the 'magic' value where all the FP 
    // implementations should use their fastest good TCK
    if (-1 == tckFreq || 0 == tckFreq) 
    {
        // No valid TCK was set, use default good speed
        LOG_INFO("Embedded FlashPro6 (revision B) TCK frequency is set to 0, use valid frequency of  %dMHz instead\r\n", defaultTckMHz);
        tckFreq = defaultTckMHz * 1000000;
    }

    tckFreq = tckFreq / 1000000;

    // This is programing guy's condition for the valid TCK range, which ouputs
    // warning instead of info message.
    // In essence it could be simplified to the one condition, but for now I like
    // to have them separated
    int error;
    if (tckFreq < 4 && tckFreq > 20)
    {
        // Still no valid range
        LOG_WARNING("Embedded FlashPro6 (revision B) frequency range is 4 MHz - 20 MHz. Setting frequency to %dMHz\r\n", defaultTckMHz);
        tckFreq = defaultTckMHz;
    }

    payload_buffer[0] = EFP6_2_SET_TCK_FREQUENCY;
    payload_buffer[1] = tckFreq;

    error = ConstructAndSendPacket(EFP6_2_SEND_PACKET_OPCODE, payload_buffer, EFP6_2_PAYLOAD_ATOMIC_PACKET_LENGTH, EFP6_2_TARGET_FREQUENCY_ADDRESS);
    if (error == EFP6_2_RESPONSE_OK)
        error = ConstructAndSendPacket(EFP6_2_FREQUENCY_INTERRUPT_OPCODE, NULL, 0, 0);

    busy = false;

    return error;
}


int eFP6_2_readTckFrequency(void)
{
    int error;

    busy = true;
    error = ConstructAndSendPacket(EFP6_2_READ_TCK_OPCODE, NULL, 0, EFP6_2_TARGET_FREQUENCY_ADDRESS);
    busy = false;

    if (error != EFP6_2_RESPONSE_OK)
    {
        LOG_ERROR("Embedded FlashPro6 (revision B) failed to read TCK frequency");
        return 0;
    }

    return usbOutBuffer[5];
}


int eFP6_2_enableJtagPort(void)
{
    busy = true;
    int error = ConstructAndSendPacket(EFP6_2_ENABLE_JTAG_PORT_OPCODE, NULL, 0, 0);
    busy = false;
    return error;
}


int eFP6_2_disableJtagPort(void)
{
    busy = true;
    int error = ConstructAndSendPacket(EFP6_2_DISABLE_JTAG_PORT_OPCODE, NULL, 0, 0);
    busy = false;
    return error;
}


int eFP6_2_setTrst(struct reset_command *cmd)
{
    uint8_t payload_buffer[2] = { 0x0, 0x0 };

    busy = true;

    if (!cmd->trst) 
    {
        payload_buffer[0] = EFP6_2_TRSTB_BIT;
    }

    int error = ConstructAndSendPacket(EFP6_2_SET_JTAG_PINS_OPCODE, payload_buffer, EFP6_2_ATOMIC_JTAG_OPERATION_PACKET_LENGTH, 0);

    busy = false;

    return error;
}


int eFP6_2_setLed(uint8_t status)
{
    int error;
    uint8_t payload_buffer[2] = { 0x0, 0x0 };
    payload_buffer[0] = status; // bit2 is to enable toggling

    busy = true;

    error = ConstructAndSendPacket(EFP6_2_TURN_ACTIVITY_LED_OFF, NULL, 0, 0);
    if (status != EFP6_2_OFF)
        error = ConstructAndSendPacket(EFP6_2_TURN_ACTIVITY_LED_ON, payload_buffer, EFP6_2_ATOMIC_JTAG_OPERATION_PACKET_LENGTH, 0);

    busy = false;

    return error;
}


// Just take the state number
int eFP6_2_jtagGotoState_raw(int state)
{
    int error = EFP6_2_RESPONSE_OK;
    uint8_t payload_buffer[EFP6_2_ATOMIC_JTAG_OPERATION_PACKET_LENGTH];

    busy = true;

    ((uint16_t*) payload_buffer)[0] = state;

    AddPacketToUsbBuffer(EFP6_2_JTAG_ATOMIC_OPCODE, payload_buffer, EFP6_2_ATOMIC_JTAG_OPERATION_PACKET_LENGTH, 0, 0);
    
    if ( EFP6_2_JTAG_RESET == state)
    {
        error = FlushUsbBuffer();
    }

    busy = false;

    return error;
}


// Take the state move command instead, to make this API compatible with fpcommwrapper client
int eFP6_2_jtagGotoState(struct statemove_command *cmd)
{
    int error = EFP6_2_RESPONSE_OK;
    uint8_t payload_buffer[EFP6_2_ATOMIC_JTAG_OPERATION_PACKET_LENGTH];

    busy = true;

    ((uint16_t*) payload_buffer)[0] = cmd->end_state;
    // payload_buffer[0] = (uint8_t)(jtag_state & 0xff);
    // payload_buffer[1] = (uint8_t)((jtag_state >> 8) & 0xff);

    AddPacketToUsbBuffer(EFP6_2_JTAG_ATOMIC_OPCODE, payload_buffer, EFP6_2_ATOMIC_JTAG_OPERATION_PACKET_LENGTH, 0, 0);
    
    if ( cmd->end_state == EFP6_2_JTAG_RESET)
        error = FlushUsbBuffer();

    busy = false;

    return error;
}


int  eFP6_2_runtest(struct runtest_command *cmd) 
{
    busy = true;

    if (cmd->end_state != EFP6_2_JTAG_IDLE)
    {
        LOG_ERROR("Embedded FlashPro6 (revision B) execute run-test state only implemented with TAP_IDLE end-state");
        LOG_ERROR(MICROSEMI_MISSING_IMPLEMENTATION_STRING);
    }

    for (int i=0; i<cmd->num_cycles; i++) {
        // When using slow TCK it might cause the need for longer TAP idle delays
        eFP6_2_jtagGotoState_raw(EFP6_2_JTAG_IDLE);
    }

    int error = eFP6_2_jtagGotoState_raw(EFP6_2_JTAG_IDLE);

    busy = false;

    return error;
}


// This could be implemented like the dr scan and calling various helper functions to
// populate the buffer (like AddPacketToUsbBuffer) and construct the ir scan from 
// various eFP6 packets, but because IR scans are so much simpler and static
// and was pushing for the host have as little overhead as possible this
// ended up being a hardcoded structure, changing just relevant bytes in the structure
// and pusshing it in one go instead of building it from 4 packets one by one
int eFP6_2_irScan(uint32_t bit_length, const uint8_t* out, uint8_t* in, uint16_t end_state)
{
    int error = EFP6_2_RESPONSE_OK;

    __attribute__((aligned(8)))  // aligned(4) could be enough, to do the 32-bit alignment
    static efp6_2_IR_scan fullScan = 
    {
        .setLen = {
            .packetStart = EFP6_2_PACKET_START_CODE, 
            .packetType  = EFP6_2_SET_SHIFT_IR_DR_BIT_LENGTH_OPCODE,
            .address     = 0,
            .lenHigh     = 0,
            .lenLow      = EFP6_2_ATOMIC_JTAG_OPERATION_PACKET_LENGTH,
            .data[0]     = 0,
            .data[1]     = 0,
            .crc         = 0
        },
        
        .data = {
            .packetStart = EFP6_2_PACKET_START_CODE, 
            .packetType  = EFP6_2_SHIFT_DATA_FROM_REGISTER,
            .address     = 0,
            .lenHigh     = 0,
            .lenLow      = EFP6_2_ATOMIC_JTAG_OPERATION_PACKET_LENGTH,
            .data[0]     = 0,
            .data[1]     = 0,
            .crc         = 0
        },

        .shift = {
            .packetStart = EFP6_2_PACKET_START_CODE, 
            .packetType  = EFP6_2_JTAG_ATOMIC_OPCODE,
            .address     = 0,
            .lenHigh     = 0,
            .lenLow      = EFP6_2_ATOMIC_JTAG_OPERATION_PACKET_LENGTH,
            .data[0]     = EFP6_2_JTAG_SHIFT_IR,
            .data[1]     = 0,
            .crc         = 0
        },

        .endState = {
            .packetStart = EFP6_2_PACKET_START_CODE, 
            .packetType  = EFP6_2_JTAG_ATOMIC_OPCODE,
            .address     = 0,
            .lenHigh     = 0,
            .lenLow      = EFP6_2_ATOMIC_JTAG_OPERATION_PACKET_LENGTH,
            .data[0]     = 0,
            .data[1]     = 0,
            .crc         = 0
        }
    };

    fullScan.setLen.data[0]   = bit_length;
    fullScan.data.data[0]     = out[0];
    fullScan.data.data[1]     = out[1];
    fullScan.endState.data[0] = end_state;

    uint32_t *dst = (uint32_t *)&usbInBuffer[bytesInBuffer];
    uint32_t *src = (uint32_t *)&fullScan;

    // Copy 4 packets (16 bytes each) to the USB buffer (this is just unrolled memcpy)
    *dst++=*src++;
    *dst++=*src++;
    *dst++=*src++;
    *dst++=*src++;

    *dst++=*src++;
    *dst++=*src++;
    *dst++=*src++;
    *dst++=*src++;

    *dst++=*src++;
    *dst++=*src++;
    *dst++=*src++;
    *dst++=*src++;

    *dst++=*src++;
    *dst++=*src++;
    *dst++=*src++;
    *dst=*src;     // No need to increment on the last copy as we are not using the pointers for anything else

    // memcpy(&usbInBuffer[bytesInBuffer], (uint8_t *)&fullScan, sizeof(fullScan));
    bytesInBuffer  += sizeof(fullScan); // No need to align the buffer's index after this copy because size of the fullScan is 64 bytes
    packetsInBuffer+=4;                 // Read back from the USB if we are expecting results
    
    if (EFP6_2_JTAG_RESET == end_state)
    {
        error = FlushUsbBuffer();
    }

    if ((NULL != in) && (EFP6_2_RESPONSE_OK == error))
    {
        // Read back from the USB only if we are expecting to read the content
        error = retrieveData(in, bit_length);
    }

    return error;
}


int eFP6_2_drScan(uint32_t bit_length, const uint8_t* out, uint8_t* in, uint16_t end_state)
{
    int error = EFP6_2_RESPONSE_OK;
    uint8_t payload_buffer[EFP6_2_ATOMIC_JTAG_OPERATION_PACKET_LENGTH];
    uint32_t BytesToTransmit;
    uint32_t PaddingBytes;

    ((uint16_t *)payload_buffer)[0] = bit_length;
    AddPacketToUsbBuffer(EFP6_2_SET_SHIFT_IR_DR_BIT_LENGTH_OPCODE, payload_buffer, EFP6_2_ATOMIC_JTAG_OPERATION_PACKET_LENGTH, 0, 0);

    PaddingBytes = 0;
    if (bit_length > 16)
    {
        BytesToTransmit = (uint32_t)((bit_length + 7) / 8);
        if (BytesToTransmit % 16)
        {
            PaddingBytes = (16 - BytesToTransmit % 16);
        }
        AddPacketToUsbBuffer(EFP6_2_SHIFT_DATA_FROM_DDR, out, BytesToTransmit, PaddingBytes, 0);

        if (bytesInBuffer % EFP6_2_PACKET_OFFSET)
        {
            bytesInBuffer += (EFP6_2_PACKET_OFFSET - bytesInBuffer % EFP6_2_PACKET_OFFSET);
        }        
    }
    else
    {
        AddPacketToUsbBuffer(EFP6_2_SHIFT_DATA_FROM_REGISTER, out, 2, 0, 0);
    }

    payload_buffer[0] = EFP6_2_JTAG_SHIFT_DR;
    payload_buffer[1] = 0;
    AddPacketToUsbBuffer(EFP6_2_JTAG_ATOMIC_OPCODE, payload_buffer, EFP6_2_ATOMIC_JTAG_OPERATION_PACKET_LENGTH, 0, 0);
    error = eFP6_2_jtagGotoState_raw(end_state);

    if ((NULL != in) && (EFP6_2_RESPONSE_OK == error))
    {
        error = retrieveData(in, bit_length);
    }

    return error;
}


int eFP6_2_executeScan(struct scan_command *cmd) 
{
    busy = true;
    // Experimentally proven that rarely the end state is something else than IDLE

    // Then num_fields is 1 even when the base structure support bigger scans

    if (cmd->num_fields > 1) {
        LOG_ERROR("Execute scan is implemented only with one field but this request has %d fields", cmd->num_fields);
        LOG_ERROR(MICROSEMI_MISSING_IMPLEMENTATION_STRING);

        return ERROR_FAIL;
    }

    int error;

    if (cmd->ir_scan) 
    {
        error = eFP6_2_irScan(cmd->fields[0].num_bits, cmd->fields[0].out_value, cmd->fields[0].in_value, cmd->end_state);
    }
    else 
    {
        error = eFP6_2_drScan(cmd->fields[0].num_bits, cmd->fields[0].out_value, cmd->fields[0].in_value, cmd->end_state);
    }

    busy = false;
    return error;

    // Commented out implementation which supports multiple fields, instead of just one
    // if (cmd->ir_scan) 
    // {
    //     for (int i = 0; i < cmd->num_fields; i++) {
    //         int error = IrScan(cmd->fields[0].num_bits, cmd->fields[0].out_value, cmd->fields[0].in_value, cmd->end_state);
    //         if (error!= ERROR_OK) return error;
    //     }
    // }
    // else 
    // {
    //     for (int i = 0; i < cmd->num_fields; i++) {
    //         int error = DrScan(cmd->fields[0].num_bits, cmd->fields[0].out_value, cmd->fields[0].in_value, cmd->end_state);
    //         if (error!= ERROR_OK) return error;
    //     }
    // }
    // return ERROR_OK;
}
