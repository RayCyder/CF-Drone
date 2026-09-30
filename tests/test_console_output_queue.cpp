#include <cassert>
#include <cstdio>
#include <cstring>
#include "../console_output_queue.h"

int main() {
	ConsoleOutputQueue queue;
	const char *first = "diagnostic output\n";
	assert(queue.push(first, strlen(first)) == strlen(first));
	assert(queue.available() == strlen(first));
	char output[32] = {};
	assert(queue.pop(output, 5) == 5);
	assert(memcmp(output, "diagn", 5) == 0);
	assert(queue.push(" + more\n", 8) == 8);
	char remainder[32] = {};
	const size_t remaining = queue.pop(remainder, sizeof(remainder));
	assert(remaining == strlen(first) - 5 + 8);
	assert(memcmp(remainder, "ostic output\n + more\n", remaining) == 0);
	assert(queue.available() == 0);

	char full[ConsoleOutputQueue::CAPACITY + 1];
	memset(full, 'x', sizeof(full));
	assert(queue.push(full, sizeof(full)) == ConsoleOutputQueue::CAPACITY);
	assert(queue.available() == ConsoleOutputQueue::CAPACITY);
	assert(queue.pop(output, sizeof(output)) == sizeof(output));
	assert(queue.available() == ConsoleOutputQueue::CAPACITY - sizeof(output));
	puts("bounded console output queue regression: PASS");
}
