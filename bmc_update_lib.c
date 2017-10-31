/*
 * This file is part of the flashrom project.
 *
 * Copyright (C) 2014 Alexandre Boeglin <alex@boeglin.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include "flash.h"
#include "bmc_update_lib.h"

extern int32_t I2CSendData(uint8_t const *pui8Data, uint8_t ui8Size);
extern int32_t I2CReceiveData(uint8_t *pui8Data, uint8_t ui8Size);
extern int32_t I2CEnterBootloader(uint8_t *pui8Command, uint8_t ui8Size);

extern void delay(uint32_t mills);
extern uint32_t g_BlockTransferSize;

uint8_t  g_pui8Buffer[256];
uint32_t g_ui32FileLength;

//****************************************************************************
//
//! EnterBootloader() sends a command to the serial boot loader.
//!
//! \param pui8Command is the unformatted command to send to the device.
//! \param ui8Size is the size, in bytes, of the command to be sent.
//!
//! This function will send a command to the device and read back the status
//! code from the device to see if the command completed successfully.
//!
//! \return If any part of the function fails, the function will return a
//!     negative error code.  The function will return 0 to indicate success.
//
//****************************************************************************
int32_t
EnterBootloader(uint8_t *pui8Command, uint8_t ui8Size)
{
    uint8_t ui8Status;
	if(I2CEnterBootloader(pui8Command, ui8Size)<0) {
		msg_pinfo("Failed to Enter Bootloader FRU Mode\n");
        return(-1);
	}
    //
    // Send the get status command to tell the device to return status to
    // the host.
    //
 
    ui8Status = COMMAND_GET_STATUS;
    if(SendPacket(&ui8Status, 1, 1) < 0)
    {
        msg_pinfo("Failed to Get Bootloader Status\n");
        return(-1);
    }
    //
    // Read back the status provided from the device.
    //
    ui8Size = sizeof(ui8Status);
    if(GetPacket(&ui8Status, &ui8Size) < 0)
    {

        msg_pinfo("Failed to Get Bootloader Packet\n");
        return(-1);
    }
    return(0);
}

//****************************************************************************
//
//! SendCommand() sends a command to the serial boot loader.
//!
//! \param pui8Command is the properly formatted serial flash loader command to
//!     send to the device.
//! \param ui8Size is the size, in bytes, of the command to be sent.
//!
//! This function will send a command to the device and read back the status
//! code from the device to see if the command completed successfully.
//!
//! \return If any part of the function fails, the function will return a
//!     negative error code.  The function will return 0 to indicate success.
//
//****************************************************************************
int32_t
SendCommand(uint8_t *pui8Command, uint8_t ui8Size)
{
    uint8_t ui8Status;

    //
    // Send the command itself.
    //
    if(SendPacket(pui8Command, ui8Size, 1) < 0)
    {
        return(-1);
    }

    //
    // Send the get status command to tell the device to return status to
    // the host.
    //
    ui8Status = COMMAND_GET_STATUS;
    if(SendPacket(&ui8Status, 1, 1) < 0)
    {
        msg_pinfo("\nFailed to Get Status");
        return(-1);
    }

    //
    // Read back the status provided from the device.
    //
    ui8Size = sizeof(ui8Status);
    if(GetPacket(&ui8Status, &ui8Size) < 0)
    {
        msg_pinfo("\nFailed to Get Packet");
        return(-1);
    }
    if(ui8Status != COMMAND_RET_SUCCESS)
    {
        msg_pinfo("\nCommand fails with return code: %04x",ui8Status);
        return(-1);
    }
    return(0);
}

//*****************************************************************************
//
//! UpdateFlash() programs data to the flash.
//!
//! \param hFile is an open file pointer to the binary data to program into the
//!     flash as the application.
//! \param hBootFile is an open file pointer to the binary data for the
//!     boot loader binary.  This will be programmed at offset zero.
//! \param ui32Address is address to start programming data to the falsh.
//!
//! This routine handles the commands necessary to program data to the flash.
//! If hFile should always have a value if hBootFile also has a valid value.
//! This function will concatenate the two files in memory to reduce the number
//! of flash erases that occur when both the boot loader and the application
//! are being updated.
//!
//! \return This function either returns a negative value indicating a failure
//!     or zero if the update was successful.
//
//*****************************************************************************
int32_t
UpdateFlash(FILE *hFile, FILE *hBootFile, uint32_t ui32Address)
{
    uint32_t ui32BootFileLength;
    uint32_t ui32TransferStart;
    uint32_t ui32TransferLength;
    uint8_t *pui8FileBuffer;
    uint8_t fsegment = 0;                     /* actual processed 32kB file segment */
    uint32_t ui32FileBufferLength = (FILE_BUFFER_LENGTH / g_BlockTransferSize) * g_BlockTransferSize;
    uint32_t ui32Offset;
    uint32_t TotalLength;

    //
    // At least one file must be specified.
    //
    if(hFile == 0)
    {
        return(-1);
    }

    //
    // Get the file sizes.
    //
    fseek(hFile, 0, SEEK_END);
    g_ui32FileLength = ftell(hFile);
    fseek(hFile, 0, SEEK_SET);
    /*
    if((g_pcFilename == g_pcBootLoadName) && g_ui32FileLength > 0x2000)
    {
        msg_pinfo("Bootloader file is too big\n");
        return(-1);
    }
    */

    //
    // Default the transfer length to be the size of the application.
    //
    ui32TransferLength = g_ui32FileLength;
    ui32TransferStart = ui32Address;

    if(hBootFile)
    {
        fseek(hBootFile, 0, SEEK_END);
        ui32BootFileLength = ftell(hBootFile);
        fseek(hBootFile, 0, SEEK_SET);

        if(ui32BootFileLength != 0x2000)
        {
            msg_pinfo("Wrong Bootloader file size (need exactly 8192 bytes).\n");
            return(-1);
        }

        ui32TransferLength = ui32Address + g_ui32FileLength;
        ui32TransferStart = 0;
    }
    else if(g_ui32FileLength == 0x2000 && ui32Address != 0x0000)
    {
        msg_pinfo("Bootloader file must be programmed with -l option.\n");
        return(-1);
    }

    pui8FileBuffer = malloc(ui32FileBufferLength);
    if(pui8FileBuffer == 0)
    {
        msg_pinfo("No Memory to allocate Buffer.\n");
        return(-1);
    }

    if(hBootFile)
    {
        if(fread(pui8FileBuffer, sizeof(uint8_t), ui32BootFileLength, hBootFile) !=
            ui32BootFileLength)
        {
            return(-1);
        }

        if(ui32Address < ui32BootFileLength)
        {
            return(-1);
        }

        //
        // Pad the unused code space with 0xff to have all of the flash in
        // a known state.
        //
        memset(&pui8FileBuffer[ui32BootFileLength], 0xff,
            ui32Address - ui32BootFileLength);

        //
        // Append the application to the boot loader image.
        //
        if(!fread(&pui8FileBuffer[ui32Address], sizeof(uint8_t), ui32FileBufferLength-ui32Address, hFile))
        {
            return(-1);
        }
    }
    else
    {
        //
        // Just read in one image (bootloader or application)
        //
        if(!fread(pui8FileBuffer, sizeof(uint8_t), ui32FileBufferLength, hFile))
        {
            return(-1);
        }
    }

    //
    // Build up the download command and send it to the board.
    //
    g_pui8Buffer[0] = COMMAND_DOWNLOAD;
    g_pui8Buffer[1] = (uint8_t)(ui32TransferStart >> 24);
    g_pui8Buffer[2] = (uint8_t)(ui32TransferStart >> 16);
    g_pui8Buffer[3] = (uint8_t)(ui32TransferStart >> 8);
    g_pui8Buffer[4] = (uint8_t)ui32TransferStart;
    g_pui8Buffer[5] = (uint8_t)(ui32TransferLength>>24);
    g_pui8Buffer[6] = (uint8_t)(ui32TransferLength>>16);
    g_pui8Buffer[7] = (uint8_t)(ui32TransferLength>>8);
    g_pui8Buffer[8] = (uint8_t)ui32TransferLength;
    if(SendCommand(g_pui8Buffer, 9) < 0)
    {
        msg_pinfo("\nFailed to Send Download Command\n");
        msg_pinfo("Flash might be erased\n");
        return(-1);
    }
    else
    {
        msg_pinfo("Flash erased\n");
    }

    ui32Offset = 0;
    TotalLength = ui32TransferLength;

    msg_pinfo("Remaining Bytes: ");
    do
    {
        uint8_t ui8BytesSent;

        g_pui8Buffer[0] = COMMAND_SEND_DATA;

        msg_pinfo("%08d (%02d%%)", ui32TransferLength, (TotalLength-ui32TransferLength)*100/TotalLength);

        //
        // Send out 8 bytes at a time to throttle download rate and avoid
        // overruning the device since it is programming flash on the fly.
        //
        if(ui32TransferLength >= g_BlockTransferSize)
        {
            memcpy(&g_pui8Buffer[1], &pui8FileBuffer[ui32Offset-fsegment*ui32FileBufferLength], g_BlockTransferSize);
            ui32Offset += g_BlockTransferSize;
            ui32TransferLength -= g_BlockTransferSize;
            ui8BytesSent = g_BlockTransferSize + 1;
        }
        else
        {
            memcpy(&g_pui8Buffer[1], &pui8FileBuffer[ui32Offset-fsegment*ui32FileBufferLength], ui32TransferLength);
            ui32Offset += ui32TransferLength;
            ui8BytesSent = ui32TransferLength + 1;
            ui32TransferLength = 0;
        }
        //
        // Send the Send Data command to the device.
        //
        if(SendCommand(g_pui8Buffer, ui8BytesSent) < 0)
        {
            msg_pinfo("\nFailed to Send Packet data\n");
            return(-1);
        }

        // Read next 32k bytes
        if(ui32Offset + g_BlockTransferSize > ui32FileBufferLength*(fsegment+1))
        {
            if(fread(pui8FileBuffer, sizeof(uint8_t), ui32FileBufferLength, hFile) )
                fsegment++;
        }
        
        msg_pinfo("\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
    } while (ui32TransferLength);
    msg_pinfo("00000000 (100%%)\n\r");

    if(pui8FileBuffer)
    {
        free(pui8FileBuffer);
    }
    return(0);
}










