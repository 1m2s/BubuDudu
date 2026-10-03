#include "../fixtures/scenario_helpers.h"

void testLedAnimation()
{
    struct Frame { uint32_t offset; uint8_t red; };
    const Frame frames[]{{0, 0}, {4, 5}, {65, 90}, {130, 180}, {134, 175}, {195, 90},
                         {260, 0}, {295, 0}, {330, 0}, {334, 5}, {422, 126},
                         {515, 255}, {519, 250}, {608, 127}, {699, 2}};
    for (uint32_t start : {0U, UINT32_MAX - 300U})
    {
        freshApp(); assert(!led.busy() && hostPixel().shown == 0);
        const auto shows = hostPixel().shows;
        led.requestHeartbeat();
        assert(led.busy() && hostPixel().shows == shows); // Request never touches hardware or waits.
        for (const auto& frame : frames) ledStep(start + frame.offset, frame.red);
        ledStep(start + 700, 0, false);
        ledStep(start + 2000, 0, false); // No blocking tail, cooldown or recurring animation.
    }
    for (uint32_t restartAt : {65U, 195U, 295U, 422U, 608U})
    {
        freshApp(); led.requestHeartbeat(); led.update(0); hostNow = restartAt; led.update(hostNow);
        const auto shows = hostPixel().shows;
        led.requestHeartbeat(); assert(hostPixel().shows == shows);
        ledStep(restartAt, 0); ledStep(restartAt + 65, 90); ledStep(restartAt + 130, 180);
        ledStep(restartAt + 515, 255); ledStep(restartAt + 700, 0, false);
    }
    freshApp(); led.requestHeartbeat(); ledStep(0, 0);
    ledStep(450, 165); ledStep(10000, 0, false); // Late service jumps directly to the current frame.
    freshApp(); hostPixel().ready = false; led.requestHeartbeat(); hostNow = 100;
    const auto shows = hostPixel().shows; led.update(hostNow);
    assert(led.busy() && hostPixel().shows == shows && hostNow == 100);
    hostPixel().ready = true; ledStep(100, 0); ledStep(230, 180);
    hostPixel().ready = false; hostNow = 300; led.update(hostNow);
    assert(hostPixel().shown == uint32_t(180) << 16 && hostNow == 300);
    hostPixel().ready = true; ledStep(360, 0); ledStep(800, 0, false);
    freshApp(); led.requestHeartbeat(); ledStep(0, 0); ledStep(130, 180);
    led.off(); assert(!led.busy() && hostPixel().shown == 0); ledStep(515, 0, false);
    puts("PASS: real LED 180/255 red double pulse, 130/70/185 ms fades/gap, 700 ms finish, rollover, restart, skipped frames, latch deferral and no delay/state side effects");
}

