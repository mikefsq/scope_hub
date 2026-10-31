# scope_hub driver

Go package `scopehub` and the `scopehub` command for the scope_hub board. 

Linux udev rule, for example `/etc/udev/rules.d/70-scopehub.rules`:

```
KERNEL=="hidraw*", ATTRS{idVendor}=="1209", ATTRS{idProduct}=="0001", MODE="0660", TAG+="uaccess"
```

## Commands

```
go build ./cmd/scopehub
scopehub list                # attached boards
scopehub info                # firmware version and serial
scopehub status              # every reading and output state; -json for JSON
scopehub watch -i 1s         # status repeatedly
scopehub dc 1 on             # DC output on at its duty cycle, or off
scopehub dc 1 60             # duty cycle 0..100, used while PWM is on
scopehub pwm-port 1 on       # run the output at its duty cycle, or steady at 100%
scopehub pwm 2               # DC PWM frequency, 0.1..10 Hz
scopehub usb 6 off           # enable a USB port, or force its VBUS off
scopehub led 10              # indicator LED brightness, ends auto mode
scopehub led-auto on         # brightness follows the light sensor
scopehub led-set dark 0.05   # min, max (%) or dark, bright (lux) for led-auto
scopehub dew                 # dew controller state and settings
scopehub dew-auto 3 on       # put a DC output under dew auto control
scopehub dew-set offset -2.5 # offset, start, full (C) or min, max, fail (%)
scopehub label dc 6 Dew Heater
scopehub labels              # port names stored on the board
scopehub scan                # I2C addresses that answer on both buses
scopehub time -n 20          # GPS time against the host clock, best of n exchanges
scopehub chrony -sock /run/chrony.scopehub.sock -i 1s -n 16
scopehub gps-reset           # add cold to clear the stored orbit data
scopehub save                # write the current settings to flash
scopehub defaults            # restore default settings; save makes them permanent
scopehub reboot              # restart the firmware
scopehub bootsel             # reboot into the boot ROM for flashing
scopehub help                # this list; status -h and similar show a command's options
```

`-s <serial>` selects a board when several are attached.

A DC output has three settings: on or off, a duty cycle, and whether PWM runs. With PWM off the output is steady at 100% when on. With PWM on it cycles at the duty cycle and the `pwm` frequency. Dew auto control takes over the duty of the ports it is enabled on.

## Package

```go
dev, err := scopehub.Open("") // first board, or pass its serial
if err != nil {
	log.Fatal(err)
}
defer dev.Close()

st, err := dev.Status()
fmt.Println(st.Input.Volts, st.DC[0].Amps, st.Env.TempC)
err = dev.SetDCEnabled(1, true) // ports are numbered 1..6; Status slices are 0-based
err = dev.SetDC(1, 60)
```

## Time sync with chrony

`scopehub chrony` sends GPS time to chrony's SOCK refclock once a second, the interface gpsd uses. Each sample is the best of 16 exchanges by round trip. chrony creates the socket from this line in `chrony.conf`:

```
refclock SOCK /run/chrony.scopehub.sock refid SHUB precision 1e-3
```

Accuracy is limited by the USB link. `scopehub time` shows the best round trip time.


`go test` checks the struct sizes in `proto.go` with the firmware `firmware/src/hid_proto.h`.

## License

[AGPL-3.0-only](LICENSE).
