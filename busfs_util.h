/* from the GNU libc manual */

#include <sys/time.h>
#include <time.h>

/* Subtract the `struct timeval' values X and Y,
   storing the result in RESULT.
   Return 1 if the difference is negative, otherwise 0.  */

static inline int
busfs_util_timeval_subtract(struct timeval *result,
         struct timeval *x,
         struct timeval *y)
{
  /* Perform the carry for the later subtraction by updating Y. */
  if (x->tv_usec < y->tv_usec) {
    int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
    y->tv_usec -= 1000000 * nsec;
    y->tv_sec += nsec;
  }
  if (x->tv_usec - y->tv_usec > 1000000) {
    int nsec = (x->tv_usec - y->tv_usec) / 1000000;
    y->tv_usec += 1000000 * nsec;
    y->tv_sec -= nsec;
  }

  /* Compute the time remaining to wait.
     `tv_usec' is certainly positive. */
  result->tv_sec = x->tv_sec - y->tv_sec;
  result->tv_usec = x->tv_usec - y->tv_usec;

  /* Return 1 if result is negative. */
  return x->tv_sec < y->tv_sec;
}

static inline int busfs_timeval_add(struct timeval *res, const struct timeval *x,
        const struct timeval *y)
{
    res->tv_sec = x->tv_sec + y->tv_sec;

    res->tv_usec = x->tv_usec + y->tv_usec;

    while (res->tv_usec > 1000000) {
        res->tv_usec -= 1000000;
        res->tv_sec++;
    }

    return 0;
}
