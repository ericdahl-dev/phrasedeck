// Package confdir owns the helper's config directory: its file names, their formats and
// permissions. The running helper records what it serves here, and `phrased pair`, a separate
// process, reads it back.
package confdir

import (
	"bufio"
	"crypto/tls"
	"errors"
	"fmt"
	"io/fs"
	"net"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/ericdahl-dev/phrasedeck/helper/internal/auth"
	"github.com/ericdahl-dev/phrasedeck/helper/internal/lan"
	"github.com/ericdahl-dev/phrasedeck/helper/internal/privfile"
)

const (
	tokensFile  = "tokens.json"
	pairingFile = "pairing.json"
	// portFile and lanAddrFile hold what the running helper serves, for `phrased pair`.
	portFile    = "port"
	lanAddrFile = "lan-addr"
	// subnetsFile lists -lan subnets, one per line; # starts a comment.
	subnetsFile = "lan-subnets"
	certFile    = "lan-cert.pem"
	keyFile     = "lan-key.pem"
)

// Dir is a config directory, usually ~/.config/phrased.
type Dir struct{ path string }

func New(path string) Dir { return Dir{path} }

func (d Dir) file(name string) string { return filepath.Join(d.path, name) }

// Store opens the paired devices' token hashes.
func (d Dir) Store() (*auth.Store, error) { return auth.NewStore(d.file(tokensFile)) }

// Pairer checks codes against the pairing window that OpenPairingWindow opens.
func (d Dir) Pairer(store *auth.Store) *auth.Pairer {
	return auth.NewPairer(d.file(pairingFile), store)
}

// OpenPairingWindow opens a pairing window from now and returns its code.
func (d Dir) OpenPairingWindow(now time.Time) (string, error) {
	return auth.OpenWindow(d.file(pairingFile), now)
}

// Cert returns the LAN certificate and its fingerprint, making them on first use.
func (d Dir) Cert() (tls.Certificate, string, error) {
	return lan.LoadOrCreateCert(d.file(certFile), d.file(keyFile))
}

// Fingerprint reads the LAN certificate's fingerprint without making one.
func (d Dir) Fingerprint() (string, error) { return lan.Fingerprint(d.file(certFile)) }

// Subnets returns the subnets listed in the config dir, or none when there's no list.
func (d Dir) Subnets() ([]string, error) {
	f, err := os.Open(d.file(subnetsFile))
	if err != nil {
		return nil, nil // no list (as before, an unopenable one counts as none)
	}
	defer f.Close()
	var values []string
	sc := bufio.NewScanner(f)
	for sc.Scan() {
		line, _, _ := strings.Cut(sc.Text(), "#")
		if line = strings.TrimSpace(line); line != "" {
			values = append(values, line)
		}
	}
	if err := sc.Err(); err != nil {
		return nil, fmt.Errorf("%s: %w", subnetsFile, err)
	}
	return values, nil
}

// RecordPort records the tailnet port the helper listens on.
func (d Dir) RecordPort(port int) error {
	return privfile.Write(d.file(portFile), []byte(strconv.Itoa(port)+"\n"))
}

// RecordLAN records the LAN address and port `phrased pair` puts in the QR code. An empty ip
// (LAN off, or no LAN address) removes the record, so a stale route never reaches the QR code.
func (d Dir) RecordLAN(ip string, port int) error {
	if ip == "" {
		if err := os.Remove(d.file(lanAddrFile)); err != nil && !errors.Is(err, fs.ErrNotExist) {
			return err
		}
		return nil
	}
	return privfile.Write(d.file(lanAddrFile), []byte(net.JoinHostPort(ip, strconv.Itoa(port))+"\n"))
}

// Advertised returns what the running helper recorded: its tailnet port and its LAN address
// and port. Anything not recorded, or unreadable, is 0 or "".
func (d Dir) Advertised() (port int, lanIP string, lanPort int) {
	if b, err := os.ReadFile(d.file(portFile)); err == nil {
		if p, err := strconv.Atoi(strings.TrimSpace(string(b))); err == nil && validPort(p) {
			port = p
		}
	}
	if b, err := os.ReadFile(d.file(lanAddrFile)); err == nil {
		ip, p, err := net.SplitHostPort(strings.TrimSpace(string(b)))
		if n, perr := strconv.Atoi(p); err == nil && perr == nil && validPort(n) {
			lanIP, lanPort = ip, n
		}
	}
	return port, lanIP, lanPort
}

func validPort(p int) bool { return p > 0 && p < 65536 }
