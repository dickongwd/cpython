#include "Python.h"
#include "pycore_scheduler.h"

/* Copied/referenced over from _randommodule.c */

/* initializes mt[N] with a seed */
static void init_genrand(_PyScheduler_RandomObject *self, uint32_t s) {
    const int N = 624;

    int mti;
    uint32_t *mt;

    mt = self->state;
    mt[0]= s;
    for (mti=1; mti<N; mti++) {
        mt[mti] =
        (1812433253U * (mt[mti-1] ^ (mt[mti-1] >> 30)) + mti);
        /* See Knuth TAOCP Vol2. 3rd Ed. P.106 for multiplier. */
        /* In the previous versions, MSBs of the seed affect   */
        /* only MSBs of the array mt[].                        */
        /* 2002/01/09 modified by Makoto Matsumoto             */
    }
    self->index = mti;
    return;
}

/* generates a random number on [0,0xffffffff]-interval */
static uint32_t genrand_uint32(_PyScheduler_RandomObject *self) {
    const int N = 624;
    const int M = 397;
    const unsigned int MATRIX_A = 0x9908b0dfU;
    const unsigned int UPPER_MASK = 0x80000000U;
    const unsigned int LOWER_MASK = 0x7fffffffU;

    uint32_t y;
    static const uint32_t mag01[2] = {0x0U, MATRIX_A};
    /* mag01[x] = x * MATRIX_A  for x=0,1 */
    uint32_t *mt;

    mt = self->state;
    if (self->index >= N) { /* generate N words at one time */
        int kk;

        for (kk=0;kk<N-M;kk++) {
            y = (mt[kk]&UPPER_MASK)|(mt[kk+1]&LOWER_MASK);
            mt[kk] = mt[kk+M] ^ (y >> 1) ^ mag01[y & 0x1U];
        }
        for (;kk<N-1;kk++) {
            y = (mt[kk]&UPPER_MASK)|(mt[kk+1]&LOWER_MASK);
            mt[kk] = mt[kk+(M-N)] ^ (y >> 1) ^ mag01[y & 0x1U];
        }
        y = (mt[N-1]&UPPER_MASK)|(mt[0]&LOWER_MASK);
        mt[N-1] = mt[M-1] ^ (y >> 1) ^ mag01[y & 0x1U];

        self->index = 0;
    }

    y = mt[self->index++];
    y ^= (y >> 11);
    y ^= (y << 7) & 0x9d2c5680U;
    y ^= (y << 15) & 0xefc60000U;
    y ^= (y >> 18);
    return y;
}

void _PyScheduler_Init(_PyScheduler* scheduler, PyInterpreterState* interp, uint32_t seed) {
    scheduler->next = NULL;
    scheduler->interp = interp;
    init_genrand(&scheduler->random_obj, seed);
    scheduler->initialized = 1;
}
