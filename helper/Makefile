# Signing with a stable identity keeps the macOS Accessibility grant across rebuilds.
SIGN_IDENTITY ?= Apple Development: Eric Dahl (D6LCAABBEQ)

.PHONY: build test firewall install-mac plist install-linux

BIN_DIR ?= $(HOME)/.local/bin
AGENT ?= $(HOME)/Library/LaunchAgents/dev.ericdahl.phrased.plist
FIREWALL_BIN ?= $(CURDIR)/phrased
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
	@echo "  sudo /usr/libexec/ApplicationFirewall/socketfilterfw --add $(FIREWALL_BIN)"
	@echo "  sudo /usr/libexec/ApplicationFirewall/socketfilterfw --unblockapp $(FIREWALL_BIN)"
	@echo "On a managed Mac these are refused; see \"On a managed Mac\" in the README."

# Renders the LaunchAgent with this account's home folder.
plist:
	@mkdir -p $(dir $(AGENT))
	sed 's|__HOME__|$(HOME)|g' install/dev.ericdahl.phrased.plist > $(AGENT)
	plutil -lint $(AGENT)

# Installs the signed build to $(BIN_DIR) and (re)loads the LaunchAgent, which starts phrased at
# login and keeps it running. install -S swaps the file in with a rename, so a running copy
# isn't overwritten in place.
install-mac: build plist
	@mkdir -p $(BIN_DIR) $(HOME)/Library/Logs
	install -S -m 755 phrased $(BIN_DIR)/phrased
	-launchctl bootout gui/$$(id -u)/dev.ericdahl.phrased 2>/dev/null
	launchctl bootstrap gui/$$(id -u) $(AGENT)
	@echo "phrased is running from $(BIN_DIR)/phrased; log: $(HOME)/Library/Logs/phrased.log"
	@echo "The first install there needs Accessibility for $(BIN_DIR)/phrased (see the README)."
	@$(MAKE) --no-print-directory firewall FIREWALL_BIN=$(BIN_DIR)/phrased

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
