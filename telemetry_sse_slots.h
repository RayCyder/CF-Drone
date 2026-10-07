#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// SSE-R1: service several browser generations concurrently so a refresh does
// not wait for the previous EventSource TCP connection to disappear.
// SSE-R2: a fixed slot array is allocated once when the task starts; every
// client owns one bounded frame and advances by one non-blocking send attempt
// per service pass. A frame that cannot drain by its deadline is reclaimed.
// The oldest connection is reclaimed when all slots are occupied.
// SSE-R3: wire builders below retain the existing schema, system-log and sample
// event formats. Samples remain limited to 2 Hz by the shared interval.
static constexpr size_t TELEMETRY_SSE_CLIENT_CAPACITY = 4;
static constexpr size_t TELEMETRY_SSE_FRAME_CAPACITY = 1024;
static constexpr size_t TELEMETRY_SSE_INPUT_DRAIN_BYTES = 128;
static constexpr uint32_t TELEMETRY_SSE_FRAME_DEADLINE_MS = 1500;
static constexpr uint32_t TELEMETRY_SSE_SAMPLE_INTERVAL_MS = 500;

enum class TelemetrySseFrameKind : uint8_t {
	NONE,
	HTTP_HEADERS,
	SCHEMA,
	SYSTEM_LOG,
	SAMPLE,
};

enum class TelemetrySseInitStage : uint8_t {
	HTTP_HEADERS,
	SCHEMA,
	READY,
};

enum class TelemetrySseSendResult : uint8_t {
	IDLE,
	PROGRESS,
	COMPLETE,
	WOULD_BLOCK,
	FAILED,
	TIMED_OUT,
};

static constexpr int TELEMETRY_SSE_SEND_WOULD_BLOCK = -1;
static constexpr int TELEMETRY_SSE_SEND_FAILED = -2;

class TelemetrySseFrameWriter {
public:
	TelemetrySseFrameWriter(char *buffer, size_t capacity)
		: buffer_(buffer), capacity_(capacity), used_(0), valid_(buffer && capacity) {}

	bool append(const char *format, ...) {
		if (!valid_ || used_ >= capacity_) return false;
		va_list arguments;
		va_start(arguments, format);
		const int added = vsnprintf(buffer_ + used_, capacity_ - used_, format, arguments);
		va_end(arguments);
		if (added < 0 || (size_t)added >= capacity_ - used_) {
			valid_ = false;
			return false;
		}
		used_ += (size_t)added;
		return true;
	}

	bool valid() const { return valid_; }
	size_t size() const { return valid_ ? used_ : 0; }

private:
	char *buffer_;
	size_t capacity_;
	size_t used_;
	bool valid_;
};

template<class Client>
struct TelemetrySseClientSlot {
	Client client;
	bool active = false;
	uint32_t connectionOrder = 0;
	TelemetrySseInitStage initStage = TelemetrySseInitStage::HTTP_HEADERS;
	uint32_t lastSequence = UINT32_MAX;
	uint32_t lastSystemSequence = 0;
	uint32_t lastSampleMs = 0;
	TelemetrySseFrameKind pendingKind = TelemetrySseFrameKind::NONE;
	uint32_t pendingCursor = 0;
	uint32_t pendingDeadlineMs = 0;
	size_t pendingLength = 0;
	size_t pendingOffset = 0;
	char pendingFrame[TELEMETRY_SSE_FRAME_CAPACITY] = {};
};

inline bool telemetrySseDeadlineReached(uint32_t now, uint32_t deadline) {
	return (int32_t)(now - deadline) >= 0;
}

template<class Client>
void telemetrySseDeactivate(TelemetrySseClientSlot<Client> &slot) {
	if (slot.active) slot.client.stop();
	slot.active = false;
	slot.pendingKind = TelemetrySseFrameKind::NONE;
	slot.pendingLength = 0;
	slot.pendingOffset = 0;
}

template<class Client>
size_t telemetrySseSelectSlot(TelemetrySseClientSlot<Client> *slots, size_t count) {
	for (size_t i = 0; i < count; ++i) if (!slots[i].active) return i;
	size_t oldest = 0;
	for (size_t i = 1; i < count; ++i) {
		if ((int32_t)(slots[i].connectionOrder - slots[oldest].connectionOrder) < 0) oldest = i;
	}
	return oldest;
}

