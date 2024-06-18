#include <stdio.h>
#include "eFP6client2.h"
#include "eFP6api2.h"


// ---------------  Global variables for the programmer ------------------------


hid_device* fp6Handle;
wchar_t eFP6SerialNumber[MCHP_EFP6_MAX_SERIAL_STRING];

__attribute__((aligned(8)))
uint8_t usbInBuffer[ EFP6_2_MAX_USB_BUFFER_BYTE_SIZE];

__attribute__((aligned(8)))
uint8_t usbOutBuffer[EFP6_2_MAX_USB_BUFFER_BYTE_SIZE];

uint8_t  packetsInBuffer = 0;
uint32_t bytesInBuffer   = EFP6_2_PACKET_OFFSET;

#ifdef MHCP_EFP6_PACKET_SIZE_STATS
uint32_t mhcp_efp6_stats_total = 0;
uint32_t mhcp_efp6_stats_max   = 0;
uint32_t number_of_hid_writes  = 0;
#endif


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

    // Still skipping CRC - but possible to enable it bif really needed, so far we were just setting it to DEAD
    // casted = (uint16_t *)&usbInBuffer[bytesInBuffer + packet_length + EFP6_2_PACKET_HEADER_TOTAL_LENGTH - 2];
    // *casted = packet_crc;
}


__attribute__((always_inline)) inline
void AddUsbPacketHeaderSimpler(uint16_t packet_type, uint32_t packet_length)
{
    // Simpler packet header assumes
    // - the target address is 0
    // - the length is under 65536
    // - the CRC is ignored in the firmware and doesn't have to be set
    // - that we correctly zeroized the buffer previously and all the skipped writes will have 0 in the buffer

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

    #ifdef MHCP_EFP6_PACKET_SIZE_STATS
        // Collect statistics and display them every x packets
        if (mhcp_efp6_stats_max < bytesInBuffer) mhcp_efp6_stats_max = bytesInBuffer;
        mhcp_efp6_stats_total += bytesInBuffer;

        if ((number_of_hid_writes+1)%MCHP_EFP6_PACKET_SIZE_STATS_FREQUENCY==0) {
            printf("hid write stats: max_total=%d avr_from_last_update=%d\r\n", mhcp_efp6_stats_max, mhcp_efp6_stats_total/MCHP_EFP6_PACKET_SIZE_STATS_FREQUENCY);
            mhcp_efp6_stats_total=0;
        }
        number_of_hid_writes++;
    #endif

    // Write to HID
    usbInBuffer[0] = packetsInBuffer;
    error = hid_write(fp6Handle, usbInBuffer, EFP6_2_MAX_USB_BUFFER_BYTE_SIZE);
    if (error < 0)
    {
        printf("\nError: Failed to send data to the eFP6.  Programmer reset is required.  Err = %d", error);
        return error;
    }
    memset(usbInBuffer, 0, bytesInBuffer); // Only clean section we polluted

    // Read from HID
    error = hid_read(fp6Handle, usbOutBuffer, EFP6_2_MAX_USB_BUFFER_BYTE_SIZE);
    if (error < (int)EFP6_2_MAX_USB_BUFFER_BYTE_SIZE)
    {
        printf("\nError: Failed to read data from USB buffer.  Programmer reset is required.  Err = %d", error);
        return error;
    }

    // Post write/read tasks
    bytesInBuffer   = EFP6_2_PACKET_OFFSET;  // Because the first 32-bits will be number of packets anyway
    packetsInBuffer = 0;

    return EFP6_2_RESPONSE_OK;
}


__attribute__((always_inline)) inline
int AddPacketToUsbBuffer(uint16_t opcode, const uint8_t* buf, uint32_t packetLength, uint32_t paddingBytes, uint32_t targetAddress)
{
    if (buf != NULL)
    {
        // Populate the USB buffer only when buf is non NULL
        // When buf is NULL populating the buffer with 0 can be skipped because we zeroize buffer in the flush anyway
        memcpy(&usbInBuffer[bytesInBuffer + EFP6_2_PACKET_HEADER_PREAMBLE_LENGTH], buf, packetLength);
    }

    // Zero padding can be skipped here as well as we still have the zeros here from the flush

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
    return EFP6_2_RESPONSE_OK;
}



__attribute__((always_inline)) inline
int ConstructAndSendPacket(uint16_t opcode, uint8_t* buf, uint32_t packetLength, uint32_t targetAddress)
{
    int error = EFP6_2_RESPONSE_OK;
    error = AddPacketToUsbBuffer(opcode, buf, packetLength, 0, targetAddress);
    if (error == EFP6_2_RESPONSE_OK)
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

    memcpy(buffer, usbOutBuffer, byte_length);
    
    return error;
}


