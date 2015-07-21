# Copyright (C) 2014-2015 Tobias Klauser <tklauser@distanz.ch>

TOOLS 	= nvtegraparts trdx-configblock

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

all: $(TOOLS)

define TOOL_templ
$(1): $(1).o
	$$(LDQ) $$(LDFLAGS) -o $$@ $$<
$(1)_install: $(P)
	@echo "  INSTALL $(1)"
	@$(INSTALL) -d -m 755 $(DESTDIR)$(BINDIR)
	@$(INSTALL) -m 755 $(1) $(BINDIR)/$(1)
endef

$(foreach tool,$(TOOLS),$(eval $(call TOOL_templ,$(tool))))

%.o: %.c %.h
	$(CCQ) $(CFLAGS) -o $@ -c $<

%.o: %.c
	$(CCQ) $(CFLAGS) -o $@ -c $<

install: $(foreach tool,$(TOOLS),$(tool)_install)

clean:
	@echo "  CLEAN"
	@rm -f $(OBJS) $(P)
