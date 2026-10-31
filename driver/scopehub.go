// Package scopehub is the host driver for the scope_hub USB hub controller.
//
// The board exposes one vendor-defined USB HID interface, so no driver install
// is needed. The host reads device info and status feature reports and writes
// a third to run commands. On Linux the hidraw node needs a udev rule:
//
//	KERNEL=="hidraw*", ATTRS{idVendor}=="1209", ATTRS{idProduct}=="0001", MODE="0660", TAG+="uaccess"
package scopehub

import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"strings"
	"sync"
	"time"
)

// Errors returned by commands.
var (
	ErrOutOfRange    = errors.New("scopehub: value out of range")
	ErrCommandFailed = errors.New("scopehub: board failed to apply the command")
	ErrProtocol      = errors.New("scopehub: firmware protocol version does not match the driver")
)

// DeviceInfo identifies an attached board before it is opened.
type DeviceInfo struct {
	Serial  string `json:"serial"`  // RP2350 unique ID, stable per board
	Product string `json:"product"` // USB product string
	Path    string `json:"path"`    // platform device locator
}

// Info is the board's identity, read once at Open.
type Info struct {
	ProtoVersion    int    `json:"proto_version"`
	FirmwareVersion string `json:"firmware_version"`
	Serial          string `json:"serial"`
	DCPorts         int    `json:"dc_ports"`
	USBPorts        int    `json:"usb_ports"`
}

// Reading is one voltage and current measurement.
type Reading struct {
	Valid bool    `json:"valid"`
	Volts float64 `json:"volts"`
	Amps  float64 `json:"amps"`
}

// DCPort is one switched 12 V output.
type DCPort struct {
	Port        int     `json:"port"`
	Enabled     bool    `json:"enabled"`
	PWM         bool    `json:"pwm"`
	Duty        int     `json:"duty"`
	On          bool    `json:"on"`
	VoltsValid  bool    `json:"volts_valid"`
	Volts       float64 `json:"volts"`
	AmpsValid   bool    `json:"amps_valid"`
	Amps        float64 `json:"amps"`
	AmpsOnValid bool    `json:"amps_on_valid"`
	AmpsOn      float64 `json:"amps_on"`
}

// USBPort is one USB-C downstream port.
type USBPort struct {
	Port     int  `json:"port"`
	Attached bool `json:"attached"`
	Enabled  bool `json:"enabled"` // false while the firmware forces VBUS off
	Reading
}

// Env holds the environment sensor readings.
type Env struct {
	THValid      bool    `json:"th_valid"` // SHT45
	TempC        float64 `json:"temp_c"`
	RHPct        float64 `json:"rh_pct"`
	BaroValid    bool    `json:"baro_valid"` // BMP581
	PressureHPa  float64 `json:"pressure_hpa"`
	BaroTempC    float64 `json:"baro_temp_c"`
	LuxValid     bool    `json:"lux_valid"` // VEML7700
	Lux          float64 `json:"lux"`
	LuxSaturated bool    `json:"lux_saturated"`
}

// GPS holds the NEO-M9N state.
type GPS struct {
	Seen       bool          `json:"seen"` // NMEA received in the last 3 s
	RMCValid   bool          `json:"rmc_valid"`
	Fix        int           `json:"fix"` // GGA fix quality, 0 = none
	Satellites int           `json:"satellites"`
	LatDeg     float64       `json:"lat_deg"`
	LonDeg     float64       `json:"lon_deg"`
	AltM       float64       `json:"alt_m"`
	UTC        string        `json:"utc"`  // hhmmss.ss
	Date       string        `json:"date"` // ddmmyy
	PPSSeen    bool          `json:"pps_seen"`
	PPSCount   uint32        `json:"pps_count"`
	PPSAge     time.Duration `json:"pps_age_ns"`
}