void testLedEvents(ProximityClassification classification)
{
    for (auto radio : {Transport::ESP_NOW, Transport::CC1101})
    {
        freshKnownApp(classification);
        auto event = proximityObservation(70, -50, hostNow).message;
        event.event = Protocol::EventType::UserHeartbeat;
        if (radio == Transport::ESP_NOW)
        {
            queueReceivedData(reinterpret_cast<const uint8_t*>(&event), sizeof(event));
            assert(!led.busy() && hostPixel().shows == 0); // Callback only queues; no LED work.
            loop();
        }
        else receiveVia(event, radio);
        assert(led.busy() && hostPixel().shows == 0); // Exactly the new EVENT requested an animation.
        auto receipts = [&]() { return radio == Transport::ESP_NOW ? wire.size() : ccWire.size(); };
        const auto& receipt = radio == Transport::ESP_NOW ? wire.back() : ccWire.back();
        assert(receipt.type == Type::Ack && receipt.ackForMessageId == 70 && receipts() == 1);
        const auto start = hostNow; ledStep(start, 0);
        hostNow = start + 65; receiveVia(event, radio);
        assert(receipts() == 2 && hostPixel().shown == uint32_t(90) << 16);
        ledStep(start + 130, 180); // Duplicate did not restart the clock.
        receiveVia(event, radio == Transport::ESP_NOW ? Transport::CC1101 : Transport::ESP_NOW);
        ledStep(start + 195, 90); // Cross-radio duplicate uses the same dedup path.
        event.messageId = 71; receiveVia(event, radio);
        const auto restarted = hostNow;
        ledStep(restarted, 0); ledStep(restarted + 65, 90); ledStep(restarted + 515, 255);
        ledStep(restarted + 700, 0, false);
        const auto ackCount = receipts(); receiveVia(event, radio);
        assert(!led.busy() && receipts() == ackCount + 1); // A late retry is still re-ACKed, never replayed.
    }
    for (auto radio : {Transport::ESP_NOW, Transport::CC1101})
    for (auto type : {Type::Ack, Type::ProximityProbe, Type::ProximityProbeReply,
                      Type::SleepRequest, Type::SleepReady, Type::SleepCommit, Type::SleepAck, Type::SleepCancel})
    {
        freshKnownApp(classification); const auto packet = incoming(type, type == Type::ProximityProbe ? 0 : 23, 23);
        receiveVia(packet, radio); assert(!led.busy() && hostPixel().shown == 0);
    }
    for (auto radio : {Transport::ESP_NOW, Transport::CC1101})
    {
        freshKnownApp(classification); selectedTransport = radio; startHeartbeatEvent();
        ledStep(0, 0); ledStep(65, 90);
        receiveVia(incoming(Type::Ack, pendingMessage.messageId), radio);
        assert(!waitingForAck && led.busy());
        ledStep(130, 180); // Matching ACK cannot restart the background pulse.
        for (unsigned invalid = 0; invalid < 7; ++invalid)
        {
            freshKnownApp(classification); auto packet = proximityObservation(70, -50, hostNow).message;
            if (invalid == 0) ++packet.version;
            if (invalid == 1) packet.sender = LOCAL_DEVICE;
            if (invalid == 2) packet.event = Protocol::EventType::None;
            if (invalid == 3) packet.ackForMessageId = 1;
            if (invalid == 4) packet.type = static_cast<Type>(99);
            if (invalid == 6) packet.event = static_cast<Protocol::EventType>(3);
            handleReceivedData(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet) - (invalid == 5), radio);
            assert(!led.busy() && hostPixel().shows == 0);
        }
    }
    puts("PASS: new remote EVENT on either radio requests identical LED pulses; queued callbacks, duplicates, ACK/probe/sleep traffic and malformed packets cannot replay/request them");
}

