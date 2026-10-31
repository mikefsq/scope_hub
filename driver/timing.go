package scopehub

import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"sort"
	"time"
)

// Time report layout, mirroring hid_time_t.
const (
	reportTime   = 7
	timeSize     = 40
	timeUTCValid = 1 << 0
	timePPSFresh = 1 << 1
)

type wireTime struct {
	DevUS        uint64
	PPSDevUS     uint64
	PPSUTCs      int64
	CrystalPPM   float32
	PPSJitterUS  float32
	PPSCount     uint32
	PPSIntervals uint16
	Flags        uint8
	Reserved     uint8
}

// ErrNoGPSTime: the board has no UTC-labeled PPS edge yet, or the pulses stopped.
var ErrNoGPSTime = errors.New("scopehub: no GPS time: no fresh PPS edge with a UTC label")

// PPSInfo describes the board's PPS timing.
type PPSInfo struct {
	Count      uint32  `json:"count"`
	Intervals  int     `json:"intervals"`   // consecutive good 1 s intervals behind the statistics, up to 32
	CrystalPPM float64 `json:"crystal_ppm"` // positive when the board crystal runs fast
	JitterUS   float64 `json:"jitter_us"`   // standard deviation of the measured intervals
	UTCValid   bool    `json:"utc_valid"`
	Fresh      bool    `json:"fresh"` // last edge less than 1.5 s ago
}

// TimeSample is one exchange of the time report.
type TimeSample struct {
	HostMid  time.Time     // host clock halfway through the exchange
	RTT      time.Duration // host time from request to reply
	BoardUTC time.Time     // GPS time on the board when it filled the reply
	Offset   time.Duration // BoardUTC minus HostMid: how far the host clock runs behind GPS
	PPS      PPSInfo
}

// TimeSample reads the time report once, bracketed by host clock readings.
// The board fills the reply inside the USB transfer, so its time lies inside
// the bracket; the midpoint is off by at most half the round trip.
func (d *Device) TimeSample() (*TimeSample, error) {
	if d.protoErr != nil {
		return nil, d.protoErr
	}
	d.mu.Lock()
	t1 := time.Now()
	buf, err := d.t.getFeature(reportTime, timeSize)
	t4 := time.Now()
	d.mu.Unlock()
	if err != nil {
		return nil, fmt.Errorf("scopehub: read time: %w", err)
	}
	var w wireTime
	if err := binary.Read(bytes.NewReader(buf), binary.LittleEndian, &w); err != nil {
		return nil, fmt.Errorf("scopehub: decode time: %w", err)
	}
	s := &TimeSample{
		RTT:     t4.Sub(t1),
		HostMid: t1.Add(t4.Sub(t1) / 2),
		PPS: PPSInfo{
			Count:      w.PPSCount,
			Intervals:  int(w.PPSIntervals),
			CrystalPPM: float64(w.CrystalPPM),
			JitterUS:   float64(w.PPSJitterUS),
			UTCValid:   w.Flags&timeUTCValid != 0,
			Fresh:      w.Flags&timePPSFresh != 0,
		},
	}
	if !s.PPS.UTCValid || !s.PPS.Fresh {
		return s, ErrNoGPSTime
	}
	// Board microseconds since the labeled edge, corrected for the crystal's error.
	since := float64(int64(w.DevUS-w.PPSDevUS)) / (1 + float64(w.CrystalPPM)*1e-6)
	s.BoardUTC = time.Unix(w.PPSUTCs, 0).Add(time.Duration(since * float64(time.Microsecond)))
	s.Offset = s.BoardUTC.Sub(s.HostMid)
	return s, nil
}

// TimeOffset takes n samples and returns the one with the shortest round trip,
// the least disturbed by USB scheduling and host load, as NTP clients do.
// It also returns every sample, sorted by round trip.
func (d *Device) TimeOffset(n int) (*TimeSample, []*TimeSample, error) {
	var all []*TimeSample
	var lastErr error
	for i := 0; i < n; i++ {
		s, err := d.TimeSample()
		switch {
		case errors.Is(err, ErrNoGPSTime), errors.Is(err, ErrProtocol):
			return nil, nil, err
		case err != nil:
			lastErr = err // a failed exchange only costs one sample
		default:
			all = append(all, s)
		}
		time.Sleep(3 * time.Millisecond)
	}
	if len(all) == 0 {
		return nil, nil, fmt.Errorf("scopehub: no time samples: %w", lastErr)
	}
	sort.Slice(all, func(i, j int) bool { return all[i].RTT < all[j].RTT })
	return all[0], all, nil
}
