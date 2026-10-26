import XCTest

/// Runs the app against an in-process stub helper (launch argument
/// -uiTestingStubHelper), so no network or Keychain is involved.
final class SendTests: XCTestCase {
    @MainActor
    func testATapSendsExactlyOnce() {
        let app = XCUIApplication()
        app.launchArguments = ["-uiTestingStubHelper"]
        app.launch()

        let button = app.buttons["phrase.agent-status"]
        XCTAssertTrue(button.waitForExistence(timeout: 20))
        button.tap()

        let sends = app.staticTexts["stub.sendCount"]
        XCTAssertTrue(sends.waitForExistence(timeout: 20))
        let sentOnce = NSPredicate(format: "label == %@", "1")
        expectation(for: sentOnce, evaluatedWith: sends)
        waitForExpectations(timeout: 20)
        // Still one a moment later: no retry.
        Thread.sleep(forTimeInterval: 1)
        XCTAssertEqual(sends.label, "1")
    }

    /// After a send the Button says what happened, in words, not only a color (UX review).
    @MainActor
    func testTheButtonSaysTypedAfterASend() {
        let app = XCUIApplication()
        app.launchArguments = ["-uiTestingStubHelper"]
        app.launch()

        let button = app.buttons["phrase.agent-status"]
        XCTAssertTrue(button.waitForExistence(timeout: 20))
        button.tap()

        let typed = NSPredicate(format: "label CONTAINS %@", "Typed")
        expectation(for: typed, evaluatedWith: button)
        waitForExpectations(timeout: 5)
    }
}
