import XCTest

final class LaunchTests: XCTestCase {
    @MainActor
    func testFirstLaunchShowsTheStarterDeck() {
        let app = XCUIApplication()
        app.launchArguments = ["-uiTestingStubHelper"]
        app.launch()

        XCTAssertTrue(app.buttons["phrase.agent-status"].waitForExistence(timeout: 5))
        XCTAssertTrue(app.buttons["phrase.merge-it"].exists)
        XCTAssertTrue(app.buttons["helperPicker"].exists)
    }
}
