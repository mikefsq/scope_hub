# scope_hub firmware

Firmware for the RP2350A on the scope_hub board. It reads the power monitors, environment sensors and GPS, and switches the DC and USB outputs. A controls it over one vendor-defined USB HID interface. See the driver and the `scopehub` tool in `../driver` for details.

## Build and flash

Pico SDK 2.2 and the Arm GNU toolchain are required.

```
make          # configure on first use, then build
make flash    # build, reboot the running board into the boot ROM, load, run
make bootsel  # reboot the running board into the boot ROM and stay there
```

## License

[AGPL-3.0-only](LICENSE). The Pico SDK and TinyUSB keep their BSD-3 and MIT licenses.
