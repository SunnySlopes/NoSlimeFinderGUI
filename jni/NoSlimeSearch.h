#ifndef NOSLIME_SEARCH_H
#define NOSLIME_SEARCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed geometry (the ring is fixed at 24 < d <= 128, 49640 blocks). */
#define NOSLIME_RING_INNER 24
#define NOSLIME_RING_OUTER 128
#define NOSLIME_RING_TOTAL 49640
#define NOSLIME_DONUT_CHUNKS 192

enum
{
    NOSLIME_STAGE_IDLE = 0,
    NOSLIME_STAGE_PREFILTER = 1,
    NOSLIME_STAGE_SCAN = 2,
    NOSLIME_STAGE_COARSE = 3,
    NOSLIME_STAGE_EXACT = 4,
    NOSLIME_STAGE_DONE = 5,
    NOSLIME_STAGE_CANCELLED = 6,
    NOSLIME_STAGE_ERROR = 7
};

typedef struct NoSlimeParams
{
    int64_t seed;
    int32_t afk_x0, afk_x1, afk_z0, afk_z1;
    int32_t threads;
    int32_t biome_y;
    int32_t max_results;
    int32_t min_area;
    /* 0 => max(1000, 10 * max_results), capped at 1000. */
    int32_t candidate_limit;
    int32_t mc; /* cubiomes MCVersion */
    const int32_t *avoid_ids;
    int32_t avoid_count;
} NoSlimeParams;

typedef struct NoSlimeResult
{
    int32_t x, z;
    int32_t area;    /* 49640 - slime - avoid_x */
    int32_t slime;   /* ring blocks in slime chunks */
    int32_t chunks;  /* slime chunks in the 192-chunk donut */
    int32_t avoid_x; /* avoided ring blocks that are not slime */
    int32_t avoid_y; /* avoided ring blocks that are also slime */
} NoSlimeResult;

/*
 * Runs synchronously on the calling thread.
 * Returns 0 on success, 1 when cancelled, < 0 on error.
 * Progress and results are available through the accessors below.
 */
int noslime_run(const NoSlimeParams *p);

int noslime_pause(void);
int noslime_resume(void);
int noslime_cancel(void);

/*
 * out must have 8 elements:
 * [0]=stage, [1]=done, [2]=paused, [3]=error,
 * [4]=stage_current, [5]=stage_total, [6]=elapsed_ms, [7]=result_count
 */
void noslime_get_progress(int64_t out[8]);

const char *noslime_last_error(void);

int32_t noslime_result_count(void);
int32_t noslime_get_results(NoSlimeResult *out, int32_t capacity);

#ifdef __cplusplus
}
#endif

#endif
