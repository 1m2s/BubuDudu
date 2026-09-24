// Exercise the actual production FSM and loop with deterministic time/radio
// substitutes. No PlatformIO, Wi-Fi, hardware, or additional test framework.
#include "Arduino.h"
#include "../../src/PowerManager.cpp"
#include "../../src/main.cpp"

uint32_t hostNow = 0;
HostSerial Serial;
std::vector<Protocol::Message> wire;
bool radioAccepts = true;
unsigned armAttempts = 0;
CC1101SleepArm::Result armResult = CC1101SleepArm::Result::Ready;
namespace CC1101SleepArm
{
    Result begin() { return Result::Ready; }
    Report prepareForSleep()
    {
        // Real loop must consume only after transport drain, never in callback.
        assert(!waitingForAck && controlCount == 0);
        assert(PowerManager::localState() == PowerManager::LocalState::SLEEPING);
        ++armAttempts;
        Report report;
        report.result = armResult;
        return report;
    }
    const char* toString(Result result)
    {
        return result == Result::Ready ? "READY" : "RADIO_UNAVAILABLE";
    }
}
namespace ESPNowRadio
{
    bool begin(ReceiveHandler) { return true; }
    bool send(const uint8_t* data, size_t length)
    {
        assert(length == 8);
        Protocol::Message message;
        memcpy(&message, data, length);
        wire.push_back(message);
        return radioAccepts;
    }
}

