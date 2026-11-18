// DYLD interpose: shorten Boost.Interprocess thread_sleep_tick() (exactly tick/2 = 5 ms) to CLAMP_NS.
#include <stdlib.h>
#include <time.h>
static int my_nanosleep(const struct timespec *r, struct timespec *rem) {
  static long c = -1; if (c < 0) {const char *e = getenv("CLAMP_NS"); c = e ? atol(e) : 100000;}
  if (r && r->tv_sec == 0 && r->tv_nsec == 5000000) {struct timespec t = {0, c}; return nanosleep(&t, rem);}
  return nanosleep(r, rem);
}
__attribute__((used, section("__DATA,__interpose"))) static struct {const void *n, *o;} ip[] = {{(const void *)my_nanosleep, (const void *)nanosleep}};
