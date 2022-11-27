
# note:
# need libsml
# need https://raw.githubusercontent.com/volkszaehler/vzlogger/master/include/unit.h

CFLAGS = -Wall -O3
LDFLAGS = -lsml -lm

all: decodeE220sml

clean:
	rm -rf decodeE220sml.o decodeE220sml

decodeE220sml: decodeE220sml.o

capture: decodeE220sml
	./pyUDPrx.py
	./decodeE220sml

