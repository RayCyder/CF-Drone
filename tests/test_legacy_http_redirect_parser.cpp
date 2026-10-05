#include <assert.h>
#include <string.h>

#include "../legacy_http_redirect_parser.h"

int main() {
	LegacyHttpRedirectParser parser;
	const char *request = "GET /web_rc HTTP/1.1\r\nHost: drone.local:8080\r\n\r\n";
	for (const char *p = request; *p; ++p) {
		assert(parser.append(*p));
		if (*(p + 1)) assert(!parser.complete());
	}
	assert(parser.complete());
	assert(parser.size() == strlen(request));
	assert(strcmp(parser.data(), request) == 0);

	parser.reset();
	assert(!parser.complete());
	assert(parser.size() == 0);
	const char *lfRequest = "GET / HTTP/1.0\nHost: drone\n\n";
	for (const char *p = lfRequest; *p; ++p) assert(parser.append(*p));
	assert(parser.complete());

	parser.reset();
	for (size_t i = 0; i < LEGACY_HTTP_REDIRECT_MAX_REQUEST_BYTES - 1; ++i)
		assert(parser.append('x'));
	assert(!parser.append('x'));
	assert(!parser.complete());
	assert(parser.size() == LEGACY_HTTP_REDIRECT_MAX_REQUEST_BYTES - 1);
}
