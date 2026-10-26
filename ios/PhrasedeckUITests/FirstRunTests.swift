import XCTest

/// The first run, with no Computer yet, and an empty Page (UX review).
final class FirstRunTests: XCTestCase {
    @MainActor
    func launch(computers: Bool) -> XCUIApplication {
        let app = XCUIApplication()
        app.launchArguments = ["-uiTestingStubHelper"] + (computers ? [] : ["-uiTestingStubNoComputers"])
        app.launch()
        return app
    }

    /// With no Computer, the title opens the add sheet directly, not a menu.
    @MainActor
    func testWithNoComputerTheTitleOpensTheAddSheet() {
        let app = launch(computers: false)
        let title = app.buttons["helperPicker"]
        XCTAssertTrue(title.waitForExistence(timeout: 20))

        title.tap()
        XCTAssertTrue(app.navigationBars["Add a computer"].waitForExistence(timeout: 10))
    }

    /// A send with no Computer explains why and offers Add.
    @MainActor
    func testWithNoComputerASendOffersAdd() {
        let app = launch(computers: false)
        let button = app.buttons["phrase.agent-status"]
        XCTAssertTrue(button.waitForExistence(timeout: 20))

        button.tap()
        let add = app.buttons["banner.add"]
        XCTAssertTrue(add.waitForExistence(timeout: 10))
        add.tap()
        XCTAssertTrue(app.navigationBars["Add a computer"].waitForExistence(timeout: 10))
    }

    /// An empty Page says what Buttons are and offers to add one.
    @MainActor
    func testAnEmptyPageOffersToAddAButton() {
        let app = launch(computers: true)
        XCTAssertTrue(app.buttons["phrase.agent-status"].waitForExistence(timeout: 20))
        app.buttons["deck.pages"].tap()
        let field = app.textFields["pages.newName"]
        XCTAssertTrue(field.waitForExistence(timeout: 20))
        field.tap()
        field.typeText("Empty")
        app.buttons["Add"].tap()
        app.buttons["Done"].tap()

        app.swipeLeft()
        let add = app.buttons["Add a Button"]
        XCTAssertTrue(add.waitForExistence(timeout: 10))
        add.tap()
        XCTAssertTrue(app.textFields["editor.label"].waitForExistence(timeout: 10))
    }
}
