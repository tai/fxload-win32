/*
 * Copyright (c) 2007 Claudio Favi (claudio.favi@epfl.ch)
 * Copyright (c) 2001 Stephen Williams (steve@icarus.com)
 * Copyright (c) 2001-2002 David Brownell (dbrownell@users.sourceforge.net)
 *
 *    This source code is free software; you can redistribute it
 *    and/or modify it in source code form under the terms of the GNU
 *    General Public License as published by the Free Software
 *    Foundation; either version 2 of the License, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>

#include <ctype.h>
#include <string.h>
#include <getopt.h>

#include "libusb.h"
#include "ezusb.h"

#ifndef	FXLOAD_VERSION
#	define FXLOAD_VERSION (__DATE__ " (modified version at http://github.com/tai)")
#endif

struct device_spec { int index, vid, pid, bus, port; };

void logerror(const char *format, ...)
    __attribute__ ((format (__printf__, 1, 2)));

void logerror(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);

    vfprintf(stderr, format, ap);
    va_end(ap);
}

void usage(const char *argv0) {
    char *p = strrchr(argv0, '/');
    char *q = strrchr(argv0, '\\');

    // basename of argv0
    p = (p > q) ? p+1 : q+1;

    fprintf(stderr, "Usage: %s [options] -I file [-c 0xC[02] -s loader]\n", p);
    fprintf(stderr,
            "Options:\n"
            "  -D <dev>   : select device by vid:pid or bus.port\n"
            "  -t <type>  : select type from (an21|fx|fx2|fx2lp)\n"
            "  -I <file>  : program hex file\n"
            "  -s <loader>: program stage1 loader to write a file into EEPROM\n"
            "  -c <byte>  : program first byte of EEPROM with either 0xC0 or 0xC2\n"
            "  -V         : show version\n"
            "  -v         : show verbose message\n");
    fprintf(stderr,
            "Examples:\n"
            "  // program fw.hex to the FIRST device with given vid\n"
            "  $ %s -d 04b4:@0 -I fw.hex\n"
            "\n"
            "  // program fw.hex to the SECOND device at given bus\n"
            "  $ %s -d 004.@1 -I fw.hex\n"
            "\n"
            "  // program vid:pid info to EEPROM\n"
            "  $ %s -I vidpid.hex -c 0xC0 -s Vend_Ax.hex\n"
            "\n"
            "  // program whole firmware to EEPROM\n"
            "  $ %s -I fwfile.hex -c 0xC2 -s Vend_Ax.hex\n", p, p, p, p);
    exit(1);
}

libusb_device_handle *get_usb_device(struct device_spec *wanted) {
    libusb_device **list;
    libusb_device_handle *dev_h = NULL;

    libusb_init(NULL);
    libusb_set_debug(NULL, 0);

    libusb_device *found = NULL;
    int nr_found = 0;
    int interactive = (wanted->vid == 0 && wanted->bus == 0);
    
    int i, nr = libusb_get_device_list(NULL, &list);
    for (i = 0; i < nr; i++) {
        libusb_device *dev = list[i];

        struct libusb_device_descriptor desc;
        libusb_get_device_descriptor(dev, &desc);

        uint16_t  vid = desc.idVendor;
        uint16_t  pid = desc.idProduct;
        uint8_t   bus = libusb_get_bus_number(dev);
        uint8_t  port = libusb_get_port_number(dev);

        if ((bus == wanted->bus && (port == wanted->port || wanted->port == 0)) ||
            (vid == wanted->vid && ( pid == wanted->pid  || wanted->pid  == 0))) {
            if (nr_found++ == wanted->index) {
                found = dev;
                break;
            }
        }

        if (interactive) {
            unsigned char mfg[80] = {0}, prd[80] = {0}, ser[80] = {0};
            libusb_device_handle *dh;

            if (libusb_open(dev, &dh) == LIBUSB_SUCCESS) {
                libusb_get_string_descriptor_ascii(dh, desc.iManufacturer, mfg, sizeof(mfg));
                libusb_get_string_descriptor_ascii(dh, desc.iProduct     , prd, sizeof(prd));
                libusb_get_string_descriptor_ascii(dh, desc.iSerialNumber, ser, sizeof(ser));
                libusb_close(dh);
            }
            printf("%d: Bus %03d Device %03d: ID %04X:%04X %s %s %s\n", i, bus, port, vid, pid, mfg, prd, ser);
        }
    }

    if (interactive) {
        char sbuf[32];
        printf("Please select device to configure [0-%d]: ", nr - 1);
        fflush(NULL);
        fgets(sbuf, 32, stdin);

        int sel = atoi(sbuf);
        if( sel < 0 || sel >= nr) {
            logerror("device selection out of bound: %d\n", sel);
            return NULL;
        }

        found = list[sel];
    }

    if (! found) {
        logerror("device not selected\n");
        return NULL;
    }
      
    libusb_open(found, &dev_h);
    libusb_free_device_list(list, 1);

    return dev_h;
}

void
parse_device_path(const char *device_path, struct device_spec *spec) {
    char *sub;

    if ((sub = strchr(device_path, ':')) != NULL) {
        spec->vid = strtol(device_path, NULL, 16);
        spec->pid = strtol(    sub + 1, NULL, 16);
    }
    else if ((sub = strchr(device_path, '.')) != NULL) {
        spec->bus  = atoi(device_path);
        spec->port = atoi(sub + 1);
    }

    if ((sub = strchr(device_path, '@')) != NULL) {
        spec->index = atoi(sub + 1);
    }
}

int main(int argc, char*argv[])
{
    struct device_spec spec = {0};
    ezusb_chip_t type = NONE;

    const char	*ihex_path = 0;
    const char	*stage1 = 0;
    int		opt;
    int		config = -1;

    while ((opt = getopt (argc, argv, "h?VvI:D:c:s:t:")) != EOF) {
        switch (opt) {
        case 'I':
	    ihex_path = optarg;
	    break;

        case 'V':
	    puts(FXLOAD_VERSION);
	    return 0;

        case 'D':
	    parse_device_path(optarg, &spec);
	    break;

        case 'c':
	    config = strtoul (optarg, 0, 0);
	    if (config < 0 || config > 255) {
		logerror("illegal config byte: %s\n", optarg);
		usage(argv[0]);
	    }
	    break;

        case 's':
	    stage1 = optarg;
	    break;

        case 't':
	    type = ((strcmp(optarg, "an21")  == 0) ? AN21 :
                    (strcmp(optarg, "fx")    == 0) ? FX   :
                    (strcmp(optarg, "fx2")   == 0) ? FX2  :
                    (strcmp(optarg, "fx2lp") == 0) ? FX2LP : 0);
            if (type == 0) {
		logerror("illegal microcontroller type: %s\n", optarg);
		usage(argv[0]);
	    }
	    break;

        case 'v':
	    verbose++;
	    break;

        default:
            usage(argv[0]);
        }
    }

    if (config >= 0) {
        if (type == 0) {
            logerror("must specify microcontroller type %s",
                     "to write EEPROM!\n");
            usage(argv[0]);
        }
        if (!stage1 || !ihex_path) {
            logerror("need 2nd stage loader and firmware %s",
                     "to write EEPROM!\n");
            usage(argv[0]);
        }
    }

    if (ihex_path) {
        libusb_device_handle *device;
        int status;

        device = get_usb_device(&spec);

        if (device == NULL) {
            logerror("No device to configure\n");
            return -1;
        }

        if (type == NONE) {
            type = FX; /* an21-compatible for most purposes */
        }

        if (stage1) {
            /* first stage:  put loader into internal memory */
            if (verbose)
                logerror("1st stage:  load 2nd stage loader\n");
            status = ezusb_load_ram (device, stage1, type, 0);
            if (status != 0)
                return status;
		
            /* second stage ... write either EEPROM, or RAM.  */
            if (config >= 0)
                status = ezusb_load_eeprom (device, ihex_path, type, config);
            else
                status = ezusb_load_ram (device, ihex_path, type, 1);
            if (status != 0)
                return status;
        } else {
            /* single stage, put into internal memory */
            if (verbose)
                logerror("single stage:  load on-chip memory\n");
            status = ezusb_load_ram (device, ihex_path, type, 0);
            if (status != 0)
                return status;
        }

        libusb_close(device);
    }

  
    if (!ihex_path) {
        logerror("missing hex file\n");
        return -1;
    }

    exit(0);
}


