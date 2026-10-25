import XCTest

/// Opening a phrasedeck://pair link (what the Camera app does with the QR
/// code from phrased pair) asks once, then pairs with the stub helper.
final class PairLinkUITests: XCTestCase {
    @MainActor
    func testAPairLinkAddsTheComputerAfterOneConfirm() {
        let app = XCUIApplication()
        app.launchArguments = ["-uiTestingStubHelper"]
        app.launch()
        XCTAssertTrue(app.buttons["helperPicker"].waitForExistence(timeout: 20))

        app.open(URL(string: "phrasedeck://pair?code=123456&host=omarchy-laptop.tail1fdac.ts.net&port=47811&v=1")!)

        let confirm = app.buttons["addHelper.confirmLink"]
        XCTAssertTrue(confirm.waitForExistence(timeout: 20))
        XCTAssertTrue(app.staticTexts["omarchy-laptop.tail1fdac.ts.net"].exists)
        confirm.tap()

        let picker = app.buttons["helperPicker"]
        expectation(for: NSPredicate(format: "label CONTAINS %@", "omarchy-laptop"), evaluatedWith: picker)
        waitForExpectations(timeout: 20)
    }

    @MainActor
    func testANonTailnetLinkIsRefused() {
        let app = XCUIApplication()
        app.launchArguments = ["-uiTestingStubHelper"]
        app.launch()
        XCTAssertTrue(app.buttons["helperPicker"].waitForExistence(timeout: 20))

        app.open(URL(string: "phrasedeck://pair?code=123456&host=evil.example.com&v=1")!)

        XCTAssertTrue(app.staticTexts["addHelper.error"].waitForExistence(timeout: 20))
        XCTAssertFalse(app.buttons["addHelper.confirmLink"].exists)
    }
}
