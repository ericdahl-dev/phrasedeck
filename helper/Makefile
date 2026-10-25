# Signing with a stable identity keeps the macOS Accessibility grant across rebuilds.
SIGN_IDENTITY ?= Apple Development: Eric Dahl (D6LCAABBEQ)

.PHONY: build test firewall install-mac plist

BIN_DIR ?= $(HOME)/.local/bin
AGENT ?= $(HOME)/Library/LaunchAgents/dev.ericdahl.phrased.plist
FIREWALL_BIN ?= $(CURDIR)/phrased

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
