import XCTest

/// Pinching the deck resizes its buttons and never sends a phrase (#38).
final class PinchTests: XCTestCase {
    @MainActor
    private func launch() -> XCUIApplication {
        let app = XCUIApplication()
        app.launchArguments = ["-uiTestingStubHelper"]
        app.launch()
        return app
    }

    @MainActor
    func testAPinchOnAButtonSendsNothing() {
        let app = launch()
        let button = app.buttons["phrase.agent-status"]
        XCTAssertTrue(button.waitForExistence(timeout: 20))

        button.pinch(withScale: 2, velocity: 1)
        button.pinch(withScale: 0.5, velocity: -1)
        Thread.sleep(forTimeInterval: 1)

        XCTAssertEqual(app.staticTexts["stub.sendCount"].label, "0")
    }

    @MainActor
    func testPinchingChangesTheColumnsWithinTheLimits() {
        let app = launch()
        let grid = app.scrollViews["deck.grid"].firstMatch
        XCTAssertTrue(grid.waitForExistence(timeout: 20))
        XCTAssertEqual(grid.value as? String, "3 columns")

        grid.pinch(withScale: 4, velocity: 2)
        XCTAssertEqual(grid.value as? String, "2 columns")

        grid.pinch(withScale: 0.2, velocity: -2)
        XCTAssertEqual(grid.value as? String, "5 columns")
    }

    @MainActor
    func testResetButtonSizeGoesBackToThreeColumns() {
        let app = launch()
        let grid = app.scrollViews["deck.grid"].firstMatch
        XCTAssertTrue(grid.waitForExistence(timeout: 20))
        grid.pinch(withScale: 4, velocity: 2)
        XCTAssertEqual(grid.value as? String, "2 columns")

        app.buttons["deck.pages"].tap()
        app.buttons["pages.resetButtonSize"].tap()
        app.buttons["Done"].tap()

        XCTAssertEqual(grid.value as? String, "3 columns")
    }
}
