/*----------------------
 | sfxconv.c
 | Description: Sample location, decoding and padding for the sound-effect
 |   backend. See sfxconv.h for why this is its own translation unit.
 | Author: suinevere
 | Dependencies: sfxconv.h, vm.h
 ----------------------*/
#include "sfxconv.h"
#include "vm.h"

/*----------------------
 | g_decode
 | Description: sfxconv_decode_byte as a 256-entry table. A table rather than
 |   two compares because the decode runs once per byte over a whole sample --
 |   up to tens of thousands of iterations for one play -- and because the
 |   table is literally the test fixture, so the mapping cannot drift without
 |   test_sfxconv.c saying so. 0x00..0x7f map to 0..127, 0x80 maps to -128 (see
 |   sfxconv_decode_byte), and 0x81..0xff map to -1..-127.
 | Author: suinevere
 ----------------------*/
static const signed char g_decode[256] = {
	   0,    1,    2,    3,    4,    5,    6,    7,    8,    9,   10,   11,   12,   13,   14,   15,
	  16,   17,   18,   19,   20,   21,   22,   23,   24,   25,   26,   27,   28,   29,   30,   31,
	  32,   33,   34,   35,   36,   37,   38,   39,   40,   41,   42,   43,   44,   45,   46,   47,
	  48,   49,   50,   51,   52,   53,   54,   55,   56,   57,   58,   59,   60,   61,   62,   63,
	  64,   65,   66,   67,   68,   69,   70,   71,   72,   73,   74,   75,   76,   77,   78,   79,
	  80,   81,   82,   83,   84,   85,   86,   87,   88,   89,   90,   91,   92,   93,   94,   95,
	  96,   97,   98,   99,  100,  101,  102,  103,  104,  105,  106,  107,  108,  109,  110,  111,
	 112,  113,  114,  115,  116,  117,  118,  119,  120,  121,  122,  123,  124,  125,  126,  127,
	-128,  -1,   -2,   -3,   -4,   -5,   -6,   -7,   -8,   -9,  -10,  -11,  -12,  -13,  -14,  -15,
	 -16,  -17,  -18,  -19,  -20,  -21,  -22,  -23,  -24,  -25,  -26,  -27,  -28,  -29,  -30,  -31,
	 -32,  -33,  -34,  -35,  -36,  -37,  -38,  -39,  -40,  -41,  -42,  -43,  -44,  -45,  -46,  -47,
	 -48,  -49,  -50,  -51,  -52,  -53,  -54,  -55,  -56,  -57,  -58,  -59,  -60,  -61,  -62,  -63,
	 -64,  -65,  -66,  -67,  -68,  -69,  -70,  -71,  -72,  -73,  -74,  -75,  -76,  -77,  -78,  -79,
	 -80,  -81,  -82,  -83,  -84,  -85,  -86,  -87,  -88,  -89,  -90,  -91,  -92,  -93,  -94,  -95,
	 -96,  -97,  -98,  -99, -100, -101, -102, -103, -104, -105, -106, -107, -108, -109, -110, -111,
	-112, -113, -114, -115, -116, -117, -118, -119, -120, -121, -122, -123, -124, -125, -126, -127
};

signed char sfxconv_decode_byte(unsigned char u)
{
	return g_decode[u];
}
