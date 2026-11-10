@preconcurrency import CoreBluetooth
import Foundation

/// The Bluetooth route's `DongleLink` (#202): CoreBluetooth, and nothing else. Frames, Results,
/// Stop and Outcomes are in `Dongle` and `DongleSender`, which unit tests cover without a radio.
///
/// The central manager is made on first use, so iOS asks for Bluetooth only when the user adds
/// a dongle or has one. A connection is kept once made: the next Send skips connecting.
@MainActor
final class CoreBluetoothDongles: NSObject, DongleLink {
    static let connectTimeout: Duration = .seconds(5)

    private static let service = CBUUID(string: Dongle.serviceUUID)

    private var manager: CBCentralManager?
    private var stateWaiters: [CheckedContinuation<Void, Never>] = []
    private var scanning: AsyncThrowingStream<[FoundDongle], any Error>.Continuation?
    /// Which scan is current, so an older one's end doesn't stop it.
    private var scanID = UUID()
    private var seen: [UUID: FoundDongle] = [:]
    /// CoreBluetooth drops a peripheral nobody holds, so every one found or retrieved is kept.
    private var peripherals: [UUID: CBPeripheral] = [:]
    private var links: [UUID: PeripheralLink] = [:]
    /// A connection being made, so a status check and a Send at once share it.
    private var connecting: [UUID: Task<PeripheralLink, any Error>] = [:]

    // MARK: DongleLink

    func scan() -> AsyncThrowingStream<[FoundDongle], any Error> {
        let (stream, continuation) = AsyncThrowingStream<[FoundDongle], any Error>.makeStream()
        scanning?.finish()
        scanning = continuation
        let id = UUID()
        scanID = id
        seen = [:]
        continuation.onTermination = { [weak self] _ in
            Task { @MainActor in self?.stopScanning(id) }
        }
        Task {
            do {
                let manager = try await poweredOn()
                guard scanID == id else { return }
                manager.scanForPeripherals(withServices: [Self.service])
            } catch {
                continuation.finish(throwing: error)
            }
        }
        return stream
    }

    func open(_ dongle: UUID) async throws -> any DongleChannel {
        try await ready(dongle).channel()
    }

    func connect(_ dongle: UUID) async throws {
        _ = try await ready(dongle)
    }

    // MARK: inside

    /// The dongle's link, connected and prepared: the one there is, or a new one.
    private func ready(_ dongle: UUID) async throws -> PeripheralLink {
        if let link = links[dongle], link.isReady, link.peripheral.state == .connected { return link }
        if let task = connecting[dongle] { return try await task.value }
        let task = Task { try await self.connectNew(dongle) }
        connecting[dongle] = task
        defer { connecting[dongle] = nil }
        return try await task.value
    }

    private func connectNew(_ dongle: UUID) async throws -> PeripheralLink {
        let manager = try await poweredOn()
        guard let peripheral = peripherals[dongle] ?? manager.retrievePeripherals(withIdentifiers: [dongle]).first
        else { throw Dongle.Failure.notInRange }
        peripherals[dongle] = peripheral

        let link = PeripheralLink(peripheral: peripheral)
        links[dongle] = link
        peripheral.delegate = link
        do {
            try await link.step(timeout: Self.connectTimeout, failure: .notInRange) { manager.connect(peripheral) }
            try await link.prepare()
        } catch {
            if links[dongle] === link { links[dongle] = nil }
            manager.cancelPeripheralConnection(peripheral)
            throw error
        }
        return link
    }

    /// The manager once Bluetooth is on; the first call makes it, which asks for permission.
    private func poweredOn() async throws -> CBCentralManager {
        let manager = manager ?? CBCentralManager(delegate: self, queue: nil)
        self.manager = manager
        while true {
            switch manager.state {
            case .poweredOn: return manager
            case .poweredOff, .unsupported: throw Dongle.Failure.bluetoothOff
            case .unauthorized: throw Dongle.Failure.bluetoothDenied
            default: await withCheckedContinuation { stateWaiters.append($0) }
            }
        }
    }

    private func stopScanning(_ id: UUID) {
        // Only the scan that ended; a newer one keeps going.
        guard scanID == id else { return }
        scanning = nil
        if manager?.state == .poweredOn { manager?.stopScan() }
    }
}

extension CoreBluetoothDongles: @preconcurrency CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        let waiters = stateWaiters
        stateWaiters = []
        waiters.forEach { $0.resume() }
        if central.state != .poweredOn { links.values.forEach { $0.disconnected() } }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        guard let scanning else { return }
        peripherals[peripheral.identifier] = peripheral
        let name = advertisementData[CBAdvertisementDataLocalNameKey] as? String ?? peripheral.name ?? Dongle.name
        seen[peripheral.identifier] = FoundDongle(id: peripheral.identifier, name: name)
        scanning.yield(seen.values.sorted { ($0.name, $0.id.uuidString) < ($1.name, $1.id.uuidString) })
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        links[peripheral.identifier]?.finishStep(.success(()))
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: (any Error)?) {
        links[peripheral.identifier]?.finishStep(.failure(Dongle.Failure.notInRange))
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral,
                        error: (any Error)?) {
        links[peripheral.identifier]?.disconnected()
        links[peripheral.identifier] = nil
    }
}

/// One dongle's GATT: its characteristics, the step being waited for, the writes waiting for
/// their response, and where Result notifies go.
@MainActor
private final class PeripheralLink: NSObject {
    let peripheral: CBPeripheral
    private(set) var isReady = false
    private var send: CBCharacteristic?
    private var result: CBCharacteristic?
    private var token: CBCharacteristic?
    private var tokenText = Data()