void testLedLocalHeartbeat(ProximityClassification classification)
{
    for (auto radio : {Transport::ESP_NOW, Transport::CC1101})
    for (bool accepted : {false, true})
    {
        freshKnownApp(classification); notePeerSeen(); selectedTransport = radio;
        radioAccepts = ccAccepts = accepted; nextMessageId = 0xFFFF;
        const Protocol::Message expected{Protocol::VERSION, Type::Event, 0xFFFF,
            LOCAL_DEVICE, Protocol::EventType::Heartbeat, 0};
        startHeartbeatEvent();
        assert(led.busy() && led.requests == 1 && hostPixel().shows == 0 && hostNow == 0);
        assert(waitingForAck && retryCount == 0 && ackWaitStart == 0 && nextMessageId == 0);
        assert(pendingTransport == radio && memcmp(&pendingMessage, &expected, sizeof(expected)) == 0);
        ledStep(0, 0); ledStep(65, 90); ledStep(130, 180);
        hostNow = 300; loop();
        assert(waitingForAck && retryCount == 1 && ackWaitStart == 300 && led.requests == 1);
        ledStep(300, 0); ledStep(334, 5); ledStep(515, 255);
        hostNow = 600; loop();
        assert(waitingForAck && retryCount == 2 && ackWaitStart == 600 && led.requests == 1);
        ledStep(600, 138); ledStep(608, 127); ledStep(700, 0, false);
        hostNow = 900; loop();
        assert(!waitingForAck && retryCount == 0 && peerState() == PeerState::OFFLINE);
        assert(!led.busy() && led.requests == 1 && hostPixel().shown == 0 && nextMessageId == 0);
        assert(pendingTransport == radio && memcmp(&pendingMessage, &expected, sizeof(expected)) == 0);
        const auto& packets = radio == Transport::ESP_NOW ? wire : ccSubmitAttempts;
        assert(packets.size() == 3);
        for (const auto& packet : packets) assert(memcmp(&packet, &expected, sizeof(expected)) == 0);
        assert(selectedTransport == Transport::CC1101); // Existing fallback, without replaying the exhausted EVENT.
        assert(radio == Transport::CC1101 ? wire.empty() : ccWire.empty());
        const uint32_t nextPulse = classification == ProximityClassification::FAR ? 6000 : 1000;
        ledStep(nextPulse, 0, false);
        startHeartbeatEvent(); // A fresh transaction after the FAR guard may start another pulse.
        assert(pendingMessage.messageId == 0 && pendingTransport == Transport::CC1101 && led.busy() && led.requests == 2);
        ledStep(nextPulse, 0); ledStep(nextPulse + 65, 90);
    }
    for (auto radio : {Transport::ESP_NOW, Transport::CC1101})
    {
        freshKnownApp(classification); notePeerSeen(); selectedTransport = radio; startHeartbeatEvent();
        auto sharedEvent = pendingMessage;
        ledStep(0, 0); ledStep(65, 90);
        receiveVia(incoming(Type::Ack, sharedEvent.messageId), radio);
        assert(led.requests == 1);
        ledStep(130, 180); ledStep(195, 90); // ACK cannot restart the sender's pulse.
        startHeartbeatEvent();
        assert(pendingMessage.messageId != sharedEvent.messageId);
        if (classification == ProximityClassification::CLOSE)
        {
            assert(led.requests == 2);
            ledStep(195, 0); ledStep(260, 90); ledStep(325, 180); // CLOSE retains its restart behavior.
        }
        else
        {
            assert(led.requests == 1);
            ledStep(260, 0); ledStep(515, 255); ledStep(700, 0, false); // FAR cannot restart early.
        }

        // Model the opposite endpoint's identity; the same EVENT ID/payload is
        // delivered through its real receive path (both identities run this suite).
        freshKnownApp(classification); sharedEvent.sender = PEER_DEVICE;
        hostNow = 2; receiveVia(sharedEvent, radio);
        assert(led.busy() && led.requests == 1 && hostPixel().shows == 0 && lastPeerEventId == sharedEvent.messageId);
        ledStep(2, 0); ledStep(67, 90);
        receiveVia(sharedEvent, radio); // Re-ACK without restarting the peer's pulse.
        assert(led.requests == 1);
        ledStep(132, 180); ledStep(517, 255); ledStep(702, 0, false);
        const auto& receipts = radio == Transport::ESP_NOW ? wire : ccWire;
        assert(receipts.size() == 2);
        for (const auto& receipt : receipts)
            assert(receipt.type == Type::Ack && receipt.ackForMessageId == sharedEvent.messageId);
    }
    puts("PASS: new local EVENT starts one pulse even on rejected TX; identical 300/600ms retries and 900ms exhaustion/fallback never restart it; fresh CLOSE EVENT restarts, FAR waits 6000ms; peer mirrors same ID/payload and re-ACKs duplicates");
}

