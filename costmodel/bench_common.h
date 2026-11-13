// bench_common.h — timing shared by the in-process cost-model programs
// (kernel_costmodel, list_ops_kernel, capi_costmodel).
//
// Every measurement runs one warm-up batch and BATCHES measured batches and
// records, per batch, wall-clock time and process CPU time. Process CPU time
// adds up all threads, so work done by a concurrent collector thread (which
// wall-clock on a multi-core machine hides) is counted. The RES line reports
// the median of the batches (the headline), plus the minimum and maximum, for
// both clocks:
//
//   RES op=<name> [N=<n>] ns=<median wall> min=<..> max=<..> cpu=<median cpu> cpu_max=<..> cells=<..>
//
// Header-only, C and C++.
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef COSTMODEL_BATCHES
#define COSTMODEL_BATCHES 7
#endif

static inline double cm_clock_ns(clockid_t id) {
    struct timespec ts;
    clock_gettime(id, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}
static inline double cm_wall_ns(void) { return cm_clock_ns(CLOCK_MONOTONIC); }
static inline double cm_cpu_ns(void) { return cm_clock_ns(CLOCK_PROCESS_CPUTIME_ID); }

static inline int cm_cmp_double(const void* a, const void* b) {
    double x = *(const double*)a, y = *(const double*)b;
    return (x > y) - (x < y);
}

typedef struct {
    double wall[COSTMODEL_BATCHES];
    double cpu[COSTMODEL_BATCHES];
    int n;
} cm_samples;

static inline void cm_add(cm_samples* s, double wallPerOp, double cpuPerOp) {
    if (s->n < COSTMODEL_BATCHES) { s->wall[s->n] = wallPerOp; s->cpu[s->n] = cpuPerOp; s->n++; }
}

// Sorts in place; returns the median.
static inline double cm_median(double* v, int n) {
    qsort(v, (size_t)n, sizeof(double), cm_cmp_double);
    return (n % 2) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

// Prints the timing fields (no newline): ns= min= max= cpu= cpu_max=.
// `per` divides every figure again (elements per call).
static inline void cm_print_times(cm_samples* s, double per) {
    double w = cm_median(s->wall, s->n), c = cm_median(s->cpu, s->n);
    printf(" ns=%.2f min=%.2f max=%.2f cpu=%.2f cpu_max=%.2f",
           w / per, s->wall[0] / per, s->wall[s->n - 1] / per, c / per, s->cpu[s->n - 1] / per);
}

static inline void cm_fail(const char* what) {
    fprintf(stderr, "VERIFY FAILED: %s\n", what);
    exit(2);
}
