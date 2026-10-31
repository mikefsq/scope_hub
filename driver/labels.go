package scopehub

import (
	"fmt"
	"time"
)

const (
	reportLabels = 8
	labelLen     = 16
	labelsSize   = 2 * 6 * labelLen
	MaxLabelLen  = labelLen - 1
)

// Labels are the user's port names, stored on the board.
type Labels struct {
	DC  [6]string `json:"dc"`
	USB [6]string `json:"usb"`
}

// Labels reads the port names.
func (d *Device) Labels() (Labels, error) {
	if d.protoErr != nil {
		return Labels{}, d.protoErr
	}
	d.mu.Lock()
	buf, err := d.t.getFeature(reportLabels, labelsSize)
	d.mu.Unlock()
	if err != nil {
		return Labels{}, fmt.Errorf("scopehub: read labels: %w", err)
	}
	var l Labels
	for i := 0; i < 6; i++ {
		l.DC[i] = cstring(buf[i*labelLen : (i+1)*labelLen])
		l.USB[i] = cstring(buf[(6+i)*labelLen : (7+i)*labelLen])
	}
	return l, nil
}

// SetLabels replaces every port name in flash
func (d *Device) SetLabels(l Labels) error {
	if d.protoErr != nil {
		return d.protoErr
	}
	buf := make([]byte, labelsSize)
	for i, names := range [][6]string{l.DC, l.USB} {
		for j, n := range names {
			if len(n) > MaxLabelLen {
				return fmt.Errorf("%w: name %q is longer than %d bytes", ErrOutOfRange, n, MaxLabelLen)
			}
			copy(buf[(i*6+j)*labelLen:], n)
		}
	}
	d.mu.Lock()
	err := d.t.setFeature(reportLabels, buf)
	d.mu.Unlock()
	if err != nil {
		return fmt.Errorf("scopehub: write labels: %w", err)
	}
	// The board writes flash from its main loop; read back to confirm.
	time.Sleep(100 * time.Millisecond)
	got, err := d.Labels()
	if err != nil {
		return err
	}
	if got != l {
		return fmt.Errorf("scopehub: labels read back as %v", got)
	}
	return nil
}

// SetDCLabel names DC port (1..6). An empty name clears it.
func (d *Device) SetDCLabel(port int, name string) error {
	return d.setLabel(func(l *Labels) *[6]string { return &l.DC }, port, name)
}

// SetUSBLabel names USB port (1..6). An empty name clears it.
func (d *Device) SetUSBLabel(port int, name string) error {
	return d.setLabel(func(l *Labels) *[6]string { return &l.USB }, port, name)
}

func (d *Device) setLabel(pick func(*Labels) *[6]string, port int, name string) error {
	if port < 1 || port > 6 {
		return fmt.Errorf("%w: port %d, valid 1..6", ErrOutOfRange, port)
	}
	l, err := d.Labels()
	if err != nil {
		return err
	}
	pick(&l)[port-1] = name
	return d.SetLabels(l)
}
