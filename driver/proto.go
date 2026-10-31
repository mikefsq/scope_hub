package scopehub

// Wire layouts of the HID feature reports.

const (
	VID uint16 = 0x1209
	PID uint16 = 0x0001

	protoVersion = 1
)

const (
	reportInfo    = 1
	reportStatus  = 2
	reportCommand = 3
	reportDew     = 4
	reportAux     = 5
	reportScan    = 6
)

type wireInfo struct {
	ProtoVersion uint8
	DCPorts      uint8
	USBPorts     uint8
	Reserved     uint8
	FWVersion    [12]byte
	Serial       [20]byte
}

// Status flags.
const (
	stLocal5VGood = 1 << iota
	stHostMounted
	stPCAPresent
	stInputValid
	stSHTValid
	stBMPValid
	stALSValid
	stALSSaturated
	stGPSSeen
	stGPSRMCValid
	stPPSSeen
)

// DC flags.
const (
	dcOn = 1 << iota
	dcVValid
	dcAValid
	dcAOnValid
	dcEnabled
	dcPWM
)

// USB flags.
const (
	usbAttached = 1 << iota
	usbEnabled
	usbValid
)

type wireDC struct {
	Duty     uint8
	Flags    uint8
	Reserved [2]uint8
	Volts    float32
	Amps     float32
	AmpsOn   float32
}

type wireUSB struct {
	Flags    uint8
	Reserved [3]uint8
	Volts    float32
	Amps     float32
}

type wireStatus struct {
	UptimeMs    uint32
	Flags       uint32
	LEDPct      uint8
	GPSFix      uint8
	GPSSats     uint8
	Reserved    uint8
	DCPWMHz     float32
	LEDPWMHz    float32
	InVolts     float32
	InAmps      float32
	DC          [6]wireDC
	USB         [6]wireUSB
	TempC       float32
	RHPct       float32
	PressureHPa float32
	BaroTempC   float32
	Lux         float32
	LatDeg      float64
	LonDeg      float64
	AltM        float32
	UTC         [12]byte
	Date        [8]byte
	PPSCount    uint32
	PPSAgeMs    uint32
}

// Command ops and result codes.
const (
	cmdDC       = 1
	cmdUSB      = 2
	cmdLED      = 3
	cmdDCPWMHz  = 4
	cmdReboot   = 5
	cmdBootsel  = 6
	cmdGPSReset = 7
	cmdDCAuto   = 8
	cmdDewSet   = 9
	cmdLEDAuto  = 10
	cmdLEDSet   = 11
	cmdSave     = 12
	cmdDefaults = 13
	cmdDCEnable = 14
	cmdDCPWM    = 15

	resOK        = 0
	resBadArgs   = 1
	resFailed    = 2
	resUnknownOp = 3
	resPending   = 0xFF
)

type wireCommand struct {
	Seq    uint8
	Op     uint8
	Port   uint8
	Status uint8
	Value  float32
}

// Dew setting ids for cmdDewSet.
const (
	dewTempOffset = 1
	dewStart      = 2
	dewFull       = 3
	dewMin        = 4
	dewMax        = 5
	dewFail       = 6
)

const dewfValid = 1

// LED setting ids for cmdLEDSet.
const (
	ledMin    = 1
	ledMax    = 2
	ledDark   = 3
	ledBright = 4
)

// Aux flags.
const (
	auxSettingsStored = 1 << iota
	auxSettingsDirty
	auxWatchdogReset
	auxPCARetained
)

type wireAux struct {
	LEDAuto        uint8
	ALSRange       uint8
	SatsInView     uint8
	SatsTracked    uint8
	MaxCNR         uint8
	Flags          uint8
	ALSRaw         uint16
	LEDMinPct      float32
	LEDMaxPct      float32
	LEDDarkLux     float32
	LEDBrightLux   float32
	ALSLuxPerCount float32
	HDOP           float32
}

type wireDew struct {
	AutoMask    uint8
	Flags       uint8
	AutoDuty    uint8
	Reserved    uint8
	TempOffsetC float32
	StartC      float32
	FullC       float32
	MinPct      float32
	MaxPct      float32
	FailPct     float32
	AmbientC    float32
	DewpointC   float32
	MarginC     float32
}

const (
	dewSize     = 40
	auxSize     = 32
	scanSize    = 32
	infoSize    = 36
	statusSize  = 264
	commandSize = 8
)
