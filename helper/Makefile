# Signing with a stable identity keeps the macOS Accessibility grant across rebuilds.
SIGN_IDENTITY ?= Apple Development: Eric Dahl (D6LCAABBEQ)

.PHONY: build test firewall install-linux

BIN_DIR ?= $(HOME)/.local/bin
UNIT_DIR ?= $(HOME)/.config/systemd/user

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

# Installs phrased to $(BIN_DIR) and runs it as a systemd user service tied to the graphical
# session. Needs wtype and wl-clipboard (pacman -S wtype wl-clipboard). The rename swaps in the
# new binary even while the old one is running.
install-linux: build
	@mkdir -p $(BIN_DIR) $(UNIT_DIR)
	install -m 755 phrased $(BIN_DIR)/phrased.new
	mv -f $(BIN_DIR)/phrased.new $(BIN_DIR)/phrased
	install -m 644 install/phrased.service $(UNIT_DIR)/phrased.service
	systemctl --user daemon-reload
	systemctl --user enable phrased
	systemctl --user restart phrased
	@echo "phrased is running; see: systemctl --user status phrased; journalctl --user -u phrased"
