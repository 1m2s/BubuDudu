#include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
bool failObservationQueue = false;
QueueHandle_t createObservationQueue(size_t capacity, size_t size)
{ return failObservationQueue ? nullptr : xQueueCreate(capacity, size); }
#define xQueueCreate createObservationQueue
#include "../../src/ESPNowRadio.cpp"
#undef xQueueCreate
uint32_t hostNow = 0;
HostSerial Serial;
HostWiFi WiFi;
esp_now_send_cb_t sentCallback = nullptr;
esp_now_recv_cb_t receivedCallback = nullptr;
wifi_promiscuous_cb_t promiscuousCallback = nullptr;
unsigned observerFailure = 0, observerSetupCalls = 0;
bool promiscuousEnabled = false;
bool rejectSend = false, immediateCallback = false, finishPreviousDuringReject = false;
unsigned driverCalls = 0, callbacks = 0, received = 0;
const uint8_t mac[6]{};
const uint8_t packet[8]{};
void complete(esp_now_send_status_t status = ESP_NOW_SEND_SUCCESS)
{
    assert(ESPNowRadio::txInFlight() > 0);
    sentCallback(mac, status); ++callbacks;
}
esp_err_t esp_now_send(const uint8_t* peer, const uint8_t*, size_t length)
{
    assert(peer != nullptr && length == 8 && ESPNowRadio::txInFlight() > 0);
    ++driverCalls;
    if (finishPreviousDuringReject) complete();
    if (rejectSend) return -1;
    if (immediateCallback) complete();
    return ESP_OK;
}
void receive(const uint8_t*, size_t length)
{
    assert(ESPNowRadio::receiveCallbackActive() && length == 8);
    ++received;
}

std::vector<uint8_t> observedFrame(uint16_t id = 42, int8_t rssi = -61)
{
    // Independent fixture for documented ESP-NOW v1: header/body/FCS = 51 bytes.
    std::vector<uint8_t> buffer(sizeof(wifi_promiscuous_pkt_t) + 51, 0);
    auto* packet = reinterpret_cast<wifi_promiscuous_pkt_t*>(buffer.data());
    packet->rx_ctrl = {rssi, 51, 0};
    uint8_t* f = packet->payload;
    f[0] = 0xD0;
    const uint8_t bubu[] = {0xE8,0xF6,0x0A,0x12,0x4C,0xA4};
    const uint8_t dudu[] = {0xE8,0xF6,0x0A,0x12,0x5B,0x84};
#ifdef DEVICE_BUBU
    memcpy(f + 4, bubu, 6); memcpy(f + 10, dudu, 6);
    const auto sender = Protocol::DeviceId::Dudu;
#else
    memcpy(f + 4, dudu, 6); memcpy(f + 10, bubu, 6);
    const auto sender = Protocol::DeviceId::Bubu;
#endif
    memset(f + 16, 0xFF, 6);
    f[24] = 127; f[25] = 0x18; f[26] = 0xFE; f[27] = 0x34;
    f[32] = 221; f[33] = 13; f[34] = 0x18; f[35] = 0xFE; f[36] = 0x34;
    f[37] = 4; f[38] = 1;
    const Protocol::Message message{1, Protocol::MessageType::Ack, id, sender, Protocol::EventType::None, 17};
    memcpy(f + 39, &message, sizeof(message));
    return buffer;
}