using Type = Protocol::MessageType;
using Device = Protocol::DeviceId;
using namespace PowerManager;
struct Intent { Type type; uint16_t id; };
std::vector<Intent> intents;
bool acceptIntent = true;
bool captureIntent(Type type, uint16_t id)
{
    intents.push_back({type, id});
    return acceptIntent;
}
Protocol::Message control(Type type, uint16_t id, uint16_t packetId = 90, Device from = Device::Dudu)
{
    return {Protocol::VERSION, type, type == Type::SleepRequest ? id : packetId,
            from, Protocol::EventType::None, id};
}
void freshPower(Device device = Device::Bubu)
{
    Serial.log.clear();
    intents.clear();
    acceptIntent = true;
    PowerManager::begin(device, captureIntent);
}
size_t occurrences(const std::string& text, const std::string& needle)
{
    size_t count = 0, pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) { ++count; pos += needle.size(); }
    return count;
}
void testFsm()
{
    // Coordinator: packet receipt is not the sleep agreement.
    freshPower();
    assert(!requestSleep(27, 0));
    forceIdle(0); assert(requestSleep(27, 0));
    assert(transaction().role == SleepRole::COORDINATOR && transaction().phase == SleepPhase::WAIT_READY);
    assert(intents.back().type == Type::SleepRequest && intents.back().id == 27);
    notePeerSeen(); assert(peerState() == PeerState::SLEEP_PENDING);
    handleControl(control(Type::SleepAck, 27), 100); // Correct ID, wrong phase.
    assert(transaction().phase == SleepPhase::WAIT_READY);
    handleControl(control(Type::SleepReady, 27), 500);
    assert(transaction().phase == SleepPhase::WAIT_ACK && intents.back().type == Type::SleepCommit);
    auto phaseDeadline = transaction().phaseDeadline;
    handleControl(control(Type::SleepReady, 27), 600);
    assert(transaction().phaseDeadline == phaseDeadline && transaction().hardDeadline == 5000);
    handleControl(control(Type::SleepAck, 27), 700);
    assert(localState() == LocalState::SLEEPING && peerState() == PeerState::SLEEPING && !transaction().active);
    notePeerSeen(); notePeerUnreachable(); assert(peerState() == PeerState::SLEEPING);
    auto effects = occurrences(Serial.log, "HANDSHAKE_COMPLETE");
    handleControl(control(Type::SleepAck, 27), 710);
    assert(occurrences(Serial.log, "HANDSHAKE_COMPLETE") == effects);

    // Participant and duplicate REQUEST/COMMIT: one transition, repeatable response.
    freshPower(); forceIdle(0);
    auto request = control(Type::SleepRequest, 20);
    auto commit = control(Type::SleepCommit, 20, 21);
    handleControl(request, 100);
    assert(transaction().role == SleepRole::PARTICIPANT && transaction().phase == SleepPhase::WAIT_COMMIT);
    auto hardDeadline = transaction().hardDeadline;
    phaseDeadline = transaction().phaseDeadline;
    handleControl(request, 200);
    assert(intents.size() == 2 && intents.back().type == Type::SleepReady);
    assert(transaction().hardDeadline == hardDeadline && transaction().phaseDeadline == phaseDeadline);
    handleControl(commit, 300); handleControl(commit, 310);
    assert(transaction().active && intents.back().type == Type::SleepAck);
    controlSent(Type::SleepAck, 20, 320);
    assert(localState() == LocalState::SLEEPING && !transaction().active);
    effects = occurrences(Serial.log, "HANDSHAKE_COMPLETE");
    auto replies = intents.size();
    handleControl(commit, 330); // Exact accepted packet, receipt replay only.
    assert(intents.size() == replies + 1 && intents.back().type == Type::SleepAck);
    assert(occurrences(Serial.log, "HANDSHAKE_COMPLETE") == effects);
    commit.messageId = 99; handleControl(commit, 340);
    assert(intents.size() == replies + 1); // Same closed sleepId alone is insufficient.
    injectActivity(400); update(650); forceIdle(650);
    handleControl(request, 651); assert(!transaction().active); // Closed request stays closed.

    // Stale IDs, wrong sender, wrong role, malformed request cannot advance/cancel.
    freshPower(); forceIdle(0); requestSleep(30, 0);
    for (auto type : {Type::SleepReady, Type::SleepCommit, Type::SleepAck, Type::SleepCancel})
    {
        handleControl(control(type, 29), 10);
        assert(transaction().active && transaction().sleepId == 30 && transaction().phase == SleepPhase::WAIT_READY);
    }
    handleControl(control(Type::SleepReady, 30, 90, Device::Bubu), 10);
    handleControl(control(Type::SleepCommit, 30), 10);
    assert(transaction().phase == SleepPhase::WAIT_READY);
    freshPower(); forceIdle(0); auto malformed = control(Type::SleepRequest, 40); malformed.messageId = 41;
    handleControl(malformed, 0); assert(!transaction().active);

    // Activity closes state first; late control cannot put the device to sleep.
    freshPower(); forceIdle(0); handleControl(control(Type::SleepRequest, 40), 0);
    injectActivity(100); assert(localState() == LocalState::ACTIVE && !transaction().active);
    assert(intents.back().type == Type::SleepCancel && cooldownLeftMs(100) == 3000);
    handleControl(control(Type::SleepCommit, 40), 110);
    assert(localState() == LocalState::ACTIVE && !transaction().active);
    forceIdle(200); assert(!requestSleep(99, 200));
    update(3100); handleControl(control(Type::SleepRequest, 40), 3101); assert(!transaction().active);
    handleControl(control(Type::SleepRequest, 41), 3102); assert(transaction().sleepId == 41);
    handleControl(control(Type::SleepCancel, 41), 3200);
    assert(localState() == LocalState::IDLE && !transaction().active && cooldownLeftMs(3200) == 3000);

    // Phase and absolute hard bounds. Duplicate READY cannot extend either bound.
    freshPower(); forceIdle(0); requestSleep(27, 0); update(3000);
    assert(!transaction().active && Serial.log.find("PHASE_TIMEOUT") != std::string::npos);
    freshPower(); forceIdle(0); handleControl(control(Type::SleepRequest, 27), 0); update(3000);
    assert(!transaction().active && localState() == LocalState::IDLE && peerState() == PeerState::UNKNOWN);
    freshPower(); forceIdle(0); requestSleep(27, 0);
    handleControl(control(Type::SleepReady, 27), 2999);
    handleControl(control(Type::SleepReady, 27), 4999);
    assert(transaction().hardDeadline == 5000); update(5000);
    assert(!transaction().active && Serial.log.find("HARD_TIMEOUT") != std::string::npos);
    auto afterTimeout = Serial.log;
    for (uint32_t now = 5001; now < 20000; ++now) update(now);
    assert(!transaction().active && localState() == LocalState::IDLE && Serial.log == afterTimeout);

    // Simultaneous requests, including equal numeric IDs. Losing the collision
    // cannot extend the hard limit or send a CANCEL that kills the winning ID.
    for (uint16_t peerId : {uint16_t(20), uint16_t(44)})
    {
        freshPower(Device::Bubu); forceIdle(0); requestSleep(20, 0);
        handleControl(control(Type::SleepRequest, peerId, 0, Device::Dudu), 100);
        assert(transaction().role == SleepRole::COORDINATOR && transaction().sleepId == 20 && intents.size() == 1);
        freshPower(Device::Dudu); forceIdle(0); requestSleep(peerId, 0);
        handleControl(control(Type::SleepRequest, 20, 0, Device::Bubu), 100);
        assert(transaction().role == SleepRole::PARTICIPANT && transaction().sleepId == 20);
        assert(transaction().hardDeadline == 5000 && intents.size() == 2 && intents.back().type == Type::SleepReady);
        handleControl(control(Type::SleepCommit, 20, 25, Device::Bubu), 200);
        controlSent(Type::SleepAck, 20, 210); assert(localState() == LocalState::SLEEPING);
    }
    // Tie-break never takes over an already committed coordinator transaction.
    freshPower(Device::Dudu); forceIdle(0); requestSleep(44, 0);
    handleControl(control(Type::SleepReady, 44, 90, Device::Bubu), 10);
    handleControl(control(Type::SleepRequest, 20, 0, Device::Bubu), 20);
    assert(transaction().role == SleepRole::COORDINATOR && transaction().sleepId == 44);

    // Different failure knowledge before and after COMMIT.
    freshPower(); forceIdle(0); requestSleep(27, 0); controlFailed(Type::SleepRequest, 27, 900);
    assert(peerState() == PeerState::OFFLINE && localState() == LocalState::IDLE);
    freshPower(); forceIdle(0); requestSleep(27, 0); handleControl(control(Type::SleepReady, 27), 10);
    controlFailed(Type::SleepCommit, 27, 900); assert(peerState() == PeerState::UNKNOWN && !transaction().active);
    freshPower(); forceIdle(0); handleControl(control(Type::SleepRequest, 27), 0);
    handleControl(control(Type::SleepCommit, 27), 10); controlSent(Type::SleepAck, 27, 10);
    controlFailed(Type::SleepAck, 27, 910);
    assert(peerState() == PeerState::UNKNOWN && localState() == LocalState::SLEEPING);
    freshPower(); forceIdle(0); acceptIntent = false; assert(!requestSleep(27, 0));
    assert(!transaction().active && cooldownLeftMs(0) == 3000);

    // uint32 millis rollover, including a zero hard deadline.
    freshPower(); const uint32_t start = UINT32_MAX - 4999;
    forceIdle(start); requestSleep(27, start);
    handleControl(control(Type::SleepReady, 27), start + 2999);
    update(UINT32_MAX); assert(transaction().active); update(0);
    assert(!transaction().active && cooldownLeftMs(0) == 3000);
    update(3000); assert(cooldownLeftMs(3000) == 0);
    // Request IDs use serial arithmetic, independent of the clock rollover.
    freshPower(); forceIdle(0); handleControl(control(Type::SleepRequest, 65530), 0);
    injectActivity(10); update(3010); forceIdle(3010);
    handleControl(control(Type::SleepRequest, 5), 3011); assert(transaction().sleepId == 5);
}

