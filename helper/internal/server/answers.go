package server

import (
	"net/http"

	"github.com/ericdahl-dev/phrasedeck/helper/internal/typing"
)

// typeAnswers is what /type answers for each typing outcome, and the one place its wording
// lives. The meaning of each code is a contract with the app (design doc, "What each status
// means"): 409 (busy), 429 (rate-limited) and 503 (not ready) mean nothing was typed, so a
// tap may be repeated; only 500 means some keys may have been typed.
var typeAnswers = map[typing.Outcome]struct {
	status int
	msg    string
}{
	typing.BadMode: {http.StatusBadRequest, `mode must be "auto", "type" or "paste"`},
	typing.TooLong: {http.StatusRequestEntityTooLarge, "text over 10,000 characters"},
	typing.Busy:    {http.StatusConflict, "busy typing another phrase"},
}

// typeAnswer is the status and message for an outcome; err is NotReady's problem or Failed's cause.
func typeAnswer(outcome typing.Outcome, err error) (int, string) {
	switch outcome {
	case typing.Typed:
		return http.StatusOK, ""
	case typing.NotReady:
		return http.StatusServiceUnavailable, "can't type yet: " + err.Error()
	case typing.Failed:
		return http.StatusInternalServerError, "typing failed: " + err.Error()
	}
	a := typeAnswers[outcome]
	return a.status, a.msg
}
