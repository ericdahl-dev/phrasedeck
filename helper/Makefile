# Signing with a stable identity keeps the macOS Accessibility grant across rebuilds.
SIGN_IDENTITY ?= Apple Development: Eric Dahl (D6LCAABBEQ)

.PHONY: build test

build:
	go build -o phrased ./cmd/phrased
ifeq ($(shell uname),Darwin)
	codesign --force --sign "$(SIGN_IDENTITY)" --identifier dev.ericdahl.phrased phrased
endif

test:
	go test ./...
