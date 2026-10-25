import XCTest

final class LaunchTests: XCTestCase {
    @MainActor
    func testAppLaunches() {
        let app = XCUIApplication()
        app.launch()
        XCTAssertTrue(app.staticTexts["phrasedeck"].waitForExistence(timeout: 5))
    }
}
