#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "stm32f4xx.h"
#include "board.h"
#include "bl_usart.h"
#include "tim_delay.h"

#define PACKET_SIZE_MAX 4096

typedef enum 
{
    PACKET_STATE_HEADER,
    PACKET_STATE_OPCODE,
    PACKET_STATE_LENGTH,
    PACKET_STATE_PAYLOAD,
} packet_state_machine_t;

typedef enum
{
    PACKET_OPCODE_ERASE = 0x01,
    PACKET_OPCODE_PROGRAM = 0x02,
    PACKET_OPCODE_VERIFY  = 0x03,
    PACKET_OPCODE_BOOT = 0x04,
} packet_opcode_t;

typedef enum
{
    PACKET_ERRCODE_OK = 0,
    PACKET_ERRCODE_OPCODE,
    PACKET_ERRCODE_OVERFLOW,
    PACKET_ERRCODE_TIMEOUT,
    PACKET_ERRCODE_FORMAT,
    PACKET_ERRCODE_VERIFY,
    PACKET_ERRCODE_PARAM,
    PACKET_ERRCODE_UNKNOWN = 0xff,
} packet_errcode_t;

static uint8_t packet_buffer[PACKET_SIZE_MAX];
static uint32_t packet_index;
static packet_state_machine_t packet_state = PACKET_STATE_HEADER;
static packet_opcode_t packet_opcode;
static uint16_t packet_payload_length;
static bool packet_received;

static void bl_rx_handler(const uint8_t *data, uint32_t size)
{
    while (size--)
    {
        // printf("recv: %02X\n", *data);
        packet_buffer[packet_index++] = *data++;
        switch (packet_state)
        {
            case PACKET_STATE_HEADER:
                if (packet_buffer[0] == 0xAA)
                {
                    // printf("header ok\n");
                    packet_state = PACKET_STATE_OPCODE;
                }
                else
                {
                    packet_index = 0;
                    packet_state = PACKET_STATE_HEADER;
                }
                break;

            case PACKET_STATE_OPCODE:
                if (packet_buffer[1] == PACKET_OPCODE_ERASE ||
                    packet_buffer[1] == PACKET_OPCODE_PROGRAM ||
                    packet_buffer[1] == PACKET_OPCODE_VERIFY ||
                    packet_buffer[1] == PACKET_OPCODE_BOOT)
                {
                    // printf("opcode ok: %02X\n", packet_buffer[1]);
                    packet_opcode = (packet_opcode_t)packet_buffer[1];
                    packet_state = PACKET_STATE_LENGTH;
                }
                else
                {
                    packet_index = 0;
                    packet_state = PACKET_STATE_HEADER;
                }
                break;

            case PACKET_STATE_LENGTH:
                if (packet_index == 4)
                {
                    uint16_t payload_length = (packet_buffer[3] << 8) | packet_buffer[2];
                    if (payload_length <= PACKET_SIZE_MAX - 4)
                    {
                        // printf("length ok: %u\n", payload_length);
                        packet_payload_length = payload_length;
                        packet_state = PACKET_STATE_PAYLOAD;
                    }
                    else
                    {
                        packet_index = 0;
                        packet_state = PACKET_STATE_HEADER;
                    }
                }
                break;

            case PACKET_STATE_PAYLOAD:
                if (packet_index == 4 + packet_payload_length)
                {
                    // printf("payload ok\n");
                    packet_received = true;
                    packet_index = 0;
                    packet_state = PACKET_STATE_HEADER;
                }
                break;

            default:
                break;
        }
    }
}

void bootloader_main(void)
{
    printf("Bootloader started.\n");

    bl_usart_init();
    bl_usart_register_rx_callback(bl_rx_handler);

    while (1)
    {
        if (packet_received)
        {
            printf("packet received: opcode=%02X, length=%u\n", packet_opcode, packet_payload_length);
            printf("payload: ");
            for (uint32_t i = 0; i < packet_payload_length; i++)
            {
                printf("%02X ", packet_buffer[4 + i]);
            }
            printf("\n");
            packet_received = false;
        }
    }
}