// Status is one snapshot of every reading and output state.
type Status struct {
	Uptime        time.Duration `json:"uptime_ns"`
	Local5VGood   bool          `json:"local_5v_good"`
	HostMounted   bool          `json:"host_mounted"`
	PCAPresent    bool          `json:"pca_present"`
	LEDBrightness int           `json:"led_brightness"`
	DCPWMHz       float64       `json:"dc_pwm_hz"`
	LEDPWMHz      float64       `json:"led_pwm_hz"`
	Input         Reading       `json:"input"`
	DC            []DCPort      `json:"dc"`
	USB           []USBPort     `json:"usb"`
	Env           Env           `json:"env"`
	GPS           GPS           `json:"gps"`
}

// Device is an open board. Its methods are safe for concurrent use.
type Device struct {
	mu       sync.Mutex
	t        transport
	info     Info
	seq      uint8
	protoErr error // set when the firmware protocol does not match
}

// List returns every attached board.
func List() ([]DeviceInfo, error) {
	devs, err := listHID(VID, PID)
	if err != nil {
		return nil, err
	}
	out := make([]DeviceInfo, 0, len(devs))
	for _, d := range devs {
		out = append(out, DeviceInfo{Serial: d.serial, Product: d.product, Path: d.path})
	}
	return out, nil
}

// Open opens the board with the given serial, or the first board when serial is empty.
func Open(serial string) (*Device, error) {
	devs, err := listHID(VID, PID)
	if err != nil {
		return nil, err
	}
	for _, d := range devs {
		if serial != "" && !strings.EqualFold(d.serial, serial) {
			continue
		}
		t, err := openHID(d.path)
		if err != nil {
			return nil, err
		}
		dev := &Device{t: t}
		if err := dev.readInfo(); err != nil {
			t.close()
			return nil, err
		}
		return dev, nil
	}
	if serial != "" {
		return nil, fmt.Errorf("scopehub: no board with serial %s", serial)
	}
	return nil, errors.New("scopehub: no board found")
}

// Close releases the device.
func (d *Device) Close() error {
	d.mu.Lock()
	defer d.mu.Unlock()
	return d.t.close()
}

// Info returns the identity read at Open.
func (d *Device) Info() Info { return d.info }

func cstring(b []byte) string {
	if i := bytes.IndexByte(b, 0); i >= 0 {
		b = b[:i]
	}
	return string(b)
}

func (d *Device) readInfo() error {
	buf, err := d.t.getFeature(reportInfo, infoSize)
	if err != nil {
		return fmt.Errorf("scopehub: read info: %w", err)
	}
	var w wireInfo
	if err := binary.Read(bytes.NewReader(buf), binary.LittleEndian, &w); err != nil {
		return fmt.Errorf("scopehub: decode info: %w", err)
	}
	if w.ProtoVersion != protoVersion {
		d.protoErr = fmt.Errorf("%w: firmware %d, driver %d", ErrProtocol, w.ProtoVersion, protoVersion)
	}
	d.info = Info{
		ProtoVersion:    int(w.ProtoVersion),
		FirmwareVersion: cstring(w.FWVersion[:]),
		Serial:          cstring(w.Serial[:]),
		DCPorts:         int(w.DCPorts),
		USBPorts:        int(w.USBPorts),
	}
	return nil
}

