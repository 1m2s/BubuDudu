#include "../fixtures/scenario_helpers.h"

void testInitialProximityContact()
{
    for (auto radio : {Transport::ESP_NOW, Transport::CC1101})
    for (bool acknowledged : {false, true})
    {
        freshApp(); selectedTransport = radio; loop();
        assert(peerState() == PeerState::UNKNOWN && proximityClassification == ProximityClassification::UNKNOWN);
        assertProximityReset();
        assert(displayFrames.back().distance == "UNKNOWN");
        auto evidence = proximityObservation(90, -50, hostNow).message;
        if (acknowledged)
        {
            startHeartbeatEvent();
            evidence = incoming(Type::Ack, pendingMessage.messageId);
        }
        hostNow = 10;
        handleReceivedData(reinterpret_cast<const uint8_t*>(&evidence), sizeof(evidence), radio);
        assert(peerState() == PeerState::ONLINE && proximityUpdateState == ProximityUpdateState::CHECKING);
        assert(checkStartedAt == 10 && proximitySampleCount == 0 && countWire(Type::ProximityProbe) == 0);
        loop(); // UNKNOWN obtains evidence through loop-owned ESP-NOW probes on either application route.
        assert(countWire(Type::ProximityProbe) == 1);
        assert(displayFrames.back().distance == "UNKNOWN"); // Existing probe-busy OLED guard still applies.
        for (uint16_t id : {91, 92})
        {
            hostNow += 10;
            const auto event = proximityObservation(id, -50, hostNow).message;
            receiveVia(event, radio); receiveVia(event, radio);
            startHeartbeatEvent(); receiveVia(incoming(Type::Ack, pendingMessage.messageId), radio);
            assert(checkStartedAt == 10 && proximitySampleCount == 0);
            assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
        }
        assert(Serial.log.find("MOVEMENT |") == std::string::npos);
    }
    puts("PASS: UNKNOWN peer/UNKNOWN distance first EVENT or matched ACK starts one check on either radio; no receive-handler probes, movement or ONLINE/duplicate restarts");
}

