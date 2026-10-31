// Command scopehub reads and controls a scope_hub board over USB HID.
package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"strconv"
	"strings"
	"time"

	scopehub "github.com/mikefsq/scope_hub/driver"
)

const usage = `usage: scopehub [-s serial] <command> [args]

commands:
  list                  list attached boards
  info                  firmware version and serial
  status [-json]        all readings and output states
  watch [-i 1s]         print status repeatedly
  dc <1-6> on|off       switch a DC output on at its duty cycle, or off
  dc <1-6> <0-100>      set a DC output's duty cycle, used while PWM is on
  pwm-port <1-6> on|off run a DC output at its duty cycle, or steady at 100%
  usb <1-6> on|off      enable a USB port, or force its VBUS off
  labels                show the port names stored on the board
  label dc|usb <1-6> [name]
                        name a port (stored in flash); no name clears it
  led <0-100>           set indicator LED brightness (ends auto mode)
  led-auto on|off       LED brightness follows the light sensor
  led-set <name> <v>    set min, max (%) or dark, bright (lux) for led-auto
  scan                  list the I2C addresses that answer on both buses
  save                  write the current settings to flash; they apply at power-on
  defaults              restore default settings (save makes them permanent)
  pwm <0.1-10>          set the DC output PWM frequency in Hz
  dew                   dew controller state and settings
  dew-auto <1-6> on|off put a DC output under dew auto control
  dew-set <name> <v>    set offset, start, full (C) or min, max, fail (%)
  time [-n 20]          board GPS time against the host clock, best of n exchanges
  chrony [-sock path] [-i 1s] [-n 16]
                        feed GPS time to chrony's SOCK refclock until stopped
  gps-reset [cold]      reset the GPS receiver; cold also clears its stored orbit data
  reboot                restart the firmware
  bootsel               reboot into the USB boot ROM for flashing
`

func main() {
	serial := flag.String("s", "", "board serial (default: first board)")
	flag.Usage = func() { fmt.Fprint(os.Stderr, usage) }
	flag.Parse()
	args := flag.Args()
	if len(args) == 0 {
		flag.Usage()
		os.Exit(2)
	}
	if err := run(*serial, args[0], args[1:]); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}

