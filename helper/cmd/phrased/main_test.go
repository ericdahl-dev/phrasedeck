package main

import (
	"path/filepath"
	"reflect"
	"testing"
)

func TestParseFlagsDefaults(t *testing.T) {
	o, err := parseFlags([]string{}, "/home/eric")
	if err != nil {
		t.Fatal(err)
	}
	if o.port != 47811 || o.config != filepath.Join("/home/eric", ".config", "phrased") || len(o.allowHosts) != 0 {
		t.Fatalf("bad defaults: %+v", o)
	}
}

func TestParseFlagsRepeatableAllowHost(t *testing.T) {
	o, err := parseFlags([]string{"-allow-host", "a", "-allow-host", "b.example", "-port", "9000"}, "/h")
	if err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual([]string(o.allowHosts), []string{"a", "b.example"}) || o.port != 9000 {
		t.Fatalf("got %+v", o)
	}
}

func TestParseFlagsRejectsArgs(t *testing.T) {
	if _, err := parseFlags([]string{"pair"}, "/h"); err == nil {
		t.Fatal("want an error for an unknown subcommand")
	}
}
