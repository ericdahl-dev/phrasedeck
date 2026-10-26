import XCTest

/// The error banner after a failed send (UX review): it pushes the grid down instead of covering
/// row 1, has a close button VoiceOver can reach, and closes itself after a few seconds.
final class BannerTests: XCTestCase {
    @MainActor
    func launchFailingSends() -> XCUIApplication {
        let app = XCUIApplication()
        app.launchArguments = ["-uiTestingStubHelper", "-uiTestingStubUnreachable"]
        app.launch()
        return app
    }

    @MainActor
    func testTheBannerPushesTheGridDownAndClosesItself() {
        let app = launchFailingSends()
        let button = app.buttons["phrase.agent-status"]
        XCTAssertTrue(button.waitForExistence(timeout: 20))
        let top = button.frame.minY

        button.tap()
        let banner = app.staticTexts["banner.message"]
        XCTAssertTrue(banner.waitForExistence(timeout: 10))
        XCTAssertGreaterThan(button.frame.minY, top, "row 1 moved down")
        XCTAssertGreaterThanOrEqual(button.frame.minY, banner.frame.maxY, "row 1 isn't covered")

        XCTAssertTrue(banner.waitForNonExistence(timeout: 10))
    }

    @MainActor
    func testTheCloseButtonClosesTheBanner() {
        let app = launchFailingSends()
        let button = app.buttons["phrase.agent-status"]
        XCTAssertTrue(button.waitForExistence(timeout: 20))
        button.tap()
        let banner = app.staticTexts["banner.message"]
        XCTAssertTrue(banner.waitForExistence(timeout: 10))

        app.buttons["Close"].tap()
        XCTAssertTrue(banner.waitForNonExistence(timeout: 2))
    }
}