func run(serial, cmd string, args []string) error {
	if cmd == "help" || cmd == "-h" || cmd == "--help" {
		fmt.Print(usage)
		return nil
	}
	if cmd == "list" {
		devs, err := scopehub.List()
		if err != nil {
			return err
		}
		if len(devs) == 0 {
			fmt.Println("no boards found")
		}
		for _, d := range devs {
			fmt.Printf("%s  %s  %s\n", d.Serial, d.Product, d.Path)
		}
		return nil
	}

	// Option flags are parsed before the board is opened, so help and a mistyped
	// command answer without a board attached. The other commands take positional
	// arguments, some negative numbers, which flag parsing must not see.
	var opts struct {
		json  bool
		every time.Duration
		n     int
		sock  string
	}
	fs := flag.NewFlagSet(cmd, flag.ExitOnError)
	hasOpts := true
	switch cmd {
	case "status":
		fs.BoolVar(&opts.json, "json", false, "print JSON")
	case "watch":
		fs.DurationVar(&opts.every, "i", time.Second, "interval")
	case "time":
		fs.IntVar(&opts.n, "n", 20, "exchanges")
	case "chrony":
		fs.StringVar(&opts.sock, "sock", "/run/chrony.scopehub.sock", "chrony SOCK refclock path")
		fs.DurationVar(&opts.every, "i", time.Second, "interval")
		fs.IntVar(&opts.n, "n", 16, "exchanges per sample")
	case "info", "dc", "usb", "led", "pwm", "pwm-port", "dew", "dew-auto", "dew-set", "labels", "label",
		"led-auto", "led-set", "scan", "save", "defaults", "gps-reset", "reboot", "bootsel":
		hasOpts = false
	default:
		return fmt.Errorf("unknown command %q\n\n%s", cmd, usage)
	}
	if hasOpts {
		fs.Parse(args)
		args = fs.Args()
	}

	dev, err := scopehub.Open(serial)
	if err != nil {
		return err
	}
	defer dev.Close()

	switch cmd {
	case "info":
		i := dev.Info()
		fmt.Printf("serial %s  firmware %s  protocol %d  dc ports %d  usb ports %d\n",
			i.Serial, i.FirmwareVersion, i.ProtoVersion, i.DCPorts, i.USBPorts)
		return nil
	case "status":
		s, err := dev.Status()
		if err != nil {
			return err
		}
		a, err := dev.Aux()
		if err != nil {
			return err
		}
		if opts.json {
			enc := json.NewEncoder(os.Stdout)
			enc.SetIndent("", "  ")
			return enc.Encode(struct {
				*scopehub.Status
				Aux *scopehub.Aux `json:"aux"`
			}{s, a})
		}
		printStatus(s)
		printAux(a)
		return nil
	case "watch":
		for {
			s, err := dev.Status()
			if err != nil {
				return err
			}
			a, err := dev.Aux()
			if err != nil {
				return err
			}
			fmt.Printf("\n%s\n", time.Now().Format("15:04:05"))
			printStatus(s)
			printAux(a)
			time.Sleep(opts.every)
		}
	case "dc":
		if len(args) == 2 && (args[1] == "on" || args[1] == "off") {
			port, err := strconv.Atoi(args[0])
			if err != nil {
				return fmt.Errorf("usage: dc <1-6> on|off|<0-100>")
			}
			return dev.SetDCEnabled(port, args[1] == "on")
		}
		port, duty, err := twoInts(args, "dc <1-6> on|off|<0-100>")
		if err != nil {
			return err
		}
		return dev.SetDC(port, duty)
	case "usb":
		if len(args) != 2 || (args[1] != "on" && args[1] != "off") {
			return fmt.Errorf("usage: usb <1-6> on|off")
		}
		port, err := strconv.Atoi(args[0])
		if err != nil {
			return fmt.Errorf("usage: usb <1-6> on|off")
		}
		return dev.SetUSB(port, args[1] == "on")
	case "led":
		if len(args) != 1 {
			return fmt.Errorf("usage: led <0-100>")
		}
		pct, err := strconv.Atoi(args[0])
		if err != nil {
			return fmt.Errorf("usage: led <0-100>")
		}
		return dev.SetLEDBrightness(pct)
	case "pwm":
		if len(args) != 1 {
			return fmt.Errorf("usage: pwm <0.1-10>")
		}
		hz, err := strconv.ParseFloat(args[0], 64)
		if err != nil {
			return fmt.Errorf("usage: pwm <0.1-10>")
		}
		got, err := dev.SetDCPWMHz(hz)
		if err == nil {
			fmt.Printf("dc pwm %.2f Hz\n", got)
		}
		return err
	case "pwm-port":
		if len(args) != 2 || (args[1] != "on" && args[1] != "off") {
			return fmt.Errorf("usage: pwm-port <1-6> on|off")
		}
		port, err := strconv.Atoi(args[0])
		if err != nil {
			return fmt.Errorf("usage: pwm-port <1-6> on|off")
		}
		return dev.SetDCPWM(port, args[1] == "on")
	case "dew":
		w, err := dev.Dew()
		if err != nil {
			return err
		}
		printDew(w)
		return nil
	case "dew-auto":
		if len(args) != 2 || (args[1] != "on" && args[1] != "off") {
			return fmt.Errorf("usage: dew-auto <1-6> on|off")
		}
		port, err := strconv.Atoi(args[0])
		if err != nil {
			return fmt.Errorf("usage: dew-auto <1-6> on|off")
		}
		return dev.SetDCAuto(port, args[1] == "on")
	case "dew-set":
		if len(args) != 2 {
			return fmt.Errorf("usage: dew-set offset|start|full|min|max|fail <value>")
		}
		v, err := strconv.ParseFloat(args[1], 64)
		if err != nil {
			return fmt.Errorf("usage: dew-set offset|start|full|min|max|fail <value>")
		}
		w, err := dev.Dew()
		if err != nil {
			return err
		}
		s := w.DewSettings
		field := map[string]*float64{"offset": &s.TempOffsetC, "start": &s.StartC, "full": &s.FullC,
			"min": &s.MinPct, "max": &s.MaxPct, "fail": &s.FailPct}[args[0]]
		if field == nil {
			return fmt.Errorf("usage: dew-set offset|start|full|min|max|fail <value>")
		}
		*field = v
		return dev.SetDewSettings(s)
	case "labels":
		l, err := dev.Labels()
		if err != nil {
			return err
		}
		for i := 0; i < 6; i++ {
			fmt.Printf("DC %d   %-16s USB %d   %s\n", i+1, l.DC[i], i+1, l.USB[i])
		}
		return nil
	case "label":
		if len(args) < 2 || (args[0] != "dc" && args[0] != "usb") {
			return fmt.Errorf("usage: label dc|usb <1-6> [name]")
		}
		port, err := strconv.Atoi(args[1])
		if err != nil {
			return fmt.Errorf("usage: label dc|usb <1-6> [name]")
		}
		name := strings.Join(args[2:], " ")
		if args[0] == "dc" {
			return dev.SetDCLabel(port, name)
		}
		return dev.SetUSBLabel(port, name)
	case "led-auto":
		if len(args) != 1 || (args[0] != "on" && args[0] != "off") {
			return fmt.Errorf("usage: led-auto on|off")
		}
		return dev.SetLEDAuto(args[0] == "on")
	case "led-set":
		if len(args) != 2 {
			return fmt.Errorf("usage: led-set min|max|dark|bright <value>")
		}
		v, err := strconv.ParseFloat(args[1], 64)
		if err != nil {
			return fmt.Errorf("usage: led-set min|max|dark|bright <value>")
		}
		a, err := dev.Aux()
		if err != nil {
			return err
		}
		l := a.LED
		field := map[string]*float64{"min": &l.MinPct, "max": &l.MaxPct, "dark": &l.DarkLux, "bright": &l.BrightLux}[args[0]]
		if field == nil {
			return fmt.Errorf("usage: led-set min|max|dark|bright <value>")
		}
		*field = v
		return dev.SetLEDSettings(l)
	case "scan":
		b0, b1, err := dev.Scan()
		if err != nil {
			return err
		}
		hex := func(a []uint8) string {
			s := make([]string, len(a))
			for i, v := range a {
				s[i] = fmt.Sprintf("0x%02x", v)
			}
			return strings.Join(s, " ")
		}
		fmt.Printf("i2c0  %s\ni2c1  %s\n", hex(b0), hex(b1))
		return nil
	case "save":
		if err := dev.Save(); err != nil {
			return err
		}
		fmt.Println("settings saved to flash")
		return nil
	case "defaults":
		return dev.Defaults()
	case "time":
		best, all, err := dev.TimeOffset(opts.n)
		if err != nil {
			return err
		}
		p := best.PPS
		fmt.Printf("pps  count %d  intervals %d  crystal %+.2f ppm  jitter %.2f us\n", p.Count, p.Intervals, p.CrystalPPM, p.JitterUS)
		fmt.Printf("gps  %s\n", best.BoardUTC.UTC().Format("2006-01-02 15:04:05.000000"))
		fmt.Printf("host behind gps by %s  (best round trip %s, error under %s)\n",
			best.Offset.Round(time.Microsecond), best.RTT.Round(time.Microsecond), (best.RTT / 2).Round(time.Microsecond))
		var offs []float64
		for _, s := range all {
			offs = append(offs, float64(s.Offset.Microseconds()))
		}
		fmt.Printf("all %d samples: round trip %s to %s, offset spread %.0f us\n", len(all),
			all[0].RTT.Round(time.Microsecond), all[len(all)-1].RTT.Round(time.Microsecond), spread(offs))
		return nil
	case "chrony":
		c, err := scopehub.DialChrony(opts.sock)
		if err != nil {
			return fmt.Errorf("chrony socket %s: %w (add a refclock SOCK line to chrony.conf)", opts.sock, err)
		}
		defer c.Close()
		for {
			best, _, err := dev.TimeOffset(opts.n)
			if err == nil {
				err = c.Send(best.HostMid, best.Offset)
			}
			if err != nil {
				fmt.Fprintln(os.Stderr, err)
			}
			time.Sleep(opts.every)
		}
	case "gps-reset":
		if len(args) > 1 || (len(args) == 1 && args[0] != "cold") {
			return fmt.Errorf("usage: gps-reset [cold]")
		}
		return dev.GPSReset(len(args) == 1)
	case "reboot":
		return dev.Reboot()
	case "bootsel":
		return dev.Bootsel()
	default:
		return fmt.Errorf("unknown command %q\n\n%s", cmd, usage)
	}
}

