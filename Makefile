CC = gcc
CFLAGS = -Wall -Wextra -O2 $(shell pkg-config --cflags wayland-client)
LIBS = $(shell pkg-config --libs wayland-client)
WAYLAND_SCANNER = $(shell pkg-config --variable=wayland_scanner wayland-scanner)

WAYLAND_PROTOCOLS_DIR = $(shell pkg-config --variable=pkgdatadir wayland-protocols)
# Using the protocol found in another project as it's missing from system
WLR_LAYER_SHELL_XML = ./protocols/wlr-layer-shell-unstable-v1.xml
SINGLE_PIXEL_XML = $(WAYLAND_PROTOCOLS_DIR)/staging/single-pixel-buffer/single-pixel-buffer-v1.xml
XDG_SHELL_XML = $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml
VIEWPORTER_XML = $(WAYLAND_PROTOCOLS_DIR)/stable/viewporter/viewporter.xml

GEN_SOURCES = wlr-layer-shell-unstable-v1-protocol.c single-pixel-buffer-v1-protocol.c xdg-shell-protocol.c viewporter-protocol.c
GEN_HEADERS = wlr-layer-shell-unstable-v1-client-protocol.h single-pixel-buffer-v1-client-protocol.h xdg-shell-client-protocol.h viewporter-client-protocol.h

.PHONY: all clean

all: wlhc

wlhc: wlhc.o $(GEN_SOURCES:.c=.o)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

wlhc.o: wlhc.c $(GEN_HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

wlr-layer-shell-unstable-v1-protocol.c: $(WLR_LAYER_SHELL_XML)
	$(WAYLAND_SCANNER) private-code $< $@

wlr-layer-shell-unstable-v1-client-protocol.h: $(WLR_LAYER_SHELL_XML)
	$(WAYLAND_SCANNER) client-header $< $@

single-pixel-buffer-v1-protocol.c: $(SINGLE_PIXEL_XML)
	$(WAYLAND_SCANNER) private-code $< $@

single-pixel-buffer-v1-client-protocol.h: $(SINGLE_PIXEL_XML)
	$(WAYLAND_SCANNER) client-header $< $@

xdg-shell-protocol.c: $(XDG_SHELL_XML)
	$(WAYLAND_SCANNER) private-code $< $@

xdg-shell-client-protocol.h: $(XDG_SHELL_XML)
	$(WAYLAND_SCANNER) client-header $< $@

viewporter-protocol.c: $(VIEWPORTER_XML)
	$(WAYLAND_SCANNER) private-code $< $@

viewporter-client-protocol.h: $(VIEWPORTER_XML)
	$(WAYLAND_SCANNER) client-header $< $@

clean:
	rm -f wlhc *.o $(GEN_SOURCES) $(GEN_HEADERS)
