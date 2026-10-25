import XCTest

/// Phrases marked confirm ("Yes", "Merge it") need a second tap within
/// 1.5 seconds; other phrases send on the first. The window's expiry is
/// unit-tested with fixed times in ConfirmGateTests.
final class ConfirmTests: XCTestCase {
    /// Extra launch arguments, set before the first use of `app`.
    private var extraArguments: [String] = []

    @MainActor
    private lazy var app: XCUIApplication = {
        let app = XCUIApplication()
        app.launchArguments = ["-uiTestingStubHelper"] + extraArguments
        app.launch()
        return app
    }()

    @MainActor
    func testAConfirmPhraseSendsOnlyOnTheSecondTap() {
        // A 10-second window, so a slow simulator can't let it run out between the taps.
        extraArguments = ["-uiTestingConfirmWindow", "10"]
        let yes = app.buttons["phrase.yes"]
        XCTAssertTrue(yes.waitForExistence(timeout: 5))

        yes.tap()
        XCTAssertTrue(app.staticTexts["tap again"].waitForExistence(timeout: 2))
        XCTAssertEqual(sendCount, "0")

        yes.tap()
        waitForSendCount("1")
    }

    @MainActor
    func testANormalPhraseSendsOnTheFirstTap() {
        let status = app.buttons["phrase.agent-status"]
        XCTAssertTrue(status.waitForExistence(timeout: 5))

        status.tap()

        waitForSendCount("1")
    }

    @MainActor
    func testASecondTapAfterTheWindowDoesNotSend() {
        let yes = app.buttons["phrase.yes"]
        XCTAssertTrue(yes.waitForExistence(timeout: 5))

        // The real 1.5-second window: a slow simulator only makes the gap longer. Nothing is
        // checked while armed, since that could miss the window.
        yes.tap()
        Thread.sleep(forTimeInterval: 2)
        yes.tap()
        Thread.sleep(forTimeInterval: 1)

        XCTAssertEqual(sendCount, "0")
    }

    @MainActor
    private var sendCount: String { app.staticTexts["stub.sendCount"].label }

    @MainActor
    private func waitForSendCount(_ value: String) {
        expectation(for: NSPredicate(format: "label == %@", value), evaluatedWith: app.staticTexts["stub.sendCount"])
        waitForExpectations(timeout: 5)
    }
}
