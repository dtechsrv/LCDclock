/*
 * LCD Clock for 16x2 HD44780 over I2C by PCF8574 bus expander
 * with 1-wire Thermal Sensor extension for DS1820
 * Copyright (C) 2016 dtech(.hu) <dtech@dtech.hu>
 *
 * This source based on LCD Client witch created by the following developers:
 * Copyright (C) 2007 Micky <micky@liberailvoip.it>
 * Original Program for i2c was of Denis Bodor <lefinnois@lefinnois.net>
 * Bugfix by Walter Zarnoch <zarnochwf1@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Pins of PCF8574:   7   6   5   4   3   2   1   0
 * Pins of HD44780:   LED RS  RW  EN  D7  D6  D5  D4 (normal)
 *                    LED EN  RW  RS  D7  D6  D5  D4 (reverse)
 */

#include <sys/ioctl.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <termio.h>
#include <time.h>
#include <sys/time.h>
#include <sys/errno.h>
#include <asm/types.h>
#include "i2c-dev.h"

#define PACKAGE_VERSION	"1.2.1-r74"
#define I2C_SLAVE	0x0703
#define LEAPSEC		20

void lcd_init(void);
void cmdout(char);
void chrout(char);
void stringout(char*);
void i2c_tx(char);
void prog_cgram(char, char*);
void line_jump(void);
void jumpsec(void);
void setclock(void);
void getfraw(void);
void settemp(void);
int getleap(void);

int file;
int tfail = 0;
int rloop = 0;
int hmsg = 0;
int notmp = 0;
char degsym[8] = {0x06,0x09,0x09,0x06,0x00,0x00,0x00,0x00};
char w1_path[256];
char tempraw[256];

// Pin configuration values
char pin_led = 0x00; // 0x80 = ON, 0x00 = OFF (default: OFF)
char pin_rs  = 0x40; // 0x40 = default, 0x10 = reverse.
char pin_rw  = 0x20; // 0x20 (always)
char pin_en  = 0x10; // 0x10 = default, 0x40 = reverse.

