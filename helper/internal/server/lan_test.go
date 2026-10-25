package server

import (
	"bytes"
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"reflect"
	"strings"
	"testing"
	"time"

	"github.com/ericdahl-dev/phrasedeck/helper/internal/typer"
)

const fp = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"

func withLAN(c *Config) {
	c.CertSHA256 = fp
	c.LANPort = 47812
}

// getJSON sends GET path with tok ("" for none) and decodes the reply.
func (e *env) getJSON(path, tok string) (int, map[string]any) {
	e.t.Helper()
	req, _ := http.NewRequest("GET", e.srv.URL+path, nil)
	if tok != "" {
		req.Header.Set("Authorization", "Bearer "+tok)
	}
	resp, err := e.srv.Client().Do(req)
	if err != nil {
		e.t.Fatal(err)
	}
	defer resp.Body.Close()
	var out map[string]any
	json.NewDecoder(resp.Body).Decode(&out)
	return resp.StatusCode, out
}

func TestInfoNeedsAToken(t *testing.T) {
	e := newEnv(t, &typer.Fake{}, time.Second, withLAN)
	for _, tok := range []string{"", "wrong"} {
		if code, _ := e.getJSON("/info", tok); code != http.StatusUnauthorized {
			t.Errorf("/info with token %q: %d, want 401", tok, code)
		}
	}
}

func TestInfoGivesThePinAndLANPort(t *testing.T) {
	e := newEnv(t, &typer.Fake{}, time.Second, withLAN)
	code, got := e.getJSON("/info", token)
	wantCode(t, code, 200)
	want := map[string]any{"certSHA256": fp, "lanPort": float64(47812), "hostname": "test-mac"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("/info = %v, want %v", got, want)
	}
}

func TestInfoWithoutLANHasNoLANFields(t *testing.T) {
	e := newEnv(t, &typer.Fake{}, time.Second)
	code, got := e.getJSON("/info", token)
	wantCode(t, code, 200)
	if !reflect.DeepEqual(got, map[string]any{"hostname": "test-mac"}) {
		t.Fatalf("/info = %v", got)
	}
}

// pairReply pairs and returns the whole JSON reply.
func (e *env) pairReply() map[string]any {
	e.t.Helper()
	body := fmt.Sprintf(`{"code":%q,"device":"iPhone"}`, e.openWindow())
	resp, err := e.srv.Client().Post(e.srv.URL+"/pair", "application/json", bytes.NewBufferString(body))
	if err != nil {
		e.t.Fatal(err)
	}
	defer resp.Body.Close()
	wantCode(e.t, resp.StatusCode, 200)
	var out map[string]any
	json.NewDecoder(resp.Body).Decode(&out)
	return out
}

func TestPairAddsThePinWhenLANIsOn(t *testing.T) {
	got := newEnv(t, &typer.Fake{}, time.Second, withLAN).pairReply()
	if got["certSHA256"] != fp || got["lanPort"] != float64(47812) || got["token"] == "" {
		t.Fatalf("/pair = %v", got)
	}
}

func TestPairWithoutLANIsJustTheToken(t *testing.T) {
	got := newEnv(t, &typer.Fake{}, time.Second).pairReply()
	if len(got) != 1 || got["token"] == nil {
		t.Fatalf("/pair = %v, want only a token", got)
	}
}

func TestEachListenerAcceptsOnlyItsOwnHostNames(t *testing.T) {
	cfg, _ := testConfig(t, &typer.Fake{}, 0)
	h := NewHelper(cfg)
	tail, lanH := h.Handler([]string{"mac.tail1234.ts.net"}), h.Handler([]string{"192.168.1.20"})
	for _, c := range []struct {
		h    http.Handler
		host string
		want int
	}{
		{tail, "mac.tail1234.ts.net", 200},
		{tail, "192.168.1.20", 403},
		{lanH, "192.168.1.20:47812", 200},
		{lanH, "mac.tail1234.ts.net", 403},
	} {
		r := httptest.NewRequest("GET", "/health", nil)
		r.Host = c.host
		w := httptest.NewRecorder()
		c.h.ServeHTTP(w, r)
		if w.Code != c.want {
			t.Errorf("Host %s: %d, want %d", c.host, w.Code, c.want)
		}
	}
}

// Rate limits, lockout and the typing lock must be one set for both listeners, or a second
// listener would double the attempts a guesser gets and let two phrases type at once.
func TestBothListenersShareTheirLimits(t *testing.T) {
	cfg, _ := testConfig(t, &typer.Fake{}, 0)
	h := NewHelper(cfg)
	tail, lanH := h.Handler([]string{"tail"}), h.Handler([]string{"lan"})
	pair := func(h http.Handler, host string) int {
		r := httptest.NewRequest("POST", "/pair", strings.NewReader(`{"code":"000000","device":"x"}`))
		r.Host = host
		r.Header.Set("Content-Type", "application/json")
		w := httptest.NewRecorder()
		h.ServeHTTP(w, r)
		return w.Code
	}
	for i := 0; i < 3; i++ { // the pairing burst
		pair(tail, "tail")
	}
	if got := pair(lanH, "lan"); got != http.StatusTooManyRequests {
		t.Fatalf("LAN /pair after the tailnet used the burst: %d, want 429", got)
	}
}

// /info hands out the pin, so it's served only where the pin can be trusted: the tailnet.
func TestTheLANHandlerDoesNotServeInfo(t *testing.T) {
	cfg, _ := testConfig(t, &typer.Fake{}, 0)
	cfg.CertSHA256, cfg.LANPort = fp, 47812
	h := NewHelper(cfg)
	get := func(h http.Handler, path string) int {
		r := httptest.NewRequest("GET", path, nil)
		r.Host = "h"
		r.Header.Set("Authorization", "Bearer "+token)
		w := httptest.NewRecorder()
		h.ServeHTTP(w, r)
		return w.Code
	}
	if got := get(h.LANHandler([]string{"h"}), "/info"); got != http.StatusNotFound {
		t.Fatalf("LAN /info: %d, want 404", got)
	}
	if got := get(h.LANHandler([]string{"h"}), "/health"); got != http.StatusOK {
		t.Fatalf("LAN /health: %d, want 200", got)
	}
	if got := get(h.Handler([]string{"h"}), "/info"); got != http.StatusOK {
		t.Fatalf("tailnet /info: %d, want 200", got)
	}
}
