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

    /// Held at the right screen edge, a dragged Button flips to the next Page and is dropped there
    /// (spec section 6).
    @MainActor
    func testHoldingAtTheEdgeFlipsToTheNextPageAndDropsThere() {
        let app = launch(arrange: true)
        let button = app.buttons["phrase.agent-status"]
        XCTAssertTrue(button.waitForExistence(timeout: 20))
        addPages(app, ["Other"])
        enterArrangeMode(app, from: button)

        // Long enough for one flip (0.6 s), short of a second (another 0.8 s).
        dragToRightEdge(app, button, hold: 1.0)

        XCTAssertTrue(app.navigationBars["Other"].waitForExistence(timeout: 20))
        XCTAssertTrue(button.waitForValue("Row 1, column 3"))
    }

    /// Holding on flips again, two Pages away; the drag carries on after its own Page scrolls off.
    @MainActor
    func testHoldingOnAtTheEdgeFlipsTwice() {
        let app = launch(arrange: true)
        let button = app.buttons["phrase.agent-status"]
        XCTAssertTrue(button.waitForExistence(timeout: 20))
        addPages(app, ["Two", "Three"])
        enterArrangeMode(app, from: button)

        // Two flips (0.6 s, then 0.8 s), short of a third.
        dragToRightEdge(app, button, hold: 1.9)

        XCTAssertTrue(app.navigationBars["Three"].waitForExistence(timeout: 20))
        XCTAssertTrue(button.waitForValue("Row 1, column 3"))
    }

    // MARK: Delete (#81, slice 5)

    /// In Arrange mode each Button has a "−" badge. It asks first; Cancel keeps the Button, and Delete
    /// removes it and leaves a gap (spec section 9).
    @MainActor
    func testTheMinusBadgeAsksThenDeletesLeavingAGap() {
        let app = launch(arrange: true)
        let button = app.buttons["phrase.agent-status"]
        let next = app.buttons["phrase.status-table"]
        XCTAssertTrue(button.waitForExistence(timeout: 20))
        enterArrangeMode(app, from: button)
        XCTAssertEqual(next.value as? String, "Row 1, column 2")
        let badge = app.buttons["Delete Agent status"]
        XCTAssertTrue(badge.exists)

        badge.tap()
        let alert = app.alerts["Delete \u{201C}Agent status\u{201D}?"]
        XCTAssertTrue(alert.waitForExistence(timeout: 20))
        alert.buttons["Cancel"].tap()
        XCTAssertTrue(button.exists)

        badge.tap()
        XCTAssertTrue(alert.waitForExistence(timeout: 20))
        alert.buttons["Delete"].tap()

        XCTAssertTrue(button.waitForNonExistence(timeout: 20))
        XCTAssertEqual(next.value as? String, "Row 1, column 2", "the others stay put")
    }

    /// The long-press menu's Delete asks the same way (spec open question 4).
    @MainActor
    func testTheMenusDeleteAsksFirst() {
        let app = launch(arrange: false)
        let button = app.buttons["phrase.agent-status"]
        XCTAssertTrue(button.waitForExistence(timeout: 20))

        button.press(forDuration: 1)
        let delete = app.buttons["Delete"]
        XCTAssertTrue(delete.waitForExistence(timeout: 20))
        delete.tap()
        let alert = app.alerts["Delete \u{201C}Agent status\u{201D}?"]
        XCTAssertTrue(alert.waitForExistence(timeout: 20))
        alert.buttons["Delete"].tap()

        XCTAssertTrue(button.waitForNonExistence(timeout: 20))
    }

    @MainActor
    func addPages(_ app: XCUIApplication, _ names: [String]) {
        app.buttons["deck.pages"].tap()
        let field = app.textFields["pages.newName"]
        XCTAssertTrue(field.waitForExistence(timeout: 20))
        for name in names {
            field.tap()
            field.typeText(name)
            app.buttons["Add"].tap()
        }
        app.buttons["Done"].tap()
    }

    @MainActor
    func dragToRightEdge(_ app: XCUIApplication, _ button: XCUIElement, hold: TimeInterval) {
        let from = button.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.5))
        let window = app.windows.firstMatch
        let edge = window.coordinate(withNormalizedOffset: .zero)
            .withOffset(CGVector(dx: window.frame.width - 8, dy: button.frame.midY))
        from.press(forDuration: 0.1, thenDragTo: edge, withVelocity: .slow, thenHoldForDuration: hold)
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
