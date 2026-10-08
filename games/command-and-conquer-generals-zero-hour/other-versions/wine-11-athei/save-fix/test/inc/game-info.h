#include <stdlib.h>
static inline const char *game_getenv(const char *n) { char b[64] = "GENERALSZH_"; strcat(b, n); return getenv(b); }
