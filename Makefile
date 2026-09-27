# Kakuro for the Philips P2000C: C (Z88DK/sdcc) + Z80 assembly, CP/M target.
#
# The compiler runs in the z88dk/z88dk Docker image. Screenshots, tests and
# `make run` need the sibling p2000c-zulublaster-sasi-drive checkout (headless
# emulator, disk tool and dist/pro/ disk images) and, for the character-ROM
# font, p2000c-emulator.

VERSION    = 1.0.0
BUILD_DATE = $(shell date +%Y-%m-%d)

# -SO2, not -SO3: p2000c-battleship found the level-3 peephole rules
# dropping stores and array loads in code like this. The game uses direct
# BDOS calls, so the CP/M CRT's static stdio heap is left out.
ZCC      = docker run --rm --user $(shell id -u):$(shell id -g) -v "$(CURDIR)":/src -w /src z88dk/z88dk zcc
ZCCFLAGS = +cpm -vn -clib=sdcc_iy -O3 -SO2 --opt-code-speed --max-allocs-per-node10000 \
           -Ibuild -create-app -m -pragma-define:CLIB_STDIO_HEAP_SIZE=0

SOURCES = src/main.c src/game.c src/puzzle.c src/screen.c src/panel.c src/screens.c src/scores.c src/puzzles.c \
          src/saver.c src/clock.c src/video.asm
HEADERS = src/video.h src/puzzle.h src/game.h src/screen.h src/panel.h src/screens.h src/scores.h \
          src/saver.h src/clock.h src/sprites.h src/puzzles.h src/version.h
COM     = build/KAKURO.COM

# Deployment image for the SASI emulator (ZuluBlaster): a second-disk image
# with the standard split layout (E: low, F: high) built with the disk
# tool's CLI and its split system tracks, holding only the game on F:. The
# former p2000c-cpm-disk-tool checkout is now part of the SASI-drive
# distribution; the legacy checkout is used when present.
DISKTOOL      ?= $(firstword $(wildcard ../p2000c-cpm-disk-tool ../p2000c-zulublaster-sasi-drive))
P2000C_DISK   = PYTHONPATH=$(DISKTOOL)/src python3 -m p2000c_disk.cli
SYSTEM_TRACKS = $(DISKTOOL)/assets/boot/hdboot-split.trk
DEPLOY_IMAGE  = build/HD1_256.hda

.PHONY: all build run screenshot test sprites puzzles deploy clean

all: build

build: $(COM)

$(COM): $(SOURCES) $(HEADERS) Makefile
	mkdir -p build
	printf '#define VERSION "%s"\n#define BUILD_DATE "%s"\n' "$(VERSION)" "$(BUILD_DATE)" > build/build_info.h
	$(ZCC) $(ZCCFLAGS) $(SOURCES) -o build/kakuro
	rm -f build/kakuro build/kakuro_CODE.bin
	@echo "program file $$(wc -c < $@) bytes"

# HD1_256.hda with KAKURO.COM on F: and nothing else. Copy it to the SD card
# in place of the distribution's HD1_256.hda.
deploy: build
	rm -f $(DEPLOY_IMAGE)
	$(P2000C_DISK) build $(DEPLOY_IMAGE) --layout split --system $(SYSTEM_TRACKS)
	$(P2000C_DISK) put-many $(DEPLOY_IMAGE) $(COM) --partition high
	$(P2000C_DISK) verify $(DEPLOY_IMAGE)
	$(P2000C_DISK) list $(DEPLOY_IMAGE)

# Regenerate the cell pictures (needs the p2000c-emulator font sheet);
# the preview goes to build/sprites_preview.png.
sprites:
	python3 tools/gen_sprites.py --preview

# Regenerate src/puzzles.h and src/puzzles.c from assets/puzzles (checks every puzzle and adds
# givens until each has one solution).
puzzles:
	python3 tools/gen_puzzles.py

# Open the game in the graphical emulator.
run: build
	python3 tools/run.py

# Plain raster screenshots (start screen, boards, help) -> build/*.png
screenshot: build
	python3 tools/render.py

# Whole puzzles solved in the headless emulator, and the picture checked.
test: build
	python3 tools/test_game.py

clean:
	rm -rf build