// Status reads every reading and output state.
func (d *Device) Status() (*Status, error) {
	if d.protoErr != nil {
		return nil, d.protoErr
	}
	d.mu.Lock()
	buf, err := d.t.getFeature(reportStatus, statusSize)
	d.mu.Unlock()
	if err != nil {
		return nil, fmt.Errorf("scopehub: read status: %w", err)
	}
	var w wireStatus
	if err := binary.Read(bytes.NewReader(buf), binary.LittleEndian, &w); err != nil {
		return nil, fmt.Errorf("scopehub: decode status: %w", err)
	}
	has := func(bit uint32) bool { return w.Flags&bit != 0 }

	s := &Status{
		Uptime:        time.Duration(w.UptimeMs) * time.Millisecond,
		Local5VGood:   has(stLocal5VGood),
		HostMounted:   has(stHostMounted),
		PCAPresent:    has(stPCAPresent),
		LEDBrightness: int(w.LEDPct),
		DCPWMHz:       float64(w.DCPWMHz),
		LEDPWMHz:      float64(w.LEDPWMHz),
		Input:         Reading{Valid: has(stInputValid), Volts: float64(w.InVolts), Amps: float64(w.InAmps)},
		Env: Env{
			THValid:      has(stSHTValid),
			TempC:        float64(w.TempC),
			RHPct:        float64(w.RHPct),
			BaroValid:    has(stBMPValid),
			PressureHPa:  float64(w.PressureHPa),
			BaroTempC:    float64(w.BaroTempC),
			LuxValid:     has(stALSValid),
			Lux:          float64(w.Lux),
			LuxSaturated: has(stALSSaturated),
		},
		GPS: GPS{
			Seen:       has(stGPSSeen),
			RMCValid:   has(stGPSRMCValid),
			Fix:        int(w.GPSFix),
			Satellites: int(w.GPSSats),
			LatDeg:     w.LatDeg,
			LonDeg:     w.LonDeg,
			AltM:       float64(w.AltM),
			UTC:        cstring(w.UTC[:]),
			Date:       cstring(w.Date[:]),
			PPSSeen:    has(stPPSSeen),
			PPSCount:   w.PPSCount,
		},
	}
	if s.GPS.PPSSeen {
		s.GPS.PPSAge = time.Duration(w.PPSAgeMs) * time.Millisecond
	}
	for i, c := range w.DC {
		s.DC = append(s.DC, DCPort{
			Port:        i + 1,
			Enabled:     c.Flags&dcEnabled != 0,
			PWM:         c.Flags&dcPWM != 0,
			Duty:        int(c.Duty),
			On:          c.Flags&dcOn != 0,
			VoltsValid:  c.Flags&dcVValid != 0,
			Volts:       float64(c.Volts),
			AmpsValid:   c.Flags&dcAValid != 0,
			Amps:        float64(c.Amps),
			AmpsOnValid: c.Flags&dcAOnValid != 0,
			AmpsOn:      float64(c.AmpsOn),
		})
	}
	for i, u := range w.USB {
		s.USB = append(s.USB, USBPort{
			Port:     i + 1,
			Attached: u.Flags&usbAttached != 0,
			Enabled:  u.Flags&usbEnabled != 0,
			Reading:  Reading{Valid: u.Flags&usbValid != 0, Volts: float64(u.Volts), Amps: float64(u.Amps)},
		})
	}
	return s, nil
}

// command writes one command report and reads back its result.
func (d *Device) command(op, port uint8, value float32) (float32, error) {
	if d.protoErr != nil && op != cmdReboot && op != cmdBootsel {
		return 0, d.protoErr
	}
	d.mu.Lock()
	defer d.mu.Unlock()
	d.seq++
	var out bytes.Buffer
	binary.Write(&out, binary.LittleEndian, wireCommand{Seq: d.seq, Op: op, Port: port, Value: value})
	if err := d.t.setFeature(reportCommand, out.Bytes()); err != nil {
		return 0, fmt.Errorf("scopehub: send command: %w", err)
	}
	if op == cmdReboot || op == cmdBootsel {
		return 0, nil // the board resets about 50 ms later
	}
	buf, err := d.t.getFeature(reportCommand, commandSize)
	if err != nil {
		return 0, fmt.Errorf("scopehub: read command result: %w", err)
	}
	var r wireCommand
	if err := binary.Read(bytes.NewReader(buf), binary.LittleEndian, &r); err != nil {
		return 0, fmt.Errorf("scopehub: decode command result: %w", err)
	}
	if r.Seq != d.seq || r.Op != op {
		return 0, fmt.Errorf("scopehub: result for seq %d op %d, sent seq %d op %d", r.Seq, r.Op, d.seq, op)
	}
	for tries := 0; r.Status == resPending && tries < 50; tries++ {
		time.Sleep(10 * time.Millisecond)
		if buf, err = d.t.getFeature(reportCommand, commandSize); err != nil {
			return 0, fmt.Errorf("scopehub: read command result: %w", err)
		}
		if err := binary.Read(bytes.NewReader(buf), binary.LittleEndian, &r); err != nil {
			return 0, fmt.Errorf("scopehub: decode command result: %w", err)
		}
	}
	switch r.Status {
	case resOK:
		return r.Value, nil
	case resBadArgs:
		return 0, ErrOutOfRange
	case resFailed:
		return 0, ErrCommandFailed
	case resUnknownOp:
		return 0, errors.New("scopehub: firmware does not support this command")
	default:
		return 0, fmt.Errorf("scopehub: unexpected result code %d", r.Status)
	}
}

