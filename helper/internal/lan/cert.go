package lan

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/hex"
	"encoding/pem"
	"errors"
	"fmt"
	"io/fs"
	"math/big"
	"os"
	"path/filepath"
	"time"

	"github.com/ericdahl-dev/phrasedeck/helper/internal/auth"
)

const (
	certFile = "lan-cert.pem"
	keyFile  = "lan-key.pem"
)

// LoadOrCreateCert returns the LAN listener's certificate from dir, making a self-signed
// ECDSA P-256 one on first use, and its fingerprint: the lowercase hex SHA-256 of the leaf's
// DER, which the app pins. The certificate is kept across restarts so pinned devices keep
// working.
func LoadOrCreateCert(dir string) (tls.Certificate, string, error) {
	certPath, keyPath := filepath.Join(dir, certFile), filepath.Join(dir, keyFile)
	if exists(certPath) && exists(keyPath) {
		// Never replace a pair that exists: a new certificate breaks every device's pin.
		cert, err := tls.LoadX509KeyPair(certPath, keyPath)
		if err != nil {
			return tls.Certificate{}, "", fmt.Errorf("LAN certificate %s or key %s: %w (fix them, or delete both "+
				"to make a new one and re-pair every device)", certPath, keyPath, err)
		}
		return cert, fingerprint(cert.Certificate[0]), nil
	}
	certPEM, keyPEM, err := newCert()
	if err != nil {
		return tls.Certificate{}, "", err
	}
	// The key goes first, so a crash between the two writes leaves a cert without its key,
	// which the next start replaces.
	if err := auth.WritePrivate(filepath.Join(dir, keyFile), keyPEM); err != nil {
		return tls.Certificate{}, "", err
	}
	if err := auth.WritePrivate(filepath.Join(dir, certFile), certPEM); err != nil {
		return tls.Certificate{}, "", err
	}
	cert, err := tls.X509KeyPair(certPEM, keyPEM)
	if err != nil {
		return tls.Certificate{}, "", err
	}
	return cert, fingerprint(cert.Certificate[0]), nil
}

// exists is false only when path is missing; any other stat error counts as existing, so it
// surfaces when the pair is loaded.
func exists(path string) bool {
	_, err := os.Stat(path)
	return !errors.Is(err, fs.ErrNotExist)
}

// Fingerprint reads the stored certificate's fingerprint without creating one, for
// `phrased pair`.
func Fingerprint(dir string) (string, error) {
	b, err := os.ReadFile(filepath.Join(dir, certFile))
	if err != nil {
		return "", err
	}
	block, _ := pem.Decode(b)
	if block == nil || block.Type != "CERTIFICATE" {
		return "", errors.New(certFile + ": no certificate")
	}
	return fingerprint(block.Bytes), nil
}

func fingerprint(der []byte) string {
	sum := sha256.Sum256(der)
	return hex.EncodeToString(sum[:])
}

func newCert() (certPEM, keyPEM []byte, err error) {
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		return nil, nil, err
	}
	serial, err := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 127))
	if err != nil {
		return nil, nil, err
	}
	now := time.Now()
	tmpl := &x509.Certificate{
		SerialNumber: serial,
		Subject:      pkix.Name{CommonName: "phrased"},
		NotBefore:    now.Add(-time.Hour),
		// The app pins this exact certificate rather than trusting a chain, so its lifetime
		// only needs to outlast the pairing.
		NotAfter:    now.AddDate(20, 0, 0),
		KeyUsage:    x509.KeyUsageDigitalSignature,
		ExtKeyUsage: []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
	}
	der, err := x509.CreateCertificate(rand.Reader, tmpl, tmpl, &key.PublicKey, key)
	if err != nil {
		return nil, nil, err
	}
	keyDER, err := x509.MarshalECPrivateKey(key)
	if err != nil {
		return nil, nil, err
	}
	return pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der}),
		pem.EncodeToMemory(&pem.Block{Type: "EC PRIVATE KEY", Bytes: keyDER}), nil
}
