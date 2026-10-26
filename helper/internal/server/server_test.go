package server

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"
	"time"

	"github.com/ericdahl-dev/phrasedeck/helper/internal/auth"
	"github.com/ericdahl-dev/phrasedeck/helper/internal/typer"
)

const token = "test-token"

type env struct {
	t    *testing.T
	fake *typer.Fake
	srv  *httptest.Server
	dir  string
}

func newEnv(t *testing.T, fake *typer.Fake, lockWait time.Duration, opts ...func(*Config)) *env {
	t.Helper()
	cfg, dir := testConfig(t, fake, lockWait)
	for _, o := range opts {
		o(&cfg)
	}
	srv := httptest.NewUnstartedServer(nil)
	srv.Config.Handler = New(cfg)
	srv.Start()
	t.Cleanup(srv.Close)
	return &env{t: t, fake: fake, srv: srv, dir: dir}
}

func testConfig(t *testing.T, fake *typer.Fake, lockWait time.Duration) (Config, string) {
	t.Helper()
	sum := sha256.Sum256([]byte(token))
	dir := t.TempDir()
	p := filepath.Join(dir, "tokens.json")
	data := `{"devices":[{"name":"curl","sha256":"` + hex.EncodeToString(sum[:]) + `","paired_at":"2026-10-24T00:00:00Z"}]}`
	if err := os.WriteFile(p, []byte(data), 0o600); err != nil {
		t.Fatal(err)
	}
	store, err := auth.NewStore(p)
	if err != nil {
		t.Fatal(err)
	}
	cfg := Config{
		Store:      store,
		Pairer:     auth.NewPairer(filepath.Join(dir, "pairing.json"), store),
		Typer:      fake,
		Hostname:   "test-mac",
		OS:         "darwin",
		AllowHosts: []string{"127.0.0.1"},
		LockWait:   lockWait,
	}
	return cfg, dir
}

// post sends body to /type with the given token ("" for none) and content type.
func (e *env) post(tok, contentType, body string) *http.Response {
	e.t.Helper()
	req, err := http.NewRequest("POST", e.srv.URL+"/type", bytes.NewBufferString(body))
	if err != nil {
		e.t.Fatal(err)
	}
	req.Header.Set("Content-Type", contentType)
	if tok != "" {
		req.Header.Set("Authorization", "Bearer "+tok)
	}
	resp, err := e.srv.Client().Do(req)
	if err != nil {
		e.t.Fatal(err)
	}
	resp.Body.Close()
	return resp
}

func (e *env) typeJSON(body string) int {
	return e.post(token, "application/json", body).StatusCode
}

func wantCode(t *testing.T, got, want int) {
	t.Helper()
	if got != want {
		t.Fatalf("status %d, want %d", got, want)
	}
}

func health(t *testing.T, e *env) map[string]any {
	t.Helper()
	resp, err := e.srv.Client().Get(e.srv.URL + "/health")
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	wantCode(t, resp.StatusCode, 200)
	var h map[string]any
	if err := json.NewDecoder(resp.Body).Decode(&h); err != nil {
		t.Fatal(err)
	}
	return h
}

func TestHealthNoAuth(t *testing.T) {
	h := health(t, newEnv(t, &typer.Fake{}, time.Second))
	want := map[string]any{"hostname": "test-mac", "os": "darwin", "accessibility_ok": true}
	if !reflect.DeepEqual(h, want) {
		t.Fatalf("got %v want %v", h, want)
	}
}

func TestTypeNeedsAuth(t *testing.T) {
	e := newEnv(t, &typer.Fake{}, time.Second)
	wantCode(t, e.post("", "application/json", `{"text":"hi"}`).StatusCode, 401)
	wantCode(t, e.post("wrong", "application/json", `{"text":"hi"}`).StatusCode, 401)
	if len(e.fake.Calls()) != 0 {
		t.Fatalf("typed without auth: %v", e.fake.Calls())
	}
}

func TestTypeOK(t *testing.T) {
	e := newEnv(t, &typer.Fake{}, time.Second)
	wantCode(t, e.typeJSON(`{"text":"hi","enter":true}`), 200)
	if got := e.fake.Calls(); !reflect.DeepEqual(got, []string{"type:hi", "enter"}) {
		t.Fatalf("got %v", got)
	}
}