// Main function
int main(int argc, char** argv) {
	int errno, ret, i2c_addr, i;
	int opt = 0;
	char *filename, *sensor;
	char ch;

	// Get input arguments
	while((ret = getopt(argc, argv, "hd:a:s:brnp")) != -1) {
		switch(ret) {
			// Help message
			case 'h':
				hmsg = 1;
				break;
			// I2C Bus selection
			case 'd':
				filename = strdup(optarg);
				opt++;
				break;
			// Set PCF8574 address
			case 'a':
				i2c_addr = strtoul(optarg, NULL, 0);
				opt++;
			// Set DS1820 address
			case 's':
				sensor = strdup(optarg);
				break;
			// Blacklight: ON
			case 'b':
				pin_led = 0x80;
				break;
			// Set recursive read mode when failure
			case 'r':
				rloop = 1;
				break;
			// Enable start without sensor
			case 'n':
				notmp = 1;
				break;
			// Set reverse pin configuration
			case 'p':
				pin_rs  = 0x10;
				pin_en  = 0x40;
				break;
		}
	}

	// Check for the minimal arguments is present (device, address)
	if (opt<2 || hmsg) {
		// Print help message
		printf("===================================================================\n");
		printf("LCD Clock for HD44780 16x2 display over I2C by PCF8574 bus expander\n");
		printf("        * with 1-wire Thermal Sensor extension for DS1820 *\n");
		printf("===================================================================\n\n");
		printf("Version " PACKAGE_VERSION ", Build date: " __DATE__ " " __TIME__ ".\n");
		printf("Copyright (C) 2016 dtech(.hu)\n\n");
		printf("Usage: '%s <parameters>'\n", argv[0]);
		printf(" -h            prints this help message\n");
		printf(" -d <device>   I2C bus selection, e.g. '/dev/i2c-0' (required)\n");
		printf(" -a <address>  address of the PCF8574, e.g. '0x3f' (required)\n");
		printf(" -s <sensor>   DS1820 sensor ID, e.g. '22-1234567c1111' (required)\n\n");
		printf("Optional parameters:\n");
		printf(" -b            turns on the blacklight\n");
		printf(" -r            auto re-read sensor if CRC value is false\n");
		printf(" -n            force start without sensor\n");
		printf(" -p            reverse control pin configuration (see below)\n\n");
		printf("Command line example:\n");
		printf("'%s -d /dev/i2c-0 -a 0x3f -s 22-1234567c1111 -b -r -n'\n\n", argv[0]);
		printf("Output pin configuration:\n");
		printf("P7  P6  P5  P4  P3  P2  P1  P0\n");
		printf("LED RS  RW  EN  D7  D6  D5  D4 (normal)\n");
		printf("LED EN  RW  RS  D7  D6  D5  D4 (reverse)\n\n");
		printf("LCD panel error codes for DS1820 sensor:\n");
		printf("'FalseCRC'     Temperature read process returns with false CRC code\n");
		printf("               REASON: General communication error with sensor or high noise.\n");
		printf("               (If you permanently get this message, try the '-r' parameter.)\n");
		printf("'TempFail'     Temperature read process returns with false positive CRC code\n");
		printf("               REASON: The read process takes a long time (kernel module issue).\n");
		printf("'NoSensor'     The sensor is inaccessible via 1-wire slave device\n");
		printf("               REASON: The sensor isn't connected to 1-wire bus or load is high.\n\n");

		// Set exit codes
		if (opt == 0 || hmsg) {
			exit(EXIT_SUCCESS);
		} else {
			exit(EXIT_FAILURE);
		}
	}

	// Trying to communicate with the I2C device
	if ((file = open(filename, O_RDWR)) < 0) {
		fprintf(stderr,"Device Error: %s (%d)\n", strerror(errno), errno);
		exit(EXIT_FAILURE);
	}

	// Trying to communicate with defined address
	if (ioctl(file, I2C_SLAVE, i2c_addr) < 0) {
		fprintf(stderr, "Address Error: %s (%d)\n", strerror(errno), errno);
		exit(EXIT_FAILURE);
	}

	// Check for DS1820 ID length
	if (strlen(sensor) != 15) {
		fprintf(stderr, "Sensor Error: invalid device identifier\n");
		exit(EXIT_FAILURE);
	}

	// Create full path for w1_slave
	sprintf(w1_path, "/sys/bus/w1/devices/%s/w1_slave", sensor);

	// Check 1-wire device connection
	if (fopen(w1_path, "r") == NULL) {
		fprintf(stderr, "Sensor Error: device isn't connected to w1 bus\n");
		if (notmp != 1) {
			exit(EXIT_FAILURE);
		}
	}

	// Print start message to console
	printf("LCD Clock started.\n");

	// Start LCD initialization
	lcd_init();

	// Load degree character into the CGRAM (Address: 0x01)
	prog_cgram(0x01, degsym);
	usleep(5000); // Wait after programing

	// Print the logo on LCD (5 seconds)
	stringout(" LCD Clock with ");
	line_jump();
	stringout("DS1820 Extension");

	cmdout(0x0C); // Set the LCD -> Display ON, Curs OFF, Blink OFF
	cmdout(0x02); // Set cursor to home (insert)
	usleep(5000000);

	// Send current time to LCD
	setclock();
	line_jump();
	stringout("Sensor: *DS1820*");
	usleep(1000000);

	// First sensor query
	settemp();

	// Start infinite update loop
	while(1) {

		// Check for leap second
		if (getleap()) {

			// Print temperature value and then the clock
			settemp();
			setclock();
		}

		// Wait for the next second
		jumpsec();

		// Send current time to LCD
		setclock();
	}

	return(0);
}

/*
LED=1 Blacklight (always on)
RS=1 Data transfer
RS=0 Command transfer
RW=1 Write
D7-D4 Half data bytes
*/

// Direct I2C tranfer
void i2c_tx(char buf) {
		if (write(file,&buf,1) != 1) {
			fprintf(stderr,"Write Error : %s (%d)\n",strerror(errno),errno);
			exit(EXIT_FAILURE);
		}
}

