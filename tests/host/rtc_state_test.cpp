#include "Arduino.h"
#include "../../src/RtcState.cpp"

using namespace RtcState;

void expectEqual(const History& a, const History& b)
{
    assert(a.nextMessageId == b.nextMessageId);
    assert(a.haveLastPeerEvent == b.haveLastPeerEvent && a.lastPeerEventId == b.lastPeerEventId);
    assert(a.sleep.havePeerRequest == b.sleep.havePeerRequest &&
           a.sleep.newestPeerRequest == b.sleep.newestPeerRequest);
}

int main()
{
    const History sentinel{42, true, 43, {true, 44}};
    History out = sentinel;
    assert(!load(out)); // Zero-initialized storage is not a checkpoint.
    expectEqual(out, sentinel);
    for (uint16_t id : {uint16_t(0), uint16_t(1), uint16_t(0x7FFF), uint16_t(0xFFFF)})
    {
        for (bool event : {false, true}) for (bool request : {false, true})
        {
            const History history{id, event, id, {request, id}};
            save(history); assert(load(out)); expectEqual(out, history);
            assert(load(out)); expectEqual(out, history); // load itself is read-only.
        }
    }
    save(sentinel);
    const History replacement{0, false, 0xFFFF, {true, 0}};
    save(replacement); assert(load(out)); expectEqual(out, replacement);
    invalidate(); out = sentinel; assert(!load(out)); expectEqual(out, sentinel);

    // Fault injection changes retained bytes, without a production test API.
    save(sentinel); retained.magic ^= 1;
    assert(!load(out)); expectEqual(out, sentinel);
    save(sentinel); retained.version = 2;
    Checkpoint incompatible{MAGIC, 0, 42, 43, 44, 2, HAVE_EVENT | HAVE_REQUEST};
    retained.checksum = checksum(incompatible); // Valid checksum, unsupported version.
    assert(!load(out)); expectEqual(out, sentinel);
    save(sentinel); retained.flags |= 0x80;
    incompatible.version = VERSION; incompatible.flags |= 0x80;
    retained.checksum = checksum(incompatible); // Valid checksum, unsupported flags.
    assert(!load(out));
    save(sentinel); retained.nextMessageId ^= 1;
    assert(!load(out));
    save(sentinel); retained.lastPeerEventId ^= 1;
    assert(!load(out));
    save(sentinel); retained.newestPeerRequest ^= 1;
    assert(!load(out));
    save(sentinel); retained.checksum ^= 1;
    assert(!load(out));
    save(sentinel); retained.magic = 0; // Unpublished/interrupted save.
    assert(!load(out));
    puts("PASS: RTC invalid/save/load/replace/invalidate, magic/version/flags/checksum, all ID edges and validity flags");
}