// SetDCEnabled switches DC output port (1..6) on at its duty cycle, or off.
func (d *Device) SetDCEnabled(port int, on bool) error {
	if port < 1 || port > 6 {
		return fmt.Errorf("%w: DC port %d, valid 1..6", ErrOutOfRange, port)
	}
	v := float32(0)
	if on {
		v = 1
	}
	_, err := d.command(cmdDCEnable, uint8(port), v)
	return err
}

// SetDCPWM makes DC output port (1..6) run at its duty cycle
func (d *Device) SetDCPWM(port int, on bool) error {
	if port < 1 || port > 6 {
		return fmt.Errorf("%w: DC port %d, valid 1..6", ErrOutOfRange, port)
	}
	v := float32(0)
	if on {
		v = 1
	}
	_, err := d.command(cmdDCPWM, uint8(port), v)
	return err
}

// SetDC sets DC output port's (1..6) duty cycle in percent. 100 is steady
// on; less runs slow PWM for heaters. It does not switch the output on or off,
// and it ends dew auto control on that port.
func (d *Device) SetDC(port, duty int) error {
	if port < 1 || port > 6 {
		return fmt.Errorf("%w: DC port %d, valid 1..6", ErrOutOfRange, port)
	}
	_, err := d.command(cmdDC, uint8(port), float32(duty))
	return err
}

// SetUSB enables a USB port (1..6), or forces its VBUS off.
func (d *Device) SetUSB(port int, enabled bool) error {
	if port < 1 || port > 6 {
		return fmt.Errorf("%w: USB port %d, valid 1..6", ErrOutOfRange, port)
	}
	v := float32(0)
	if enabled {
		v = 1
	}
	_, err := d.command(cmdUSB, uint8(port), v)
	return err
}

// SetLEDBrightness sets all indicator LEDs to pct percent (0..100).
func (d *Device) SetLEDBrightness(pct int) error {
	_, err := d.command(cmdLED, 0, float32(pct))
	return err
}

// SetDCPWMHz sets the DC output PWM frequency (0.1..10 Hz) and returns the applied value.
func (d *Device) SetDCPWMHz(hz float64) (float64, error) {
	v, err := d.command(cmdDCPWMHz, 0, float32(hz))
	return float64(v), err
}

// DewSettings tune the open-loop dew-heater control.
type DewSettings struct {
	TempOffsetC float64 `json:"temp_offset_c"`
	StartC      float64 `json:"start_c"`
	FullC       float64 `json:"full_c"`
	MinPct      float64 `json:"min_pct"`
	MaxPct      float64 `json:"max_pct"`
	FailPct     float64 `json:"fail_pct"`
}

