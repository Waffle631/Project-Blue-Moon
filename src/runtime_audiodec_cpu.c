/* libSceAudiodecCpu, Spec 4b (docs/specs/2026-10-14-spec-4-audiodec.md): the HEVAG effects of vag.mrg
 * (codec 0x1008, shadPS4 PR 5289) on the game's "hevag decode thread". Until the decoder exists (plan
 * tasks 10-12) every call answers API_FAIL, logged like libSceAudiodec's (runtime_audiodec.c): the
 * first call and the first error of each function, or every call with GOW3_TRACE_AUDIODEC=1 with the
 * structures before it (its own 400 lines, so that these errors do not use up the ATRAC9 trace; =2
 * has no cap). The game ignores the three results (docs/audiodec.md 2.3), so what an error leaves in
 * its descriptors decides what it does next: QueryMemSize sets the work memory size to 0, and Decode
 * reports no PCM and leaves the AU as it came, consumed. The voice then runs to the end of its data
 * in silence (docs/audiodec.md 2.4). */
#include "runtime.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ERR_API_FAIL ((int32_t)0x80F70000)
#define CODEC_HEVAG 0x1008 /* the game's only codec (+0x11565b, +0x115804, +0x115903) */
#define PARAMS_SIZE 0x40
#define BSI_SIZE 0x30
#define TRACE_LINES 400
/* The descriptors of the game's control block (docs/audiodec.md 2.2), as in libSceAudiodec. */
typedef struct { uint32_t size, reserved; void *data; uint32_t bytes, reserved2; } Buffer;
typedef struct { void *params, *bsi; Buffer *au, *pcm; } Ctrl;
_Static_assert(sizeof(Buffer)==0x18 && sizeof(Ctrl)==0x20,"docs/audiodec.md 2.2");
enum { F_QUERY, F_INIT, F_DECODE, FUNCTIONS };
static const char *const names[FUNCTIONS]={"sceAudiodecCpuQueryMemSize","sceAudiodecCpuInitDecoder","sceAudiodecCpuDecode"};
/* What each function reads of ctrl: params; params and bsi; all four (PR 5289's QueryCtrl, InitCtrl,
   DecodeCtrl). The game passes the same 0x20 bytes to all three. */