//****************************************************************************
//
// local declarations
//
//****************************************************************************
uint8_t CheckSum(uint8_t *pui8Data, uint8_t ui8Size);

//****************************************************************************
//
//! AckPacket() sends an Acknowledge a packet.
//!
//! This function acknowledges a packet has been received from the device.
//!
//! \return The function returns zero to indicated success while any non-zero
//! value indicates a failure.
//
//****************************************************************************
int32_t
AckPacket(void)
{
    uint8_t ui8Ack;

    ui8Ack = COMMAND_ACK;
    return(I2CSendData(&ui8Ack, 1));
}

//****************************************************************************
//
//! NakPacket() sends a No Acknowledge packet.
//!
//! This function sends a no acknowledge for a packet that has been
//! received unsuccessfully from the device.
//!
//! \return The function returns zero to indicated success while any non-zero
//! value indicates a failure.
//
//****************************************************************************
int32_t
NakPacket(void)
{
    uint8_t ui8Nak;

    ui8Nak = COMMAND_NAK;
    return(I2CSendData(&ui8Nak, 1));
}

//*****************************************************************************
//
//! GetPacket() receives a data packet.
//!
//! \param pui8Data is the location to store the data received from the device.
//! \param pui8Size is the number of bytes returned in the pui8Data buffer that
//! was provided.
//!
//! This function receives a packet of data from UART port.
//!
//! \returns The function returns zero to indicated success while any non-zero
//! value indicates a failure.
//
//*****************************************************************************
int32_t
GetPacket(uint8_t *pui8Data, uint8_t *pui8Size)
{
    uint8_t ui8CheckSum;
    uint8_t ui8Size;

    //
    // Get the size and the checksum.
    //
    do
    {
        if(I2CReceiveData(&ui8Size, 1))
        {
            return(-1);
        }
    }
    while(ui8Size == 0);

    if(I2CReceiveData(&ui8CheckSum, 1))
    {
        return(-1);
    }
    *pui8Size = ui8Size - 2;

    if(I2CReceiveData(pui8Data, *pui8Size))
    {
        *pui8Size = 0;
        return(-1);
    }

    //
    // Calculate the checksum from the data.
    //
    if(CheckSum(pui8Data, *pui8Size) != ui8CheckSum)
    {
        *pui8Size = 0;
        return(NakPacket());
    }

    return(AckPacket());
}

