import XCTest

/// Arrange mode (#81). Until dragging works it's reached only in DEBUG builds launched with
/// -arrangeEnabled, so a release never shows half of it.
final class ArrangeTests: XCTestCase {
    @MainActor
    func launch(arrange: Bool) -> XCUIApplication {
        let app = XCUIApplication()
        app.launchArguments = ["-uiTestingStubHelper"] + (arrange ? ["-arrangeEnabled"] : [])
        app.launch()
        return app
    }

    @MainActor
    func testWithoutTheFlagTheMenuHasNoEditDeck() {
        let app = launch(arrange: false)
        let button = app.buttons["phrase.agent-status"]
        XCTAssertTrue(button.waitForExistence(timeout: 20))

        button.press(forDuration: 1)

        XCTAssertTrue(app.buttons["Edit"].waitForExistence(timeout: 20))
        XCTAssertFalse(app.buttons["Edit Deck"].exists)
    }

    @MainActor
    func testArrangeModeShowsDoneTypesNothingAndEndsWithDone() {
        let app = launch(arrange: true)
        let button = app.buttons["phrase.agent-status"]
        XCTAssertTrue(button.waitForExistence(timeout: 20))

        button.press(forDuration: 1)
        let editDeck = app.buttons["Edit Deck"]
        XCTAssertTrue(editDeck.waitForExistence(timeout: 20))
        editDeck.tap()

        let done = app.buttons["arrange.done"]
        XCTAssertTrue(done.waitForExistence(timeout: 20))
        XCTAssertFalse(app.buttons["deck.add"].exists, "the + tile is hidden in Arrange mode")
        XCTAssertFalse(app.buttons["deck.pages"].exists, "only Done is in the toolbar")
        XCTAssertEqual(button.value as? String, "Row 1, column 1")

        button.tap()
        XCTAssertEqual(app.staticTexts["stub.sendCount"].label, "0", "taps never type in Arrange mode")

        done.tap()
        XCTAssertTrue(app.buttons["deck.add"].waitForExistence(timeout: 20))
        button.tap()
        XCTAssertTrue(app.staticTexts["stub.sendCount"].waitForLabel("1"))
    }

    /// Leaving the app ends Arrange mode, so coming back never finds Buttons that won't type.
    @MainActor
    func testLeavingTheAppEndsArrangeMode() {
        let app = launch(arrange: true)
        let button = app.buttons["phrase.agent-status"]
        XCTAssertTrue(button.waitForExistence(timeout: 20))
        enterArrangeMode(app, from: button)

        XCUIDevice.shared.press(.home)
        app.activate()

        XCTAssertTrue(app.buttons["deck.add"].waitForExistence(timeout: 20))
        XCTAssertFalse(app.buttons["arrange.done"].exists)
    }

    // MARK: Dragging (#81, slice 3)

    @MainActor
    func enterArrangeMode(_ app: XCUIApplication, from button: XCUIElement) {
        button.press(forDuration: 1)
        let editDeck = app.buttons["Edit Deck"]
        XCTAssertTrue(editDeck.waitForExistence(timeout: 20))
        editDeck.tap()
        XCTAssertTrue(app.buttons["arrange.done"].waitForExistence(timeout: 20))
    }

    /// In Arrange mode a Button drags at once; dropped on another Button it takes that Spot.
    @MainActor
    func testInArrangeModeAButtonDragsOntoAnotherAndTakesItsSpot() {
        let app = launch(arrange: true)
        let button = app.buttons["phrase.agent-status"]
        XCTAssertTrue(button.waitForExistence(timeout: 20))
        enterArrangeMode(app, from: button)
        let target = app.buttons.matching(NSPredicate(format: "value == %@", "Row 2, column 2")).firstMatch
        XCTAssertTrue(target.exists)

        button.press(forDuration: 0.1, thenDragTo: target, withVelocity: .slow, thenHoldForDuration: 0.5)

        XCTAssertTrue(button.waitForValue("Row 2, column 2"))
        XCTAssertTrue(app.buttons["arrange.done"].exists, "still in Arrange mode after the drop")
    }

    /// From normal mode, holding a Button and then moving the finger closes the menu, enters Arrange
    /// mode, and carries the Button (spec section 3).
    @MainActor
    func testHoldingThenDraggingEntersArrangeModeCarryingTheButton() {
        let app = launch(arrange: true)
        let button = app.buttons["phrase.agent-status"]
        XCTAssertTrue(button.waitForExistence(timeout: 20))
        // Normal mode has no Row/column values, so aim one Square (plus 12 pt spacing) right and down.
        let size = button.frame.size
        let from = button.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.5))
        let to = from.withOffset(CGVector(dx: size.width + 12, dy: size.height + 12))

        from.press(forDuration: 1.0, thenDragTo: to, withVelocity: .slow, thenHoldForDuration: 0.5)

        XCTAssertTrue(app.buttons["arrange.done"].waitForExistence(timeout: 20))
        XCTAssertTrue(button.waitForValue("Row 2, column 2"))
    }
}

private extension XCUIElement {
    func waitForValue(_ value: String, timeout: TimeInterval = 20) -> Bool {
        let done = XCTNSPredicateExpectation(predicate: NSPredicate(format: "value == %@", value), object: self)
        return XCTWaiter().wait(for: [done], timeout: timeout) == .completed
    }

    func waitForLabel(_ label: String, timeout: TimeInterval = 20) -> Bool {
        let done = XCTNSPredicateExpectation(predicate: NSPredicate(format: "label == %@", label), object: self)
        return XCTWaiter().wait(for: [done], timeout: timeout) == .completed
    }
}
