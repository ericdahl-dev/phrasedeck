package typing

import (
	"errors"
	"reflect"
	"strings"
	"testing"
	"time"

	"github.com/ericdahl-dev/phrasedeck/helper/internal/plan"
	"github.com/ericdahl-dev/phrasedeck/helper/internal/typer"
)

func TestAPhraseIsTypedWithItsEnter(t *testing.T) {
	f := &typer.Fake{}
	if got, err := New(f, time.Second).Type("hi", true, ""); got != Typed || err != nil {
		t.Fatalf("outcome %v, %v", got, err)
	}
	if !reflect.DeepEqual(f.Calls(), []string{"type:hi", "enter"}) {
		t.Fatalf("calls %v", f.Calls())
	}
}

func TestPhrasesItCantTypeAreRefusedBeforeAnyKey(t *testing.T) {
	for name, c := range map[string]struct {
		fake *typer.Fake
		text string
		mode plan.Mode
		want Outcome
	}{
		"bad mode":  {&typer.Fake{}, "hi", "shout", BadMode},
		"too long":  {&typer.Fake{}, strings.Repeat("é", MaxText+1), "", TooLong},
		"not ready": {&typer.Fake{NotReady: true}, "hi", "", NotReady},
	} {
		t.Run(name, func(t *testing.T) {
			if got, _ := New(c.fake, time.Second).Type(c.text, true, c.mode); got != c.want {
				t.Fatalf("outcome %v, want %v", got, c.want)
			}
			if calls := c.fake.Calls(); len(calls) != 0 {
				t.Fatalf("typed %v", calls)
			}
		})
	}
	if got, _ := New(&typer.Fake{}, time.Second).Type(strings.Repeat("é", MaxText), false, ""); got != Typed {
		t.Fatalf("exactly MaxText runes: %v", got)
	}
}

func TestASecondPhraseWaitsThenGivesUpAsBusy(t *testing.T) {
	hold, entered := make(chan struct{}), make(chan struct{}, 1)
	f := &typer.Fake{Hold: hold, Entered: entered}
	ty := New(f, 50*time.Millisecond)
	first := make(chan Outcome)
	go func() { o, _ := ty.Type("one", false, ""); first <- o }()
	<-entered
	start := time.Now()
	if got, _ := ty.Type("two", false, ""); got != Busy {
		t.Fatalf("second phrase: %v, want Busy", got)
	}
	if waited := time.Since(start); waited < 50*time.Millisecond {
		t.Fatalf("busy after %v, before the lock wait", waited)
	}
	close(hold)
	if got := <-first; got != Typed {
		t.Fatalf("first phrase: %v", got)
	}
	if !reflect.DeepEqual(f.Calls(), []string{"type:one"}) {
		t.Fatalf("calls %v", f.Calls())
	}
}

func TestBackendFailuresComeBackWithTheirError(t *testing.T) {
	got, err := New(&typer.Fake{FailOn: "type:hi"}, time.Second).Type("hi", false, "")
	if got != Failed || !errors.Is(err, typer.ErrFake) {
		t.Fatalf("outcome %v, %v", got, err)
	}
}
