package server

import (
	"bytes"
	"encoding/json"
	"fmt"
	"os"
	"reflect"
	"testing"
	"time"

	"github.com/ericdahl-dev/phrasedeck/helper/internal/typer"
)

// The /pair reply must match contract/pairing.json, which the app's tests decode too.
func TestPairReplyMatchesTheContract(t *testing.T) {
	b, err := os.ReadFile("../../../contract/pairing.json")
	if err != nil {
		t.Fatal(err)
	}
	var c struct {
		Replies []struct {
			Name       string
			CertSHA256 string
			LANPort    int
			Body       map[string]any
		}
	}
	if err := json.Unmarshal(b, &c); err != nil {
		t.Fatal(err)
	}
	if len(c.Replies) == 0 {
		t.Fatal("no replies in the contract")
	}
	for _, r := range c.Replies {
		e := newEnv(t, &typer.Fake{}, time.Second, func(cfg *Config) {
			cfg.CertSHA256, cfg.LANPort = r.CertSHA256, r.LANPort
		})
		body := fmt.Sprintf(`{"code":%q,"device":"contract"}`, e.openWindow())
		resp, err := e.srv.Client().Post(e.srv.URL+"/pair", "application/json", bytes.NewBufferString(body))
		if err != nil {
			t.Fatal(err)
		}
		var got map[string]any
		err = json.NewDecoder(resp.Body).Decode(&got)
		resp.Body.Close()
		if resp.StatusCode != 200 || err != nil {
			t.Fatalf("%s: status %d, decode %v", r.Name, resp.StatusCode, err)
		}
		if tok, _ := got["token"].(string); tok == "" {
			t.Fatalf("%s: no token in %v", r.Name, got)
		}
		delete(got, "token")
		if !reflect.DeepEqual(got, r.Body) {
			t.Errorf("%s: reply %v, contract %v", r.Name, got, r.Body)
		}
	}
}