void testObserver()
{
    using ESPNowRadio::RssiObservation;
    RssiObservation out{};
    assert(promiscuousEnabled && promiscuousCallback && !ESPNowRadio::takeRssiObservation(out));
    const auto sentBefore = driverCalls, receivedBefore = received;
    const auto serialBefore = Serial.log;
    for (int8_t rssi : {int8_t(-127), int8_t(-61), int8_t(0), int8_t(5)})
    {
        auto buffer = observedFrame(42, rssi);
        Protocol::Message expected{};
        auto* f = reinterpret_cast<wifi_promiscuous_pkt_t*>(buffer.data())->payload;
        memcpy(&expected, f + 39, sizeof(expected));
        uint8_t source[6]; memcpy(source, f + 10, 6);
        hostNow = UINT32_MAX - 5;
        promiscuousCallback(buffer.data(), WIFI_PKT_MGMT);
        memset(buffer.data(), 0, buffer.size()); // SDK storage is overwritten before dequeue.
        hostNow = 20;
        assert(ESPNowRadio::takeRssiObservation(out));
        assert(out.rssi == rssi && out.receivedAt == UINT32_MAX - 5);
        assert(memcmp(&out.message, &expected, sizeof(expected)) == 0);
        assert(memcmp(out.sourceMac, source, 6) == 0);
        assert(!ESPNowRadio::takeRssiObservation(out));
    }
    for (unsigned length = 0; length < 51; ++length)
    {
        // Exact short allocation lets ASan catch reading beyond a truncated frame.
        auto buffer = observedFrame();
        buffer.resize(sizeof(wifi_promiscuous_pkt_t) + length);
        std::vector<uint8_t>(buffer).swap(buffer);
        reinterpret_cast<wifi_promiscuous_pkt_t*>(buffer.data())->rx_ctrl.sig_len = length;
        promiscuousCallback(buffer.data(), WIFI_PKT_MGMT);
        assert(!ESPNowRadio::takeRssiObservation(out));
    }
    for (unsigned offset : {0U,4U,10U,16U,24U,25U,26U,27U,32U,33U,34U,35U,36U,37U,38U,39U,43U})
    {
        auto buffer = observedFrame();
        reinterpret_cast<wifi_promiscuous_pkt_t*>(buffer.data())->payload[offset] ^= 0x01;
        promiscuousCallback(buffer.data(), WIFI_PKT_MGMT);
        assert(!ESPNowRadio::takeRssiObservation(out));
    }
    for (uint8_t flag : {uint8_t(1),uint8_t(2),uint8_t(4),uint8_t(0x40),uint8_t(0x80)})
    {
        auto buffer = observedFrame();
        reinterpret_cast<wifi_promiscuous_pkt_t*>(buffer.data())->payload[1] = flag;
        promiscuousCallback(buffer.data(), WIFI_PKT_MGMT);
        assert(!ESPNowRadio::takeRssiObservation(out));
    }
    for (unsigned fault = 0; fault < 3; ++fault)
    {
        auto buffer = observedFrame();
        auto* packet = reinterpret_cast<wifi_promiscuous_pkt_t*>(buffer.data());
        if (fault == 0) packet->rx_ctrl.rx_state = 1;
        if (fault == 1) packet->rx_ctrl.sig_len = 52;
        if (fault == 2) packet->payload[22] = 1;
        promiscuousCallback(buffer.data(), WIFI_PKT_MGMT);
        assert(!ESPNowRadio::takeRssiObservation(out));
    }
    for (auto type : {WIFI_PKT_CTRL, WIFI_PKT_DATA, WIFI_PKT_MISC})
    {
        uint8_t tiny = 0; // Wrong packet type must not even read a metadata header.
        promiscuousCallback(&tiny, type);
        assert(!ESPNowRadio::takeRssiObservation(out));
    }
    promiscuousCallback(nullptr, WIFI_PKT_MGMT);
    assert(Serial.log == serialBefore && driverCalls == sentBefore && received == receivedBefore);

    for (uint16_t id = 0; id < 20; ++id)
    {
        auto buffer = observedFrame(id);
        reinterpret_cast<wifi_promiscuous_pkt_t*>(buffer.data())->payload[1] = 0x08; // RF retry allowed.
        promiscuousCallback(buffer.data(), WIFI_PKT_MGMT);
    }
    assert(uxQueueMessagesWaiting(rssiQueue) == 4);
    assert(!ESPNowRadio::receiveCallbackActive()); // Diagnostics are not a new sleep guard.
    receivedCallback(mac, packet, 8); // Full observer queue cannot prevent normal delivery.
    assert(received == receivedBefore + 1);
    for (uint16_t id = 0; id < 4; ++id)
    {
        assert(ESPNowRadio::takeRssiObservation(out));
        assert(out.message.messageId == id); // Overflow discarded later observations only.
    }
    assert(!ESPNowRadio::takeRssiObservation(out));
    assert(driverCalls == sentBefore && ESPNowRadio::txInFlight() == 0);
    puts("PASS: peer/destination/framing filters, signed RSSI/time/identity copy, truncated bounds, bounded overflow and independent delivery");
}

void testObserverFailure()
{
    rejectSend = immediateCallback = finishPreviousDuringReject = false;
    for (unsigned failure = 1; failure <= 3; ++failure)
    {
        observerFailure = failure; observerSetupCalls = 0;
        assert(ESPNowRadio::begin(receive));
        assert(!promiscuousEnabled && !rssiEnabled.load() && observerSetupCalls <= 4);
        auto buffer = observedFrame();
        promiscuousCallback(buffer.data(), WIFI_PKT_MGMT);
        ESPNowRadio::RssiObservation out{};
        assert(!ESPNowRadio::takeRssiObservation(out));
        const auto count = received;
        receivedCallback(mac, packet, 8); assert(received == count + 1);
        assert(ESPNowRadio::send(packet, 8)); complete();
        assert(ESPNowRadio::txInFlight() == 0 && Serial.log.find("ESPNOW RSSI | DISABLED") != std::string::npos);
    }
    observerFailure = 0;
    puts("PASS: optional observer setup failures keep normal ESP-NOW RX/TX alive, no retry loop");
}
int main()
{
    failObservationQueue = true;
    assert(ESPNowRadio::begin(receive) && rssiQueue == nullptr && !rssiEnabled.load());
    assert(Serial.log.find("observation queue allocation failed") != std::string::npos);
    failObservationQueue = false;
    assert(ESPNowRadio::begin(receive));
    for (unsigned i = 1; i <= 3; ++i)
    {
        assert(ESPNowRadio::send(packet, 8));
        assert(ESPNowRadio::txInFlight() == i);
    }
    complete(); assert(ESPNowRadio::txInFlight() == 2);
    complete(ESP_NOW_SEND_FAIL); assert(ESPNowRadio::txInFlight() == 1);
    complete(); assert(ESPNowRadio::txInFlight() == 0);
    // A failed enqueue does not generate a callback or leave a reservation.
    rejectSend = true;
    assert(!ESPNowRadio::send(packet, 8) && ESPNowRadio::txInFlight() == 0);
    rejectSend = false; immediateCallback = true;
    assert(ESPNowRadio::send(packet, 8) && ESPNowRadio::txInFlight() == 0);
    immediateCallback = false;
    assert(ESPNowRadio::send(packet, 8));
    rejectSend = finishPreviousDuringReject = true;
    assert(!ESPNowRadio::send(packet, 8) && ESPNowRadio::txInFlight() == 0);
    assert(!ESPNowRadio::receiveCallbackActive());
    receivedCallback(mac, packet, 8);
    assert(received == 1 && !ESPNowRadio::receiveCallbackActive());
    assert(driverCalls == 7 && callbacks == 5);
    puts("PASS: actual ESP-NOW wrapper, accepted/rejected sends, early/late/failed callbacks, RX callback tracking");
    testObserver(); testObserverFailure();
    delete rssiQueue; rssiQueue = nullptr;
}
