# Copyright (C) 2014-2015 Tobias Klauser <tklauser@distanz.ch>

P 	= nvtegraparts
OBJS	= $(P).o
LIBS	=

# CROSS_COMPILE=arm-linux-gnueabi-hf-
CC	= $(CROSS_COMPILE)gcc
INSTALL	= install

CFLAGS	?= -W -Wall -Wcast-align -O2
LDFLAGS	?=

ifeq ($(DEBUG), 1)
  CFLAGS += -g -DDEBUG
endif

Q	?= @
CCQ	= $(Q)echo "  CC $<" && $(CC)
LDQ	= $(Q)echo "  LD $@" && $(CC)

prefix	?= /usr/local

BINDIR	= $(prefix)/bin
SBINDIR	= $(prefix)/sbin
DESTDIR	=

all: $(P)

$(P): $(OBJS)
	$(LDQ) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

%.o: %.c %.h
	$(CCQ) $(CFLAGS) -o $@ -c $<

%.o: %.c
	$(CCQ) $(CFLAGS) -o $@ -c $<

install: $(P)
	@echo "  INSTALL $(P)"
	@$(INSTALL) -d -m 755 $(DESTDIR)$(BINDIR)
	@$(INSTALL) -m 755 $(P) $(BINDIR)/$(P)

clean:
	@echo "  CLEAN"
	@rm -f $(OBJS) $(P)
