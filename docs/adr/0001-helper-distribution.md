# 0001: How the Mac helper is distributed

## Status

Accepted (Eric, 2026-10-25): option A, Developer ID + notarization, direct download and Homebrew cask. Researched 2026-10-25 for issue #35.

## Context

phrasedeck has two parts: an iPhone app that holds saved phrases, and `phrased`, a small Go binary on the Mac that receives a phrase over the network and types it into whatever app is focused. It types short text by posting synthetic keyboard events (`CGEventPost(kCGHIDEventTap, …)` in `helper/internal/typer/darwin.go`). For long text it puts the text on the clipboard and posts a synthetic Cmd-V. Both paths post events, so both need the user to approve event posting in System Settings.

Today the helper ships outside the Mac App Store. Issue #35 asks whether it could ship through the Mac App Store instead. The claim to test was: "A Mac App Store app can't post keystrokes into other apps under the App Sandbox."

## Findings

### Short answer

The claim is technically false and, in practice, partly true.

- **Technically:** posting keyboard events works from a sandboxed app once the user approves it. Apple's sandbox docs do not list event posting as forbidden. Apple DTS says the event-posting permission is compatible with the App Sandbox.
- **In App Review:** in 2026, App Review has rejected apps that post Cmd-V or keystrokes with `CGEvent.post`, citing guideline 2.4.5 and "Accessibility features for non-accessibility purposes". It has also approved similar apps. Outcomes are inconsistent and Apple publishes no rule for this case.

### 1. The Mac App Store requires the App Sandbox