template<class Client>
size_t telemetrySseActivate(TelemetrySseClientSlot<Client> *slots, size_t count,
	const Client &client, uint32_t now, uint32_t connectionOrder) {
	const size_t index = telemetrySseSelectSlot(slots, count);
	telemetrySseDeactivate(slots[index]);
	auto &slot = slots[index];
	slot.client = client;
	slot.active = true;
	slot.connectionOrder = connectionOrder;
	slot.initStage = TelemetrySseInitStage::HTTP_HEADERS;
	slot.lastSequence = UINT32_MAX;
	slot.lastSystemSequence = 0;
	slot.lastSampleMs = now;
	slot.pendingKind = TelemetrySseFrameKind::NONE;
	slot.pendingCursor = 0;
	slot.pendingLength = 0;
	slot.pendingOffset = 0;
	return index;
}

template<class Client>
bool telemetrySseBeginPending(TelemetrySseClientSlot<Client> &slot,
	TelemetrySseFrameKind kind, size_t length, uint32_t cursor, uint32_t now) {
	if (!slot.active || slot.pendingKind != TelemetrySseFrameKind::NONE ||
		length == 0 || length > sizeof(slot.pendingFrame)) return false;
	slot.pendingKind = kind;
	slot.pendingCursor = cursor;
	slot.pendingLength = length;
	slot.pendingOffset = 0;
	slot.pendingDeadlineMs = now + TELEMETRY_SSE_FRAME_DEADLINE_MS;
	return true;
}

template<class Client, class Send>
TelemetrySseSendResult telemetrySseAdvancePending(TelemetrySseClientSlot<Client> &slot,
	uint32_t now, Send send) {
	if (!slot.active || slot.pendingKind == TelemetrySseFrameKind::NONE)
		return TelemetrySseSendResult::IDLE;
	if (telemetrySseDeadlineReached(now, slot.pendingDeadlineMs))
		return TelemetrySseSendResult::TIMED_OUT;
	const size_t remaining = slot.pendingLength - slot.pendingOffset;
	const int sent = send(slot.client,
		reinterpret_cast<const uint8_t *>(slot.pendingFrame) + slot.pendingOffset, remaining);
	if (sent == TELEMETRY_SSE_SEND_WOULD_BLOCK) return TelemetrySseSendResult::WOULD_BLOCK;
	if (sent <= 0 || (size_t)sent > remaining) return TelemetrySseSendResult::FAILED;
	slot.pendingOffset += (size_t)sent;
	if (slot.pendingOffset != slot.pendingLength) return TelemetrySseSendResult::PROGRESS;

	if (slot.pendingKind == TelemetrySseFrameKind::HTTP_HEADERS)
		slot.initStage = TelemetrySseInitStage::SCHEMA;
	else if (slot.pendingKind == TelemetrySseFrameKind::SCHEMA)
		slot.initStage = TelemetrySseInitStage::READY;
	else if (slot.pendingKind == TelemetrySseFrameKind::SYSTEM_LOG)
		slot.lastSystemSequence = slot.pendingCursor;
	else if (slot.pendingKind == TelemetrySseFrameKind::SAMPLE)
		slot.lastSequence = slot.pendingCursor;
	slot.pendingKind = TelemetrySseFrameKind::NONE;
	slot.pendingLength = 0;
	slot.pendingOffset = 0;
	return TelemetrySseSendResult::COMPLETE;
}

template<class Client>
bool telemetrySseFormatHttpHeaders(TelemetrySseClientSlot<Client> &slot, uint32_t now) {
	TelemetrySseFrameWriter frame(slot.pendingFrame, sizeof(slot.pendingFrame));
	frame.append("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
		"Cache-Control: no-cache\r\nConnection: keep-alive\r\n"
		"Access-Control-Allow-Origin: *\r\n\r\n");
	return frame.valid() && telemetrySseBeginPending(slot,
		TelemetrySseFrameKind::HTTP_HEADERS, frame.size(), 0, now);
}

template<class Client, class ColumnName>
bool telemetrySseFormatSchema(TelemetrySseClientSlot<Client> &slot, int columns,
	ColumnName columnName, uint32_t now) {
	TelemetrySseFrameWriter frame(slot.pendingFrame, sizeof(slot.pendingFrame));
	frame.append("event: schema\ndata: ");
	for (int i = 0; i < columns && frame.valid(); ++i)
		frame.append("%s%s", i ? "," : "", columnName(i));
	frame.append("\n\n");
	return frame.valid() && telemetrySseBeginPending(slot,
		TelemetrySseFrameKind::SCHEMA, frame.size(), 0, now);
}

