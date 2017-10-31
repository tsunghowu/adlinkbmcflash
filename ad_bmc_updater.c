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

extern uint8_t  g_pui8Buffer[256];
//@bmcflash.exe cSL2v9.bin -a 0x50 -p 0x2000 -s 0x1c -r 0x2004 -c 1
//
static uint32_t g_ui32DownloadAddress = 0x2000;
static uint32_t g_ui32StartAddress = 0x2004;
uint32_t g_BlockTransferSize = 0x1c;

int32_t RunBMCUpdater(FILE *hApplFile)	//Application only
{
    // Only program application part.

    //
    // Jump to the boot loader.
    //
    g_pui8Buffer[0] = COMMAND_ENTER_BOOTLOADER;
    if(EnterBootloader(g_pui8Buffer, 1) < 0)
    {
        return(-1);
    }

	if(UpdateFlash(hApplFile, 0, g_ui32DownloadAddress) < 0)
    {
        return(-1);
    }

    //
    // If a start address was specified then send the run command to the
    // boot loader.
    //
    if(g_ui32StartAddress != 0xffffffff)
    {
        //
        // Send the run command but just send the packet, there will likely
        // be no boot loader to answer after this command completes.
        //
        g_pui8Buffer[0] = COMMAND_RUN;
        g_pui8Buffer[1] = (uint8_t)(g_ui32StartAddress>>24);
        g_pui8Buffer[2] = (uint8_t)(g_ui32StartAddress>>16);
        g_pui8Buffer[3] = (uint8_t)(g_ui32StartAddress>>8);
        g_pui8Buffer[4] = (uint8_t)g_ui32StartAddress;
        SendPacket(g_pui8Buffer, 5, 1);
        msg_pinfo("Running from address %08x\n",g_ui32StartAddress);
    }
    else
    {
        //
        // Send the reset command but just send the packet, there will likely
        // be no boot loader to answer after this command completes.
        //
        g_pui8Buffer[0] = COMMAND_RESET;
        SendPacket(g_pui8Buffer, 1, 1);
        msg_pinfo("Send Reset command\n");
    }
    if(hApplFile != 0)
    {
        fclose(hApplFile);
    }
    msg_pinfo("Successfully downloaded to device.\n");
    return(0);	
}

