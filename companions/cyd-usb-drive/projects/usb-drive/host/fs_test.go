package main

import (
	"errors"
	"os"
	"testing"
)

func TestCleanName(t *testing.T) {
	for input,want := range map[string]string{"/":"/","Files/note.md":"/Files/note.md","/Firmware/App.bin":"/Firmware/App.bin"} {
		got,err:=cleanName(input);if err!=nil||got!=want{t.Fatalf("cleanName(%q)=%q,%v",input,got,err)}
	}
	for _,input:=range []string{"/Files/.hidden","/Files/a/../.private","/.DS_Store"}{if _,err:=cleanName(input);!errors.Is(err,os.ErrPermission){t.Fatalf("%q was allowed",input)}}
}

func TestMapError(t *testing.T) {
	if !errors.Is(mapError(errors.New("not_found: missing")),os.ErrNotExist){t.Fatal("not_found mapping")}
	if !errors.Is(mapError(errors.New("exists: exists")),os.ErrExist){t.Fatal("exists mapping")}
	if !errors.Is(mapError(errors.New("forbidden: private")),os.ErrPermission){t.Fatal("forbidden mapping")}
}