// LCD initialization function (re-implemented)
void lcd_init(void) {
	usleep(5000);          // Initial pre-wait cycle
	i2c_tx(0x00);          // Start reset signal (not required)
	usleep(20000);         // Power ON wait cycle (15ms)

	i2c_tx(pin_en+0x03);  // Start initialization (Enable: high)
	usleep(10);           // Wait 10us
	i2c_tx(0x03);         // Start initialization (Enable: low)
	usleep(15000);        // Wait 15ms

	i2c_tx(pin_en+0x03);  // Init Step #2 (Enable: high)
	usleep(10);           // Wait 10us
	i2c_tx(0x03);         // Init Step #2 (Enable: low)
	usleep(5000);         // Wait 5ms

	i2c_tx(pin_en+0x03);  // Init Step #3 (Enable: high)
	usleep(10);           // Wait 10us
	i2c_tx(0x03);         // Init Step #4 (Enable: low)
	usleep(5000);         // Wait 5ms

	i2c_tx(pin_en+0x02);  // Set 4 bit output flag (enable high)
	usleep(10);           // Wait 10us
	i2c_tx(0x02);         // Set 4 bit output flag (enable low)
	usleep(5000);         // Wait 5ms

	cmdout(0x28);         // Set the LCD -> Two lines and 5*7 font
	cmdout(0x0F);         // Set the LCD -> Display ON, Curs ON, Blink ON
	cmdout(0x01);         // Clear the LCD panel
	usleep(10000);        // Wait 10ms (IMPORTANT!)

	cmdout(0x08);         // Set the LCD -> Display OFF, Curs OFF, Blink OFF
}

// Send a command for the display
void cmdout(char c) {
	// Right shift then AND with 0x0F
	int hb=(c>>4)&0x0F,lb=c&0x0F;

	// Set offset for commands (LED)
	int offset = pin_led;

	// Send the command (directly without wait cycle)
	i2c_tx(offset + hb + pin_en); // Data + Enable: high (0x10)
	i2c_tx(offset + hb);          // Data + Enable: low (0x00)
	i2c_tx(offset + lb + pin_en); // Data + Enable: high (0x10)
	i2c_tx(offset + lb);          // Data + Enable: low (0x00)
}

// Send a character for the display
void chrout(char c) {
	// Right shift then AND with 0x0F
	int hb=(c>>4)&0x0F,lb=c&0x0F;

	// Set offset for characters (RS + LED)
	int offset = pin_led + pin_rs;

	// Send actual character (directly without wait cycle)
	i2c_tx(offset + hb + pin_en); // Data + RS: high, Enable: high (0x50)
	i2c_tx(offset + hb);          // Data + RS: high, Enable: low (0x40, reverse: 0x10)
	i2c_tx(offset + lb + pin_en); // Data + RS: high, Enable: high (0x50)
	i2c_tx(offset + lb);          // Data + RS: high, Enable: low (0x40, reverse: 0x10)
}

// Print output string
void stringout(char * c) {
        int n=0;

	// Print characters
	while(c[n]!=0)
		chrout(c[n++]);
}

// Load user-defined symbol into the CGRAM
void prog_cgram(char memloc, char * symbol) {
	int i;

	// Jump to CGRAM address
	cmdout(0x40 + (memloc * 8));
	usleep(5000); // Wait 5ms

	for (i = 0; i < 8; i++) {
		chrout(symbol[i]);
		usleep(100); // Wait 100us
	}

	// Jump back to the DDRAM address
	cmdout(0x80);
	usleep(5000); // Wait 5ms
}

// Jump to the second line
void line_jump(void) {
	cmdout(0xC0);
}

// Wait for the next second and start the clock
void jumpsec(void) {
	time_t start = time(0);
	time_t now = start;

	// Check for the current second is same as the buffered
	while(start == now) {
		usleep(100000);
		now = time(0);
	}
}

