#ifndef __BMC_UPDATE_LIB__
#define __BMC_UPDATE_LIB__ 1

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define COMMAND_PING                0x20
#define COMMAND_DOWNLOAD            0x21
#define COMMAND_RUN                 0x22
#define COMMAND_GET_STATUS          0x23
#define COMMAND_SEND_DATA           0x24
#define COMMAND_RESET               0x25

#define COMMAND_RET_SUCCESS         0x40
#define COMMAND_RET_UNKNOWN_CMD     0x41
#define COMMAND_RET_INVALID_CMD     0x42
#define COMMAND_RET_INVALID_ADDR    0x43
#define COMMAND_RET_FLASH_FAIL      0x44
#define COMMAND_ACK                 0xcc
#define COMMAND_NAK                 0x33

#define COMMAND_ENTER_BOOTLOADER    0x51

#define FILE_BUFFER_LENGTH    0x8000   /* 32kB */

int32_t AckPacket(void);
int32_t NakPacket(void);
int32_t GetPacket(uint8_t *pui8Data, uint8_t *pui8Size);
int32_t SendPacket(uint8_t *pui8Data, uint8_t ucSize, uint8_t bAck);
int32_t SendCommand(uint8_t *pui8Command, uint8_t ui8Size);

int32_t UpdateFlash(FILE *hFile, FILE *hBootFile, uint32_t ui32Address);
int32_t EnterBootloader(uint8_t *pui8Command, uint8_t ui8Size);

#endif