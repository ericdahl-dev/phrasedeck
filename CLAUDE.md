# Phrasedeck: notes for agents

The README covers install, pairing, flags, the API and TestFlight setup; this file covers how to
work here. Read these first:
- `CONTEXT.md`: the domain glossary.
- `docs/plans/`: the design and the implementation plan, including "What each status means".
- `docs/launch/lan-threat-checklist.md`: before changing the Home network route.

## Workflow

- **Test first** (`/tdd`) for code: write the test, watch it fail, then make it pass. Put the red
  run in the PR. Docs-only changes are exempt.
- **Terse replies** (`/caveman`) in chat and agent-to-agent messages. If a session lost it (after a
  compaction, say), invoke it again. PRs, issues, commits, docs and UI copy stay in full sentences.
- **One issue, one PR** that closes it (`Closes #N`). Small PRs that build on each other are fine.
- **Review, then merge.** Another agent reviews. Only after their pass, turn on
  `gh pr merge <N> --auto --squash --delete-branch`.
  - Everyone shares Eric's GitHub account, so a review is a comment ("pass"), not an approval.
  - Never pass `--delete-branch` on a branch that another open PR is based on.
- **Eric decides:** release tags, anything installed on his devices, `sudo` and firewall changes.
- **Eric's live helper:** never call `/type` or `/pair` on it without asking. It types on his
  screen. Read-only `GET /health` is fine. To test, run a scratch `phrased` with its own
  `-config` directory and `-port`.

## Commits and PRs

- End AI-assisted commits with `Assisted-by: Claude Code:<model>`. Never add `Co-Authored-By:`,
  `Claude-Session:` or claude.ai links.
- Use American spelling everywhere. CI runs the org's `american-spelling` check
  (`ericdahl-dev/spelling-dialect@v1`, `dialect: american`); the same check locally:
  ```sh
  uvx --from git+https://github.com/ericdahl-dev/spelling-dialect@v1 spelling-dialect --dialect american .
  ```
  If a line has to keep another spelling (an API name), add `// spelling: ok (why)` to it.

## Build and test

Helper (`helper/`, Go):
```sh
make build                 # builds ./phrased; on macOS also signs it, so Accessibility survives rebuilds
go test -race ./...
GOOS=linux go vet ./...    # the Wayland backend only builds for Linux
gofmt -l .                 # CI fails if this prints anything
```
On Linux, CI runs `gofmt`, `go vet ./...` and `go test -race ./...`. On macOS it runs
`go build ./...`, `go vet ./...`, `go test ./internal/typer/` and `make plist`.

iOS app (`ios/`): the Xcode project is generated, not committed.
```sh
xcodegen
xcodebuild build-for-testing -scheme Phrasedeck -destination 'generic/platform=iOS Simulator'
```
CI (`test`) runs `xcodebuild test -scheme Phrasedeck -destination "id=<iPhone simulator>"`, unit
and UI tests. Keep at most one Simulator booted on this Mac across all sessions, and shut down
any you boot.

The required checks are `linux`, `macos`, `test` and `spelling`.

## Release

- Pushing a `vX.Y.Z` tag on `main` (or a `release/` branch) runs `.github/workflows/release.yml`.
  It archives the app, signs it and uploads it to TestFlight.
- The build number is `run_number.run_attempt`. Never edit build numbers in files.
- Tag only when Eric asks.
- **CloudKit:** a new synced model field needs a Debug run, which creates it in Development. Then
  CloudKit Console > Deploy Schema Changes to Production, before any release that uses it.
  Otherwise TestFlight builds can't sync it.

## Security invariants

- `phrased` listens on **47811** (plain HTTP, tailnet only) and, with `-lan`, on **47812**
  (HTTPS, only the `-lan` private subnets and tailnet peers).
- A token never goes over plain HTTP to a non-tailnet address. The app refuses this
  (`notTailnet`) before sending anything.
- The Home network route is HTTPS checked against the Computer's Pin (the SHA-256 of the Helper's
  self-signed certificate). The Pin arrives only over a trusted channel (`/pair` or `/info` over the tailnet, or the QR code).
  `/info` is never served on the Home network listener (47812).
- Each listener accepts only its own `Host` names. Tokens, limits and lockout are shared.
- Never print or commit tokens, `.p8` keys, certificates or repository secrets.