// ------------------ API - Programmer - Public ------------------------------


int set_tck_frequency_cmd(uint8_t tckFreq)
{
    uint8_t payload_buffer[EFP6_2_PAYLOAD_ATOMIC_PACKET_LENGTH];
    memset(payload_buffer, 0, EFP6_2_PAYLOAD_ATOMIC_PACKET_LENGTH);

    payload_buffer[0] = EFP6_2_SET_TCK_FREQUENCY;
    payload_buffer[1] = tckFreq;

    return ConstructAndSendPacket(EFP6_2_SEND_PACKET_OPCODE, payload_buffer, EFP6_2_PAYLOAD_ATOMIC_PACKET_LENGTH, EFP6_2_TARGET_FREQUENCY_ADDRESS);
}


int eFP6_2_enumerate(void)
{
    struct hid_device_info* devs;
    uint32_t foundDevices = 0;

    devs = hid_enumerate(EFP6_2_MICROSEMI_VID, EFP6_2_PID);

    for(struct hid_device_info* cur_dev = devs; cur_dev; cur_dev = cur_dev->next) {
        foundDevices++;
        printf("Embedded FlashPro6 found (USB_ID=%04hx:%04hx path=%s serial_number=%ls release_number=%d manufacturer_string=%ls product_string=%ls)\r\n", cur_dev->vendor_id, cur_dev->product_id, cur_dev->path, cur_dev->serial_number, cur_dev->release_number, cur_dev->manufacturer_string, cur_dev->product_string);
        wcscpy(eFP6SerialNumber, cur_dev->serial_number);        
    }

    hid_free_enumeration(devs);

    if (foundDevices == 0)
    {
        printf("Warning: No eFP6 devices found\r\n");
        
    }
    return foundDevices;
}


int eFP6_2_open(wchar_t* wcPort)
{
    int error;

    memset(usbInBuffer,  0, EFP6_2_MAX_USB_BUFFER_BYTE_SIZE);
    memset(usbOutBuffer, 0, EFP6_2_MAX_USB_BUFFER_BYTE_SIZE);

    fp6Handle = hid_open(EFP6_2_MICROSEMI_VID, EFP6_2_PID, wcPort);
    if (!fp6Handle) {
        printf("Error: Unable to open device\r\n");
        return EFP6_2_RESPONSE_DEVICE_OPEN_ERROR;
    }

    error = hid_set_nonblocking(fp6Handle, 0);
    if (error != EFP6_2_RESPONSE_OK)
    {
        printf("\nError: hid_set_nonblocking failed.");
        return error;
    }

    
    return EFP6_2_RESPONSE_OK;
}


void eFP6_2_close(void)
{
    hid_close(fp6Handle);
}


int eFP6_2_getCm3Version(void)
{
    int error;

    error = ConstructAndSendPacket(EFP6_2_CM3_FW_VERSION_OPCODE, NULL, 0, 0);
    if (error != EFP6_2_RESPONSE_OK)
        printf("\nError: Failed to read firmware version number");
    else
        printf("\nCM3 Version: %X.%X\n", usbOutBuffer[1], usbOutBuffer[0]);

    return error;
}


int eFP6_2_setTckFrequency(uint8_t tckFreq)
{
    // TODO: test if it works correctly
    int error;
    if (tckFreq < 4 && tckFreq > 20)
    {
        printf("\nWarning: Frequency range is 4 MHz - 20 MHz. Setting frequency to 4MHz");
        tckFreq = 4;
    }
    error = set_tck_frequency_cmd(tckFreq);
    if (error == EFP6_2_RESPONSE_OK)
        error = ConstructAndSendPacket(EFP6_2_FREQUENCY_INTERRUPT_OPCODE, NULL, 0, 0);
    return error;
}


uint8_t eFP6_2_readTckFrequency(void)
{
    int error;
    
    error = ConstructAndSendPacket(EFP6_2_READ_TCK_OPCODE, NULL, 0, EFP6_2_TARGET_FREQUENCY_ADDRESS);

    if (error != EFP6_2_RESPONSE_OK)
    {
        printf("\nError: Failed to read tck");
        return 0;
    }

    return usbOutBuffer[5];
}


int eFP6_2_enableJtagPort(void)
{
    return ConstructAndSendPacket(EFP6_2_ENABLE_JTAG_PORT_OPCODE, NULL, 0, 0);
}


int eFP6_2_disableJtagPort(void)
{
    return ConstructAndSendPacket(EFP6_2_DISABLE_JTAG_PORT_OPCODE, NULL, 0, 0);
}


