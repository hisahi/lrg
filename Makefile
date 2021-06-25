
-include config.inc

CC?=cc
CCFLAGS?=
OPTFLAGS?=-O2
LDFLAGS?=
MEMCNT?=
prefix?=/usr/local

.PHONY: all clean install

all: lrg

lrg: lrg.c
ifeq ($(MEMCNT),)
	$(CC) $(CCFLAGS) $(OPTFLAGS) -o lrg lrg.c $(LDFLAGS)
else
	$(CC) $(CCFLAGS) $(OPTFLAGS) $(MEMCNT) -o lrg -DLRG_HOSTED_MEMCNT=1 lrg.c $(LDFLAGS)
endif

clean:
	rm lrg

install:
	cp lrg $(prefix)/bin/