void freshApp()
{
    if (receiveQueue) delete receiveQueue;
    receiveQueue = xQueueCreate(RX_QUEUE_LENGTH, sizeof(Protocol::Message));
    protocolReady = true; waitingForAck = false; retryCount = 0; nextMessageId = 1;
    haveLastPeerEvent = false; lastPeerEventId = 0; testAckAlreadyDropped = false;
    nextEventTime = 100000; controlCount = 0; pauseAutomaticHeartbeats = true; delayControlsForTest = false;
    hostNow = 0; wire.clear(); radioAccepts = true; Serial.log.clear(); Serial.input.clear();
    armAttempts = 0; armResult = CC1101SleepArm::Result::Ready;
    PowerManager::begin(LOCAL_DEVICE, queueSleepControl);
}
void command(char c) { Serial.input.push_back(c); loop(); }
void receive(const Protocol::Message& message)
{
    queueReceivedData(reinterpret_cast<const uint8_t*>(&message), sizeof(message));
    loop();
}
Protocol::Message incoming(Type type, uint16_t id, uint16_t packet = 90)
{
    return control(type, id, packet, PEER_DEVICE);
}
size_t countWire(Type type)
{
    size_t count = 0;
    for (const auto& message : wire) if (message.type == type) ++count;
    return count;
}
void testTransport()
{
    // Receipt ACK never substitutes for SLEEP_ACK; an out-of-order READY can
    // nevertheless retire REQUEST whose receipt ACK was lost.
    freshApp(); command('i'); command('s'); const auto request = pendingMessage;
    assert(request.type == Type::SleepRequest && request.messageId == request.ackForMessageId);
    receive(incoming(Type::Ack, request.messageId));
    assert(transaction().active && transaction().phase == SleepPhase::WAIT_READY);
    receive(incoming(Type::SleepReady, request.messageId));
    assert(pendingMessage.type == Type::SleepCommit && waitingForAck);
    const auto commit = pendingMessage;
    receive(incoming(Type::Ack, commit.messageId)); assert(transaction().active);
    receive(incoming(Type::SleepAck, request.messageId));
    assert(localState() == LocalState::SLEEPING && peerState() == PeerState::SLEEPING);
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 1);
    assert(armAttempts == 1);
    assert(Serial.log.find("sleepId=" + std::to_string(request.messageId) + " | role=COORDINATOR") != std::string::npos);
    receive(incoming(Type::SleepAck, request.messageId));
    for (int i = 0; i < 100; ++i) loop();
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 1);
    assert(armAttempts == 1);

    // Missing peer: exactly three sends of the same REQUEST ID, then no loop.
    freshApp(); command('i'); command('s'); auto began = ackWaitStart; auto id = pendingMessage.messageId;
    hostNow = began + 299; loop(); assert(countWire(Type::SleepRequest) == 1);
    hostNow = began + 300; loop(); assert(retryCount == 1);
    hostNow = began + 600; loop(); assert(retryCount == 2);
    hostNow = began + 900; loop();
    assert(!waitingForAck && !transaction().active && peerState() == PeerState::OFFLINE);
    for (const auto& message : wire) if (message.type == Type::SleepRequest) assert(message.messageId == id);
    for (int i = 0; i < 1000; ++i) loop(); assert(countWire(Type::SleepRequest) == 3);
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 0);
    assert(armAttempts == 0);

    // Duplicates coalesce with in-flight responses, never restart their budget.
    freshApp(); command('i'); const auto peerRequest = incoming(Type::SleepRequest, 20);
    receive(peerRequest); assert(pendingMessage.type == Type::SleepReady);
    began = ackWaitStart; id = pendingMessage.messageId;
    receive(peerRequest);
    assert(ackWaitStart == began && pendingMessage.messageId == id && countWire(Type::SleepReady) == 1);
    hostNow = began + 300; loop(); assert(retryCount == 1 && countWire(Type::SleepReady) == 2);
    auto peerCommit = incoming(Type::SleepCommit, 20, 21);
    receive(peerCommit); // Retires READY, sends SLEEP_ACK, and only then closes.
    assert(localState() == LocalState::SLEEPING && pendingMessage.type == Type::SleepAck);
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 0);
    assert(armAttempts == 0);
    const auto finalAck = pendingMessage;
    auto transitions = occurrences(Serial.log, "HANDSHAKE_COMPLETE");
    began = ackWaitStart; receive(peerCommit);
    assert(ackWaitStart == began && countWire(Type::SleepAck) == 1);
    hostNow = began + 300; loop(); assert(countWire(Type::SleepAck) == 2 && pendingMessage.messageId == finalAck.messageId);
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 0);
    assert(armAttempts == 0);
    receive(incoming(Type::Ack, finalAck.messageId)); assert(!waitingForAck && peerState() == PeerState::SLEEPING);
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 1);
    assert(armAttempts == 1);
    assert(Serial.log.find("sleepId=20 | role=PARTICIPANT") != std::string::npos);
    receive(peerCommit); assert(countWire(Type::SleepAck) == 3);
    assert(occurrences(Serial.log, "HANDSHAKE_COMPLETE") == transitions);
    receive(incoming(Type::Ack, pendingMessage.messageId));
    for (int i = 0; i < 100; ++i) loop();
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 1);
    assert(armAttempts == 1);

    // Real loop collision retires the losing request, even with equal IDs.
    freshApp(); command('i'); command('s'); id = pendingMessage.messageId;
    receive(incoming(Type::SleepRequest, id));
    if (LOCAL_DEVICE == Device::Bubu)
        assert(transaction().role == SleepRole::COORDINATOR && pendingMessage.type == Type::SleepRequest);
    else
        assert(transaction().role == SleepRole::PARTICIPANT && pendingMessage.type == Type::SleepReady);
    assert(countWire(Type::SleepCancel) == 0);

    // Callback still copies only: state changes occur when loop drains the queue.
    freshApp(); command('i'); auto message = incoming(Type::SleepRequest, 30);
    queueReceivedData(reinterpret_cast<const uint8_t*>(&message), sizeof(message));
    assert(!transaction().active && wire.empty()); loop(); assert(transaction().active);
    // New EVENT is genuine activity, duplicates retain existing re-ACK semantics.
    Protocol::Message event{1, Type::Event, 70, PEER_DEVICE, Protocol::EventType::Heartbeat, 0};
    receive(event); assert(localState() == LocalState::ACTIVE && !transaction().active && countWire(Type::SleepCancel) == 1);
    receive(event); assert(countWire(Type::SleepCancel) == 1 && Serial.log.find("RX DUPLICATE") != std::string::npos);
    receive(incoming(Type::SleepCommit, 30)); assert(localState() == LocalState::ACTIVE);

    // Cancel removes a queued unsent COMMIT and an old in-flight REQUEST.
    freshApp(); command('d'); command('i'); command('s');
    assert(controlCount == 1 && !waitingForAck); command('a');
    assert(controlCount == 0 && localState() == LocalState::ACTIVE);
    for (int i = 0; i < 600; ++i) loop(); assert(countWire(Type::SleepRequest) == 0);
    freshApp(); command('i'); command('s'); id = pendingMessage.messageId; command('d');
    receive(incoming(Type::SleepReady, id)); assert(controlCount == 1 && !waitingForAck);
    command('a'); for (int i = 0; i < 600; ++i) loop(); assert(countWire(Type::SleepCommit) == 0);

    // Existing EVENT retry budget/ID/ACK matching remains unchanged.
    freshApp(); startHeartbeatEvent(); id = pendingMessage.messageId; began = ackWaitStart;
    receive(incoming(Type::Ack, uint16_t(id + 1))); assert(waitingForAck);
    hostNow = began + 300; loop(); hostNow = began + 600; loop();
    assert(countWire(Type::Event) == 3 && retryCount == 2);
    for (const auto& packet : wire) if (packet.type == Type::Event) assert(packet.messageId == id);
    receive(incoming(Type::Ack, id)); assert(!waitingForAck);
    // Automatic EVENTs are suppressed in negotiation and simulated SLEEPING.
    freshApp(); pauseAutomaticHeartbeats = false; nextEventTime = 0;
    PowerManager::forceIdle(0); PowerManager::requestSleep(nextMessageId++, 0); loop();
    assert(countWire(Type::Event) == 0);
    id = transaction().sleepId; receive(incoming(Type::SleepReady, id)); receive(incoming(Type::SleepAck, id));
    for (int i = 0; i < 600; ++i) loop(); assert(countWire(Type::Event) == 0);
    // A rejected first send cannot close the participant before a later accepted retry.
    freshApp(); command('i'); receive(incoming(Type::SleepRequest, 20)); radioAccepts = false;
    receive(incoming(Type::SleepCommit, 20, 21)); assert(transaction().active);
    radioAccepts = true; hostNow = ackWaitStart + 300; loop(); assert(localState() == LocalState::SLEEPING);
    // Even a delayed direct retry checks the absolute deadline before sending.
    freshApp(); command('i'); command('s'); id = transaction().sleepId;
    receive(incoming(Type::SleepReady, id)); auto commits = countWire(Type::SleepCommit);
    hostNow = transaction().hardDeadline; transmitPendingMessage(true);
    assert(countWire(Type::SleepCommit) == commits && !transaction().active && !waitingForAck);
    assert(peerState() == PeerState::UNKNOWN);
    receive(incoming(Type::Ack, wire.back().messageId)); // Late CANCEL receipt, not sleep evidence.
    assert(peerState() == PeerState::UNKNOWN);
    // Losing all receipts for the final SLEEP_ACK is bounded and reported UNKNOWN.
    freshApp(); command('i'); receive(incoming(Type::SleepRequest, 20));
    receive(incoming(Type::SleepCommit, 20, 21)); began = ackWaitStart;
    hostNow = began + 300; loop(); hostNow = began + 600; loop(); hostNow = began + 900; loop();
    assert(countWire(Type::SleepAck) == 3 && !waitingForAck && !transaction().active);
    assert(localState() == LocalState::SLEEPING && peerState() == PeerState::UNKNOWN);
}