- Guideline 2.4.5(i): Mac App Store apps "must be appropriately sandboxed" ([App Review Guidelines 2.4.5](https://developer.apple.com/app-store/review/guidelines/#2.4.5)).
- Apple's App Sandbox page: "To distribute a macOS app through the Mac App Store, you must enable the App Sandbox capability" ([App Sandbox](https://developer.apple.com/documentation/security/app-sandbox)).
- The rule has applied to new submissions since June 1, 2012 ([MacStories on TextExpander 4](https://www.macstories.net/news/textexpander-4-first-casualty-of-mac-app-store-sandboxing/)). Apps first published before then may still be unsandboxed, so finding an unsandboxed app on the store proves nothing about what new apps can do ([Quinn, DTS, Oct 2025](https://developer.apple.com/forums/thread/805556); [Quinn, Sep 2025](https://developer.apple.com/forums/thread/707680)).

### 2. Three different permissions are involved, and two of them show up under the same "Accessibility" label

Apple DTS engineer Quinn "The Eskimo!" explained this in March 2026 ([forum thread 820594](https://developer.apple.com/forums/thread/820594)):

- `PostEvent` controls posting UI events, for example with `CGEvent.post(tap:)`. This is what `phrased` uses.
- `ListenEvent` controls system-wide event monitoring, for example a CG event tap.
- `Accessibility` controls the accessibility APIs (`AXUIElement` and related), including reading the UI state of other apps.

In System Settings, both `PostEvent` and `Accessibility` appear under Privacy & Security > Accessibility. `ListenEvent` appears under Input Monitoring. Quinn calls the first mapping "a clear source of confusion" (paraphrased).

### 3. What the sandbox blocks

- **Accessibility APIs (`AXUIElement`): blocked.** Apple's "Review functionality that is incompatible with App Sandbox" list includes "Use of accessibility APIs in assistive apps" ([Protecting user data with App Sandbox](https://developer.apple.com/documentation/security/protecting-user-data-with-app-sandbox)). Quinn: "It's not possible to use the Accessibility APIs from a sandboxed app" ([thread 805556, Oct 2025](https://developer.apple.com/forums/thread/805556)), and "It's not possible for a sandboxed app to use the Accessibility privilege", with a footnote "Without employing temporary exception entitlements" ([thread 780626, Apr 2025](https://developer.apple.com/forums/thread/780626)).
- **Posting events (`CGEventPost`): not blocked.** The same restricted list does not include posting events. The nearest item is "Simulating user input in Open and Save dialogs", which is narrower. Quinn: "The APIs gated by the other two privileges *are* compatible with the App Sandbox", and in his experience the sandbox "isn't an impediment to apps that rely on the `PostEvent` and `ListenEvent` privileges" ([thread 820594, Mar 2026](https://developer.apple.com/forums/thread/820594)).
- **How a sandboxed app asks for permission.** `CGPreflightPostEventAccess()` and `CGRequestPostEventAccess()` exist from macOS 10.15 (Apple's documentation metadata for [CGRequestPostEventAccess](https://developer.apple.com/documentation/coregraphics/cgrequestposteventaccess()) and [CGPreflightPostEventAccess](https://developer.apple.com/documentation/coregraphics/cgpreflightposteventaccess())). Quinn pointed a sandboxed app developer to these in June 2022 and said explicit user approval, added in 10.15, is what made this allowed in the sandbox, while noting he had checked this for Input Monitoring, not event posting ([thread 708652](https://developer.apple.com/forums/thread/708652)). The two functions' doc pages have no discussion text.
- **The paste path does not avoid this.** Synthetic Cmd-V is a posted event, so it needs the same `PostEvent` approval as typing. Writing to the clipboard itself does not need it.

### 4. Temporary exception entitlements

- There is no `com.apple.security.accessibility` entitlement. Quinn: "It isn't a thing" ([thread 805556, Oct 2025](https://developer.apple.com/forums/thread/805556)).
- Quinn's footnote says the Accessibility privilege becomes reachable with temporary exception entitlements ([thread 780626](https://developer.apple.com/forums/thread/780626)). **Unverified:** which entitlement he means; Apple's entitlement reference pages could not be loaded to confirm one.
- On App Review: "App Review takes a very dim view of folks using temporary exception entitlements, especially when they do things ... which can result in a complete sandbox escape" ([Quinn, thread 663311, Oct 2020](https://developer.apple.com/forums/thread/663311)). The developer in that thread quoted a rejection saying the exceptions "are not appropriate and will not be granted".
- `phrased` does not need the Accessibility APIs, so this matters only if the app later wants to read other apps' UI.

### 5. What App Review actually does (sources conflict)

- **Guideline text.** 2.4.5 lists sandboxing, packaging, launch-at-login, updates and similar rules. It does not mention Accessibility or event posting ([App Review Guidelines](https://developer.apple.com/app-store/review/guidelines/#2.4.5)). Michael Tsai makes the same point ([mjtsai.com, June 2026](https://mjtsai.com/blog/2026/06/03/whisperpad-rejected-from-the-mac-app-store/)).
- **Rejections in 2026.**
  - A sandboxed clipboard manager that posts Cmd-V with `CGEvent.post` and uses no AX APIs was rejected twice under 2.4.5 for "Accessibility features for non-accessibility purposes" (poster's paraphrase). Other developers reported the same rejection in June and September 2026, with no resolution posted ([thread 820594](https://developer.apple.com/forums/thread/820594)).
  - Rejection text quoted in another thread: "The app requests access to Accessibility features on macOS but does not use these features for accessibility purposes", and on appeal, "it would be appropriate to remove Accessibility usage for non-accessibility purposes" ([thread 820676, Mar 2026](https://developer.apple.com/forums/thread/820676)). That app used a virtual HID device, not `CGEvent.post`, but got the same wording.
  - WhisperPad, a dictation app, had an update rejected under 2.4.5 in April 2026 and again in May 2026 after Apple said it would "take a closer look"; earlier versions using the same approach had been approved ([mjtsai.com](https://mjtsai.com/blog/2026/06/03/whisperpad-rejected-from-the-mac-app-store/), [developer's post](https://www.mitmllc.com/blog/apple-rejected-my-dictation-app/)). **Unverified:** whether WhisperPad uses AX APIs, `CGEventPost`, or both.
- **Approvals.** The same mjtsai post lists THOHT (approved, June 2026) and Type Mac (approved, then told it could no longer be updated, so the developer moved to direct sales). TypeIt4Me 7 (April 2025) ships sandboxed on the Mac App Store and needs Accessibility approval. Its developer says reviewers are inconsistent: "Some have waved our submissions through quickly, while others have initially rejected them point blank", and that Apple has not given clear guidance ([mjtsai.com, April 2025](https://mjtsai.com/blog/2025/04/24/typeit4me-7/)).
- **The conflict.** DTS says event posting is technically fine in the sandbox. App Review sometimes rejects it anyway, seemingly because the permission shows up as "Accessibility". DTS repeatedly says it cannot speak for App Review ([820594](https://developer.apple.com/forums/thread/820594), [805556](https://developer.apple.com/forums/thread/805556), [780626](https://developer.apple.com/forums/thread/780626)).
- **Inferred, not verified:** when TypeIt4Me's developer says the sandboxed app needs "Accessibility Access", that is probably the `PostEvent` grant, which appears in the same System Settings pane, rather than AX API use.

### 6. Other Mac App Store rules that hit `phrased` specifically

- 2.4.5(ii): apps must be "packaged and submitted using technologies provided in Xcode" and be "self-contained, single app installation bundles". A bare Go binary would need at least an app bundle wrapper. In practice that likely means a Swift or Objective-C front end.
- 2.4.5(iii): no auto-launch or login items "without consent", and no processes that keep running after the user quits "without consent". A background receiver must be opt-in.
- 2.4.5(iv): may not "download or install standalone apps ... additional code". 2.4.5(vii): updates only through the Mac App Store.
- An embedded command-line tool "must inherit the containing app's sandbox configuration" ([Protecting user data with App Sandbox](https://developer.apple.com/documentation/security/protecting-user-data-with-app-sandbox)).
- Listening for connections from the iPhone needs the sandbox's incoming network entitlement (`com.apple.security.network.server`). Not checked against current docs; listed from general knowledge.

## What comparable apps do

| App | Distribution channel | Mechanism | Source |
|---|---|---|---|
| Keyboard Maestro (Stairways) | Direct only since version 6. Version 5 was on the Mac App Store, then pulled in 2013. | Needs Accessibility approval for the app and its Engine; offers "Insert Text by Typing" and "Insert Text by Pasting". Exact API unverified. | [Stairways, 2013](https://www.stairways.com/blog/2013-09-05-exit-mac-app-store); [KM forum FAQ](https://forum.keyboardmaestro.com/t/why-km-is-not-sold-on-the-app-store/11132); [KM wiki](https://wiki.keyboardmaestro.com/assistance/Accessibility_Permission_Problem) |
| TextExpander (Smile) | Direct, Developer ID, since TextExpander 4 (June 2012). TextExpander 3 had been on the Mac App Store. | Watches typing system-wide and inserts text; the sandbox rules of 2012 did not allow it. Current insertion API unverified. | [Macworld](https://www.macworld.com/article/218191/textexpander_4_adds_features_leaves_mac_app_store.html); [Engadget](https://www.engadget.com/2012-06-21-sandboxing-keeps-textexpander-4-out-of-the-mac-app-store.html) |
| Elgato Stream Deck | Direct download from elgato.com. Local copy (7.4.2) is signed "Developer ID Application: Corsair Memory, Inc.", hardened runtime, no `app-sandbox` entitlement. No Mac App Store version found (unverified negative); the only App Store listings are mobile companion apps. | Hotkey and Text actions need Accessibility approval (Elgato help article, read via search snippet because the page returned 403). | Local `codesign` check; [Elgato downloads](https://www.elgato.com/us/en/s/downloads); [Elgato help](https://help.elgato.com/hc/en-us/articles/360028240651-Elgato-Stream-Deck-Fixing-Hotkeys-and-Text-actions-in-macOS-10-14-Mojave) |
| TypeIt4Me 7 (Ettore) | Mac App Store, sandboxed (since April 2025) | Needs Accessibility approval; reviewers inconsistent | [mjtsai.com](https://mjtsai.com/blog/2025/04/24/typeit4me-7/) |
| Rocket Typist (Witt Software) | Mac App Store | Unverified | [App Store](https://apps.apple.com/app/rocket-typist/id6463636684); [mjtsai.com](https://mjtsai.com/blog/2025/04/24/typeit4me-7/) |
| Typinator (Ergonis) | Direct from ergonis.com | Needs Accessibility approval; API unverified | [Ergonis help](https://help.typinator.ergonis.com/hc/en-us/articles/22645458376092) |
| Espanso (open source) | Homebrew or direct download | Docs say it relies on the Accessibility permission; clipboard backend unverified | [Espanso install docs](https://espanso.org/docs/install/mac) |
| aText | Unverified | Unverified | (none found) |

The pattern: the established automation and text-expansion tools ship outside the Mac App Store with Developer ID. The Mac App Store examples that do type into other apps are recent, sandboxed, and report uneven review.

## Options

### A. Developer ID + notarization, direct download and Homebrew cask (current approach)

- Works today with the existing Go binary and `CGEventPost` + Cmd-V. No sandbox, so no change to how the helper is built.
- Users approve "Accessibility" once in System Settings, the same step Keyboard Maestro, Stream Deck and Espanso ask for.
- Costs: Apple Developer Program membership (already needed for the iPhone app), notarization in the release process, our own update path (Homebrew or an in-app updater), and less discovery than the Mac App Store.

### B. Mac App Store, sandboxed

- Technically possible: `CGEventPost` and Cmd-V work in the sandbox once the user approves `PostEvent`.
- Needs a rewrite or at least a native app bundle built with Xcode, a sandbox network entitlement, opt-in launch at login, and Mac App Store updates only.
- Main risk: App Review may reject it under 2.4.5 as "Accessibility for non-accessibility purposes", as it did for a similar clipboard app in 2026. Approval of one version does not guarantee the next (WhisperPad, Type Mac).
- Benefit: store discovery, one purchase flow alongside the iPhone app, Apple-handled updates.

### C. Mac App Store app plus a separate unsandboxed helper

- Not viable under current rules. 2.4.5(ii) requires a single self-contained bundle that installs nothing in shared locations, 2.4.5(iv) forbids downloading standalone apps or extra code, and an embedded tool inherits the app's sandbox. A store app that tells users to download a separate unsandboxed helper from our site would still need option A for the helper, so it adds work without removing any.

### D. Both A and B

- Keep A as the main channel and try B as an extra. If App Review rejects B, A is unaffected.
- Costs: two builds, two update paths, and a native app bundle for B.

## Recommendation

**Recommendation (Eric decides):** stay with option A: Developer ID, notarized, distributed as a direct download and a Homebrew cask. It matches what Keyboard Maestro, TextExpander, Typinator, Espanso and Stream Deck do, it needs no rewrite of `phrased`, and it does not depend on how a given reviewer reads 2.4.5.

Revisit option B (or D) only if store discovery becomes important. If so, first submit a minimal sandboxed build that only posts events, use `CGRequestPostEventAccess()` rather than `AXIsProcessTrustedWithOptions`, explain in the review notes that the app uses the `PostEvent` permission and no Accessibility APIs, and link Quinn's thread 820594.

## Consequences

- Users still grant the Accessibility (event posting) permission by hand on first run. Onboarding should show this step clearly.
- Releases must be signed with Developer ID and notarized. Homebrew cask and update handling stay our job.
- `phrased` can stay a Go binary with no sandbox constraints, so the clipboard + Cmd-V path and any future features (for example reading the focused app) remain open.
- The iPhone app's App Store status is not affected by this decision.
- Closing #35 with this ADR leaves room to reopen it if Apple publishes clearer guidance on `PostEvent` in Mac App Store apps.

## Not verified

- Which temporary exception entitlement would allow Accessibility APIs in a sandboxed app, and whether App Review has ever accepted it for this purpose.
- Whether any Mac App Store version of the Stream Deck app exists (none found).
- How Rocket Typist, TypeIt4Me and WhisperPad insert text (`CGEventPost`, AX APIs, or both).
- aText's current distribution channel and mechanism.
- The exact APIs Keyboard Maestro, TextExpander, Typinator and Espanso use to insert text; only their permission requirements are sourced.
- The sandbox network entitlement name was not checked against current docs.
