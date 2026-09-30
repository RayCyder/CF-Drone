#pragma once

#include <stddef.h>
#include <stdint.h>

class ConsoleOutputQueue {
public:
	#if defined(CF_DRONE_ENABLE_TASK_SWITCH_TRACE)
	static constexpr size_t CAPACITY = 1024;
	#else
	static constexpr size_t CAPACITY = 1536;
	#endif

	size_t push(const char *data, size_t length) {
		size_t accepted = 0;
		while (accepted < length && size < CAPACITY) {
			buffer[tail] = data[accepted++];
			tail = (tail + 1) % CAPACITY;
			++size;
		}
		return accepted;
	}

	size_t pop(char *destination, size_t capacity) {
		size_t count = 0;
		while (count < capacity && size) {
			destination[count++] = buffer[head];
			head = (head + 1) % CAPACITY;
			--size;
		}
		return count;
	}

	size_t available() const { return size; }

private:
	char buffer[CAPACITY] = {};
	size_t head = 0;
	size_t tail = 0;
	size_t size = 0;
};