/*
 * $Log: main.c,v $
 * Revision 1.2  2007/03/20 14:31:50  cfavi
 * *** empty log message ***
 *
 * Revision 1.1  2007/03/19 20:46:30  cfavi
 * fxload ported to use libusb
 *
 * Revision 1.8  2005/01/11 03:58:02  dbrownell
 * From Dirk Jagdmann <doj@cubic.org>:  optionally output messages to
 * syslog instead of stderr.
 *
 * Revision 1.7  2002/04/12 00:28:22  dbrownell
 * support "-t an21" to program EEPROMs for those microcontrollers
 *
 * Revision 1.6  2002/04/02 05:26:15  dbrownell
 * version display now noiseless (-V);
 * '-?' (usage info) convention now explicit
 *
 * Revision 1.5  2002/02/26 20:10:28  dbrownell
 * - "-s loader" option for 2nd stage loader
 * - "-c byte" option to write EEPROM with 2nd stage
 * - "-V" option to dump version code
 *
 * Revision 1.4  2002/01/17 14:19:28  dbrownell
 * fix warnings
 *
 * Revision 1.3  2001/12/27 17:54:04  dbrownell
 * forgot an important character :)
 *
 * Revision 1.2  2001/12/27 17:43:29  dbrownell
 * fail on firmware download errors; add "-v" flag
 *
 * Revision 1.1  2001/06/12 00:00:50  stevewilliams
 *  Added the fxload program.
 *  Rework root makefile and hotplug.spec to install in prefix
 *  location without need of spec file for install.
 *
 */
