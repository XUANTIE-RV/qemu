ifeq ($(DEBUG),y)
	CFLAGS += -g -O0
endif

ifeq ($(GCOV),y)
	CFLAGS += -fprofile-arcs -ftest-coverage -g -O0
	LIBS   += -fprofile-arcs -ftest-coverage -lgcov
endif

SYS := $(shell $(CC) -dumpmachine)

ifneq (, $(findstring mingw, $(SYS)))
XML_LIBS=$(shell mingw32-pkg-config --libs libxml-2.0)
XML_CFLAGS=$(shell mingw32-pkg-config --cflags libxml-2.0)
else
XML_CFLAGS=$(shell xml2-config --cflags)
XML_LIBS=$(shell xml2-config --libs)
endif

COMPILE_DATE=`date +%Y%m%d`

all: compile_date
	$(CC) cskysim.c $(XML_CFLAGS) $(XML_LIBS) $(CFLAGS) -o cskysim -g $(LIBS)

compile_date:
	@echo "#define COMPILE_DATE "$(COMPILE_DATE) > compile_gen.h

.PHONY: win32
win32: compile_date
	$(CC) cskysim.c $(XML_CFLAGS) $(XML_LIBS) -o cskysim_w32.exe -g

.PHONY: win64
win64: compile_date
	$(CC) cskysim.c $(XML_CFLAGS) $(XML_LIBS) -o cskysim_w64.exe -g

clean:
	rm cskysim compile_gen.h