// Dew is the dew controller's settings and latest computation.
type Dew struct {
	DewSettings
	Auto      [6]bool `json:"auto"` // DC ports 1..6 under auto control
	AutoDuty  int     `json:"auto_duty"`
	Valid     bool    `json:"valid"`
	AmbientC  float64 `json:"ambient_c"`
	DewpointC float64 `json:"dewpoint_c"`
	MarginC   float64 `json:"margin_c"`
}

// Dew reads the dew controller state.
func (d *Device) Dew() (*Dew, error) {
	if d.protoErr != nil {
		return nil, d.protoErr
	}
	d.mu.Lock()
	buf, err := d.t.getFeature(reportDew, dewSize)
	d.mu.Unlock()
	if err != nil {
		return nil, fmt.Errorf("scopehub: read dew: %w", err)
	}
	var w wireDew
	if err := binary.Read(bytes.NewReader(buf), binary.LittleEndian, &w); err != nil {
		return nil, fmt.Errorf("scopehub: decode dew: %w", err)
	}
	r := &Dew{
		DewSettings: DewSettings{
			TempOffsetC: float64(w.TempOffsetC),
			StartC:      float64(w.StartC),
			FullC:       float64(w.FullC),
			MinPct:      float64(w.MinPct),
			MaxPct:      float64(w.MaxPct),
			FailPct:     float64(w.FailPct),
		},
		AutoDuty:  int(w.AutoDuty),
		Valid:     w.Flags&dewfValid != 0,
		AmbientC:  float64(w.AmbientC),
		DewpointC: float64(w.DewpointC),
		MarginC:   float64(w.MarginC),
	}
	for i := range r.Auto {
		r.Auto[i] = w.AutoMask&(1<<i) != 0
	}
	return r, nil
}

// SetDCAuto puts DC port (1..6) under dew auto control, or back to manual.
func (d *Device) SetDCAuto(port int, on bool) error {
	if port < 1 || port > 6 {
		return fmt.Errorf("%w: DC port %d, valid 1..6", ErrOutOfRange, port)
	}
	v := float32(0)
	if on {
		v = 1
	}
	_, err := d.command(cmdDCAuto, uint8(port), v)
	return err
}

// SetDewSettings writes every dew setting.
func (d *Device) SetDewSettings(s DewSettings) error {
	for _, kv := range []struct {
		id uint8
		v  float64
	}{
		{dewTempOffset, s.TempOffsetC}, {dewStart, s.StartC}, {dewFull, s.FullC},
		{dewMin, s.MinPct}, {dewMax, s.MaxPct}, {dewFail, s.FailPct},
	} {
		if _, err := d.command(cmdDewSet, kv.id, float32(kv.v)); err != nil {
			return err
		}
	}
	return nil
}

// LEDSettings tune automatic LED brightness.
type LEDSettings struct {
	MinPct    float64 `json:"min_pct"`
	MaxPct    float64 `json:"max_pct"`
	DarkLux   float64 `json:"dark_lux"`
	BrightLux float64 `json:"bright_lux"`
}

// Aux holds settings for LEDs, light-sensor, GNSS
type Aux struct {
	LEDAuto        bool        `json:"led_auto"`
	LED            LEDSettings `json:"led"`
	ALSRange       int         `json:"als_range"` // 0 = most sensitive of 9 steps
	ALSRaw         int         `json:"als_raw"`
	ALSLuxPerCount float64     `json:"als_lux_per_count"`
	SatsInView     int         `json:"sats_in_view"`
	SatsTracked    int         `json:"sats_tracked"` // satellites with a signal
	MaxCNR         int         `json:"max_cnr_dbhz"`
	HDOP           float64     `json:"hdop"`
	SettingsStored bool        `json:"settings_stored"`
	SettingsDirty  bool        `json:"settings_dirty"` // live settings differ from flash
	WatchdogReset  bool        `json:"watchdog_reset"` // the last reset came from the watchdog
	PCARetained    bool        `json:"pca_retained"`   // DC outputs survived the last MCU reset
}