// Show the clock on the LCD
void setclock(void) {
	time_t secs = time(0);
	struct tm *local = localtime(&secs);
	char strclock[8];
	const char *strmonth[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

	// Convert time to string and send it to the LCD
	sprintf(strclock, "%s %02d, %02d:%02d:%02d", strmonth[local->tm_mon], local->tm_mday, local->tm_hour, local->tm_min, local->tm_sec);

	// Set instructions for the LCD panel
	stringout(strclock);
	cmdout(0x0C); // Set the LCD -> Display ON, Curs OFF, Blink OFF
	cmdout(0x02); // Set cursor to home (insert)
}

// Get w1_slave raw content
void getfraw(void) {
	FILE *tempfile;
	char *tempbuf;
	int size;

	// Check for w1_slave read failure (device removed)
	if ((tempfile = fopen(w1_path, "r")) == NULL) {
		// Set sensor failure value (Error Code #1)
		tfail = 1;

	// Get w1_slave content
	} else {
		// Reset sensor failure value (Error Code #0)
		tfail = 0;
		
		// Get content of w1_slave to variable
		fseek(tempfile, 0, SEEK_END);
		size = ftell(tempfile);
		rewind(tempfile);
		tempbuf = calloc(size + 1, 1);
		fread(tempbuf, 1, size, tempfile);

		// Close file stream
		fclose(tempfile);
		strcpy(tempraw, tempbuf);
		free(tempbuf);
	}
}

// Get current temperature
void settemp(void) {
	char dbuf[8];
	char tbuf[8];
	char strtemp[16];
	int digit = 0;
	int pos = 0;
	int point, i;

	// Prepare sensor query - Send first line output to the LCD
	stringout("Read temperature");

	// Get start temperature raw content
	getfraw();

	// Check for sensor failure
	if (tfail != 1) {

		// Check for valid CRC (recheck cycle removed because it increased the load)
		if (tempraw[36] != 'Y') {

			// Auto re-read parameter check ('-r')
			if (rloop) {
				while (tempraw[36] == 'N') {
					getfraw();
				}
			// Set false recheck value (Error code #3)
			} else {
				tfail = 3;
//				printf("CRC error at temperature read!\n");     // Debug: CRC
			}

		// Check for false positive CRC value (Error Code #2)
		} else if (tempraw[33] == '0' && tempraw[34] == '0') {
			tfail = 2;
		}

		// Get temperature raw value length
		while(tempraw[69 + digit] != '\n') {
//			printf("%c", tempraw[69 + digit]);                      // Debug: RAW Temperature (without separators)
			digit++;
		}

		// Set float point position
		point = digit - 3;

		// Set final format of temperature value
		for (i=0; i<5; i++) {
			if (i != point) {
				dbuf[i] = tempraw[69 + pos];
				pos++;
			} else {
				dbuf[i] = '.';
			}
		}

		// Add special characters to buffer string
		dbuf[i] = ' ';      // Space (separator)
		dbuf[i + 1] = 0x01; // Degree symbol from CGRAM (0x01 in LCD charset)
		dbuf[i + 2] = 'C';  // 'C' character (Celsius)
		dbuf[i + 3] = '\0'; // End-of-string symbol (decrease length)

//		printf("Buffer length: %d\n", strlen(dbuf));                    // Debug: buffer string length
//		printf("Output temp string: '%s'\n", dbuf);                     // Debug: temperature string
	}

	// Set false CRC message (wait 50ms before send)
	if (tfail == 3) {
		sprintf(tbuf, "FalseCRC");
		usleep(50000);
	// Set temperature read fail message (without wait)
	} else if (tfail == 2) {
		sprintf(tbuf, "TempFail");
	// Set sensor fail message (wait a second before send)
	} else if (tfail == 1) {
		sprintf(tbuf, "NoSensor");
		usleep(1000000);
	// Set formatted temperature value to the LCD panel (wait 200ms before send)
	} else {
		sprintf(tbuf, "%s", dbuf);
		usleep(200000);
	}

	// Format output string for different panel sizes
	sprintf(strtemp, "Sensor: %s", tbuf);

	// Jump to the second line and send output string to LCD
	line_jump();
	stringout(strtemp);

	// Set control instructions for the LCD panel
	cmdout(0x0C); // Set the LCD -> Display ON, Curs OFF, Blink OFF
	cmdout(0x02); // Set cursor to home (insert)
}

// Get leap seconds (re-implemented, interval: 20 seconds)
int getleap(void) {
	time_t lnow = time(0);
	struct tm *ltime = localtime(&lnow);
	int leapsec = ltime->tm_sec;

	// Check for leapsec is present
	if (leapsec % LEAPSEC == 0) {
		return 1;
	} else {
		return 0;
	}
}
