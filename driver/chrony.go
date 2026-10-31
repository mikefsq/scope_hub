package scopehub

import (
	"bytes"
	"encoding/binary"
	"net"
	"time"
)

// ChronySock sends samples to chrony's SOCK refclock driver, the interface
// gpsd uses. chrony creates the socket; configure it with, for example:
//
//	refclock SOCK /run/chrony.scopehub.sock refid SHUB precision 1e-3
type ChronySock struct {
	conn *net.UnixConn
}

// sockMagic is chrony's SOCK_MAGIC, "SOCK".
const sockMagic = 0x534f434b

// sockSample mirrors chrony's struct sock_sample on 64-bit Linux and macOS:
// struct timeval tv; double offset; int pulse; int leap; int _pad; int magic.
type sockSample struct {
	Sec    int64
	USec   int64
	Offset float64
	Pulse  int32
	Leap   int32
	Pad    int32
	Magic  int32
}

// DialChrony connects to the socket chrony listens on.
func DialChrony(path string) (*ChronySock, error) {
	c, err := net.DialUnix("unixgram", nil, &net.UnixAddr{Name: path, Net: "unixgram"})
	if err != nil {
		return nil, err
	}
	return &ChronySock{conn: c}, nil
}

// Send reports that at host time `at` the true time was `at + offset`.
func (c *ChronySock) Send(at time.Time, offset time.Duration) error {
	var b bytes.Buffer
	binary.Write(&b, binary.NativeEndian, sockSample{
		Sec:    at.Unix(),
		USec:   int64(at.Nanosecond() / 1000),
		Offset: offset.Seconds(),
		Magic:  sockMagic,
	})
	_, err := c.conn.Write(b.Bytes())
	return err
}

// Close closes the socket.
func (c *ChronySock) Close() error { return c.conn.Close() }
