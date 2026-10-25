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
        XCTAssertTrue(button.waitForExistence(timeout: 5))
        button.tap()

        let sends = app.staticTexts["stub.sendCount"]
        XCTAssertTrue(sends.waitForExistence(timeout: 5))
        let sentOnce = NSPredicate(format: "label == %@", "1")
        expectation(for: sentOnce, evaluatedWith: sends)
        waitForExpectations(timeout: 5)
        // Still one a moment later: no retry.
        Thread.sleep(forTimeInterval: 1)
        XCTAssertEqual(sends.label, "1")
    }
}
