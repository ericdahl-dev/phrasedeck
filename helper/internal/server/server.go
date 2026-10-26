// Package server is phrased's HTTP API: GET /health, GET /info, POST /type and POST /pair.
package server

import (
	"encoding/json"
	"errors"
	"log"
	"mime"
	"net/http"
	"strings"
	"sync"
	"time"
	"unicode/utf8"

	"golang.org/x/time/rate"

	"github.com/ericdahl-dev/phrasedeck/helper/internal/auth"
	"github.com/ericdahl-dev/phrasedeck/helper/internal/plan"
	"github.com/ericdahl-dev/phrasedeck/helper/internal/tailnet"
	"github.com/ericdahl-dev/phrasedeck/helper/internal/typer"
	"github.com/ericdahl-dev/phrasedeck/helper/internal/typing"
)

const (
	// MaxBody caps a request body.
	MaxBody = 64 << 10
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
	// CertSHA256 and LANPort describe the LAN listener; both are empty when LAN is off.
	CertSHA256 string
	LANPort    int
	// Notice shows a desktop notification; nil shows none.
	Notice func(msg string)
}

type server struct {
	cfg       Config
	limit     *rate.Limiter
	pairLimit *rate.Limiter
	typist    *typing.Typist

	mu   sync.Mutex
	seen map[string]uint32 // panel Phrase id -> last edit counter typed
}

// New returns the helper's handler for one listener, accepting cfg.AllowHosts.
func New(cfg Config) http.Handler { return NewHelper(cfg).Handler(cfg.AllowHosts) }

// Helper holds the state every listener shares: tokens, rate limits, pairing lockout and the
// typing lock. Each listener gets its own Handler, with its own Host allowlist.
type Helper struct {
	s           *server
	mux, lanMux http.Handler
}

func NewHelper(cfg Config) *Helper {
	if cfg.LockWait == 0 {
		cfg.LockWait = time.Second
	}
	s := &server{
		cfg:       cfg,
		limit:     rate.NewLimiter(10, 10),
		pairLimit: rate.NewLimiter(1, 3),
		typist:    typing.New(cfg.Typer, cfg.LockWait),
	}
	lanMux := http.NewServeMux()
	lanMux.HandleFunc("/health", s.health)
	lanMux.HandleFunc("/type", s.typeText)
	lanMux.HandleFunc("/pair", s.pair)
	mux := http.NewServeMux()
	mux.Handle("/", lanMux)
	// Only over the tailnet: /info hands out the pin, which a LAN peer could otherwise
	// fetch over a connection it can't yet verify.
	mux.HandleFunc("/info", s.info)
	return &Helper{s: s, mux: mux, lanMux: lanMux}
}

// Handler answers 403 to any Host not in allow (this listener's names and IPs for this
// machine), so a web page can't reach the helper through DNS rebinding.
func (h *Helper) Handler(allow []string) http.Handler { return guard(allow, h.mux) }

// LANHandler is Handler for the LAN listener, which serves everything but /info.
func (h *Helper) LANHandler(allow []string) http.Handler { return guard(allow, h.lanMux) }

func guard(allow []string, next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if !tailnet.HostAllowed(r.Host, allow) {
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
		"accessibility_ok": s.typist.Ready(),
	})
}

// authorized answers 401 unless r carries a paired device's bearer token.
func (s *server) authorized(w http.ResponseWriter, r *http.Request) bool {
	tok, ok := strings.CutPrefix(r.Header.Get("Authorization"), "Bearer ")
	if !ok || !s.cfg.Store.Check(tok) {
		fail(w, http.StatusUnauthorized, "missing or invalid token")
		return false
	}
	return true
}

// info tells a paired device the LAN listener's port and certificate fingerprint, so a
// computer paired over the tailnet can pin it without pairing again.
func (s *server) info(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		fail(w, http.StatusMethodNotAllowed, "use GET")
		return
	}
	if !s.authorized(w, r) {
		return
	}
	out := map[string]any{"hostname": s.cfg.Hostname}
	s.addLAN(out)
	reply(w, http.StatusOK, out)
}

// addLAN adds the LAN listener's fingerprint and port to a reply when LAN is on.
func (s *server) addLAN(out map[string]any) {
	if s.cfg.CertSHA256 != "" {
		out["certSHA256"] = s.cfg.CertSHA256
		out["lanPort"] = s.cfg.LANPort
	}
}

type typeRequest struct {
	Text  string    `json:"text"`
	Enter bool      `json:"enter"`
	Mode  plan.Mode `json:"mode"`
	// A panel sends its Phrase's id and edit counter (#98), so an edited Phrase gets a notice.
	Phrase string `json:"phrase"`
	Edit   uint32 `json:"edit"`
}

// maxSeen caps the remembered Phrase ids; a panel holds at most 48.
const maxSeen = 1024

// noticeIfEdited shows a notice the first time a panel Phrase is typed at a new edit counter.
// shortcut: remembered in memory only, so after a Helper restart each Phrase notices once more.
func (s *server) noticeIfEdited(req typeRequest) {
	if s.cfg.Notice == nil || req.Phrase == "" || req.Edit == 0 {
		return
	}
	s.mu.Lock()
	if s.seen == nil || len(s.seen) >= maxSeen {
		s.seen = map[string]uint32{}
	}
	seen := s.seen[req.Phrase] == req.Edit
	s.seen[req.Phrase] = req.Edit
	s.mu.Unlock()
	if seen {
		return
	}
	preview := []rune(strings.ReplaceAll(req.Text, "\n", "↵"))
	if len(preview) > 80 {
		preview = append(preview[:80], '…')
	}
	// Off the request goroutine: notify-send can block on D-Bus for ~25 s, and a late reply makes
	// the panel show Maybe typed, inviting a second tap that types it twice.
	go s.cfg.Notice("Typed a Phrase changed on a panel: " + string(preview))
}

func (s *server) typeText(w http.ResponseWriter, r *http.Request) {
	if !postJSON(w, r) {
		return
	}
	if !s.authorized(w, r) {
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
	outcome, err := s.typist.Type(req.Text, req.Enter, req.Mode)
	status, msg := typeAnswer(outcome, err)
	switch {
	case status == http.StatusOK:
		s.noticeIfEdited(req)
		reply(w, status, map[string]any{"ok": true})
	case outcome == typing.Failed:
		log.Printf("type: %v", err)
		fallthrough
	default:
		fail(w, status, msg)
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
		out := map[string]any{"token": token}
		s.addLAN(out)
		reply(w, http.StatusOK, out)
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
