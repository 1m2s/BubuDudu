#include "../../src/ESPNowRadio.cpp"
uint32_t hostNow = 0;
HostSerial Serial;
HostWiFi WiFi;
esp_now_send_cb_t sentCallback = nullptr;
esp_now_recv_cb_t receivedCallback = nullptr;
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
int main()
{
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
}