template<class Client>
bool telemetrySseFormatSystemLog(TelemetrySseClientSlot<Client> &slot,
	uint32_t bootId, uint32_t sequence, uint32_t uptimeMs,
	const char *tag, const char *message, uint32_t now) {
	TelemetrySseFrameWriter frame(slot.pendingFrame, sizeof(slot.pendingFrame));
	frame.append("id: %08lx-%lu\nevent: system-log\ndata: %lu|%s|%s\n\n",
		(unsigned long)bootId, (unsigned long)sequence, (unsigned long)uptimeMs, tag, message);
	return frame.valid() && telemetrySseBeginPending(slot,
		TelemetrySseFrameKind::SYSTEM_LOG, frame.size(), sequence, now);
}

template<class Client>
bool telemetrySseFormatSample(TelemetrySseClientSlot<Client> &slot,
	uint32_t sequence, const float *row, int columns, uint32_t now) {
	TelemetrySseFrameWriter frame(slot.pendingFrame, sizeof(slot.pendingFrame));
	frame.append("id: %lu\nevent: sample\ndata: ", (unsigned long)sequence);
	for (int i = 0; i < columns && frame.valid(); ++i)
		frame.append("%s%.7g", i ? "," : "", row[i]);
	frame.append("\n\n");
	return frame.valid() && telemetrySseBeginPending(slot,
		TelemetrySseFrameKind::SAMPLE, frame.size(), sequence, now);
}

// One production service pass: inspect and advance every slot exactly once.
// Accepting a new socket remains the caller's first action so this pass can
// never delay accept with a retry loop.
template<class Client, class ColumnName, class Event, class CopyEvent, class CopyRow,
	class Receive, class Send>
void telemetrySseServicePass(TelemetrySseClientSlot<Client> *slots, size_t count,
	uint32_t now, int columns, float *row, ColumnName columnName,
	CopyEvent copyEvent, CopyRow copyRow, Receive receive, Send send) {
	uint8_t inputDiscard[TELEMETRY_SSE_INPUT_DRAIN_BYTES];
	for (size_t i = 0; i < count; ++i) {
		auto &slot = slots[i];
		if (!slot.active) continue;
		// Consume one bounded request chunk per pass. This exposes a FIN hidden
		// behind unread HTTP headers without handing the socket to NetworkClient's
		// separate receive buffer.
		const int received = receive(slot.client, inputDiscard, sizeof(inputDiscard));
		if (received == 0 || received == TELEMETRY_SSE_SEND_FAILED) {
			telemetrySseDeactivate(slot);
			continue;
		}

		bool attemptedFrame = false;
		bool frameReady = slot.pendingKind != TelemetrySseFrameKind::NONE;
		if (!frameReady && slot.initStage == TelemetrySseInitStage::HTTP_HEADERS) {
			attemptedFrame = true;
			frameReady = telemetrySseFormatHttpHeaders(slot, now);
		} else if (!frameReady && slot.initStage == TelemetrySseInitStage::SCHEMA) {
			attemptedFrame = true;
			frameReady = telemetrySseFormatSchema(slot, columns, columnName, now);
		} else if (!frameReady) {
			bool buildFailed = false;
			// Give a due sample its 500 ms opportunity before draining more system
			// history. Otherwise a continuously growing log can starve telemetry.
			if ((uint32_t)(now - slot.lastSampleMs) >= TELEMETRY_SSE_SAMPLE_INTERVAL_MS) {
				slot.lastSampleMs = now;
				uint32_t sequence;
				if (copyRow(row, columns, sequence) && sequence != slot.lastSequence) {
					attemptedFrame = true;
					frameReady = telemetrySseFormatSample(slot, sequence, row, columns, now);
					buildFailed = !frameReady;
				}
			}
			if (!frameReady && !buildFailed) {
				Event event;
				if (copyEvent(slot.lastSystemSequence, event)) {
					attemptedFrame = true;
					frameReady = telemetrySseFormatSystemLog(slot, event.bootId, event.sequence,
						event.uptimeMs, event.tag, event.message, now);
				}
			}
			if (buildFailed) {
				telemetrySseDeactivate(slot);
				continue;
			}
		}

		if (attemptedFrame && !frameReady) {
			// Never emit truncated frames when a schema or row exceeds the fixed cap.
			telemetrySseDeactivate(slot);
			continue;
		}
		if (!frameReady) continue;
		const TelemetrySseSendResult result = telemetrySseAdvancePending(slot, now, send);
		if (result == TelemetrySseSendResult::FAILED ||
			result == TelemetrySseSendResult::TIMED_OUT) telemetrySseDeactivate(slot);
	}
}
