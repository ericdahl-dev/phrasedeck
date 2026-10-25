import XCTest

/// A button's size is set in its editor and shows on the deck right away (#51).
final class SpanTests: XCTestCase {
    @MainActor
    func testAButtonSetToTwoByTwoDoublesInSizeAndStillSends() {
        let app = XCUIApplication()
        app.launchArguments = ["-uiTestingStubHelper"]
        app.launch()
        let button = app.buttons["phrase.agent-status"]
        XCTAssertTrue(button.waitForExistence(timeout: 20))
        let before = button.frame

        // The context menu animates in; on a slow simulator it isn't there the moment the press ends.
        button.press(forDuration: 1)
        let edit = app.buttons["Edit"]
        XCTAssertTrue(edit.waitForExistence(timeout: 20))
        edit.tap()
        XCTAssertTrue(app.buttons["editor.span.2x2"].waitForExistence(timeout: 20))
        app.buttons["editor.span.2x2"].tap()
        XCTAssertEqual(app.staticTexts["editor.span.value"].label, "2 × 2")
        app.buttons["editor.save"].tap()

        XCTAssertTrue(button.waitForExistence(timeout: 20))
        let after = button.frame
        XCTAssertGreaterThan(after.width, before.width * 1.9)
        XCTAssertGreaterThan(after.height, before.height * 1.9)

        button.tap()
        let count = app.staticTexts["stub.sendCount"]
        XCTAssertTrue(count.waitForExistence(timeout: 20))
        XCTAssertEqual(count.label, "1")
    }
}
