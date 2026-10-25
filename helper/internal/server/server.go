// Package server is phrased's HTTP API: GET /health and POST /type.
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
)

type Config struct {
	Store      *auth.Store
	Typer      typer.Backend
	Hostname   string
	OS         string
	AllowHosts []string
	// LockWait is how long a request waits for the typing lock before 409. Default 1 second.
	LockWait time.Duration
}

type server struct {
	cfg   Config
	limit *rate.Limiter
	lock  chan struct{}
}

// New returns the helper's handler.
func New(cfg Config) http.Handler {
	if cfg.LockWait == 0 {
		cfg.LockWait = time.Second
	}
	s := &server{cfg: cfg, limit: rate.NewLimiter(10, 10), lock: make(chan struct{}, 1)}
	mux := http.NewServeMux()
	mux.HandleFunc("/health", s.health)
	mux.HandleFunc("/type", s.typeText)
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
	if r.Method != http.MethodPost {
		fail(w, http.StatusMethodNotAllowed, "use POST")
		return
	}
	if mt, _, err := mime.ParseMediaType(r.Header.Get("Content-Type")); err != nil || mt != "application/json" {
		fail(w, http.StatusUnsupportedMediaType, "Content-Type must be application/json")
		return
	}
	if r.ContentLength > MaxBody {
		fail(w, http.StatusRequestEntityTooLarge, "body over 64 KB")
		return
	}
	r.Body = http.MaxBytesReader(w, r.Body, MaxBody)
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
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		var tooBig *http.MaxBytesError
		if errors.As(err, &tooBig) {
			fail(w, http.StatusRequestEntityTooLarge, "body over 64 KB")
			return
		}
		fail(w, http.StatusBadRequest, "bad JSON: "+err.Error())
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

func reply(w http.ResponseWriter, code int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	json.NewEncoder(w).Encode(v)
}

func fail(w http.ResponseWriter, code int, msg string) {
	reply(w, code, map[string]string{"error": msg})
}
