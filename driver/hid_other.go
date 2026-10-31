//go:build !darwin && !linux && !windows

package scopehub

import "errors"

var errUnsupported = errors.New("scopehub: HID transport not implemented on this platform")

func listHID(vid, pid uint16) ([]hidDev, error) { return nil, errUnsupported }

func openHID(path string) (transport, error) { return nil, errUnsupported }