// Aux reads the auxiliary state.
func (d *Device) Aux() (*Aux, error) {
	if d.protoErr != nil {
		return nil, d.protoErr
	}
	d.mu.Lock()
	buf, err := d.t.getFeature(reportAux, auxSize)
	d.mu.Unlock()
	if err != nil {
		return nil, fmt.Errorf("scopehub: read aux: %w", err)
	}
	var w wireAux
	if err := binary.Read(bytes.NewReader(buf), binary.LittleEndian, &w); err != nil {
		return nil, fmt.Errorf("scopehub: decode aux: %w", err)
	}
	return &Aux{
		LEDAuto: w.LEDAuto != 0,
		LED: LEDSettings{MinPct: float64(w.LEDMinPct), MaxPct: float64(w.LEDMaxPct),
			DarkLux: float64(w.LEDDarkLux), BrightLux: float64(w.LEDBrightLux)},
		ALSRange:       int(w.ALSRange),
		ALSRaw:         int(w.ALSRaw),
		ALSLuxPerCount: float64(w.ALSLuxPerCount),
		SatsInView:     int(w.SatsInView),
		SatsTracked:    int(w.SatsTracked),
		MaxCNR:         int(w.MaxCNR),
		HDOP:           float64(w.HDOP),
		SettingsStored: w.Flags&auxSettingsStored != 0,
		SettingsDirty:  w.Flags&auxSettingsDirty != 0,
		WatchdogReset:  w.Flags&auxWatchdogReset != 0,
		PCARetained:    w.Flags&auxPCARetained != 0,
	}, nil
}

// SetLEDAuto sets LED brightness auto
func (d *Device) SetLEDAuto(on bool) error {
	v := float32(0)
	if on {
		v = 1
	}
	_, err := d.command(cmdLEDAuto, 0, v)
	return err
}

// SetLEDSettings writes every LED auto setting.
func (d *Device) SetLEDSettings(s LEDSettings) error {
	for _, kv := range []struct {
		id uint8
		v  float64
	}{{ledMin, s.MinPct}, {ledMax, s.MaxPct}, {ledDark, s.DarkLux}, {ledBright, s.BrightLux}} {
		if _, err := d.command(cmdLEDSet, kv.id, float32(kv.v)); err != nil {
			return err
		}
	}
	return nil
}

// Scan probes both I2C buses and returns the 7-bit addresses that answer
func (d *Device) Scan() (i2c0, i2c1 []uint8, err error) {
	if d.protoErr != nil {
		return nil, nil, d.protoErr
	}
	d.mu.Lock()
	buf, err := d.t.getFeature(reportScan, scanSize)
	d.mu.Unlock()
	if err != nil {
		return nil, nil, fmt.Errorf("scopehub: I2C scan: %w", err)
	}
	decode := func(m []byte) []uint8 {
		var out []uint8
		for a := 0; a < 128; a++ {
			if m[a/8]&(1<<(a%8)) != 0 {
				out = append(out, uint8(a))
			}
		}
		return out
	}
	return decode(buf[:16]), decode(buf[16:32]), nil
}

// Save writes the live settings to flash
func (d *Device) Save() error {
	_, err := d.command(cmdSave, 0, 0)
	return err
}

// Defaults restores the default settings in RAM
func (d *Device) Defaults() error {
	_, err := d.command(cmdDefaults, 0, 0)
	return err
}

// GPSReset restarts the GPS receiver.
func (d *Device) GPSReset(cold bool) error {
	v := float32(0)
	if cold {
		v = 1
	}
	_, err := d.command(cmdGPSReset, 0, v)
	return err
}

// Reboot restarts the firmware.
func (d *Device) Reboot() error {
	_, err := d.command(cmdReboot, 0, 0)
	return err
}

// Bootsel reboots into the RP2350 USB boot ROM for flashing.
func (d *Device) Bootsel() error {
	_, err := d.command(cmdBootsel, 0, 0)
	return err
}
