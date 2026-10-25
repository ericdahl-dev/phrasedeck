package server

import (
	"bytes"
	"encoding/json"
	"fmt"
	"net/http"
	"path/filepath"
	"testing"
	"time"

	"github.com/ericdahl-dev/phrasedeck/helper/internal/auth"
	"github.com/ericdahl-dev/phrasedeck/helper/internal/typer"
)

// pair posts to /pair and returns the status and the token, if any.
func (e *env) pair(code, device string) (int, string) {
	e.t.Helper()
	body := fmt.Sprintf(`{"code":%q,"device":%q}`, code, device)
	resp, err := e.srv.Client().Post(e.srv.URL+"/pair", "application/json", bytes.NewBufferString(body))
	if err != nil {
		e.t.Fatal(err)
	}
	defer resp.Body.Close()
	var out struct{ Token string }
	json.NewDecoder(resp.Body).Decode(&out)
	return resp.StatusCode, out.Token
}

func (e *env) openWindow() string {
	e.t.Helper()
	code, err := auth.OpenWindow(filepath.Join(e.dir, "pairing.json"), time.Now())
	if err != nil {
		e.t.Fatal(err)
	}
	return code
}

func (e *env) typeWith(tok string) int {
	return e.post(tok, "application/json", `{"text":"hi"}`).StatusCode
}

func other(code string) string {
	if code == "000000" {
		return "000001"
	}
	return "000000"
}

func TestPairNoWindow404(t *testing.T) {
	e := newEnv(t, &typer.Fake{}, time.Second)
	code, _ := e.pair("123456", "iPhone")
	wantCode(t, code, 404)
}

func TestPairFlow(t *testing.T) {
	e := newEnv(t, &typer.Fake{}, time.Second)
	c := e.openWindow()
	status, _ := e.pair(other(c), "iPhone")
	wantCode(t, status, 401)
	status, tok := e.pair(c, "iPhone")
	wantCode(t, status, 200)
	if tok == "" {
		t.Fatal("no token")
	}
	wantCode(t, e.typeWith(tok), 200)
	time.Sleep(time.Second) // the /pair limiter allows a burst of 3
	status, _ = e.pair(c, "iPhone")
	wantCode(t, status, 404)
}

func TestPairBadRequest(t *testing.T) {
	e := newEnv(t, &typer.Fake{}, time.Second)
	e.openWindow()
	status, _ := e.pair("123456", "  ")
	wantCode(t, status, 400)
	resp, err := e.srv.Client().Post(e.srv.URL+"/pair", "text/plain", bytes.NewBufferString(`{}`))
	if err != nil {
		t.Fatal(err)
	}
	resp.Body.Close()
	wantCode(t, resp.StatusCode, 415)
}

func TestPairLimiterIsSeparate(t *testing.T) {
	e := newEnv(t, &typer.Fake{}, time.Second)
	limited := false
	for i := 0; i < 6; i++ {
		if status, _ := e.pair("123456", "iPhone"); status == 429 {
			limited = true
		}
	}
	if !limited {
		t.Fatal("6 rapid /pair requests, none rate-limited")
	}
	wantCode(t, e.typeJSON(`{"text":"hi"}`), 200)
}

func TestRevokeGets401WithoutRestart(t *testing.T) {
	e := newEnv(t, &typer.Fake{}, time.Second)
	_, tok := e.pair(e.openWindow(), "iPhone")
	wantCode(t, e.typeWith(tok), 200)
	cli, err := auth.NewStore(filepath.Join(e.dir, "tokens.json"))
	if err != nil {
		t.Fatal(err)
	}
	if err := cli.Remove("iPhone"); err != nil {
		t.Fatal(err)
	}
	wantCode(t, e.typeWith(tok), 401)
	wantCode(t, e.typeWith(token), 200)
}

func TestPairWrongHost403(t *testing.T) {
	e := newEnv(t, &typer.Fake{}, time.Second)
	req, _ := http.NewRequest("POST", e.srv.URL+"/pair", bytes.NewBufferString(`{"code":"123456","device":"x"}`))
	req.Host = "evil.example"
	req.Header.Set("Content-Type", "application/json")
	resp, err := e.srv.Client().Do(req)
	if err != nil {
		t.Fatal(err)
	}
	resp.Body.Close()
	wantCode(t, resp.StatusCode, 403)
}
