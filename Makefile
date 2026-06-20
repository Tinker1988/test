# =====================================================================
#  Boom Barrier — relay test firmware build wrapper
# =====================================================================
#  Wraps PlatformIO so the same commands work on Arch Linux and Windows.
#  The firmware itself is ESP32 (target arch is the same everywhere); the
#  OS detection below just picks the right `pio` invocation and the right
#  default serial port for the host you build from.
#
#  Usage:
#    make            # compile firmware
#    make upload     # compile + flash over USB
#    make monitor    # open the serial monitor (115200)
#    make flash      # upload then immediately open the monitor
#    make clean      # remove build artifacts
#    make env        # print detected OS / port / pio command
#
#  Override the serial port if auto-detect is wrong:
#    make upload PORT=COM5            (Windows)
#    make upload PORT=/dev/ttyUSB1    (Linux)
# =====================================================================

# ---- OS detection -------------------------------------------------
# Windows sets the OS env var to "Windows_NT". Everything else we treat
# as a Unix-like host (Arch Linux, other Linux, macOS).
ifeq ($(OS),Windows_NT)
    HOST       := windows
    DEFAULT_PORT := COM6
    # Prefer `pio`; fall back to `platformio` if that is what is on PATH.
    PIO        := pio
else
    HOST       := $(shell uname -s)
    DEFAULT_PORT := /dev/ttyUSB0
    # On Arch Linux pio is usually on PATH; if installed via the official
    # installer it lives in ~/.platformio/penv/bin/pio.
    PIO        := $(shell command -v pio 2>/dev/null || command -v platformio 2>/dev/null || echo $$HOME/.platformio/penv/bin/pio)
endif

# Serial port: user-overridable, otherwise the per-OS default above.
PORT ?= $(DEFAULT_PORT)

ENV := esp32dev

.PHONY: all build upload monitor flash clean env help

all: build

## build   — compile the firmware
build:
	$(PIO) run -e $(ENV)

## upload  — compile and flash over USB
upload:
	$(PIO) run -e $(ENV) -t upload --upload-port $(PORT)

## monitor — open the serial monitor
monitor:
	$(PIO) device monitor -p $(PORT) -b 115200

## flash   — upload then open the serial monitor
flash: upload monitor

## clean   — remove build artifacts
clean:
	$(PIO) run -e $(ENV) -t clean

## env     — show what was auto-detected
env:
	@echo "Host OS  : $(HOST)"
	@echo "PIO cmd  : $(PIO)"
	@echo "Env      : $(ENV)"
	@echo "Port     : $(PORT)"

## help    — list targets
help:
	@echo "Targets: build upload monitor flash clean env"
	@echo "Override the port with: make upload PORT=<port>"
