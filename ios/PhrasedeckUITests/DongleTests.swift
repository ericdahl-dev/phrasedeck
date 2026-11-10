import XCTest

/// Adds a USB dongle as a Computer and sends to it (#202), with a stub in place of Bluetooth
/// (launch argument -uiTestingStubHelper), so no radio, no passkey and no Keychain.
final class DongleTests: XCTestCase {
    @MainActor
    func launch(_ extra: [String] = []) -> XCUIApplication {
        let app = XCUIApplication()
        app.launchArguments = ["-uiTestingStubHelper", "-uiTestingStubNoComputers"] + extra
        app.launch()
        return app
    }

    /// Add > USB dongle > the dongle the scan found: the sheet closes on the new Computer.
    @MainActor
    func addDongle(_ app: XCUIApplication) {
        let title = app.buttons["helperPicker"]
        XCTAssertTrue(title.waitForExistence(timeout: 20))
        title.tap()

        let dongle = app.buttons["addHelper.dongle"]
        XCTAssertTrue(dongle.waitForExistence(timeout: 10))
        dongle.tap()
        let found = app.buttons["addDongle.found"]
        XCTAssertTrue(found.waitForExistence(timeout: 10))
        XCTAssertTrue(found.label.contains("Phrasedeck dongle"))
        found.tap()

        XCTAssertTrue(app.navigationBars["Add a computer"].waitForNonExistence(timeout: 10))
    }

    @MainActor
    func testAddADongleAndSendToIt() {
        let app = launch()
        addDongle(app)

        let button = app.buttons["phrase.agent-status"]
        XCTAssertTrue(button.waitForExistence(timeout: 20))
        button.tap()

        expectation(for: NSPredicate(format: "label CONTAINS %@", "Typed"), evaluatedWith: button)
        waitForExpectations(timeout: 10)
        let sends = app.staticTexts["stub.sendCount"]
        XCTAssertTrue(sends.waitForExistence(timeout: 10))
        XCTAssertEqual(sends.label, "1")
    }

    /// While a Send types, the banner offers Stop; a stopped Send is Maybe typed.
    @MainActor
    func testStopEndsASendAsMaybeTyped() {
        let app = launch(["-uiTestingStubSlowDongle"])
        addDongle(app)

        let button = app.buttons["phrase.agent-status"]
        XCTAssertTrue(button.waitForExistence(timeout: 20))
        button.tap()
        let stop = app.buttons["banner.stop"]
        XCTAssertTrue(stop.waitForExistence(timeout: 10))
        stop.tap()

        expectation(for: NSPredicate(format: "label CONTAINS %@", "Maybe typed"), evaluatedWith: button)
        waitForExpectations(timeout: 10)
    }
}