    private var pending: CheckedContinuation<Void, any Error>?
    private var stepNumber = 0
    /// Writes with response answer in order.
    private var writes: [CheckedContinuation<Void, any Error>] = []
    private var notify: AsyncThrowingStream<Data, any Error>.Continuation?

    init(peripheral: CBPeripheral) { self.peripheral = peripheral }

    /// Discovers the service, subscribes to Result and reads the Token characteristic, which
    /// encrypts the link: the first time, iOS asks for the passkey, so that read waits longer.
    func prepare() async throws {
        let ids = [Dongle.sendUUID, Dongle.resultUUID, Dongle.tokenUUID].map(CBUUID.init(string:))
        try await step(timeout: .seconds(5), failure: .notInRange) {
            peripheral.discoverServices([CBUUID(string: Dongle.serviceUUID)])
        }
        guard let service = peripheral.services?.first(where: { $0.uuid == CBUUID(string: Dongle.serviceUUID) })
        else { throw Dongle.Failure.notInRange }
        try await step(timeout: .seconds(5), failure: .notInRange) {
            peripheral.discoverCharacteristics(ids, for: service)
        }
        let found = service.characteristics ?? []
        send = found.first { $0.uuid == ids[0] }
        result = found.first { $0.uuid == ids[1] }
        token = found.first { $0.uuid == ids[2] }
        guard let result, let token, send != nil else { throw Dongle.Failure.notInRange }
        try await step(timeout: .seconds(10), failure: .notInRange) { peripheral.setNotifyValue(true, for: result) }
        try await step(timeout: .seconds(90), failure: .notPaired) { peripheral.readValue(for: token) }
        tokenText = token.value ?? Data()
        isReady = true
    }

    /// A new channel; its Results start now, and an older channel's end. The first channel after
    /// a connect takes the Token text, and the link forgets it, so the secret isn't kept here.
    func channel() -> any DongleChannel {
        let (stream, continuation) = AsyncThrowingStream<Data, any Error>.makeStream()
        notify?.finish(throwing: Dongle.Failure.lost)
        notify = continuation
        let text = tokenText
        tokenText = Data()
        return Channel(link: self, results: stream, tokenText: text)
    }

    func write(_ frame: Data) async throws {
        // No link to write on: nothing was written. A write that fails once sent comes back as lost.
        guard isReady, peripheral.state == .connected, let send else { throw Dongle.Failure.notInRange }
        try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, any Error>) in
            writes.append(continuation)
            peripheral.writeValue(frame, for: send, type: .withResponse)
        }
    }

    /// Starts something CoreBluetooth answers through the delegate, and waits for that answer,
    /// or fails with `failure` after `timeout`.
    func step(timeout: Duration, failure: Dongle.Failure, _ start: () -> Void) async throws {
        stepNumber += 1
        let number = stepNumber
        Task { [weak self] in
            try? await Task.sleep(for: timeout)
            guard let self, self.stepNumber == number else { return }
            self.finishStep(.failure(failure))
        }
        try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, any Error>) in
            pending = continuation
            start()
        }
    }

    func finishStep(_ outcome: Result<Void, any Error>) {
        guard let pending else { return }
        self.pending = nil
        stepNumber += 1
        pending.resume(with: outcome)
    }

    func disconnected() {
        isReady = false
        finishStep(.failure(Dongle.Failure.notInRange))
        let waiting = writes
        writes = []
        waiting.forEach { $0.resume(throwing: Dongle.Failure.lost) }
        notify?.finish(throwing: Dongle.Failure.lost)
        notify = nil
    }

    /// An ATT refusal means the dongle didn't take it (not encrypted, a wrong passkey);
    /// anything else, the link may be gone.
    private static func failure(_ error: any Error) -> Dongle.Failure {
        if error is CBATTError { return .notPaired }
        if let error = error as? CBError, error.code == .peerRemovedPairingInformation || error.code == .encryptionTimedOut {
            return .notPaired
        }
        return .lost
    }

    private func stepFailure(_ error: (any Error)?, otherwise: Dongle.Failure) -> Result<Void, any Error> {
        guard let error else { return .success(()) }
        return .failure(Self.failure(error) == .notPaired ? Dongle.Failure.notPaired : otherwise)
    }

    @MainActor
    final class Channel: DongleChannel {
        private let link: PeripheralLink
        let results: AsyncThrowingStream<Data, any Error>
        let tokenText: Data

        init(link: PeripheralLink, results: AsyncThrowingStream<Data, any Error>, tokenText: Data) {
            self.link = link
            self.results = results
            self.tokenText = tokenText
        }

        /// One frame per write: the write without response's length is ATT MTU − 3. Writes with
        /// response could be longer, as prepared writes, but the dongle takes a frame per write.
        var writeLength: Int { link.peripheral.maximumWriteValueLength(for: .withoutResponse) }

        func write(_ frame: Data) async throws { try await link.write(frame) }
    }
}

extension PeripheralLink: @preconcurrency CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: (any Error)?) {
        finishStep(stepFailure(error, otherwise: .notInRange))
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: (any Error)?) {
        finishStep(stepFailure(error, otherwise: .notInRange))
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic,
                    error: (any Error)?) {
        finishStep(stepFailure(error, otherwise: .notInRange))
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: (any Error)?) {
        if characteristic.uuid == result?.uuid {
            if error == nil, let value = characteristic.value { notify?.yield(value) }
        } else if characteristic.uuid == token?.uuid {
            // A canceled or wrong passkey fails the read: the bond didn't finish.
            finishStep(stepFailure(error, otherwise: .notPaired))
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: (any Error)?) {
        guard !writes.isEmpty else { return }
        let write = writes.removeFirst()
        if let error { write.resume(throwing: Self.failure(error)) } else { write.resume() }
    }
}