void testInitialProximityCadence()
{
    // Exercise real outgoing probes and queued replies/RSSI; no heartbeat traffic
    // supplies the samples. Both device identities execute this test.
    for (unsigned trigger = 0; trigger < 3; ++trigger) // Movement, peer contact, cold startup.
    for (int8_t rssi : {int8_t(-50), int8_t(-85)})
    {
        freshApp(); suppressPeriodicForTest = false; nextEventTime = FIRST_EVENT_DELAY_MS;
        uint32_t start = 20000; // The old first-EVENT deadline is already stale.
        if (trigger == 2)
        {
            protocolReady = false; delete receiveQueue; receiveQueue = nullptr;
            setup();
            assert(protocolReady && peerState() == PeerState::UNKNOWN);
            assert(proximityClassification == ProximityClassification::UNKNOWN);
            assert(proximityUpdateState == ProximityUpdateState::CHECKING && wire.empty());
            start = checkStartedAt + 2000; // Service after the original 1000ms EVENT deadline.
            assert(nextEventTime < start && led.requests == 0);
        }
        else if (trigger == 1)
        {
            hostNow = start; notePeerSeen();
            startPeerAvailabilityProximityCheck(PeerState::UNKNOWN);
        }
        else settleForCheck(start); // Existing movement trigger also works before peer ONLINE.
        const auto peer = peerState();
        struct Delivery { uint32_t at; Protocol::Message packet; };
        std::vector<Delivery> deliveries;
        size_t sent = 0;
        uint16_t peerId = 100;
        uint16_t peerProbeId = 0;
        std::vector<uint16_t> peerSamples;
        bool completed = false, sawChecking = false;
        uint32_t completedAt = 0;
        for (uint32_t now = start; now < start + CHECK_TIMEOUT_MS; now += 10)
        {
            hostNow = now;
            if (trigger == 2 && peerSamples.size() < 3)
            {
                // The other UNKNOWN endpoint independently probes too. Run our
                // real responder; each endpoint collects RSSI from replies.
                peerProbeId = peerId++;
                const auto probe = incoming(Type::ProximityProbe, 0, peerProbeId);
                queueReceivedData(reinterpret_cast<const uint8_t*>(&probe), sizeof(probe));
            }
            for (auto it = deliveries.begin(); it != deliveries.end();)
            {
                if (it->at != now) { ++it; continue; }
                ESPNowRadio::RssiObservation observation{};
                observation.message = it->packet; observation.rssi = rssi; observation.receivedAt = now;
                rssiObservations.push_back(observation);
                queueReceivedData(reinterpret_cast<const uint8_t*>(&it->packet), sizeof(it->packet));
                it = deliveries.erase(it);
            }
            loop();
            for (; sent < wire.size(); ++sent)
            {
                const auto& packet = wire[sent];
                assert(packet.event == Protocol::EventType::None);
                if (packet.type == Type::ProximityProbe)
                    deliveries.push_back({now + 10, incoming(Type::ProximityProbeReply, packet.messageId, peerId++)});
                else
                {
                    assert(trigger == 2 && packet.type == Type::ProximityProbeReply);
                    assert(packet.ackForMessageId == peerProbeId);
                    for (const auto id : peerSamples) assert(id != packet.messageId);
                    peerSamples.push_back(packet.messageId);
                }
            }
            assert(!waitingForAck && retryCount == 0 && controlCount == 0 && peerState() == peer);
            assert(led.requests == 0 && !led.busy() && hostPixel().shown == 0 && !haveLastPeerEvent);
            if (!displayFrames.empty() && displayFrames.back().distance == "CHECKING") sawChecking = true;
            if (proximityClassification != ProximityClassification::UNKNOWN)
            {
                completedAt = now;
                assert(uint32_t(now - start) < CHECK_TIMEOUT_MS && sawChecking);
                assertProximityReset();
                assert(occurrences(Serial.log, "PROXIMITY CHECK | SAMPLE") == 3);
                assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
                assert(countWire(Type::ProximityProbe) == 3 && countWire(Type::Event) == 0 && countWire(Type::Ack) == 0);
                completed = true;
                break;
            }
        }
        assert(completed);
        if (trigger == 2)
        {
            assert(peerSamples.size() == 3 && countWire(Type::ProximityProbeReply) == 3);
            assert(peerState() == PeerState::UNKNOWN && Serial.log.find("MOVEMENT |") == std::string::npos);
        }
        const bool far = rssi <= ENTER_FAR_DBM;
        const uint32_t interval = far ? 6000 : 2500;
        assert(proximityClassification == (far ? ProximityClassification::FAR : ProximityClassification::CLOSE));
        assert(nextEventTime == completedAt + interval);
        hostNow = nextEventTime - 1; loop();
        assert(!waitingForAck && led.requests == 0);
        assert(selectedTransport == (far ? Transport::CC1101 : Transport::ESP_NOW));
        assert(displayFrames.back().distance == (far ? "FAR" : "CLOSE"));
        assert(displayFrames.back().radio == (far ? "CC1101" : "ESP-NOW"));
        hostNow = nextEventTime; loop();
        assert(waitingForAck && pendingMessage.type == Type::Event && led.requests == 1);
        assert(ackWaitStart == completedAt + interval && pendingTransport == selectedTransport);
        assert(countWire(Type::ProximityProbe) == 3); // Completion does not start background probing.
    }
    puts("PASS: UNKNOWN obtains three fresh correlated probe/reply RSSI samples within 12s without heartbeat EVENTs, LED, ACK transactions or peer mutation; first CLOSE/FAR EVENT waits 2500/6000ms after classification");
    puts("PASS: cold startup with peer UNKNOWN starts one check; both conceptually simultaneous UNKNOWN endpoints exchange three distinct probe replies without movement or heartbeat traffic");
}

