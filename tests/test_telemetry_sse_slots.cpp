// SSE-R1: a new browser refresh receives headers/schema while an older socket
// remains alive. SSE-R2: slow sockets cannot block service passes and four
// slots are bounded/reclaimed. SSE-R3: short writes preserve exact wire frames,
// per-client cursors and the established 2 Hz sample interval.
#include "../telemetry_sse_slots.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

struct FakeClient {
	int id = -1;
	bool alive = false;
	void stop() { alive = false; }
	bool connected() const { return alive; }
};

struct FakeEvent {
	uint32_t bootId, sequence, uptimeMs;
	const char *tag, *message;
};

using Slot = TelemetrySseClientSlot<FakeClient>;

struct FakeServerLoop {
	Slot slots[TELEMETRY_SSE_CLIENT_CAPACITY];
	uint32_t order = 0;
	std::vector<std::string> output = std::vector<std::string>(64);
	bool blocked[64] = {};
	size_t maxChunk[64] = {};
	size_t requestBytes[64] = {};
	bool remoteClosed[64] = {};

	size_t accept(int clientId, uint32_t now) {
		FakeClient client{clientId, true};
		return telemetrySseActivate(slots, TELEMETRY_SSE_CLIENT_CAPACITY,
			client, now, ++order);
	}

	void service(uint32_t now) {
		float row[2] = {};
		telemetrySseServicePass<FakeClient, const char *(*)(int), FakeEvent>(
			slots, TELEMETRY_SSE_CLIENT_CAPACITY, now, 2, row,
			[](int i) { return i ? "battery_v" : "attitude.x"; },
			[](uint32_t, FakeEvent &) { return false; },
			[](float *, int, uint32_t &) { return false; },
			[&](FakeClient &client, uint8_t *, size_t capacity) {
				if (!client.alive) return 0;
				if (requestBytes[client.id]) {
					const size_t received = requestBytes[client.id] < capacity
						? requestBytes[client.id] : capacity;
					requestBytes[client.id] -= received;
					return (int)received;
				}
				return remoteClosed[client.id] ? 0 : TELEMETRY_SSE_SEND_WOULD_BLOCK;
			},
				[&](FakeClient &client, const uint8_t *data, size_t length) {
					if (blocked[client.id]) return TELEMETRY_SSE_SEND_WOULD_BLOCK;
					const size_t chunk = maxChunk[client.id] && maxChunk[client.id] < length
						? maxChunk[client.id] : length;
					output[client.id].append(reinterpret_cast<const char *>(data), chunk);
					return (int)chunk;
				});
	}
};

static void refreshWhileOldConnectionAlive() {
	FakeServerLoop loop;
	loop.maxChunk[1] = 7;
	const size_t oldSlot = loop.accept(1, 0);
	loop.service(0); // old client has only a short header fragment pending
	assert(loop.slots[oldSlot].active && loop.slots[oldSlot].pendingOffset == 7);

	const size_t newSlot = loop.accept(2, 1);
	assert(newSlot != oldSlot && loop.slots[oldSlot].active);
	loop.service(1);
	loop.service(2);
	assert(loop.output[2].find("HTTP/1.1 200 OK\r\n") == 0);
	assert(loop.output[2].find("event: schema\ndata: attitude.x,battery_v\n\n") != std::string::npos);
	assert(loop.slots[oldSlot].active && loop.slots[newSlot].active);
}

static void blockedOldConnectionCannotBlockNewConnection() {
	FakeServerLoop loop;
	const size_t oldSlot = loop.accept(3, 100);
	loop.blocked[3] = true;
	loop.service(100);
	assert(loop.slots[oldSlot].pendingOffset == 0);
	const size_t newSlot = loop.accept(4, 101);
	loop.service(101);
	loop.service(102);
	assert(loop.output[4].find("event: schema") != std::string::npos);
	assert(loop.slots[oldSlot].active);
	loop.service(100 + TELEMETRY_SSE_FRAME_DEADLINE_MS);
	assert(!loop.slots[oldSlot].active);
	assert(loop.slots[newSlot].active);
}

