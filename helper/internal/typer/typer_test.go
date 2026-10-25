package typer

import (
	"errors"
	"reflect"
	"testing"

	"github.com/ericdahl-dev/phrasedeck/helper/internal/plan"
)

func TestRunInOrder(t *testing.T) {
	f := &Fake{}
	if err := Run(f, plan.Build("a\nb", true, plan.ModeType)); err != nil {
		t.Fatal(err)
	}
	want := []string{"type:a", "shift-enter", "type:b", "enter"}
	if !reflect.DeepEqual(f.Calls(), want) {
		t.Fatalf("got %v want %v", f.Calls(), want)
	}
}

func TestRunStopsOnError(t *testing.T) {
	f := &Fake{FailOn: "type:a"}
	if err := Run(f, plan.Build("a", true, plan.ModeType)); !errors.Is(err, ErrFake) {
		t.Fatalf("want ErrFake, got %v", err)
	}
	if len(f.Calls()) != 0 {
		t.Fatalf("ran steps after the error: %v", f.Calls())
	}
}