void testUnknownHeartbeatSilence()
{
    freshApp(); suppressPeriodicForTest = false; nextEventTime = FIRST_EVENT_DELAY_MS;
    for (uint32_t now : {0U, 999U, 1000U, 4000U, 12000U, 24000U})
    {
        hostNow = now; loop();
        assert(proximityClassification == ProximityClassification::UNKNOWN && automaticHeartbeatIntervalMs() == 0);
        assert(!waitingForAck && wire.empty() && ccWire.empty() && led.requests == 0 && !led.busy());
        assert(hostPixel().shown == 0 && nextMessageId == 1);
        assertProximityReset(); // Silence does not create a recurring check.
    }
    for (auto radio : {Transport::ESP_NOW, Transport::CC1101})
    {
        freshApp(); selectedTransport = radio;
        const auto event = proximityObservation(90, -50, 0).message;
        receiveVia(event, radio);
        assert(haveLastPeerEvent && lastPeerEventId == 90 && peerState() == PeerState::ONLINE);
        receiveVia(event, radio);
        const auto& receipts = radio == Transport::ESP_NOW ? wire : ccWire;
        unsigned acks = 0;
        for (const auto& packet : receipts)
            if (packet.type == Type::Ack) { ++acks; assert(packet.ackForMessageId == 90); }
        assert(acks == 2 && led.requests == 0 && !led.busy() && hostPixel().shown == 0);
        assert(Serial.log.find("PARTNER LED") == std::string::npos);
        assert(proximityClassification == ProximityClassification::UNKNOWN && !waitingForAck);
        assert(occurrences(Serial.log, "RX NEW EVENT") == 1 && occurrences(Serial.log, "RX DUPLICATE") == 1);
        startHeartbeatEvent(); // Even an explicitly created transaction cannot light an UNKNOWN device.
        assert(led.requests == 0 && !led.busy());
        for (unsigned attempt = 0; attempt < 3; ++attempt)
        {
            hostNow = ackWaitStart + 300; loop();
            assert(led.requests == 0 && !led.busy());
        }
        loop(); // Apply fallback after the expired transaction cancels its UNKNOWN check.
        assert(!waitingForAck && selectedTransport == Transport::CC1101);
        const auto sent = wire.size(), ccSent = ccWire.size();
        suppressPeriodicForTest = false;
        hostNow += 20000; loop();
        assert(!waitingForAck && wire.size() == sent && ccWire.size() == ccSent && led.requests == 0);
    }
    puts("PASS: UNKNOWN has no automatic EVENT/local LED; ordinary remote background EVENT never masquerades as user intent and duplicate re-ACKs without replay on either radio");
}

void testKnownHeartbeatRechecks()
{
    for (auto prior : {ProximityClassification::CLOSE, ProximityClassification::FAR})
    for (bool cancel : {false, true})
    for (unsigned samples : {0U, 2U})
    {
        freshApp(); notePeerSeen(); proximityClassification = prior;
        selectedTransport = prior == ProximityClassification::FAR ? Transport::CC1101 : Transport::ESP_NOW;
        const uint32_t interval = prior == ProximityClassification::FAR ? 6000 : 2500;
        suppressPeriodicForTest = false; nextEventTime = 100;
        startProximityCheck(0);
        hostNow = 100; loop();
        assert(proximityUpdateState == ProximityUpdateState::CHECKING && proximityClassification == prior);
        assert(waitingForAck && led.requests == 1);
        hostNow = 150; receiveVia(incoming(Type::Ack, pendingMessage.messageId), pendingTransport);
        assert(nextEventTime == 150 + interval && !waitingForAck && led.requests == 1);
        suppressPeriodicForTest = true; // Isolate a failed measurement from unrelated ACK exhaustion.
        for (unsigned i = 0; i < samples; ++i) classificationSample(800 + i, -60);
        if (cancel) movementStep(hostNow + 10, MotionEvent::Activity);
        else { hostNow = CHECK_TIMEOUT_MS; loop(); }
        assertProximityReset();
        assert(proximityClassification == prior && automaticHeartbeatIntervalMs() == interval);
        assert(nextEventTime == 150 + interval);
        assert(led.requests == (prior == ProximityClassification::FAR && !cancel ? 2U : 1U));
    }
    puts("PASS: CLOSE/FAR keep their heartbeat and cadence while CHECKING; zero/partial timeout or movement cancellation preserves known classification and scheduling");
}

