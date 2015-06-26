fxload-libusb: ezusb.c ezusb.h main.c
	mingw32-gcc -Wall  ezusb.c main.c libusb.a -o fxload-libusb

clean:
	rm -f *~ fxload-libusb