void testFarLoopCadence()
{
    using Event = Protocol::EventType;
    for (auto outgoing : {Transport::ESP_NOW, Transport::CC1101})
    for (auto incomingRadio : {Transport::ESP_NOW, Transport::CC1101})
    for (int ackDelay : {100, 700, -1})
    // No peer traffic, simultaneous, peer first, and an offset peer clock.
    for (int peerOffset : {-1, 1000, 500, 2500})
    {
        freshKnownApp(ProximityClassification::FAR); notePeerSeen(); selectedTransport = outgoing;
        suppressPeriodicForTest = false; nextEventTime = FIRST_EVENT_DELAY_MS;
        std::vector<uint32_t> pulses, sends;
        uint16_t ownId = 0, peerId = 70;
        uint32_t sentAt = 0;
        Protocol::Message peerEvent{};
        auto enqueue = [&](const Protocol::Message& packet, Transport radio) {
            if (radio == Transport::CC1101) ccIncoming.push_back(packet);
            else queueReceivedData(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet));
        };
        const uint32_t firstPulse = peerOffset == 500 ? 500 : 1000;
        for (uint32_t at = 0; at <= 26000; at += 10)
        {
            hostNow = at;
            if (peerOffset >= 0 && at >= uint32_t(peerOffset))
            {
                const auto peerElapsed = (at - uint32_t(peerOffset)) % 6000;
                if (peerElapsed == 0)
                {
                    peerEvent = {Protocol::VERSION, Type::Event, peerId++, PEER_DEVICE, Event::Heartbeat, 0};
                    enqueue(peerEvent, incomingRadio);
                }
                if (peerElapsed == 300 || peerElapsed == 600)
                    enqueue(peerEvent, incomingRadio); // Same-ID peer retries.
                if (peerElapsed == 400)
                    enqueue(peerEvent, incomingRadio == Transport::ESP_NOW ? Transport::CC1101 : Transport::ESP_NOW);
            }
            if (waitingForAck && ackDelay >= 0 && at - sentAt == uint32_t(ackDelay))
                enqueue(incoming(Type::Ack, ownId), pendingTransport);
            const auto before = led.requests;
            loop(); // Unmodified production schedule, RX, retries and real LED update.
            if (led.requests != before)
            {
                assert(led.requests == before + 1 && led.userRequests == 0);
                pulses.push_back(at);
                assert(at == firstPulse + (pulses.size() - 1) * 6000);
            }
            const unsigned expected = at < firstPulse ? 0 : 1 + (at - firstPulse) / 6000;
            assert(led.requests == expected); // Requires repetition, not merely a rate cap.
            if (waitingForAck && pendingMessage.messageId != ownId)
            {
                ownId = pendingMessage.messageId; sentAt = at; sends.push_back(at);
                assert(pendingMessage.event == Event::Heartbeat && retryCount == 0);
            }
            if (at >= firstPulse)
            {
                const auto elapsed = (at - firstPulse) % 6000;
                // Requests start on the next 10ms loop; keep the real red waveform.
                if (elapsed == 140) assert(hostPixel().shown == uint32_t(180) << 16);
                if (elapsed == 520) assert(hostPixel().shown == uint32_t(248) << 16);
                if (elapsed >= 710) assert(!led.busy() && hostPixel().shown == 0);
            }
        }
        assert(pulses.size() == 5 && sends.size() >= 4);
        const uint32_t messageInterval = 6000 + (ackDelay < 0 ? 900 : ackDelay);
        assert(sends.front() == 1000);
        for (size_t i = 1; i < sends.size(); ++i)
        {
            // CC1101 drains one queued packet per loop; an ACK behind a peer
            // EVENT can add one 10ms loop to the unchanged completion-based clock.
            const auto interval = sends[i] - sends[i - 1];
            assert(interval >= messageInterval && interval <= messageInterval + (peerOffset < 0 ? 0 : 10));
        }
    }
    puts("PASS: real-loop FAR pulses at 6s intervals over five cycles with 100/700ms ACKs or exhaustion, offset/simultaneous peer traffic and cross-radio duplicates; wire cadence and red waveform retained");
}