static const size_t ctrl_sizes[FUNCTIONS]={8,0x10,0x20};
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static size_t calls[FUNCTIONS], failures[FUNCTIONS], errors, trace_lines;
static int tracing(void) {
    const char *v=getenv("GOW3_TRACE_AUDIODEC");
    return !v || !*v || !strcmp(v,"0") ? 0 : strcmp(v,"2") ? 1 : 2;
}
static int trace_line(void) {
    int level=tracing();
    if (level!=1) return level;
    pthread_mutex_lock(&lock); size_t n=++trace_lines; pthread_mutex_unlock(&lock);
    if (n==TRACE_LINES+1) printf("Runtime: AudiodecCpu trace stops after %d lines; GOW3_TRACE_AUDIODEC=2 has no cap\n",TRACE_LINES);
    return n<=TRACE_LINES;
}
static int guest(const void *p, uint64_t size) { return p && runtime_memory_is_mapped((uintptr_t)p,size); }
typedef struct { char text[1024]; size_t used; } Line;
static void put(Line *l, const char *format, ...) {
    va_list a; va_start(a,format);
    int n=vsnprintf(l->text+l->used,sizeof l->text-l->used,format,a);
    va_end(a);
    if (n>0) l->used+=(size_t)n<sizeof l->text-l->used ? (size_t)n : sizeof l->text-1-l->used;
}
static void put_hex(Line *l, const char *label, const void *p, size_t size) {
    if (size>256) size=256;
    put(l," %s ",label);
    if (!guest(p,size)) { put(l,p ? "unmapped" : "null"); return; }
    for (size_t i=0;i<size;++i) put(l,"%02x",((const uint8_t *)p)[i]);
}
/* ctrl, what f reads of it and the resource descriptor; for Decode, the first bytes of the AU too. */
static void trace_before(int f, const char *args, const void *control, const void *resource) {
    if (!trace_line()) return;
    Line l={{0},0};
    put_hex(&l,"ctrl",control,ctrl_sizes[f]);
    if (guest(control,ctrl_sizes[f])) {
        const Ctrl *c=control;
        put_hex(&l,"params",c->params,PARAMS_SIZE);
        if (f!=F_QUERY) put_hex(&l,"bsi",c->bsi,BSI_SIZE);
        if (f==F_DECODE) {
            put_hex(&l,"au",c->au,sizeof(Buffer)); put_hex(&l,"pcm",c->pcm,sizeof(Buffer));
            if (guest(c->au,sizeof(Buffer))) put_hex(&l,"au.data",c->au->data,c->au->bytes<16 ? c->au->bytes : 16);
        }
    }
    put_hex(&l,"resource",resource,sizeof(Buffer));
    printf("Runtime: %s(%s) before:%s\n",names[f],args,l.text);
}
/* A descriptor the runtime may write: mapped and of the size the game gives it (0x18). */
static Buffer *descriptor(void *p) { Buffer *b=p; return guest(b,sizeof *b) && b->size==sizeof *b ? b : NULL; }
/* The answer until HEVAG exists, and what it leaves for the game to read (docs/audiodec.md 2.4). */
static int32_t not_implemented(int f, void *control, void *resource, uint32_t codec) {
    if (codec!=CODEC_HEVAG) {
        fprintf(stderr,"STOP: %s: unsupported AudiodecCpu codec %u (0x%x); the game uses only 0x1008 (HEVAG)\n",names[f],codec,codec);
        exit(21);
    }
    char args[96]; snprintf(args,sizeof args,"ctrl %p, resource %p, codec %#x",control,resource,codec);
    if (tracing()) trace_before(f,args,control,resource);
    const char *left="";
    if (f==F_QUERY) {
        /* The game allocates this size when it is more than it has (+0x115795): 0, nothing. */
        Buffer *r=descriptor(resource);
        if (r) r->bytes=0;
        left=r ? "; work memory size 0" : "; no resource descriptor to write";
    } else if (f==F_DECODE) {
        /* The game adds pcm.bytes / (channels x 2) samples and moves past au.bytes (+0x115920,
           +0x11594b): no samples, and the AU it sent counts as consumed, so the voice ends. */
        Buffer *pcm=guest(control,sizeof(Ctrl)) ? descriptor(((Ctrl *)control)->pcm) : NULL;
        if (pcm) pcm->bytes=0;
        left=pcm ? "; no PCM" : "; no PCM descriptor to write";
    }
    pthread_mutex_lock(&lock);
    size_t n=++calls[f], e=++failures[f]; ++errors;
    pthread_mutex_unlock(&lock);
    if (n==1 || e==1 || trace_line())
        printf("Runtime: %s(%s) = %#x: HEVAG is not implemented yet (Spec 4b)%s\n",names[f],args,(unsigned)ERR_API_FAIL,left);
    return ERR_API_FAIL;
}
/* One call site each, all three results ignored: +0x115788, +0x115811, +0x11590c. */
static ABI int32_t query_mem_size(void *control, void *resource, uint32_t codec) { return not_implemented(F_QUERY,control,resource,codec); }
static ABI int32_t init_decoder(void *control, void *resource, uint32_t codec) { return not_implemented(F_INIT,control,resource,codec); }
static ABI int32_t decode(void *control, void *resource, uint32_t codec) { return not_implemented(F_DECODE,control,resource,codec); }
static const RuntimeExport exports[]={
    {"sceAudiodecCpuQueryMemSize",query_mem_size}, {"sceAudiodecCpuInitDecoder",init_decoder}, {"sceAudiodecCpuDecode",decode},
};
uintptr_t runtime_audiodec_cpu_resolve(const char *name) { return RUNTIME_LOOKUP(exports,name); }
/* Nothing is created or decoded until HEVAG exists. */
void runtime_audiodec_cpu_report(void) {
    pthread_mutex_lock(&lock);
    printf("Runtime: AudiodecCpu decoders=0, frames=0, errors=%zu\n",errors);
    pthread_mutex_unlock(&lock);
}
