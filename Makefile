
CC?=cc
CCFLAGS?=-O3 -march=native
LDFLAGS?=
MEMCNT?=
prefix?=/usr/local

-include config.inc

.PHONY: all clean install

all: lrg

lrg: lrg.c
ifeq ($(MEMCNT),)
	$(CC) $(CCFLAGS) -o lrg lrg.c $(LDFLAGS)
else
	$(CC) $(CCFLAGS) $(MEMCNT) -o lrg -DLRG_HOSTED_MEMCNT=1 lrg.c $(LDFLAGS)
endif

clean:
	rm lrg

install:
	cp lrg $(prefix)/bin/