void testDelayedCollision()
{
    // Replay both sides of the same timed hardware exchange, each against its
    // peer's scheduled packets. Both start as coordinators before any REQUEST
    // is delivered. Use the real loop/outbox and the unchanged d=1000ms delay.
    freshApp(); delayControlsForTest = true;
    const uint16_t ownId = LOCAL_DEVICE == Device::Bubu ? 40 : 33;
    hostNow = LOCAL_DEVICE == Device::Bubu ? 100 : 0;
    nextMessageId = ownId;
    PowerManager::forceIdle(hostNow);
    assert(PowerManager::requestSleep(nextMessageId++, hostNow));
    assert(transaction().role == SleepRole::COORDINATOR);
    const uint32_t hard = transaction().hardDeadline;

    if (LOCAL_DEVICE == Device::Bubu)
    {
        // Dudu's delayed REQUEST arrives while Bubu's REQUEST is still queued.
        hostNow = 1000; receive(incoming(Type::SleepRequest, 33));
        assert(transaction().role == SleepRole::COORDINATOR && transaction().sleepId == 40);
        hostNow = 1100; loop(); assert(countWire(Type::SleepRequest) == 1);
        receive(incoming(Type::Ack, 40));
        hostNow = 2110; receive(incoming(Type::SleepReady, 40, 35));
        assert(transaction().phase == SleepPhase::WAIT_ACK && countWire(Type::SleepCommit) == 0);
        hostNow = 3110; loop(); assert(countWire(Type::SleepCommit) == 1);
        receive(incoming(Type::Ack, pendingMessage.messageId));
        assert(transaction().hardDeadline == hard);
        hostNow = 4120; receive(incoming(Type::SleepAck, 40, 37));
    }
    else
    {
        hostNow = 1000; loop(); assert(countWire(Type::SleepRequest) == 1);
        receive(incoming(Type::Ack, 33));
        hostNow = 1100; receive(incoming(Type::SleepRequest, 40));
        assert(transaction().role == SleepRole::PARTICIPANT && transaction().sleepId == 40);
        assert(transaction().hardDeadline == hard); // Losing ID 33 cannot extend 5000ms.
        const uint32_t oldWaitCommitDeadline = transaction().phaseDeadline; // 4100ms
        hostNow = 2100; loop(); assert(countWire(Type::SleepReady) == 1);
        receive(incoming(Type::Ack, pendingMessage.messageId));
        hostNow = 3110; receive(incoming(Type::SleepCommit, 40, 43));
        assert(transaction().phase == SleepPhase::WAIT_SLEEP_ACK_TX);
        assert(transaction().phaseDeadline == 6110 && transaction().hardDeadline == hard);
        assert(countWire(Type::SleepAck) == 0); // Still in the delayed outbox.
        hostNow = oldWaitCommitDeadline; loop();
        // Regression: old code cancelled here, ten milliseconds before ACK TX.
        assert(transaction().active && countWire(Type::SleepCancel) == 0);
        assert(transaction().hardDeadline == hard);
        hostNow = 4110; loop(); assert(countWire(Type::SleepAck) == 1);
    }
    assert(hostNow < hard && localState() == LocalState::SLEEPING && !transaction().active);
    assert(peerState() == PeerState::SLEEPING && countWire(Type::SleepCancel) == 0);
    assert(occurrences(Serial.log, "HANDSHAKE_COMPLETE") == 1);
    if (LOCAL_DEVICE == Device::Dudu)
    {
        assert(waitingForAck && occurrences(Serial.log, "SLEEP EXECUTION READY |") == 0);
        assert(armAttempts == 0);
        receive(incoming(Type::Ack, pendingMessage.messageId));
    }
    const char* role = LOCAL_DEVICE == Device::Bubu ? "COORDINATOR" : "PARTICIPANT";
    assert(Serial.log.find(std::string("SLEEP EXECUTION READY | sleepId=40 | role=") + role) != std::string::npos);
    for (int i = 0; i < 100; ++i) loop();
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 1);
    assert(armAttempts == 1);
}

