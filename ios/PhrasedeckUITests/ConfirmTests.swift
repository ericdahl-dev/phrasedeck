import XCTest

/// Phrases marked confirm ("Yes", "Merge it") need a second tap within
/// 1.5 seconds; other phrases send on the first.
final class ConfirmTests: XCTestCase {
    @MainActor
    private lazy var app: XCUIApplication = {
        let app = XCUIApplication()
        app.launchArguments = ["-uiTestingStubHelper"]
        app.launch()
        return app
    }()

    @MainActor
    func testAConfirmPhraseSendsOnlyOnTheSecondTap() {
        let yes = app.buttons["phrase.yes"]
        XCTAssertTrue(yes.waitForExistence(timeout: 5))

        // Nothing is queried between the taps: on a slow simulator a query can outlast the
        // 1.5-second window. testASecondTapAfterTheWindowDoesNotSend checks the armed state.
        yes.tap()
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

        yes.tap()
        XCTAssertTrue(app.staticTexts["tap again"].waitForExistence(timeout: 2))
        XCTAssertEqual(sendCount, "0")
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
