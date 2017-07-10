#include <stdlib.h>
#include <iconv.h>
#include <errno.h>
#include "types.h"


char* utf16to8(u8* s, size_t len) {
	iconv_t cd;
	char *in, *out, *t;
	size_t inlen, outlen, tlen;

	if (len == 0) {
		out = malloc(1);
		out[0] = '\0';
		return out;
	}

	// XXX iconv is ridiculously heavyweight for this
	cd = iconv_open("UTF-8", "UTF-16LE");
	in = (char*)s;
	inlen = len;
	outlen = len;
	out = malloc(outlen);
	t = out;
	tlen = outlen - 1;
	for (;;) {
		if (iconv(cd, &in, &inlen, &t, &tlen) == (size_t)-1) {
			if (errno == E2BIG) {
				// resize output buffer if necessary.
				// tbh this will never happen.
				// you'd have to have a string full of
				// japanese or smth for utf16->utf8 to
				// result in expansion
				size_t converted = t - out;
				outlen *= 2;
				out = realloc(out, outlen*2);
				t = out + converted;
				tlen = outlen - converted - 1;
				continue;
			}
		}
		break;
	}
	t[0] = '\0';
	iconv_close(cd);
	return out;
}