static void boundedSlotsReclaimOldest() {
	FakeServerLoop loop;
	for (int id = 0; id < 4; ++id) loop.accept(id, 10 + id);
	assert(loop.slots[0].client.id == 0 && loop.slots[0].active);
	const size_t reused = loop.accept(8, 20);
	assert(reused == 0);
	assert(loop.slots[0].client.id == 8 && loop.slots[0].active);
	for (size_t i = 1; i < TELEMETRY_SSE_CLIENT_CAPACITY; ++i) assert(loop.slots[i].active);
}

static void thirtyRefreshesEachReceiveSchema() {
	FakeServerLoop loop;
	for (int id = 0; id < 30; ++id) {
		loop.accept(id, 1000 + id * 2);
		loop.service(1000 + id * 2);
		loop.service(1001 + id * 2);
		assert(loop.output[id].find("HTTP/1.1 200 OK\r\n") == 0);
		assert(loop.output[id].find("event: schema\ndata: attitude.x,battery_v\n\n") != std::string::npos);
	}
	int active = 0;
	for (const auto &slot : loop.slots) active += slot.active ? 1 : 0;
	assert(active == 4);
}

static void disconnectAndWrappedDeadlineAreReclaimed() {
	FakeServerLoop loop;
	const size_t first = loop.accept(10, 1);
	const size_t disconnected = loop.accept(11, 2);
	loop.remoteClosed[11] = true;
	loop.service(3);
	assert(!loop.slots[disconnected].active);
	const size_t reused = loop.accept(12, 3);
	assert(reused == disconnected);
	assert(loop.slots[first].active && loop.slots[first].client.id == 10);

	Slot slot;
	FakeClient client{13, true};
	const uint32_t start = UINT32_MAX - 100;
	telemetrySseActivate(&slot, 1, client, start, 1);
	assert(telemetrySseFormatHttpHeaders(slot, start));
	auto blockedSend = [](FakeClient &, const uint8_t *, size_t) {
		return TELEMETRY_SSE_SEND_WOULD_BLOCK;
	};
	assert(telemetrySseAdvancePending(slot, start + TELEMETRY_SSE_FRAME_DEADLINE_MS - 1,
		blockedSend) == TelemetrySseSendResult::WOULD_BLOCK);
	assert(telemetrySseAdvancePending(slot, start + TELEMETRY_SSE_FRAME_DEADLINE_MS,
		blockedSend) == TelemetrySseSendResult::TIMED_OUT);
}

static void finBehindUnreadRequestIsEventuallyObserved() {
	FakeServerLoop loop;
	const size_t slot = loop.accept(16, 0);
	loop.requestBytes[16] = TELEMETRY_SSE_INPUT_DRAIN_BYTES * 2 + 17;
	loop.remoteClosed[16] = true;
	loop.service(0);
	assert(loop.slots[slot].active && loop.requestBytes[16] == TELEMETRY_SSE_INPUT_DRAIN_BYTES + 17);
	loop.service(1);
	assert(loop.slots[slot].active && loop.requestBytes[16] == 17);
	loop.service(2);
	assert(loop.slots[slot].active && loop.requestBytes[16] == 0);
	loop.service(3);
	assert(!loop.slots[slot].active);
}

static void partialFramesCommitOnlyWhenComplete() {
	Slot slot;
	FakeClient client{5, true};
	telemetrySseActivate(&slot, 1, client, 200, 1);
	const float row[] = {1.25f, -2.5f};
	assert(telemetrySseFormatSample(slot, 42, row, 2, 200));
	const std::string expected = "id: 42\nevent: sample\ndata: 1.25,-2.5\n\n";
	std::string output;
	while (slot.pendingKind != TelemetrySseFrameKind::NONE) {
		const auto result = telemetrySseAdvancePending(slot, 201,
			[&](FakeClient &, const uint8_t *data, size_t length) {
				const size_t chunk = length < 3 ? length : 3;
				output.append(reinterpret_cast<const char *>(data), chunk);
				return (int)chunk;
			});
		if (slot.pendingKind != TelemetrySseFrameKind::NONE) {
			assert(result == TelemetrySseSendResult::PROGRESS);
			assert(slot.lastSequence == UINT32_MAX);
		}
	}
	assert(output == expected);
	assert(slot.lastSequence == 42);

	assert(TELEMETRY_SSE_SAMPLE_INTERVAL_MS == 500);
	assert(TELEMETRY_SSE_CLIENT_CAPACITY == 4);
	assert(sizeof(slot.pendingFrame) == 1024);
}

