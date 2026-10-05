#pragma once

#include <stddef.h>

constexpr size_t LEGACY_HTTP_REDIRECT_MAX_REQUEST_BYTES = 768;
constexpr size_t LEGACY_HTTP_REDIRECT_READ_BUDGET_BYTES = 128;
constexpr unsigned long LEGACY_HTTP_REDIRECT_REQUEST_TIMEOUT_MS = 200;

class LegacyHttpRedirectParser {
public:
	void reset() {
		length_ = 0;
		complete_ = false;
		buffer_[0] = '\0';
	}

	bool append(char value) {
		if (length_ + 1 >= sizeof(buffer_)) return false;
		buffer_[length_++] = value;
		buffer_[length_] = '\0';
		if ((length_ >= 4 && buffer_[length_ - 4] == '\r' && buffer_[length_ - 3] == '\n' &&
			buffer_[length_ - 2] == '\r' && buffer_[length_ - 1] == '\n') ||
			(length_ >= 2 && buffer_[length_ - 2] == '\n' && buffer_[length_ - 1] == '\n')) {
			complete_ = true;
		}
		return true;
	}

	bool complete() const { return complete_; }
	const char *data() const { return buffer_; }
	size_t size() const { return length_; }

private:
	char buffer_[LEGACY_HTTP_REDIRECT_MAX_REQUEST_BYTES] = {};
	size_t length_ = 0;
	bool complete_ = false;
};