void testInitialProximityTimeout()
{
    for (unsigned samples : {0U, 2U})
    {
        freshApp(); suppressPeriodicForTest = false; nextEventTime = FIRST_EVENT_DELAY_MS;
        const auto event = proximityObservation(90, -50, hostNow).message;
        receive(event);
        const auto began = checkStartedAt;
        for (unsigned i = 0; i < samples; ++i) classificationSample(100 + i, -50);
        hostNow = began + CHECK_TIMEOUT_MS - 1; loop();
        assert(checkStartedAt == began && proximitySampleCount == samples);
        hostNow = began + CHECK_TIMEOUT_MS; loop();
        assertProximityReset();
        assert(proximityClassification == ProximityClassification::UNKNOWN && peerState() == PeerState::ONLINE);
        assert(displayFrames.back().distance == "UNKNOWN" && selectedTransport == Transport::ESP_NOW);
        const auto probes = countWire(Type::ProximityProbe);
        receive(event); // Retry of the original first-contact EVENT after timeout.
        receive(proximityObservation(91, -50, hostNow).message);
        receive(incoming(Type::Ack, 999)); // Unrelated receipt cannot restart the expired UNKNOWN check.
        for (unsigned i = 0; i < 100; ++i) { hostNow += 1000; loop(); }
        assertProximityReset();
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | TIMEOUT") == 1);
        assert(countWire(Type::Event) == 0 && led.requests == 0 && !led.busy());
        assert(countWire(Type::ProximityProbe) == probes);
        settleForCheck(hostNow + 2000); // A legitimate future movement may try again.
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 2);
    }
    puts("PASS: initial check with zero/two samples times out at 12s, OLED returns UNKNOWN, ONLINE traffic/duplicates/loops never retry; future movement can start another check");
}

void testInitialProximityStartup()
{
    for (bool deep : {false, true})
    for (bool recovered : {false, true})
    for (bool runtimeOk : {false, true})
    {
        freshApp(); protocolReady = false; suppressPeriodicForTest = false;
        delete receiveQueue; receiveQueue = nullptr;
        injectedBoot.deep = deep;
        RtcState::save({123, false, 0, {false, 0}});
        injectWakePacket = recovered;
        injectedPacket = proximityObservation(90, -50, hostNow).message;
        radioStarts = runtimeOk;
        atRadioStart = [] {
            assert(!protocolReady && motionInitializations == 1 && displayInitializations == 1);
            assert(hostPixel().begins == 1 && proximityClassification == ProximityClassification::UNKNOWN);
            assertProximityReset(); // Bootstrap waits for successful ESP-NOW/runtime initialization.
            assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 0);
            assert((peerState() == PeerState::ONLINE) == (injectedBoot.deep && injectWakePacket));
        };
        setup();
        assert(protocolReady == runtimeOk);
        assert((proximityUpdateState == ProximityUpdateState::CHECKING) == runtimeOk);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == (runtimeOk ? 1U : 0U));
        assert(wire.empty() && led.requests == 0);
        if (!runtimeOk)
        {
            for (unsigned i = 0; i < 100; ++i) loop();
            assertProximityReset();
            assert(wire.empty() && occurrences(Serial.log, "PROXIMITY CHECK | START") == 0);
            continue;
        }
        const auto peer = peerState();
        assert(peer == (deep && recovered ? PeerState::ONLINE : PeerState::UNKNOWN));
        if (deep && recovered) assert(rtcRestored && wakeReport.processed && wakeReport.ackSent);
        const auto began = checkStartedAt;
        // An ONLINE startup/peer-contact path cannot overlap the bootstrap check.
        startPeerAvailabilityProximityCheck(PeerState::UNKNOWN);
        assert(checkStartedAt == began && occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
        for (uint32_t elapsed = 0; elapsed < CHECK_TIMEOUT_MS; elapsed += PROBE_REPLY_WAIT_MS)
        {
            hostNow = began + elapsed; loop();
            assert(checkStartedAt == began && proximityUpdateState == ProximityUpdateState::CHECKING);
            assert(proximityClassification == ProximityClassification::UNKNOWN && peerState() == peer);
            assert(!waitingForAck && led.requests == 0 && !led.busy());
        }
        hostNow = began + CHECK_TIMEOUT_MS; loop();
        assertProximityReset();
        assert(countWire(Type::ProximityProbe) == 24 && wire.size() == 24 && ccWire.empty());
        assert(proximityClassification == ProximityClassification::UNKNOWN && hostPixel().shown == 0);
        automaticSleepArmed = false; // Isolate non-recurring discovery; automatic sleep has its own tests.
        for (unsigned i = 0; i < 100; ++i) { hostNow += 1000; loop(); }
        assertProximityReset();
        assert(wire.size() == 24 && countWire(Type::Event) == 0 && led.requests == 0);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | TIMEOUT") == 1);
        settleForCheck(hostNow + 2000); // A later real movement remains an eligible one-shot trigger.
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 2);
        hostNow = checkStartedAt + CHECK_TIMEOUT_MS; loop();
        assertProximityReset();
        assert(occurrences(Serial.log, "PROXIMITY CHECK | TIMEOUT") == 2 && led.requests == 0);
    }
    // Model another startup path having already started a valid check, or an
    // initialization callback holding button intent. Use the existing guards.
    for (bool checking : {false, true})
    {
        freshApp(); protocolReady = false;
        delete receiveQueue; receiveQueue = nullptr;
        if (checking) atRadioStart = [] {
            startProximityCheck(1);
            proximitySampleCount = 1; proximitySamples[0] = {900, -50};
        };
        else atRadioStart = [] { buttonHeartbeatPending = true; };
        setup();
        assert(protocolReady && led.requests == 0 && wire.empty());
        if (checking)
        {
            assert(checkStartedAt == 1 && proximitySampleCount == 1 && proximitySamples[0].messageId == 900);
            assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
        }
        else
        {
            assert(localState() == LocalState::ACTIVE && buttonHeartbeatPending);
            assertProximityReset();
            assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 0);
        }
    }
    puts("PASS: cold/deep runtime starts one UNKNOWN check without ONLINE; isolated startup times out at 12s after 24 bounded probes, stays silent without restart, permits later movement; retained wake, ACTIVE/overlap and failed-runtime guards preserved");
}

