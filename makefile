PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
PALETTEDIR = $(PREFIX)/share/muse/palettes

CC = gcc
CFLAGS = -O2 -Wall
LDFLAGS = -lm

SRC = muse.c
BIN = muse

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

install: $(BIN)
	install -d $(DESTDIR)$(BINDIR)
	install -d $(DESTDIR)$(PALETTEDIR)
	install -m 755 $(BIN) $(DESTDIR)$(BINDIR)
	install -m 644 p/*.txt $(DESTDIR)$(PALETTEDIR)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)
	rm -rf $(DESTDIR)$(PALETTEDIR)

clean:
	rm -f $(BIN)

.PHONY: all install uninstall clean