PICO_SDK_PATH ?= /opt/pico-sdk
BUILD := build
UF2 := $(BUILD)/scope_hub.uf2
SCOPEHUB := $(BUILD)/scopehub
DRIVER_SRC := $(wildcard ../driver/*.go ../driver/cmd/scopehub/*.go) ../driver/go.mod

.PHONY: all build flash bootsel clean

all: build

$(BUILD)/build.ninja:
	cmake -S . -B $(BUILD) -G Ninja -DPICO_SDK_PATH=$(PICO_SDK_PATH)

build: $(BUILD)/build.ninja
	ninja -C $(BUILD)

# The host tool, rebuilt only when the driver sources change.
$(SCOPEHUB): $(DRIVER_SRC) | $(BUILD)/build.ninja
	cd ../driver && go build -o ../firmware/$(SCOPEHUB) ./cmd/scopehub

# Asks the running firmware to reboot into the boot ROM over HID, waits for the
# boot ROM to enumerate, then loads and starts the new image. A board already in
# the boot ROM is loaded directly. No button press is needed.
flash: build $(SCOPEHUB)
	-$(SCOPEHUB) bootsel 2>/dev/null
	@i=0; until picotool info >/dev/null 2>&1; do \
		i=$$((i+1)); [ $$i -lt 20 ] || { echo "boot ROM did not enumerate within 5 s" >&2; exit 1; }; \
		sleep 0.25; done
	picotool load -x $(UF2)

# Reboots the running firmware into the boot ROM and leaves it there.
bootsel: $(SCOPEHUB)
	$(SCOPEHUB) bootsel

clean:
	rm -rf $(BUILD)