void testFarLoopGuardsAndRollover()
{
    using Event = Protocol::EventType;
    const Protocol::Message background{Protocol::VERSION, Type::Event, 70, PEER_DEVICE, Event::Heartbeat, 0};
    // Hold radio scheduling out of the rollover fixture: host unsigned long is
    // 64-bit, unlike millis on the MCU. Run the actual loop/LED with uint32 time.
    for (uint32_t start : {0U, UINT32_MAX - 3000U})
    {
        freshKnownApp(ProximityClassification::FAR); notePeerSeen();
        hostNow = start; receive(background);
        for (uint32_t elapsed = 10; elapsed <= 18000; elapsed += 10)
        {
            hostNow = start + elapsed;
            if (elapsed % 6000 == 0)
                queueReceivedData(reinterpret_cast<const uint8_t*>(&background), sizeof(background));
            loop();
            assert(led.requests == 1 + elapsed / 6000 && led.userRequests == 0);
            if (elapsed % 6000 == 140) assert(hostPixel().shown == uint32_t(180) << 16);
        }
        // A stalled loop services one overdue pulse; no catch-up burst or restart.
        hostNow = start + 37000; loop(); assert(led.requests == 5);
        loop(); assert(led.requests == 5);
        hostNow = start + 42999; loop(); assert(led.requests == 5);
        hostNow = start + 43000; loop(); assert(led.requests == 6);
    }
    for (auto route : {Transport::ESP_NOW, Transport::CC1101})
    {
        freshKnownApp(ProximityClassification::FAR); notePeerSeen(); selectedTransport = route;
        receiveVia(background, route);
        auto user = background; user.messageId = 71; user.event = Event::UserHeartbeat;
        hostNow = 5900; receiveVia(user, route);
        const unsigned requests = led.requests;
        for (uint32_t at = 5910; at < 6610; at += 10)
        {
            hostNow = at;
            if (at == 6000) receiveVia(background, route);
            else if (at == 6300) receiveVia(user, route);
            else loop();
            assert(led.requests == requests && led.userRequests == 1 && led.userHeartbeatActive());
        }
        hostNow = 6610; loop(); // Resume one due FAR pulse only after user ownership ends.
        assert(led.requests == requests + 1 && !led.userHeartbeatActive());
        hostNow = 12609; loop(); assert(led.requests == requests + 1);
        hostNow = 12610; loop(); assert(led.requests == requests + 2);

        // Debounced physical intent wins the outbox at the same FAR deadline.
        freshKnownApp(ProximityClassification::FAR); notePeerSeen(); selectedTransport = route;
        receiveVia(background, route);
        suppressPeriodicForTest = false; nextEventTime = 6000;
        buttonLevel = LOW;
        hostNow = 5970; loop(); hostNow = 6000; loop();
        assert(waitingForAck && pendingMessage.event == Event::UserHeartbeat && ackWaitStart == 6000);
        assert(led.requests == 2 && led.userRequests == 0); // Only the normally due sender background.
    }
    for (unsigned guard = 0; guard < 4; ++guard)
    {
        freshKnownApp(ProximityClassification::FAR); notePeerSeen(); receive(background);
        hostNow = 10; loop(); hostNow = 710; loop();
        assert(!led.busy());
        if (guard == 0) proximityClassification = ProximityClassification::UNKNOWN;
        if (guard == 1) proximityClassification = ProximityClassification::CLOSE;
        if (guard == 2) protocolReady = false;
        if (guard == 3)
        {
            hostNow = 5900; loop(); requestSleepForTest();
            assert(transaction().active && !backgroundHeartbeatAllowed());
        }
        hostNow = 6000; loop();
        assert(led.requests == 1 && !led.busy() && hostPixel().shown == 0);
    }
    puts("PASS: FAR loop cadence survives uint32 rollover, skips catch-up bursts, yields to receiver user pulse and physical-button outbox priority; UNKNOWN/CLOSE/runtime/sleep guards stop FAR repetition");
}

