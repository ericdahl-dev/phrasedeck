// Package server is phrased's HTTP API: GET /health, POST /type and POST /pair.
package server

import (
	"encoding/json"
	"errors"
	"log"
	"mime"
	"net/http"
	"strings"
	"time"
	"unicode/utf8"

	"golang.org/x/time/rate"

	"github.com/ericdahl-dev/phrasedeck/helper/internal/auth"
	"github.com/ericdahl-dev/phrasedeck/helper/internal/plan"
	"github.com/ericdahl-dev/phrasedeck/helper/internal/tailnet"
	"github.com/ericdahl-dev/phrasedeck/helper/internal/typer"
)

const (
	// MaxBody caps a request body.
	MaxBody = 64 << 10
	// MaxText caps the text of one phrase, in runes.
	MaxText = 10000
	// MaxDevice caps a device name, in runes.
	MaxDevice = 64
)

type Config struct {
	Store      *auth.Store
	Pairer     *auth.Pairer
	Typer      typer.Backend
	Hostname   string
	OS         string
	AllowHosts []string
	// LockWait is how long a request waits for the typing lock before 409. Default 1 second.
	LockWait time.Duration
}

type server struct {
	cfg       Config
	limit     *rate.Limiter
	pairLimit *rate.Limiter
	lock      chan struct{}
}

// New returns the helper's handler.
func New(cfg Config) http.Handler {
	if cfg.LockWait == 0 {
		cfg.LockWait = time.Second
	}
	s := &server{
		cfg:       cfg,
		limit:     rate.NewLimiter(10, 10),
		pairLimit: rate.NewLimiter(1, 3),
		lock:      make(chan struct{}, 1),
	}
	mux := http.NewServeMux()
	mux.HandleFunc("/health", s.health)
	mux.HandleFunc("/type", s.typeText)
	mux.HandleFunc("/pair", s.pair)
	return s.hostGuard(mux)
}

// hostGuard answers 403 to any Host that isn't this machine's tailnet name or IP, so a web
// page can't reach the helper through DNS rebinding.
func (s *server) hostGuard(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if !tailnet.HostAllowed(r.Host, s.cfg.AllowHosts) {
			fail(w, http.StatusForbidden, "host not allowed: "+r.Host)
			return
		}
		next.ServeHTTP(w, r)
	})
}

func (s *server) health(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		fail(w, http.StatusMethodNotAllowed, "use GET")
		return
	}
	reply(w, http.StatusOK, map[string]any{
		"hostname":         s.cfg.Hostname,
		"os":               s.cfg.OS,
		"accessibility_ok": s.cfg.Typer.Ready(),
	})
}

type typeRequest struct {
	Text  string    `json:"text"`
	Enter bool      `json:"enter"`
	Mode  plan.Mode `json:"mode"`
}

func (s *server) typeText(w http.ResponseWriter, r *http.Request) {
	if !postJSON(w, r) {
		return
	}
	tok, ok := strings.CutPrefix(r.Header.Get("Authorization"), "Bearer ")
	if !ok || !s.cfg.Store.Check(tok) {
		fail(w, http.StatusUnauthorized, "missing or invalid token")
		return
	}
	if !s.limit.Allow() {
		fail(w, http.StatusTooManyRequests, "too many requests")
		return
	}

	var req typeRequest
	if !decode(w, r, &req) {
		return
	}
	switch req.Mode {
	case "":
		req.Mode = plan.ModeAuto
	case plan.ModeAuto, plan.ModeType, plan.ModePaste:
	default:
		fail(w, http.StatusBadRequest, `mode must be "auto", "type" or "paste"`)
		return
	}
	if utf8.RuneCountInString(req.Text) > MaxText {
		fail(w, http.StatusRequestEntityTooLarge, "text over 10,000 characters")
		return
	}
	if !s.cfg.Typer.Ready() {
		fail(w, http.StatusServiceUnavailable, "can't type yet: grant Accessibility to phrased")
		return
	}

	t := time.NewTimer(s.cfg.LockWait)
	defer t.Stop()
	select {
	case s.lock <- struct{}{}:
		defer func() { <-s.lock }()
	case <-t.C:
		fail(w, http.StatusConflict, "busy typing another phrase")
		return
	}

	err := typer.Run(s.cfg.Typer, plan.Build(req.Text, req.Enter, req.Mode))
	switch {
	case err == nil:
		reply(w, http.StatusOK, map[string]any{"ok": true})
	case errors.Is(err, typer.ErrPasteUnsupported):
		fail(w, http.StatusNotImplemented, "paste is not supported yet on this computer; send single-line text under 300 characters, or mode \"type\"")
	default:
		log.Printf("type: %v", err)
		fail(w, http.StatusInternalServerError, "typing failed: "+err.Error())
	}
}

type pairRequest struct {
	Code   string `json:"code"`
	Device string `json:"device"`
}

func (s *server) pair(w http.ResponseWriter, r *http.Request) {
	if !postJSON(w, r) {
		return
	}
	if !s.pairLimit.Allow() {
		fail(w, http.StatusTooManyRequests, "too many pairing attempts")
		return
	}
	var req pairRequest
	if !decode(w, r, &req) {
		return
	}
	device := strings.TrimSpace(req.Device)
	if device == "" || utf8.RuneCountInString(device) > MaxDevice {
		fail(w, http.StatusBadRequest, "device must be 1 to 64 characters")
		return
	}
	token, err := s.cfg.Pairer.Pair(req.Code, device)
	switch {
	case err == nil:
		log.Printf("paired %q", device)
		reply(w, http.StatusOK, map[string]string{"token": token})
	case errors.Is(err, auth.ErrNoWindow):
		fail(w, http.StatusNotFound, err.Error())
	case errors.Is(err, auth.ErrWrongCode):
		fail(w, http.StatusUnauthorized, err.Error())
	default:
		log.Printf("pair: %v", err)
		fail(w, http.StatusInternalServerError, "pairing failed: "+err.Error())
	}
}

// postJSON answers 405, 415 or 413 unless r is a POST with a JSON body of at most MaxBody, and
// caps the body for the reads that follow.
func postJSON(w http.ResponseWriter, r *http.Request) bool {
	if r.Method != http.MethodPost {
		fail(w, http.StatusMethodNotAllowed, "use POST")
		return false
	}
	if mt, _, err := mime.ParseMediaType(r.Header.Get("Content-Type")); err != nil || mt != "application/json" {
		fail(w, http.StatusUnsupportedMediaType, "Content-Type must be application/json")
		return false
	}
	if r.ContentLength > MaxBody {
		fail(w, http.StatusRequestEntityTooLarge, "body over 64 KB")
		return false
	}
	r.Body = http.MaxBytesReader(w, r.Body, MaxBody)
	return true
}

// decode reads the JSON body into v, answering 413 or 400 on failure.
func decode(w http.ResponseWriter, r *http.Request, v any) bool {
	err := json.NewDecoder(r.Body).Decode(v)
	var tooBig *http.MaxBytesError
	switch {
	case err == nil:
		return true
	case errors.As(err, &tooBig):
		fail(w, http.StatusRequestEntityTooLarge, "body over 64 KB")
	default:
		fail(w, http.StatusBadRequest, "bad JSON: "+err.Error())
	}
	return false
}

func reply(w http.ResponseWriter, code int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	json.NewEncoder(w).Encode(v)
}

func fail(w http.ResponseWriter, code int, msg string) {
	reply(w, code, map[string]string{"error": msg})
}
