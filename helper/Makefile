# Signing with a stable identity keeps the macOS Accessibility grant across rebuilds.
SIGN_IDENTITY ?= Apple Development: Eric Dahl (D6LCAABBEQ)

.PHONY: build test firewall

build:
	go build -o phrased ./cmd/phrased
ifeq ($(shell uname),Darwin)
	codesign --force --sign "$(SIGN_IDENTITY)" --identifier dev.ericdahl.phrased phrased
endif

test:
	go test ./...

# Prints (never runs) the commands that let the macOS firewall pass incoming connections to
# phrased. In stealth mode it drops them silently, without asking.
firewall:
	@echo "Run these to let other tailnet devices reach phrased:"
	@echo "  sudo /usr/libexec/ApplicationFirewall/socketfilterfw --add $(CURDIR)/phrased"
	@echo "  sudo /usr/libexec/ApplicationFirewall/socketfilterfw --unblockapp $(CURDIR)/phrased"
