package server

import (
	"bytes"
	"encoding/json"
	"fmt"
	"net/http"
	"reflect"
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
