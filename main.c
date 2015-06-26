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
#ident "$Id: main.c,v 1.2 2007/03/20 14:31:50 cfavi Exp $"

/*
 * This program supports loading firmware into a target USB device
 * that is discovered and referenced by the hotplug usb agent. It can
 * also do other useful things, like set the permissions of the device
 * and create a symbolic link for the benefit of applications that are
 * looking for the device.
 *
 *     -I <path>       -- Download this firmware (intel hex)
 *     -t <type>       -- uController type: an21, fx, fx2
 *     -s <path>       -- use this second stage loader
 *     -c <byte>       -- Download to EEPROM, with this config byte
 *
 *     -V              -- Print version ID for program
 *
 */

# include  <stdlib.h>
# include  <stdio.h>
# include  <getopt.h>
# include  <string.h>

//# include  <sys/types.h>
//# include  <sys/stat.h>
# include  <fcntl.h>
# include  <unistd.h>

#include "libusb.h"
# include  "ezusb.h"

#ifndef	FXLOAD_VERSION
#	define FXLOAD_VERSION (__DATE__ " (development)")
#endif

#include <errno.h>

#include <stdarg.h>


void logerror(const char *format, ...)
    __attribute__ ((format (__printf__, 1, 2)));

void logerror(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);

    vfprintf(stderr, format, ap);
    va_end(ap);
}

#define DEV_LIST_MAX 128
libusb_device_handle *get_usb_device() {
      libusb_device **list;
      libusb_device_handle *dev_h = NULL;

      libusb_init(NULL);
      libusb_set_debug(NULL, 0);

      int i, nr = libusb_get_device_list(NULL, &list);
      for (i = 0; i < nr; i++) {
          struct libusb_device_descriptor desc;
          libusb_get_device_descriptor(list[i], &desc);

          printf("%d: VendId: %04X ProdId: %04X\n",
                 i, desc.idVendor, desc.idProduct);
      }

      char sbuf[32];

      printf("Please select device to configure: ");
      fflush(NULL);
      fgets(sbuf, 32, stdin);

      int sel = atoi(sbuf);
      if( sel < 0 || sel >= nr) {
            logerror("device selection out of bound: %d\n", sel);
	    return NULL;
      }  
      
      libusb_open(list[sel], &dev_h);
      libusb_free_device_list(list, 1);

      return dev_h;
}


int main(int argc, char*argv[])
{
      const char	*link_path = 0;
      const char	*ihex_path = 0;
      const char	*device_path = getenv("DEVICE");
      const char	*type = 0;
      const char	*stage1 = 0;
      mode_t		mode = 0;
      int		opt;
      int		config = -1;

      while ((opt = getopt (argc, argv, "2vV?I:c:s:t:")) != EOF)
      switch (opt) {

	  case '2':		// original version of "-t fx2"
	    type = "fx2";
	    break;

	  case 'I':
	    ihex_path = optarg;
	    break;

	  case 'V':
	    puts (FXLOAD_VERSION);
	    return 0;

	  case 'c':
	    config = strtoul (optarg, 0, 0);
	    if (config < 0 || config > 255) {
		logerror("illegal config byte: %s\n", optarg);
		goto usage;
	    }
	    break;

	  case 's':
	    stage1 = optarg;
	    break;

	  case 't':
	    if (strcmp (optarg, "an21")		// original AnchorChips parts
		    && strcmp (optarg, "fx")	// updated Cypress versions
		    && strcmp (optarg, "fx2")	// Cypress USB 2.0 versions
		    ) {
		logerror("illegal microcontroller type: %s\n", optarg);
		goto usage;
	    }
	    type = optarg;
	    break;

	  case 'v':
	    verbose++;
	    break;

	  case '?':
	  default:
	    goto usage;

      }

      if (config >= 0) {
	    if (type == 0) {
		logerror("must specify microcontroller type %s",
				"to write EEPROM!\n");
		goto usage;
	    }
	    if (!stage1 || !ihex_path) {
		logerror("need 2nd stage loader and firmware %s",
				"to write EEPROM!\n");
		goto usage;
	    }
	    if (link_path || mode) {
		logerror("links and modes not set up when writing EEPROM\n");
		goto usage;
	    }
      }



      if (ihex_path) {
	    libusb_device_handle *device;
	    device = get_usb_device();
	    int status;
	    int	fx2;

	    if (device == NULL) {
		logerror("No device to configure\n");
		return -1;
	    }

	    if (type == 0) {
	    	type = "fx";	/* an21-compatible for most purposes */
		fx2 = 0;
	    } else
 		fx2 = (strcmp (type, "fx2") == 0);
	    
	    if (verbose)
		logerror("microcontroller type: %s\n", type);

	    if (stage1) {
		/* first stage:  put loader into internal memory */
		if (verbose)
		    logerror("1st stage:  load 2nd stage loader\n");
		status = ezusb_load_ram (device, stage1, fx2, 0);
		if (status != 0)
		    return status;
		
		/* second stage ... write either EEPROM, or RAM.  */
		if (config >= 0)
		    status = ezusb_load_eeprom (device, ihex_path, type, config);
		else
		    status = ezusb_load_ram (device, ihex_path, fx2, 1);
		if (status != 0)
		    return status;
	    } else {
		/* single stage, put into internal memory */
		if (verbose)
		    logerror("single stage:  load on-chip memory\n");
		status = ezusb_load_ram (device, ihex_path, fx2, 0);
		if (status != 0)
		    return status;
	    }

	    libusb_close(device);
      }

  
      if (!ihex_path) {
	    logerror("missing hex file\n");
	    return -1;
      }

      return 0;

usage:
      fputs ("usage: ", stderr);
      fputs (argv [0], stderr);
      fputs (" [-vV] [-t type]\n", stderr);
      fputs ("\t\t-I firmware_hexfile ", stderr);
      fputs ("[-s loader] [-c config_byte]\n", stderr);
      fputs ("... device types:  one of an21, fx, fx2\n", stderr);
      return -1;

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