//*****************************************************************************
//
//! CheckSum() Calculates an 8 bit checksum
//!
//! \param pui8Data is a pointer to an array of 8 bit data of size ui8Size.
//! \param ui8Size is the size of the array that will run through the checksum
//!     algorithm.
//!
//! This function simply calculates an 8 bit checksum on the data passed in.
//!
//! \return The function returns the calculated checksum.
//
//*****************************************************************************
uint8_t
CheckSum(uint8_t *pui8Data, uint8_t ui8Size)
{
    int32_t i;
    uint8_t ui8CheckSum;

    ui8CheckSum = 0;

    for(i = 0; i < ui8Size; ++i)
    {
        ui8CheckSum += pui8Data[i];
    }
    return(ui8CheckSum);
}

//*****************************************************************************
//
//! SendPacket() sends a data packet.
//!
//! \param pui8Data is the location of the data to be sent to the device.
//! \param ui8Size is the number of bytes to send from puData.
//! \param bAck is a boolean that is true if an ACK/NAK packet should be
//! received in response to this packet.
//!
//! This function sends a packet of data to the device.
//!
//! \returns The function returns zero to indicated success while any non-zero
//!     value indicates a failure.
//
//*****************************************************************************
int32_t
SendPacket(uint8_t *pui8Data, uint8_t ui8Size, uint8_t bAck)
{
    uint8_t ui8CheckSum;
    uint32_t ui32Ack;

    ui8CheckSum = CheckSum(pui8Data, ui8Size);

    //
    // Make sure that we add the bytes for the size and checksum to the total.
    //
    ui8Size += 2;
    //
    // Send the Size in bytes.
    //
    if(I2CSendData(&ui8Size, 1))
    {
        return(-1);
    }
    //
    // Send the CheckSum
    //
    if(I2CSendData(&ui8CheckSum, 1))
    {
        return(-1);
    }
    //
    // Now send the remaining bytes out.
    //
    ui8Size -= 2;

    //
    // Send the Data
    //
    if(I2CSendData(pui8Data, ui8Size))
    {
        return(-1);
    }

    //
    // Return immediately if no ACK/NAK is expected.
    //
    if(!bAck)
    {
        return(0);
    }
    //
    // Wait for the acknowledge from the device.
    //
    do
    {
        if(pui8Data[0]==COMMAND_DOWNLOAD)
        {
            // wait 9ms for each block to erase in Flash
            delay((g_ui32FileLength/0x400 + 1)*9);
        }
        if(I2CReceiveData((uint8_t*)&ui32Ack, 1))
        {
            return(-1);
        }
    }    
    while(ui32Ack == 0);
    if((uint8_t)(ui32Ack>>8) != COMMAND_ACK)
    {
        return(-1);
    }
    return(0);
}
