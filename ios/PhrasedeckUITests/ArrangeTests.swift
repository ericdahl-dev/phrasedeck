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
        XCTAssertTrue(app.buttons["deck.add"].exists, "the + tile shows only in Arrange mode")
        XCTAssertTrue(app.buttons["deck.new"].exists, "the toolbar + is always there")
        XCTAssertFalse(app.buttons["deck.pages"].exists, "Pages leaves the toolbar")
        XCTAssertEqual(button.value as? String, "Row 1, column 1")

        button.tap()
        XCTAssertEqual(app.staticTexts["stub.sendCount"].label, "0", "taps never type in Arrange mode")

        done.tap()
        XCTAssertTrue(app.buttons["deck.pages"].waitForExistence(timeout: 20))
        XCTAssertFalse(app.buttons["deck.add"].exists, "no + tile in normal mode")
        button.tap()
        XCTAssertTrue(app.staticTexts["stub.sendCount"].waitForLabel("1"))
    }

    /// Leaving the app ends Arrange mode, so coming back never finds Buttons that won't type.
    /// The toolbar "+" adds a Button to the Page shown, in normal mode too.
    @MainActor
    func testTheToolbarPlusAddsAButton() {
        let app = launch(arrange: false)
        XCTAssertTrue(app.buttons["phrase.agent-status"].waitForExistence(timeout: 20))
        XCTAssertFalse(app.buttons["deck.add"].exists, "no + tile in normal mode")

        app.buttons["deck.new"].tap()

        XCTAssertTrue(app.navigationBars.buttons["Cancel"].waitForExistence(timeout: 20), "the add sheet opens")
    }

    @MainActor
    func testLeavingTheAppEndsArrangeMode() {
        let app = launch(arrange: true)
        let button = app.buttons["phrase.agent-status"]
        XCTAssertTrue(button.waitForExistence(timeout: 20))
        enterArrangeMode(app, from: button)

        XCUIDevice.shared.press(.home)
        app.activate()

        XCTAssertTrue(app.buttons["deck.pages"].waitForExistence(timeout: 20))
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

    /// Portrait and landscape each keep their own layout: a move in landscape leaves portrait as it was.
    @MainActor
    func testLandscapeHasItsOwnLayout() {
        defer { XCUIDevice.shared.orientation = .portrait }
        let app = launch(arrange: true)
        let button = app.buttons["phrase.agent-status"]
        XCTAssertTrue(button.waitForExistence(timeout: 20))
        enterArrangeMode(app, from: button)
        let portraitTarget = app.buttons.matching(NSPredicate(format: "value == %@", "Row 2, column 2")).firstMatch
        button.press(forDuration: 0.1, thenDragTo: portraitTarget, withVelocity: .slow, thenHoldForDuration: 0.5)
        XCTAssertTrue(button.waitForValue("Row 2, column 2"))

        XCUIDevice.shared.orientation = .landscapeLeft
        let first = app.buttons.matching(NSPredicate(format: "value == %@", "Row 1, column 1")).firstMatch
        XCTAssertTrue(first.waitForExistence(timeout: 20))
        button.press(forDuration: 0.1, thenDragTo: first, withVelocity: .slow, thenHoldForDuration: 0.5)
        XCTAssertTrue(button.waitForValue("Row 1, column 1"))

        XCUIDevice.shared.orientation = .portrait
        XCTAssertTrue(button.waitForValue("Row 2, column 2"), "portrait kept its own layout")
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

    // MARK: Auto-scroll (spec section 5)

    /// Held near the bottom of a Page taller than the screen, a dragged Button scrolls it, so the
    /// first row goes off the top.
    @MainActor
    func testHoldingNearTheBottomScrollsThePage() {
        let app = launch(arrange: true)
        let button = app.buttons["phrase.agent-status"]
        let first = app.buttons["phrase.status-table"]
        XCTAssertTrue(button.waitForExistence(timeout: 20))
        let grid = app.scrollViews["deck.grid"].firstMatch
        // Two columns and a 1 by 3 Button at the end run the Page past the bottom of the screen.
        grid.pinch(withScale: 4, velocity: 2)
        XCTAssertEqual(grid.value as? String, "2 columns")
        app.buttons["deck.new"].tap()
        let label = app.textFields["editor.label"]
        XCTAssertTrue(label.waitForExistence(timeout: 20))
        label.tap()
        label.typeText("Tall")
        app.textViews["editor.text"].firstMatch.tap()
        app.textViews["editor.text"].firstMatch.typeText("tall")
        app.buttons["editor.size.1x3"].tap()
        app.buttons["editor.save"].tap()
        XCTAssertTrue(app.buttons["phrase.Tall"].waitForExistence(timeout: 20))
        enterArrangeMode(app, from: button)
        let top = grid.frame.minY

        let from = button.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.5))
        let bottom = grid.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 1)).withOffset(CGVector(dx: 0, dy: -8))
        from.press(forDuration: 0.1, thenDragTo: bottom, withVelocity: .slow, thenHoldForDuration: 2.0)

        XCTAssertTrue(app.buttons["arrange.done"].exists)
        XCTAssertLessThan(first.frame.maxY, top, "the first row scrolled off the top")
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