void testInitialProximityIsolation()
{
    for (auto radio : {Transport::ESP_NOW, Transport::CC1101})
    {
        for (auto type : {Type::ProximityProbe, Type::ProximityProbeReply, Type::SleepRequest,
                          Type::SleepReady, Type::SleepCommit, Type::SleepAck, Type::SleepCancel, Type::Ack})
        {
            freshApp(); selectedTransport = radio;
            receiveVia(incoming(type, type == Type::ProximityProbe ? 0 : 23, 23), radio);
            assertProximityReset();
            assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 0);
        }
        for (unsigned invalid = 0; invalid < 6; ++invalid)
        {
            freshApp(); auto packet = proximityObservation(90, -50, hostNow).message;
            if (invalid == 0) ++packet.version;
            if (invalid == 1) packet.sender = LOCAL_DEVICE;
            if (invalid == 2) packet.event = Protocol::EventType::None;
            if (invalid == 3) packet.ackForMessageId = 1;
            if (invalid == 4) packet.type = static_cast<Type>(99);
            if (invalid == 6) packet.event = static_cast<Protocol::EventType>(3);
            handleReceivedData(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet) - (invalid == 5), radio);
            assert(peerState() == PeerState::UNKNOWN); assertProximityReset();
        }
        freshApp(); selectedTransport = radio; startHeartbeatEvent();
        const auto id = pendingMessage.messageId;
        receiveVia(incoming(Type::Ack, id), radio == Transport::ESP_NOW ? Transport::CC1101 : Transport::ESP_NOW);
        receiveVia(incoming(Type::Ack, id + 1), radio);
        auto malformed = incoming(Type::Ack, id); malformed.event = Protocol::EventType::Heartbeat;
        receiveVia(malformed, radio);
        assert(waitingForAck && peerState() == PeerState::UNKNOWN); assertProximityReset();
        for (auto prior : {ProximityClassification::CLOSE, ProximityClassification::FAR})
        {
            freshApp(); proximityClassification = prior;
            receiveVia(proximityObservation(90, -50, hostNow).message, radio);
            assert(peerState() == PeerState::ONLINE && proximityClassification == prior); assertProximityReset();
        }
        freshApp(); loop();
        receiveVia(proximityObservation(90, -50, hostNow).message, radio);
        assert(peerState() == PeerState::ONLINE && localState() == LocalState::ACTIVE);
        assert(proximityUpdateState == ProximityUpdateState::CHECKING);
    }
    puts("PASS: initial checks reject probes, sleep traffic, unrelated/wrong-radio/malformed ACKs and malformed EVENTs; known classification and ACTIVE guards retained");
}

int main()
{
    static_assert(sizeof(Protocol::Message) == 8, "wire size changed");
    runCase(testInitialProximityContact);
    runCase(testInitialProximityCadence);
    runCase(testUnknownHeartbeatSilence);
    runCase(testKnownHeartbeatRechecks);
    runCase(testInitialProximityTimeout);
    runCase(testInitialProximityStartup);
    runCase(testInitialProximityIsolation);
    finishSuite("proximity_startup");
}