func spread(v []float64) float64 {
	if len(v) == 0 {
		return 0
	}
	lo, hi := v[0], v[0]
	for _, x := range v {
		lo, hi = min(lo, x), max(hi, x)
	}
	return hi - lo
}

func twoInts(args []string, use string) (int, int, error) {
	if len(args) != 2 {
		return 0, 0, fmt.Errorf("usage: %s", use)
	}
	a, err1 := strconv.Atoi(args[0])
	b, err2 := strconv.Atoi(args[1])
	if err1 != nil || err2 != nil {
		return 0, 0, fmt.Errorf("usage: %s", use)
	}
	return a, b, nil
}

func val(ok bool, format string, v float64) string {
	if !ok {
		return "--"
	}
	return fmt.Sprintf(format, v)
}

func yes(b bool) string {
	if b {
		return "yes"
	}
	return "no"
}

func printAux(a *scopehub.Aux) {
	fmt.Printf("led auto %s  min %.0f%%  max %.0f%%  dark %.3g lux  bright %.3g lux  light sensor range %d, raw %d\n",
		yes(a.LEDAuto), a.LED.MinPct, a.LED.MaxPct, a.LED.DarkLux, a.LED.BrightLux, a.ALSRange, a.ALSRaw)
	fmt.Printf("gnss in view %d  tracked %d  best %d dB-Hz  hdop %.2f\n", a.SatsInView, a.SatsTracked, a.MaxCNR, a.HDOP)
	fmt.Printf("settings saved %s  unsaved changes %s  last reset by watchdog %s  dc outputs kept through reset %s\n",
		yes(a.SettingsStored), yes(a.SettingsDirty), yes(a.WatchdogReset), yes(a.PCARetained))
}

