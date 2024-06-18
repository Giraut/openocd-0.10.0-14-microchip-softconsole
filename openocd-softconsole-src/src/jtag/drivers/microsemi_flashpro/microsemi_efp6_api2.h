#ifndef EFP6_API2
#define EFP6_API2

#include <stdint.h>

#define EFP6_2_MICROSEMI_VID            0x1514
#define EFP6_2_PID                      0x200b
#define EFP6_2_MAX_USB_BUFFER_BYTE_SIZE 1024
#define EFP6_2_MAX_BITS_TO_SHIFT        4096

#define EFP6_2_SET_TCK_FREQUENCY  0
#define EFP6_2_READ_TCK_FREQUENCY 1

// Atomic operation opcodes
#define EFP6_2_JTAG_RESET     15 // 0xF
#define EFP6_2_JTAG_IDLE      12 // 0xC
#define EFP6_2_JTAG_SHIFT_DR  2  // 0x2
#define EFP6_2_JTAG_PAUSE_DR  3  // 0x3
#define EFP6_2_JTAG_UPDATE_DR 5  // 0x5
#define EFP6_2_JTAG_SHIFT_IR  10 // 0xA
#define EFP6_2_JTAG_PAUSE_IR  11 // 0xB
#define EFP6_2_JTAG_UPDATE_IR 13 // 0xD

// Programmer opcodes
#define EFP6_2_PACKET_START_CODE          0x01
#define EFP6_2_SEND_PACKET_OPCODE         0x11
#define EFP6_2_READ_TCK_OPCODE            0x25
#define EFP6_2_FREQUENCY_INTERRUPT_OPCODE 0x30

#define EFP6_2_INSERT_WAIT_CYCLES_OPCODE         0x31
#define EFP6_2_ENABLE_JTAG_PORT_OPCODE           0x32
#define EFP6_2_DISABLE_JTAG_PORT_OPCODE          0x33
#define EFP6_2_SET_JTAG_PINS_OPCODE              0x34
#define EFP6_2_GET_JTAG_PINS_OPCODE              0x35
#define EFP6_2_JTAG_ATOMIC_OPCODE                0x2A
#define EFP6_2_SET_SHIFT_IR_DR_BIT_LENGTH_OPCODE 0x2C
#define EFP6_2_READ_TDO_BUFFER_COMMAND_OPCODE    0x2F
#define EFP6_2_SHIFT_DATA_FROM_REGISTER          0x2D
#define EFP6_2_SHIFT_DATA_FROM_DDR               0x2E

#define EFP6_2_TURN_ACTIVITY_LED_ON  0x41
#define EFP6_2_TURN_ACTIVITY_LED_OFF 0x42

#define EFP6_2_CM3_FW_VERSION_OPCODE 0xC0

// Discrete JTAG pins
#define EFP6_2_TDI_BIT   0x1
#define EFP6_2_TMS_BIT   0x2
#define EFP6_2_TRSTB_BIT 0x4

// num packets in buffer 32-bit, it's only for few packets in the buffer, but 
//             32-bit will make the packets aligned with the 32-bit boundary 
//             and make things such as DMA easier
//
// Packet start 16-bit
// Packet type (opcode) 16-bit
// Address high 16-bit
// Address low  16-bit
// Length  high 16-bit
// Length  low  16-bit
// Payload (what ever size is specified in the Length entry in bytes)
// CRC          16-bit
//
// Repeat for all other packets in the buffer


typedef struct __attribute__((__packed__))
{
    uint16_t packetStart;
    uint16_t packetType;
    uint32_t address;      // not correct endiness but because it will be used with address 0 it's ok
    uint16_t lenHigh;
    uint16_t lenLow;      // not correct endiness but because these are numbers under 16-bits, it's ok

    uint8_t data[2];
    uint16_t crc;
} efp6_2_packet_atomic;

typedef struct 
{
    efp6_2_packet_atomic setLen;
    efp6_2_packet_atomic data;
    efp6_2_packet_atomic shift;
    efp6_2_packet_atomic endState;
} efp6_2_IR_scan;

#define EFP6_2_PACKET_OFFSET                  4    // first packet starts after the Number Packets In The Buffer entry, so this is the size of that entry
#define EFP6_2_PACKET_HEADER_OPCODE_OFFSET    2    // offset where the IN the packet is the opcode (it's after packet start which is 16-bit)
#define EFP6_2_PACKET_HEADER_PREAMBLE_LENGTH  (10 + EFP6_2_PACKET_HEADER_OPCODE_OFFSET)   // size of start + type + address + payload_len
#define EFP6_2_PACKET_HEADER_POSTAMBLE_LENGTH 2    // size of CRC
#define EFP6_2_PACKET_HEADER_TOTAL_LENGTH	  (EFP6_2_PACKET_HEADER_PREAMBLE_LENGTH + EFP6_2_PACKET_HEADER_POSTAMBLE_LENGTH)

#define EFP6_2_PAYLOAD_ATOMIC_PACKET_LENGTH        16
#define EFP6_2_ATOMIC_JTAG_OPERATION_PACKET_LENGTH 2

#define EFP6_2_TARGET_FREQUENCY_ADDRESS 0xA0FFFFF0

#define EFP6_2_RESPONSE_PACKET_SIZE_ERROR -100
#define EFP6_2_RESPONSE_DEVICE_OPEN_ERROR -101
#define EFP6_2_RESPONSE_BITS_SIZE_ERROR   -102
#define EFP6_2_RESPONSE_OK                0

typedef enum {
    EFP6_2_OFF         = 0,
    EFP6_2_SOLID_GREEN = 1,
    EFP6_2_SOLID_RED   = 2,
    EFP6_2_BLINK_GREEN = 5,
    EFP6_2_BLINK_RED   = 6
} EFP6_2_color;

#endif