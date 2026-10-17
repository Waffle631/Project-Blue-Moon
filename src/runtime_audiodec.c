/* libSceAudiodec, Spec 4 phase A (docs/specs/2026-10-14-spec-4-audiodec.md): there is no decoder
 * yet, so each function the game calls answers with an explicit error after logging its
 * arguments; GOW3_TRACE_AUDIODEC=1 logs every call instead of only the first one. Functions are
 * added as the game reaches them; the rest stay unresolved and stop. The error values are the
 * Vita's libSceAudiodec ones (0x807F00xx); libSceAudiodecCpu uses 0x80F700xx (shadPS4 PR 5289). */
#include "runtime.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define AUDIODEC_INVALID_TYPE ((int32_t)0x807F0001)
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static size_t inits;
static int tracing(void) { const char *v=getenv("GOW3_TRACE_AUDIODEC"); return v && *v && strcmp(v,"0"); }
/* codecType 1 is ATRAC9 (eboot +0x10e200), and the game ignores the result. */
static ABI int32_t init_library(uint32_t codec) {
    pthread_mutex_lock(&lock); size_t n=++inits; pthread_mutex_unlock(&lock);
    if (n==1 || tracing()) printf("Runtime: sceAudiodecInitLibrary(codec %u): no decoder yet (Spec 4 phase A), 0x%x\n",codec,(unsigned)AUDIODEC_INVALID_TYPE);
    return AUDIODEC_INVALID_TYPE;
}
uintptr_t runtime_audiodec_resolve(const char *name) {
    if (!strcmp(name,"VjhsmxpcezI#libSceAudiodec")) return (uintptr_t)init_library;
    return 0;
}
void runtime_audiodec_report(void) {
    pthread_mutex_lock(&lock);
    printf("Runtime: Audiodec library inits=%zu (provisional errors, no decoder)\n",inits);
    pthread_mutex_unlock(&lock);
}
