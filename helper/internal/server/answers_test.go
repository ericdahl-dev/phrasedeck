package server

import (
	"bytes"
	"encoding/json"
	"errors"
	"net/http"
	"testing"
	"time"

	"github.com/ericdahl-dev/phrasedeck/helper/internal/typer"
	"github.com/ericdahl-dev/phrasedeck/helper/internal/typing"
)

// The app reads these codes (design doc, "What each status means"): 409, 429 and 503 mean
// nothing was typed and a tap may be repeated; only 500 means some keys may have been typed.
func TestEachTypingOutcomeHasItsDocumentedStatus(t *testing.T) {
	want := map[typing.Outcome]int{
		typing.Typed:    http.StatusOK,
		typing.BadMode:  http.StatusBadRequest,
		typing.TooLong:  http.StatusRequestEntityTooLarge,
		typing.NotReady: http.StatusServiceUnavailable,
		typing.Busy:     http.StatusConflict,
		typing.Failed:   http.StatusInternalServerError,
	}
	for outcome, status := range want {
		if got, _ := typeAnswer(outcome, errors.New("x")); got != status {
			t.Errorf("outcome %v: %d, want %d", outcome, got, status)
		}
	}
}

// The app shows the message when there is one, so every refusal is {"error": "<text>"}.
func TestBusyAndFailedAnswersAreAnErrorObject(t *testing.T) {
	hold, entered := make(chan struct{}), make(chan struct{}, 1)
	busy := newEnv(t, &typer.Fake{Hold: hold, Entered: entered}, 10*time.Millisecond)
	go busy.typeJSON(`{"text":"one"}`)
	<-entered
	defer close(hold)
	failed := newEnv(t, &typer.Fake{FailOn: "type:hi"}, time.Second)

	for _, c := range []struct {
		e    *env
		want int
	}{{busy, http.StatusConflict}, {failed, http.StatusInternalServerError}} {
		req, _ := http.NewRequest("POST", c.e.srv.URL+"/type", bytes.NewBufferString(`{"text":"hi"}`))
		req.Header.Set("Content-Type", "application/json")
		req.Header.Set("Authorization", "Bearer "+token)
		resp, err := c.e.srv.Client().Do(req)
		if err != nil {
			t.Fatal(err)
		}
		var body map[string]any
		err = json.NewDecoder(resp.Body).Decode(&body)
		resp.Body.Close()
		msg, _ := body["error"].(string)
		if resp.StatusCode != c.want || err != nil || len(body) != 1 || msg == "" {
			t.Errorf("status %d body %v (%v); want %d and only a non-empty error", resp.StatusCode, body, err, c.want)
		}
	}
}

// An outcome added to typing without a row here would answer status 0, which panics in
// WriteHeader.
func TestEveryTypingOutcomeHasAnAnswer(t *testing.T) {
	for o := typing.Outcome(0); o < typing.NumOutcomes; o++ {
		if status, _ := typeAnswer(o, errors.New("x")); status == 0 {
			t.Errorf("outcome %v has no answer", o)
		}
	}
}
