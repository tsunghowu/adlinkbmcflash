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

static int i2cbmc_fd;
static int i2cbmc_addr;

int programmer_init(const char *param);
char *extract_programmer_param(const char *param_name);

extern int32_t RunBMCUpdater (FILE* image);

int32_t I2CSendData(uint8_t const *pui8Data, uint8_t ui8Size);
int32_t I2CReceiveData(uint8_t *pui8Data, uint8_t ui8Size);
void delay(uint32_t mills);

/* udelay.c */
void myusec_delay(unsigned int usecs);
void myusec_calibrate_delay(void);
void internal_sleep(unsigned int usecs);
void internal_delay(unsigned int usecs);

/* Returns 0 upon success, a negative number upon errors. */
int sema_bmc_update_main(
		const char* filename, 
		uint8_t read_it, 
		uint8_t write_it, 
		uint8_t erase_it, 
		uint8_t verify_it )
{
	FILE *image;
	int ret = 0;

	if ((image = fopen(filename, "rb")) == NULL) {
		msg_pwarn("Error: opening file \"%s\" failed: %s\n", filename, strerror(errno));
		return -1;
	}

	// Get device, address from command-line
	// Example: flashrom -p dev=/dev/device:address.
	char *i2c_device = extract_programmer_param("dev");
	msg_pwarn("Warn: %s\n",i2c_device );
	if (i2c_device != NULL && strlen(i2c_device) > 0) {
		char *i2c_address = strchr(i2c_device, ':');
		if (i2c_address != NULL) {
			*i2c_address = '\0';
			i2c_address++;
		}
		if (i2c_address == NULL || strlen(i2c_address) == 0) {
			msg_perr("Error: no address specified.\n"
				 "Use flashrom -p i2c:dev=/dev/device:address.\n");
			ret = -1;
			goto out;
		}
		i2cbmc_addr = strtol(i2c_address, NULL, 16); // FIXME: error handling
	} else {
		msg_perr("Error: no device specified.\n"
			 "Use flashrom -p i2c:dev=/dev/device:address.\n");
		ret = -1;
		goto out;
	}
	msg_pinfo("Info: Will try to use device %s and address 0x%02x.\n", i2c_device, i2cbmc_addr);

//	msg_pinfo("Info: Will %sreset the device at the end.\n", i2cbmc_doreset ? "" : "NOT ");

	// Open device
	if ((i2cbmc_fd = open(i2c_device, O_RDWR)) < 0) {
		switch (errno) {
		case EACCES:
			msg_perr("Error opening %s: Permission denied.\n"
				 "Please use sudo or run as root.\n",
				 i2c_device);
			break;
		case ENOENT:
			msg_perr("Error opening %s: No such file.\n"
				 "Please check you specified the correct device.\n",
				 i2c_device);
			break;
		default:
			msg_perr("Error opening %s: %s.\n", i2c_device, strerror(errno));
		}
		ret = -1;
		goto out;
	}
	// Set slave address
	if (ioctl(i2cbmc_fd, I2C_SLAVE, i2cbmc_addr) < 0) {
		msg_perr("Error setting slave address 0x%02x: errno %d.\n",
			 i2cbmc_addr, errno);
		ret = -1;
		goto out;
	}

	{
		uint8_t buffer[4];
		int32_t status;
		status = i2c_smbus_read_block_data(i2cbmc_fd, 0x28, buffer);
		msg_pinfo("status is %x\n", status);
		msg_pwarn("Buffer: %x-%x-%x-%x\n", buffer[0],buffer[1],buffer[2],buffer[3]);
	}	

	RunBMCUpdater(image);
	/*
	int i = 700;
	msg_pwarn("Time starts\n");
	while(i--)
		delay(10);
	msg_pwarn("Time ends\n");
*/

	if (close(i2cbmc_fd) < 0) {
		msg_perr("Error closing device: errno %d.\n", errno);
		return -1;
	}
out:
	free(i2c_device);
	return ret;
}

/*
  __s32 i2c_smbus_read_block_data(int file, __u8 command, __u8 *values);
  __s32 i2c_smbus_write_block_data(int file, __u8 command, __u8 length,  __u8 *values);
*/
int32_t
I2CSendData(uint8_t const *pui8Data, uint8_t ui8Size)
{
    int32_t status;

	status = i2c_smbus_write_block_data(i2cbmc_fd, 0x21, ui8Size, pui8Data);
    if(status>=0)  // Bytes send
    {
      return(0);
    }

    return(-1);
}

//*****************************************************************************
//
//! I2CReceiveData() receives data over a UART port.
//!
//! \param pui8Data is the buffer to read data into from the UART port.
//! \param ui8Size is the number of bytes provided in pui8Data buffer that should
//!     be written with data from the UART port.
//!
//! This function reads back ui8Size bytes of data from the UART port, that was
//! opened by a call to initI2C(), into the buffer that is pointed to by
//! pui8Data.
//!
//! \return This function returns zero to indicate success while any non-zero
//!     value indicates a failure.
//
//*****************************************************************************
int32_t
I2CReceiveData(uint8_t *pui8Data, uint8_t ui8Size)
{
	int32_t status;
	uint8_t smbusBuffer[32];
	status = i2c_smbus_read_block_data(i2cbmc_fd, 0xFF, smbusBuffer);
	if(status < 0)
		return (-1);
	else {
		uint8_t i;
		for(i=0;i<status;i++){
			pui8Data[i] = smbusBuffer[i];
		}
	}
    return(0);
}