func printDew(w *scopehub.Dew) {
	auto := []string{}
	for i, on := range w.Auto {
		if on {
			auto = append(auto, strconv.Itoa(i+1))
		}
	}
	ports := "none"
	if len(auto) > 0 {
		ports = strings.Join(auto, ",")
	}
	fmt.Printf("air %s C  dew point %s C  margin %s C  auto duty %d%%  auto ports %s\n",
		val(w.Valid, "%.2f", w.AmbientC), val(w.Valid, "%.2f", w.DewpointC), val(w.Valid, "%.2f", w.MarginC), w.AutoDuty, ports)
	fmt.Printf("offset %.2f C  start %.2f C  full %.2f C  min %.0f%%  max %.0f%%  fail %.0f%%\n",
		w.TempOffsetC, w.StartC, w.FullC, w.MinPct, w.MaxPct, w.FailPct)
}

func printStatus(s *scopehub.Status) {
	fmt.Printf("uptime %s  local 5V good %s  host %s  led %d%%  dc pwm %.2f Hz  led pwm %.0f Hz\n",
		s.Uptime.Truncate(time.Second), yes(s.Local5VGood), yes(s.HostMounted), s.LEDBrightness, s.DCPWMHz, s.LEDPWMHz)
	fmt.Printf("input  %s V  %s A\n", val(s.Input.Valid, "%.3f", s.Input.Volts), val(s.Input.Valid, "%.3f", s.Input.Amps))

	fmt.Println("dc  switch pwm  duty  on   volts    amps  amps_on")
	for _, d := range s.DC {
		fmt.Printf("%2d  %-6s %-4s %3d%%  %-3s %7s %7s %8s\n", d.Port, map[bool]string{true: "on", false: "off"}[d.Enabled],
			map[bool]string{true: "on", false: "off"}[d.PWM], d.Duty, yes(d.On),
			val(d.VoltsValid, "%.3f", d.Volts), val(d.AmpsValid, "%.3f", d.Amps), val(d.AmpsOnValid, "%.3f", d.AmpsOn))
	}
	fmt.Println("usb attached enabled  volts    amps")
	for _, u := range s.USB {
		fmt.Printf("%2d  %-8s %-7s %7s %7s\n", u.Port, yes(u.Attached), yes(u.Enabled),
			val(u.Valid, "%.3f", u.Volts), val(u.Valid, "%.3f", u.Amps))
	}

	e := s.Env
	fmt.Printf("env  %s C  %s %%RH  %s hPa  %s C baro  %s lux%s\n",
		val(e.THValid, "%.2f", e.TempC), val(e.THValid, "%.1f", e.RHPct), val(e.BaroValid, "%.2f", e.PressureHPa),
		val(e.BaroValid, "%.2f", e.BaroTempC), val(e.LuxValid, "%.4f", e.Lux), map[bool]string{true: " (saturated)"}[e.LuxSaturated])

	g := s.GPS
	pos := "no fix"
	if g.Fix > 0 {
		pos = fmt.Sprintf("%.7f %.7f %.1f m", g.LatDeg, g.LonDeg, g.AltM)
	}
	pps := "none"
	if g.PPSSeen {
		pps = fmt.Sprintf("%d, last %s ago", g.PPSCount, g.PPSAge)
	}
	fmt.Printf("gps  nmea %s  fix %d  sats %d  %s  utc %s %s  pps %s\n",
		yes(g.Seen), g.Fix, g.Satellites, pos, strings.TrimSpace(g.UTC), g.Date, pps)
}
