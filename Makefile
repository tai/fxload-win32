
LIBUSB = ./libusb-1.0

CFLAGS += -I$(LIBUSB)
LIBS   += -L$(LIBUSB) -lusb-1.0

fxload-libusb.exe: ezusb.c ezusb.h main.c
	mingw32-gcc -g -Wall ezusb.c main.c -o $@ $(CFLAGS) $(LIBS)

clean:
	rm -f *~ fxload-libusb.exe