void delay(uint32_t mills) 
{
	internal_delay(mills*1000);
}
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
I2CEnterBootloader(uint8_t *pui8Command, uint8_t ui8Size)
{
    int32_t Status;
    
    if(ui8Size==1) {
		Status = i2c_smbus_write_block_data(i2cbmc_fd, pui8Command[0], 0, NULL);
    } else {
    	Status = i2c_smbus_write_block_data(i2cbmc_fd, pui8Command[0], ui8Size--, &pui8Command[1]);
    }

	if(Status<0)  //
    {
        msg_pinfo("Failed to send ENTER_BOOTLOADER command\n");
        return(-1);
    }
    delay(400); // wait for Application exit & Bootloader start

    return(0);
}
static void cli_classic_abort_usage(void)
{
	msg_pinfo("Please run \"flashrom --help\" for usage info.\n");
	exit(1);
}

static int check_filename(char *filename, char *type)
{
	if (!filename || (filename[0] == '\0')) {
		fprintf(stderr, "Error: No %s file specified.\n", type);
		return 1;
	}
	/* Not an error, but maybe the user intended to specify a CLI option instead of a file name. */
	if (filename[0] == '-')
		fprintf(stderr, "Warning: Supplied %s file name starts with -\n", type);
	return 0;
}

int main(int argc, char *argv[])
{
	const char *name;
	int namelen;
	int opt;
	int operation_specified = 0, option_index = 0;
	int read_it = 0, erase_it = 0,write_it = 0, verify_it = 0;
	int dont_verify_it = 0;
	int ret = 0;

	static const char optstring[] = "r:Rw:v:nVEfc:l:i:p:Lzho:";
	static const struct option long_options[] = {
		{"read",		1, NULL, 'r'},
		{"write",		1, NULL, 'w'},
		{"erase",		0, NULL, 'E'},
		{"verify",		1, NULL, 'v'},
		{"programmer",		1, NULL, 'p'},
		{NULL,			0, NULL, 0},
		/*
		{"noverify",		0, NULL, 'n'},
		{"chip",		1, NULL, 'c'},
		{"verbose",		0, NULL, 'V'},
		{"force",		0, NULL, 'f'},
		{"layout",		1, NULL, 'l'},
		{"image",		1, NULL, 'i'},
		{"list-supported",	0, NULL, 'L'},
		{"list-supported-wiki",	0, NULL, 'z'},
		{"programmer",		1, NULL, 'p'},
		{"help",		0, NULL, 'h'},
		{"version",		0, NULL, 'R'},
		{"output",		1, NULL, 'o'},
		*/
	};

	char *filename = NULL;
	char *layoutfile = NULL;
	char *pparam = NULL;

	setbuf(stdout, NULL);
	/* FIXME: Delay all operation_specified checks until after command
	 * line parsing to allow --help overriding everything else.
	 */
	while ((opt = getopt_long(argc, argv, optstring,
				  long_options, &option_index)) != EOF) {
		switch (opt) {
		case 'r':
			if (++operation_specified > 1) {
				fprintf(stderr, "More than one operation "
					"specified. Aborting.\n");
				cli_classic_abort_usage();
			}
			filename = strdup(optarg);
			read_it = 1;
			break;
		case 'w':
			if (++operation_specified > 1) {
				fprintf(stderr, "More than one operation "
					"specified. Aborting.\n");
				cli_classic_abort_usage();
			}
			filename = strdup(optarg);
			write_it = 1;
			break;
		case 'v':
			//FIXME: gracefully handle superfluous -v
			if (++operation_specified > 1) {
				fprintf(stderr, "More than one operation "
					"specified. Aborting.\n");
				cli_classic_abort_usage();
			}
			if (dont_verify_it) {
				fprintf(stderr, "--verify and --noverify are mutually exclusive. Aborting.\n");
				cli_classic_abort_usage();
			}
			filename = strdup(optarg);
			verify_it = 1;
			break;
		case 'p':
			//for (prog = 0; prog < PROGRAMMER_INVALID; prog++) {
				name = "i2c";
				namelen = strlen(name);
				if (strncmp(optarg, name, namelen) == 0) {
					switch (optarg[namelen]) {
					case ':':
						pparam = strdup(optarg + namelen + 1);
						if (!strlen(pparam)) {
							free(pparam);
							pparam = NULL;
						}
						break;
					case '\0':
						break;
					default:
						/* The continue refers to the
						 * for loop. It is here to be
						 * able to differentiate between
						 * foo and foobar.
						 */
						continue;
					}
					break;
				}
			//}
			break;	
		default:
			cli_classic_abort_usage();
			break;
		}
	}
	if (optind < argc) {
		fprintf(stderr, "Error: Extra parameter found.\n");
		cli_classic_abort_usage();
	}

	if ((read_it | write_it | verify_it) && check_filename(filename, "image")) {
		cli_classic_abort_usage();
	}
	if (programmer_init(pparam)) {
		msg_perr("Error: Programmer initialization failed.\n");
		ret = 1;
		goto out_shutdown;
	}
	myusec_calibrate_delay();

	erase_it = 0;
	sema_bmc_update_main(filename, read_it, write_it, erase_it, verify_it);
out_shutdown:
	free(filename);
	free(layoutfile);
	free(pparam);

	return ret;
}