int eFP6_2_SetTrst(bool state)
{
    uint8_t payload_buffer[2] = { 0x0, 0x0 };

    if (state == true)
        payload_buffer[0] = EFP6_2_TRSTB_BIT;

    return ConstructAndSendPacket(EFP6_2_SET_JTAG_PINS_OPCODE, payload_buffer, EFP6_2_ATOMIC_JTAG_OPERATION_PACKET_LENGTH, 0);
}


int eFP6_2_setLed(uint8_t status)
{
    int error;
    uint8_t payload_buffer[2] = { 0x0, 0x0 };
    payload_buffer[0] = status; // bit2 is to enable toggling
    
    error = ConstructAndSendPacket(EFP6_2_TURN_ACTIVITY_LED_OFF, NULL, 0, 0);
    if (status != OFF)
        error = ConstructAndSendPacket(EFP6_2_TURN_ACTIVITY_LED_ON, payload_buffer, EFP6_2_ATOMIC_JTAG_OPERATION_PACKET_LENGTH, 0);

    return error;
}


int eFP6_2_jtagGotoState(uint16_t jtag_state)
{
    int error = EFP6_2_RESPONSE_OK;
    uint8_t payload_buffer[EFP6_2_ATOMIC_JTAG_OPERATION_PACKET_LENGTH];

    ((uint16_t*) payload_buffer)[0] = jtag_state;
    // payload_buffer[0] = (uint8_t)(jtag_state & 0xff);
    // payload_buffer[1] = (uint8_t)((jtag_state >> 8) & 0xff);

    error = AddPacketToUsbBuffer(EFP6_2_JTAG_ATOMIC_OPCODE, payload_buffer, EFP6_2_ATOMIC_JTAG_OPERATION_PACKET_LENGTH, 0, 0);
    if (error == EFP6_2_RESPONSE_OK)
    {
        if (jtag_state == EFP6_2_JTAG_RESET)
            error = FlushUsbBuffer();
    }
    return error;
}


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
    
    if (end_state == EFP6_2_JTAG_RESET)
        error = FlushUsbBuffer();

    if ((in != NULL) && (error == EFP6_2_RESPONSE_OK))
    {
        // Read back from the USB only if we are expecting to read the content
        error = retrieveData(in, bit_length);
    }

    return error;
}


// API - JTAG - Public
int eFP6_2_drScan(uint32_t bit_length, const uint8_t* out, uint8_t* in, uint16_t end_state)
{
    int error = EFP6_2_RESPONSE_OK;
    uint8_t payload_buffer[EFP6_2_ATOMIC_JTAG_OPERATION_PACKET_LENGTH];
    uint32_t BytesToTransmit;
    uint32_t PaddingBytes;

    ((uint16_t *)payload_buffer)[0] = bit_length;
    error = AddPacketToUsbBuffer(EFP6_2_SET_SHIFT_IR_DR_BIT_LENGTH_OPCODE, payload_buffer, EFP6_2_ATOMIC_JTAG_OPERATION_PACKET_LENGTH, 0, 0);
    
    if (error != EFP6_2_RESPONSE_OK) return error;

    PaddingBytes = 0;
    if (bit_length > 16)
    {
        BytesToTransmit = (uint32_t)((bit_length + 7) / 8);
        if (BytesToTransmit % 16)
        {
            PaddingBytes = (16 - BytesToTransmit % 16);
        }
        error = AddPacketToUsbBuffer(EFP6_2_SHIFT_DATA_FROM_DDR, out, BytesToTransmit, PaddingBytes, 0);

        if (bytesInBuffer % EFP6_2_PACKET_OFFSET)
        {
            // printf("have to align padding \r\n");
            bytesInBuffer += (EFP6_2_PACKET_OFFSET - bytesInBuffer % EFP6_2_PACKET_OFFSET);
        }        
    }
    else
    {
        error = AddPacketToUsbBuffer(EFP6_2_SHIFT_DATA_FROM_REGISTER, out, 2, 0, 0);
    }

    if (error != EFP6_2_RESPONSE_OK) return error;

    payload_buffer[0] = EFP6_2_JTAG_SHIFT_DR;
    payload_buffer[1] = 0;
    error = AddPacketToUsbBuffer(EFP6_2_JTAG_ATOMIC_OPCODE, payload_buffer, EFP6_2_ATOMIC_JTAG_OPERATION_PACKET_LENGTH, 0, 0);
    if (error == EFP6_2_RESPONSE_OK)
    {
        error = eFP6_2_jtagGotoState(end_state);
    }

    if ((in != NULL) && (error == EFP6_2_RESPONSE_OK))
    {
        error = retrieveData(in, bit_length);
    }

    return error;
}

