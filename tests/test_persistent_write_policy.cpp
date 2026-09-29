#include "../persistent_write_policy.h"
#include <cassert>

int main() {
	assert(persistentWritesAllowed(false, false));
	assert(!persistentWritesAllowed(true, false));
	assert(!persistentWritesAllowed(false, true));
	assert(!persistentWritesAllowed(true, true));
	return 0;
}