void testFinalSendBounds()
{
    // READY is already delivered. Delay final SLEEP_ACK using the actual bench
    // setting; COMMIT must create one new phase budget, not a new transaction.
    const auto prepare = [](uint32_t commitAt)
    {
        freshApp(); command('i'); receive(incoming(Type::SleepRequest, 20));
        receive(incoming(Type::Ack, pendingMessage.messageId));
        delayControlsForTest = true;
        const uint32_t hard = transaction().hardDeadline;
        hostNow = commitAt; receive(incoming(Type::SleepCommit, 20, 21));
        assert(transaction().phase == SleepPhase::WAIT_SLEEP_ACK_TX && transaction().active);
        assert(transaction().phaseDeadline == commitAt + 3000 && transaction().hardDeadline == hard);
        assert(controlCount == 1 && countWire(Type::SleepAck) == 0);
        assert(controlStillNeeded(Type::SleepAck, 20) && !controlStillNeeded(Type::SleepReady, 20));
        assert(std::string(toString(transaction().phase)) == "WAIT_SLEEP_ACK_TX");
    };

    prepare(500);
    const uint32_t phase = transaction().phaseDeadline;
    const uint32_t hard = transaction().hardDeadline;
    const auto queued = controlQueue[0];
    hostNow = 700; receive(incoming(Type::SleepCommit, 20, 21));
    assert(transaction().phaseDeadline == phase && transaction().hardDeadline == hard);
    assert(controlCount == 1 && controlQueue[0].notBefore == queued.notBefore);
    assert(controlQueue[0].message.messageId == queued.message.messageId);
    assert(occurrences(Serial.log, "WAIT_COMMIT -> WAIT_SLEEP_ACK_TX") == 1);

    // Rejected submissions and retransmissions retain both absolute deadlines.
    radioAccepts = false; hostNow = 1500; loop();
    assert(transaction().active && waitingForAck && retryCount == 0);
    const auto firstAttempt = ackWaitStart;
    hostNow = 1600; receive(incoming(Type::SleepCommit, 20, 21));
    assert(ackWaitStart == firstAttempt && transaction().phaseDeadline == phase);
    hostNow = 1800; loop();
    assert(retryCount == 1 && transaction().phaseDeadline == phase && transaction().hardDeadline == hard);
    radioAccepts = true; hostNow = 2100; loop();
    assert(localState() == LocalState::SLEEPING && !transaction().active);
    assert(countWire(Type::SleepAck) == 3 && occurrences(Serial.log, "HANDSHAKE_COMPLETE") == 1);
    for (const auto& packet : wire)
        if (packet.type == Type::SleepAck) assert(packet.messageId == queued.message.messageId);

    // Model a stalled/unserviced transport queue. Expiry must purge the unsent
    // ACK before loop can submit it, even though it is now eligible for TX.
    prepare(500);
    const uint32_t finalPhaseDeadline = transaction().phaseDeadline;
    assert(finalPhaseDeadline < transaction().hardDeadline);
    PowerManager::update(finalPhaseDeadline - 1); assert(transaction().active);
    hostNow = finalPhaseDeadline; loop();
    assert(!transaction().active && localState() == LocalState::IDLE && controlCount == 0);
    assert(countWire(Type::SleepAck) == 0 && countWire(Type::SleepCancel) == 1);
    assert(Serial.log.find("PHASE_TIMEOUT") != std::string::npos && cooldownLeftMs(finalPhaseDeadline) == 3000);
    PowerManager::controlSent(Type::SleepAck, 20, finalPhaseDeadline + 1);
    assert(localState() == LocalState::IDLE); // Late notification cannot complete.
    SleepDecision decision{};
    assert(!takeSleepDecision(decision) && occurrences(Serial.log, "SLEEP EXECUTION READY |") == 0);
    assert(armAttempts == 0);

    // A late but valid COMMIT gets a fresh phase deadline beyond the hard cap.
    // The unchanged hard deadline still cancels first, before any ACK is sent.
    prepare(2500);
    const uint32_t finalHardDeadline = transaction().hardDeadline;
    assert(transaction().phaseDeadline > finalHardDeadline);
    hostNow = 3010; loop(); assert(transaction().active); // Old WAIT_COMMIT limit.
    hostNow = finalHardDeadline; loop();
    assert(!transaction().active && localState() == LocalState::IDLE && controlCount == 0);
    assert(countWire(Type::SleepAck) == 0 && Serial.log.find("HARD_TIMEOUT") != std::string::npos);
    assert(!takeSleepDecision(decision) && occurrences(Serial.log, "SLEEP EXECUTION READY |") == 0);
    assert(armAttempts == 0);

    for (bool remoteCancel : {false, true})
    {
        prepare(500);
        hostNow = 800;
        if (remoteCancel) receive(incoming(Type::SleepCancel, 20));
        else command('a');
        const auto expected = remoteCancel ? LocalState::IDLE : LocalState::ACTIVE;
        assert(!transaction().active && localState() == expected && controlCount == 0);
        receive(incoming(Type::SleepCommit, 20, 21)); // Stale after cancellation.
        hostNow = 5000; loop();
        assert(localState() == expected && countWire(Type::SleepAck) == 0);
        assert(!takeSleepDecision(decision) && occurrences(Serial.log, "SLEEP EXECUTION READY |") == 0);
        assert(armAttempts == 0);
    }
}