static void existingSystemLogWireFormatIsStable() {
	Slot slot;
	FakeClient client{6, true};
	telemetrySseActivate(&slot, 1, client, 300, 1);
	assert(telemetrySseFormatSystemLog(slot, 0x12ab, 9, 1234, "WIFI", "state=READY", 300));
	const std::string frame(slot.pendingFrame, slot.pendingLength);
	assert(frame == "id: 000012ab-9\nevent: system-log\ndata: 1234|WIFI|state=READY\n\n");
}

static void dueSamplesAreNotStarvedByContinuousSystemLogs() {
	Slot slot;
	FakeClient client{14, true};
	telemetrySseActivate(&slot, 1, client, 0, 1);
	float row[1] = {3.5f};
	std::string output;
	auto service = [&](uint32_t now) {
		telemetrySseServicePass<FakeClient, const char *(*)(int), FakeEvent>(
			&slot, 1, now, 1, row, [](int) { return "battery_v"; },
			[](uint32_t cursor, FakeEvent &event) {
				event = FakeEvent{1, cursor + 1, 10, "LOG", "continuous"};
				return true;
			},
			[](float *, int, uint32_t &sequence) { sequence = 77; return true; },
			[](FakeClient &, uint8_t *, size_t) { return TELEMETRY_SSE_SEND_WOULD_BLOCK; },
			[&](FakeClient &, const uint8_t *data, size_t length) {
				output.append(reinterpret_cast<const char *>(data), length);
				return (int)length;
			});
	};
	service(0);   // headers
	service(1);   // schema
	service(100); // continuous system event
	const size_t beforeDue = output.size();
	service(500); // sample must win this pass even though another event exists
	const std::string dueFrame = output.substr(beforeDue);
	assert(dueFrame == "id: 77\nevent: sample\ndata: 3.5\n\n");
	assert(slot.lastSequence == 77 && slot.lastSystemSequence == 1);
	service(501);
	assert(output.find("id: 00000001-2\nevent: system-log", beforeDue) != std::string::npos);
}

static void oversizedReadyFrameClosesInsteadOfRetryingForever() {
	Slot slot;
	FakeClient client{15, true};
	telemetrySseActivate(&slot, 1, client, 0, 1);
	slot.initStage = TelemetrySseInitStage::READY;
	float row[300];
	for (float &value : row) value = 1234567.0f;
	telemetrySseServicePass<FakeClient, const char *(*)(int), FakeEvent>(
		&slot, 1, TELEMETRY_SSE_SAMPLE_INTERVAL_MS, 300, row,
		[](int) { return "large"; },
		[](uint32_t, FakeEvent &event) {
			event = FakeEvent{9, 10, 11, "LOG", "must-not-mask-sample-overflow"};
			return true;
		},
		[](float *, int, uint32_t &sequence) { sequence = 1; return true; },
		[](FakeClient &, uint8_t *, size_t) { return TELEMETRY_SSE_SEND_WOULD_BLOCK; },
		[](FakeClient &, const uint8_t *, size_t) { return 1; });
	assert(!slot.active && !slot.client.alive);
}

int main() {
	refreshWhileOldConnectionAlive();
	blockedOldConnectionCannotBlockNewConnection();
	boundedSlotsReclaimOldest();
	thirtyRefreshesEachReceiveSchema();
	disconnectAndWrappedDeadlineAreReclaimed();
	finBehindUnreadRequestIsEventuallyObserved();
	partialFramesCommitOnlyWhenComplete();
	existingSystemLogWireFormatIsStable();
	dueSamplesAreNotStarvedByContinuousSystemLogs();
	oversizedReadyFrameClosesInsteadOfRetryingForever();
	puts("telemetry SSE multi-client slots: PASS");
}
