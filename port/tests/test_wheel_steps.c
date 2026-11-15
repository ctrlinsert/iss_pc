#include "wheel_steps.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
/* Runs `reads` reads with the notches given at read 0; returns the presses (edges) seen. */
static int edges(WheelAxis *a, const int *neg, const int *pos, int reads, char *trace) {
    int prev = 0, count = 0;
    for (int i = 0; i < reads; i++) {
        const int d = wheel_axis_read(a, neg ? neg[i] : 0, pos ? pos[i] : 0);
        if (trace) trace[i] = d > 0 ? '+' : d < 0 ? '-' : '.';
        if (d && d != prev) count++;
        prev = d;
    }
    if (trace) trace[reads] = 0;
    return count;
}
int main(void) {
    char t[64];
    { /* one notch: down two reads, up, nothing more */
        WheelAxis a = {0}; int pos[12] = {1};
        assert(edges(&a, NULL, pos, 12, t) == 1 && !strcmp(t, "++.........."));
    }
    { /* three notches at once: each its own press, queue capped at two */
        WheelAxis a = {0}; int pos[12] = {3};
        assert(edges(&a, NULL, pos, 12, t) == 2 && !strcmp(t, "++.++......."));
    }
    { /* notches one per read: every press separated by a release */
        WheelAxis a = {0}; int pos[12] = {1, 1, 1, 1, 1, 1};
        edges(&a, NULL, pos, 12, t);
        for (int i = 0; t[i + 1]; i++) assert(!(t[i] == '+' && t[i + 1] == '+' && i % 3 == 1));
        assert(strstr(t, "++.++.++"));
    }
    { /* reversal mid-press: the press ends, the queue drops, the other way follows */
        WheelAxis a = {0}; int pos[12] = {2}, neg[12] = {0, 1};
        assert(!strcmp((edges(&a, neg, pos, 12, t), t), "+.--........"));
    }
    { /* reversal while queued: only the new direction steps */
        WheelAxis a = {0}; int pos[12] = {2}, neg[12] = {0, 0, 0, 1};
        edges(&a, neg, pos, 12, t);
        assert(!strcmp(t, "++.--......."));
    }
    puts("Wheel steps: one edge per notch, release between steps, capped queue, reversal drops the queue.");
    return 0;
}