void testSleepDecisionInterface()
{
    SleepDecision decision{};
    for (bool participant : {false, true})
    {
        freshPower(); forceIdle(0);
        assert(!takeSleepDecision(decision));
        if (participant)
        {
            handleControl(control(Type::SleepRequest, 27), 0);
            handleControl(control(Type::SleepCommit, 27, 28), 10);
            assert(!takeSleepDecision(decision)); // Queued is not submitted.
            controlSent(Type::SleepAck, 27, 20);
        }
        else
        {
            requestSleep(27, 0);
            handleControl(control(Type::SleepReady, 27), 10);
            assert(!takeSleepDecision(decision));
            handleControl(control(Type::SleepAck, 27), 20);
        }
        assert(takeSleepDecision(decision));
        assert(decision.sleepId == 27);
        assert(decision.role == (participant ? SleepRole::PARTICIPANT : SleepRole::COORDINATOR));
        for (int i = 0; i < 10; ++i)
        {
            handleControl(control(participant ? Type::SleepCommit : Type::SleepAck, 27, 28), 30 + i);
            controlSent(Type::SleepAck, 27, 30 + i);
            assert(!takeSleepDecision(decision));
        }
    }

    // Stale controls and cancellation never create a decision.
    freshPower(); forceIdle(0);
    for (auto type : {Type::SleepReady, Type::SleepCommit, Type::SleepAck, Type::SleepCancel})
    {
        handleControl(control(type, 99), 0);
        assert(!takeSleepDecision(decision));
    }
    requestSleep(27, 0); injectActivity(10);
    handleControl(control(Type::SleepAck, 27), 20);
    assert(!takeSleepDecision(decision));

    // A pending decision cannot survive activity wake or initialization.
    for (bool reinitialize : {false, true})
    {
        freshPower(); forceIdle(0); requestSleep(27, 0);
        handleControl(control(Type::SleepReady, 27), 10);
        handleControl(control(Type::SleepAck, 27), 20);
        if (reinitialize) PowerManager::begin(Device::Bubu, captureIntent);
        else { injectActivity(30); update(280); }
        assert(!takeSleepDecision(decision));
        // The latch is per completion, not a once-per-boot flag.
        forceIdle(300); assert(requestSleep(28, 300));
        handleControl(control(Type::SleepReady, 28), 310);
        handleControl(control(Type::SleepAck, 28), 320);
        assert(takeSleepDecision(decision) && decision.sleepId == 28);
        assert(!takeSleepDecision(decision));
    }
}