void testFarBackgroundOverlap()
{
    using Event = Protocol::EventType;
    for (auto outgoing : {Transport::ESP_NOW, Transport::CC1101})
    for (auto incomingRadio : {Transport::ESP_NOW, Transport::CC1101})
    for (bool incomingFirst : {false, true})
    for (uint32_t start : {0U, UINT32_MAX - 3000U})
    {
        freshKnownApp(ProximityClassification::FAR); notePeerSeen(); selectedTransport = outgoing;
        uint16_t peerId = 70;
        auto deliver = [&](const Protocol::Message& message, Transport radio) {
            handleReceivedData(reinterpret_cast<const uint8_t*>(&message), sizeof(message), radio);
        };
        for (unsigned cycle = 0; cycle < 3; ++cycle)
        {
            const uint32_t at = start + cycle * 6000U;
            hostNow = at;
            Protocol::Message background{Protocol::VERSION, Type::Event, peerId++, PEER_DEVICE, Event::Heartbeat, 0};
            if (incomingFirst) deliver(background, incomingRadio);
            startHeartbeatEvent();
            if (!incomingFirst) deliver(background, incomingRadio);
            const auto own = pendingMessage;
            assert(led.requests == cycle + 1 && led.userRequests == 0);
            ledStep(at, 0); ledStep(at + 65, 90);
            // A fresh peer event during a fade cannot restart it; ACK is still sent.
            background.messageId = peerId++;
            deliver(background, incomingRadio);
            const auto& receipts = incomingRadio == Transport::ESP_NOW ? wire : ccWire;
            assert(receipts.back().type == Type::Ack && receipts.back().ackForMessageId == background.messageId);
            ledStep(at + 130, 180); ledStep(at + 195, 90); ledStep(at + 260, 0);
            hostNow = at + 300; handleAckTimeout();
            assert(retryCount == 1 && memcmp(&pendingMessage, &own, sizeof(own)) == 0);
            ledStep(at + 330, 0); ledStep(at + 334, 5); ledStep(at + 515, 255);
            hostNow = at + 600; handleAckTimeout();
            assert(retryCount == 2 && memcmp(&pendingMessage, &own, sizeof(own)) == 0);
            deliver(incoming(Type::Ack, own.messageId), outgoing);
            assert(!waitingForAck && led.requests == cycle + 1);
            ledStep(at + 608, 127); ledStep(at + 700, 0, false);
            // New background events after the animation also wait for the same deadline.
            for (uint32_t offset : {701U, 3000U, 5999U})
            {
                hostNow = at + offset; background.messageId = peerId++;
                deliver(background, incomingRadio);
                deliver(background, outgoing); // Same/cross-radio duplicate still receives its ACK.
                assert(led.requests == cycle + 1 && !led.busy() && hostPixel().shown == 0);
            }
            // Even at the deadline a duplicate cannot consume the next pulse.
            hostNow = at + 6000U; deliver(background, incomingRadio);
            assert(led.requests == cycle + 1 && !led.busy());
        }
    }
    // The real loop processes an incoming event and a due local event in one batch.
    for (auto radio : {Transport::ESP_NOW, Transport::CC1101})
    {
        freshKnownApp(ProximityClassification::FAR); notePeerSeen(); selectedTransport = radio;
        suppressPeriodicForTest = false; nextEventTime = 0;
        receiveVia({Protocol::VERSION, Type::Event, 70, PEER_DEVICE, Event::Heartbeat, 0}, radio);
        assert(waitingForAck && pendingMessage.event == Event::Heartbeat && led.requests == 1);
        loop(); assert(led.requests == 1 && led.busy());
    }
    // Existing power guards cancel background output and reject both trigger paths.
    freshKnownApp(ProximityClassification::FAR);
    startHeartbeatEvent(); ledStep(0, 0); ledStep(65, 90);
    receive(incoming(Type::Ack, pendingMessage.messageId));
    loop(); assert(backgroundHeartbeatAllowed());
    requestSleepForTest(); assert(transaction().active && !backgroundHeartbeatAllowed());
    loop(); assert(!led.busy() && hostPixel().shown == 0);
    const auto requests = led.requests;
    receive({Protocol::VERSION, Type::Event, 70, PEER_DEVICE, Event::Heartbeat, 0});
    startHeartbeatEvent();
    assert(led.requests == requests && !led.busy() && transaction().active);
    puts("PASS: FAR TX/RX share 6000ms across both radios and rollover; simultaneous/overlapping/new background events, retries, ACKs and duplicates cannot restart or add pulses; exact 700ms CLOSE waveform and power guards preserved");
}