func TestTypeNoEnter(t *testing.T) {
	e := newEnv(t, &typer.Fake{}, time.Second)
	wantCode(t, e.typeJSON(`{"text":"merge ","enter":false}`), 200)
	if got := e.fake.Calls(); !reflect.DeepEqual(got, []string{"type:merge "}) {
		t.Fatalf("got %v", got)
	}
}

func TestTypeMultilineAutoPastes(t *testing.T) {
	e := newEnv(t, &typer.Fake{}, time.Second)
	wantCode(t, e.typeJSON(`{"text":"a\nb"}`), 200)
	if got := e.fake.Calls(); !reflect.DeepEqual(got, []string{"paste:a\nb"}) {
		t.Fatalf("got %v", got)
	}
}

func TestTypeBadJSON(t *testing.T) {
	e := newEnv(t, &typer.Fake{}, time.Second)
	wantCode(t, e.typeJSON(`{"text":`), 400)
}

func TestTypeBadMode(t *testing.T) {
	e := newEnv(t, &typer.Fake{}, time.Second)
	wantCode(t, e.typeJSON(`{"text":"hi","mode":"x"}`), 400)
}

func TestTypeTooLong(t *testing.T) {
	e := newEnv(t, &typer.Fake{}, time.Second)
	wantCode(t, e.typeJSON(`{"text":"`+strings.Repeat("é", 10001)+`"}`), 413)
	if len(e.fake.Calls()) != 0 {
		t.Fatal("typed an over-long text")
	}
}

func TestBodyCap(t *testing.T) {
	e := newEnv(t, &typer.Fake{}, time.Second)
	wantCode(t, e.typeJSON(`{"text":"hi","pad":"`+strings.Repeat("x", 65*1024)+`"}`), 413)
}

func TestContentType(t *testing.T) {
	e := newEnv(t, &typer.Fake{}, time.Second)
	wantCode(t, e.post(token, "text/plain", `{"text":"hi"}`).StatusCode, 415)
	wantCode(t, e.post(token, "application/json; charset=utf-8", `{"text":"hi"}`).StatusCode, 200)
}

func TestMethod(t *testing.T) {
	e := newEnv(t, &typer.Fake{}, time.Second)
	resp, err := e.srv.Client().Get(e.srv.URL + "/type")
	if err != nil {
		t.Fatal(err)
	}
	resp.Body.Close()
	wantCode(t, resp.StatusCode, 405)
}

func TestHostAllowlist(t *testing.T) {
	e := newEnv(t, &typer.Fake{}, time.Second)
	for _, path := range []string{"/type", "/health"} {
		req, _ := http.NewRequest("POST", e.srv.URL+path, bytes.NewBufferString(`{"text":"hi"}`))
		if path == "/health" {
			req.Method = "GET"
		}
		req.Host = "evil.example"
		req.Header.Set("Content-Type", "application/json")
		req.Header.Set("Authorization", "Bearer "+token)
		resp, err := e.srv.Client().Do(req)
		if err != nil {
			t.Fatal(err)
		}
		resp.Body.Close()
		wantCode(t, resp.StatusCode, 403)
	}
	if len(e.fake.Calls()) != 0 {
		t.Fatal("typed for a bad Host")
	}
}

func TestTypeNotReady(t *testing.T) {
	e := newEnv(t, &typer.Fake{NotReady: true}, time.Second)
	wantCode(t, e.typeJSON(`{"text":"hi"}`), 503)
	if h := health(t, e); h["accessibility_ok"] != false {
		t.Fatalf("accessibility_ok = %v, want false", h["accessibility_ok"])
	}
}

func TestRateLimitAfterAuth(t *testing.T) {
	e := newEnv(t, &typer.Fake{}, time.Second)
	for i := 0; i < 20; i++ {
		wantCode(t, e.post("wrong", "application/json", `{"text":"hi"}`).StatusCode, 401)
	}
	wantCode(t, e.typeJSON(`{"text":"hi"}`), 200)
}

func TestRateLimit(t *testing.T) {
	e := newEnv(t, &typer.Fake{}, time.Second)
	limited := 0
	for i := 0; i < 15; i++ {
		if e.typeJSON(`{"text":"hi"}`) == 429 {
			limited++
		}
	}
	if limited == 0 {
		t.Fatal("15 rapid requests, none rate-limited")
	}
}