void testExecutionDrain()
{
    // Final ACK retries block readiness through both retries; exhaustion may
    // release the semantic decision but does not remove delivery uncertainty.
    freshApp(); command('i'); receive(incoming(Type::SleepRequest, 20));
    receive(incoming(Type::SleepCommit, 20, 21));
    const auto began = ackWaitStart;
    const auto finalId = pendingMessage.messageId;
    for (uint32_t elapsed : {299U, 300U, 600U, 899U})
    {
        hostNow = began + elapsed; loop();
        assert(waitingForAck && occurrences(Serial.log, "SLEEP EXECUTION READY |") == 0);
        assert(armAttempts == 0);
    }
    hostNow = began + 900; loop();
    assert(!waitingForAck && controlCount == 0 && peerState() == PeerState::UNKNOWN);
    assert(countWire(Type::SleepAck) == 3);
    for (const auto& packet : wire)
        if (packet.type == Type::SleepAck) assert(packet.messageId == finalId);
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 1);
    assert(armAttempts == 1);
    for (int i = 0; i < 100; ++i) loop();
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 1);
    assert(armAttempts == 1);

    // Receipt followed by duplicate COMMIT in the same RX batch can leave a
    // required replay queued but not yet eligible for TX. Queue also must drain.
    freshApp(); command('i'); receive(incoming(Type::SleepRequest, 20));
    const auto commit = incoming(Type::SleepCommit, 20, 21);
    receive(commit); delayControlsForTest = true;
    const auto receipt = incoming(Type::Ack, pendingMessage.messageId);
    for (const auto& packet : {receipt, commit})
        queueReceivedData(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet));
    loop();
    assert(!waitingForAck && controlCount == 1);
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 0);
    assert(armAttempts == 0);
    hostNow = controlQueue[0].notBefore; loop();
    assert(waitingForAck && controlCount == 0);
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 0);
    assert(armAttempts == 0);
    receive(incoming(Type::Ack, pendingMessage.messageId));
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 1);
    assert(armAttempts == 1);

    // Activity after semantic completion but before transport drain revokes
    // the unconsumed decision, even if a late receipt/COMMIT follows.
    freshApp(); command('i'); receive(incoming(Type::SleepRequest, 20));
    receive(commit); const auto pendingId = pendingMessage.messageId;
    assert(localState() == LocalState::SLEEPING && waitingForAck);
    command('a');
    receive(incoming(Type::Ack, pendingId)); receive(commit);
    for (int i = 0; i < 100; ++i) loop();
    assert(localState() == LocalState::ACTIVE);
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 0);
    assert(armAttempts == 0);
}