void testLedProtocolIsolation(ProximityClassification classification)
{
    for (auto radio : {Transport::ESP_NOW, Transport::CC1101})
    for (bool acknowledged : {false, true})
    {
        std::string baseline;
        for (bool animate : {false, true})
        {
            freshKnownApp(classification); selectedTransport = radio; pendingMessage = {}; ackWaitStart = 0;
            auto event = proximityObservation(70, -50, hostNow).message;
            event.event = Protocol::EventType::UserHeartbeat;
            receiveVia(event, radio);
            if (!animate) led.off();
            led.update(hostNow);
            startHeartbeatEvent();
            if (!animate) led.off();
            const auto message = pendingMessage;
            const auto start = ackWaitStart;
            std::string trace;
            for (uint32_t elapsed : {100U, 299U, 300U, 599U, 600U, 700U, 900U})
            {
                hostNow = start + elapsed;
                if (elapsed == 100) receiveVia({Protocol::VERSION, Type::Event, 71, PEER_DEVICE, Protocol::EventType::UserHeartbeat, 0}, radio);
                else if (elapsed == 700 && acknowledged) receiveVia(incoming(Type::Ack, message.messageId), radio);
                else loop();
                if (!animate) led.off();
                if (elapsed == 299) assert(waitingForAck && retryCount == 0 && ackWaitStart == start);
                if (elapsed == 300) assert(waitingForAck && retryCount == 1 && ackWaitStart == start + 300);
                if (elapsed == 600) assert(waitingForAck && retryCount == 2 && ackWaitStart == start + 600);
                if (animate && elapsed <= 700) assert(led.busy());
                assert(memcmp(&pendingMessage, &message, sizeof(message)) == 0 && pendingTransport == radio);
                trace += displayObservedRuntime();
            }
            assert(!waitingForAck && retryCount == 0);
            assert(peerState() == (acknowledged ? PeerState::ONLINE : PeerState::OFFLINE));
            for (const auto& packet : wire) trace.append(reinterpret_cast<const char*>(&packet), sizeof(packet));
            for (const auto& packet : ccWire) trace.append(reinterpret_cast<const char*>(&packet), sizeof(packet));
            if (!animate) baseline = trace;
            else assert(trace == baseline);
        }
    }
    puts("PASS: identical EVENT/ACK traffic, pending bytes/radio, 300 ms/two-retry deadlines, exhaustion/fallback and power state with LED enabled or cancelled");
}