func TestBusyLock409(t *testing.T) {
	hold, entered := make(chan struct{}), make(chan struct{}, 1)
	e := newEnv(t, &typer.Fake{Hold: hold, Entered: entered}, 50*time.Millisecond)
	first := make(chan int)
	go func() { first <- e.typeJSON(`{"text":"one"}`) }()
	select {
	case <-entered: // the first request holds the lock
	case <-time.After(2 * time.Second):
		t.Fatal("first request never started typing")
	}
	start := time.Now()
	wantCode(t, e.typeJSON(`{"text":"two"}`), 409)
	if waited := time.Since(start); waited < 50*time.Millisecond {
		t.Fatalf("409 after %v, before LockWait", waited)
	}
	close(hold)
	wantCode(t, <-first, 200)
	if got := e.fake.Calls(); !reflect.DeepEqual(got, []string{"type:one"}) {
		t.Fatalf("got %v", got)
	}
}

func TestTyperError500(t *testing.T) {
	e := newEnv(t, &typer.Fake{FailOn: "type:hi"}, time.Second)
	wantCode(t, e.typeJSON(`{"text":"hi"}`), 500)
}

// noticeEnv records notices; they're shown off the request goroutine, so they arrive on a channel.
func noticeEnv(t *testing.T, fake *typer.Fake) (*env, chan string) {
	notices := make(chan string, 10)
	return newEnv(t, fake, time.Second, func(c *Config) { c.Notice = func(msg string) { notices <- msg } }), notices
}

func wantNotices(t *testing.T, notices chan string, want ...string) {
	t.Helper()
	for _, w := range want {
		select {
		case got := <-notices:
			if got != w {
				t.Fatalf("notice %q, want %q", got, w)
			}
		case <-time.After(2 * time.Second):
			t.Fatalf("no notice, want %q", w)
		}
	}
	select {
	case got := <-notices:
		t.Fatalf("extra notice %q", got)
	case <-time.After(100 * time.Millisecond):
	}
}

// A panel Phrase carries an edit counter (#98). The first time the Helper types a Phrase at a
// counter it hasn't seen, it shows a notice, since an edit on the panel's web page could have
// turned it into a command. The same counter again, or a request with none, shows nothing.
func TestTypeNoticeOnFirstSendOfAnEditedPhrase(t *testing.T) {
	e, notices := noticeEnv(t, &typer.Fake{})
	wantCode(t, e.typeJSON(`{"text":"ls\nrm -rf x","phrase":"p7","edit":1}`), 200)
	wantCode(t, e.typeJSON(`{"text":"ls\nrm -rf x","phrase":"p7","edit":1}`), 200)
	wantCode(t, e.typeJSON(`{"text":"hi"}`), 200)
	wantCode(t, e.typeJSON(`{"text":"ls -l","phrase":"p7","edit":2}`), 200)
	wantNotices(t, notices,
		"Typed a Phrase changed on a panel: ls↵rm -rf x",
		"Typed a Phrase changed on a panel: ls -l")
}

// A failed type shows no notice and doesn't count as seen: the next successful one notices.
func TestTypeNoticeOnlyAfterTyping(t *testing.T) {
	e, notices := noticeEnv(t, &typer.Fake{FailOn: "type:boom"})
	wantCode(t, e.typeJSON(`{"text":"boom","phrase":"p1","edit":1}`), 500)
	wantNotices(t, notices)
	wantCode(t, e.typeJSON(`{"text":"ok","phrase":"p1","edit":1}`), 200)
	wantNotices(t, notices, "Typed a Phrase changed on a panel: ok")
}

// A notification that hangs (notify-send waiting on D-Bus) mustn't hold the reply, or the
// panel times out, shows Maybe typed, and a second tap types twice.
func TestTypeNoticeDoesNotDelayTheReply(t *testing.T) {
	block := make(chan struct{})
	t.Cleanup(func() { close(block) })
	e := newEnv(t, &typer.Fake{}, time.Second, func(c *Config) { c.Notice = func(string) { <-block } })
	start := time.Now()
	wantCode(t, e.typeJSON(`{"text":"hi","phrase":"p1","edit":1}`), 200)
	if d := time.Since(start); d > time.Second {
		t.Fatalf("reply took %v", d)
	}
}