void testArmFailure()
{
    for (auto failure : {CC1101SleepArm::Result::RadioUnavailable,
                         CC1101SleepArm::Result::WrongConfig,
                         CC1101SleepArm::Result::NotInRx,
                         CC1101SleepArm::Result::GdoHigh,
                         CC1101SleepArm::Result::RxPending,
                         CC1101SleepArm::Result::RxOverflow})
    {
        for (bool participant : {false, true})
        {
            freshApp(); armResult = failure; command('i');
            uint16_t id = 20;
            if (participant)
            {
                receive(incoming(Type::SleepRequest, id));
                receive(incoming(Type::SleepCommit, id, 21));
                assert(armAttempts == 0);
                receive(incoming(Type::Ack, pendingMessage.messageId));
            }
            else
            {
                command('s'); id = transaction().sleepId;
                receive(incoming(Type::SleepReady, id));
                receive(incoming(Type::SleepAck, id));
            }
            assert(armAttempts == 1 && !transaction().active);
            assert(Serial.log.find("CC1101 SLEEP ARM | FAILED") != std::string::npos);
            const auto requests = countWire(Type::SleepRequest);
            receive(incoming(participant ? Type::SleepCommit : Type::SleepAck, id, 21));
            receive(incoming(Type::SleepAck, uint16_t(id - 1))); // Stale.
            if (waitingForAck) receive(incoming(Type::Ack, pendingMessage.messageId));
            for (int i = 0; i < 1000; ++i) loop();
            assert(armAttempts == 1 && countWire(Type::SleepRequest) == requests);
            assert(localState() == LocalState::SLEEPING && peerState() == PeerState::SLEEPING);
            assert(!transaction().active && cooldownLeftMs(hostNow) == 0);
            command('p'); // Serial command remains usable after failure.
            command('a'); hostNow += 250; loop();
            assert(localState() == LocalState::ACTIVE);
            // ESP-NOW remains usable after either role's arm failure.
            startHeartbeatEvent(); receive(incoming(Type::Ack, pendingMessage.messageId));
            assert(!waitingForAck && armAttempts == 1);
        }
    }
}

int main()
{
    static_assert(sizeof(Protocol::Message) == 8, "wire size changed");
    testFsm(); testTransport(); testDelayedCollision(); testFinalSendBounds();
    testSleepDecisionInterface(); testExecutionDrain(); testArmFailure();
    puts("PASS: one arm attempt per decision, failure isolation, no rearming, ESP-NOW remains usable");
    delete receiveQueue;
    printf("PASS %s: coordinator/participant, collisions, stale/duplicates, hard/phase deadlines, activity, rollover\n", DEVICE_NAME);
    puts("PASS: actual RX queue/loop, packet ACK vs SLEEP_ACK, bounded same-ID retries, cancellation purge, heartbeat gating");
    puts("PASS: delayed collision, bounded WAIT_SLEEP_ACK_TX, duplicate/retry deadlines, unsent phase/hard expiry, cancellation");
    puts("PASS: one-shot sleep decisions, duplicate/stale suppression, transport drain/exhaustion, queued replay, activity revocation");
}