void testLedMovementAndSleep(ProximityClassification classification)
{
    for (auto radio : {Transport::ESP_NOW, Transport::CC1101})
    {
        freshKnownApp(classification); selectedTransport = radio;
        auto event = proximityObservation(70, -50, hostNow).message;
        event.event = Protocol::EventType::UserHeartbeat;
        receiveVia(event, radio);
        const auto start = hostNow; ledStep(start, 0);
        movementStep(start + 65, MotionEvent::Activity);
        assert(movementState == MovementState::MOVING && led.busy() && hostPixel().shown == uint32_t(90) << 16);
        movementStep(start + 130, MotionEvent::Inactivity);
        assert(movementState == MovementState::WAITING && led.busy() && hostPixel().shown == uint32_t(180) << 16);
        const auto settled = settleStartedAt + SETTLE_MS;
        hostNow = settled - 100; receiveVia({Protocol::VERSION, Type::Event, 71, PEER_DEVICE, Protocol::EventType::UserHeartbeat, 0}, radio);
        const auto nextStart = hostNow; ledStep(nextStart, 0);
        movementStep(settled);
        assert(proximityUpdateState == ProximityUpdateState::CHECKING && led.busy() && hostPixel().shown != 0);
        if (radio == Transport::ESP_NOW) assert(displayFrames.back().distance == "CHECKING");
        else assert(probeOutstanding);
        movementStep(nextStart + 130, MotionEvent::Activity);
        assert(proximityUpdateState == ProximityUpdateState::READY && led.busy() && hostPixel().shown == uint32_t(180) << 16);
        movementStep(nextStart + 515);
        assert(led.busy() && hostPixel().shown == uint32_t(255) << 16);
        movementStep(nextStart + 700); assert(!led.busy() && hostPixel().shown == 0);
    }
    {
        freshKnownApp(classification);
        receive({Protocol::VERSION, Type::Event, 70, PEER_DEVICE, Protocol::EventType::UserHeartbeat, 0});
        const auto start = hostNow; ledStep(start, 0); ledStep(start + 65, 90);
        loop(); requestSleepForTest(); const auto id = transaction().sleepId;
        receive(incoming(Type::Ack, pendingMessage.messageId)); receive(incoming(Type::SleepReady, id));
        receive(incoming(Type::Ack, pendingMessage.messageId));
        mockedTxInFlight = 1; receive(incoming(Type::SleepAck, id));
        assert(localState() == LocalState::SLEEPING && led.busy() && physicalSleeps == 0);
        mockedTxInFlight = 0; assert(sleepTransportBlockedReason() == nullptr);
        loop(); assert(physicalSleeps == 1);
        assert(!led.busy() && hostPixel().shown == 0);
        ledStep(start + 515, 0, false);
    }
    // Initialization resets a running animation on either cold boot or deep wake.
    for (bool deep : {false, true})
    {
        freshKnownApp(classification);
        receive({Protocol::VERSION, Type::Event, 70, PEER_DEVICE, Protocol::EventType::UserHeartbeat, 0});
        const auto start = hostNow; ledStep(start, 0); ledStep(start + 65, 90);
        saveRtcHistory(); protocolReady = false; injectedBoot.deep = deep;
        delete receiveQueue; receiveQueue = nullptr;
        setup();
        assert(protocolReady && hostPixel().begins == ledBeginsAtBoot + 1 && !led.busy() && hostPixel().shown == 0);
    }
    puts("PASS: MOVING/WAITING/CHECKING and OLED/probe work do not pause/cancel LED; coordinated sleep forces OFF immediately; cold/deep startup resets animation");
}

int main()
{
    static_assert(sizeof(Protocol::Message) == 8, "wire size changed");
    runCase(testLedAnimation);
    runCase(testFarLoopCadence);
    runCase(testFarLoopGuardsAndRollover);
    runCase(testFarBackgroundOverlap);
    for (auto classification : {ProximityClassification::CLOSE, ProximityClassification::FAR})
        runCase([=] { testLedEvents(classification); });
    for (auto classification : {ProximityClassification::CLOSE, ProximityClassification::FAR})
        runCase([=] { testLedLocalHeartbeat(classification); });
    for (auto classification : {ProximityClassification::CLOSE, ProximityClassification::FAR})
        runCase([=] { testLedProtocolIsolation(classification); });
    for (auto classification : {ProximityClassification::CLOSE, ProximityClassification::FAR})
        runCase([=] { testLedMovementAndSleep(classification); });
    finishSuite("led_runtime");
}
