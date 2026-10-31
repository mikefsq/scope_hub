package scopehub

import (
	"encoding/binary"
	"testing"
)

func TestWireSizes(t *testing.T) {
	for _, c := range []struct {
		name string
		v    any
		want int
	}{
		{"info", wireInfo{}, infoSize},
		{"dc", wireDC{}, 16},
		{"usb", wireUSB{}, 12},
		{"status", wireStatus{}, statusSize},
		{"command", wireCommand{}, commandSize},
		{"dew", wireDew{}, dewSize},
		{"aux", wireAux{}, auxSize},
		{"time", wireTime{}, timeSize},
		{"chrony", sockSample{}, 40},
	} {
		if got := binary.Size(c.v); got != c.want {
			t.Errorf("%s: %d bytes, firmware sends %d", c.name, got, c.want)
		}
	}
}
