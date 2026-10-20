/* libSceAudiodec, Spec 4a (docs/specs/2026-10-14-spec-4-audiodec.md): ATRAC9 decoders over LibAtrac9
 * (MIT, third_party/LibAtrac9). No public source describes the PS4 structures: the layout below is the
 * one the game builds (docs/audiodec.md 1.2), and every call checks it. Decode and ClearContext still
 * answer an explicit error (task 4 of the plan). The error values are the Vita's libSceAudiodec ones
 * (0x807F00xx, vitasdk psp2/audiodec.h); libSceAudiodecCpu uses 0x80F700xx (shadPS4 PR 5289). Any
 * error stops the game's track without a message (docs/audiodec.md 1.6), so each one is logged with
 * its cause: the first call and the first error of each function, or every call with
 * GOW3_TRACE_AUDIODEC=1. */
#include "runtime.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "third_party/LibAtrac9/C/src/libatrac9.h"
#define ERR_INVALID_TYPE ((int32_t)0x807F0001)
#define ERR_INVALID_INIT_PARAM ((int32_t)0x807F0002)
#define ERR_ALREADY_INITIALIZED ((int32_t)0x807F0003)
#define ERR_OUT_OF_MEMORY ((int32_t)0x807F0004)
#define ERR_NOT_INITIALIZED ((int32_t)0x807F0005)
#define ERR_ALL_HANDLES_IN_USE ((int32_t)0x807F0007)
#define ERR_INVALID_PTR ((int32_t)0x807F0008)
#define ERR_INVALID_HANDLE ((int32_t)0x807F0009)
#define ERR_INVALID_WORD_LENGTH ((int32_t)0x807F000C)
#define ERR_INVALID_SIZE ((int32_t)0x807F000D)
#define CODEC_ATRAC9 1 /* the game's only codec (InitLibrary +0x10e200, CreateDecoder +0x113172) */
#define MAX_DECODERS 64 /* one per "at9 thread" (32) at most, docs/audiodec.md 1.1 */
#define BSI_SIZE 0x24
/* What ctrl points to: the descriptors the game fills in its control block. */
typedef struct { uint32_t size, reserved; void *data; uint32_t bytes, reserved2; } Buffer;
typedef struct { uint32_t size, word_length; uint8_t config[4]; } Params;
typedef struct { uint32_t size, channels; } Bsi; /* BSI_SIZE bytes; the game reads only +4 (1.4) */
typedef struct { Params *params; Bsi *bsi; Buffer *au, *pcm; } Ctrl;
_Static_assert(sizeof(Buffer)==0x18 && sizeof(Params)==0xC && sizeof(Ctrl)==0x20,"docs/audiodec.md 1.2");
/* Delete and Term take a decoder out of the table at once (its id is stale from then on) but free
 * it only when no call holds a reference (refs, taken by acquire). Ids grow and are never reused. */
typedef struct Decoder {
    int32_t id; int refs, live;
    void *handle; uint8_t config[4]; Atrac9CodecInfo info;
    uint32_t frames, superframe_remain;
    pthread_mutex_t lock;
} Decoder;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static Decoder *table[MAX_DECODERS];
static int initialized;
static int32_t next_id=1; /* 0 is never an id: a zeroed field is not a decoder */
static size_t objects, created, frames_decoded, errors;
enum { F_INIT, F_TERM, F_CREATE, F_DELETE, F_DECODE, F_CLEAR, FUNCTIONS };
static const char *const names[FUNCTIONS]={"sceAudiodecInitLibrary","sceAudiodecTermLibrary","sceAudiodecCreateDecoder",
    "sceAudiodecDeleteDecoder","sceAudiodecDecode","sceAudiodecClearContext"};
static size_t calls[FUNCTIONS], failures[FUNCTIONS];
static int tracing(void) { const char *v=getenv("GOW3_TRACE_AUDIODEC"); return v && *v && strcmp(v,"0"); }
static int32_t finish(int f, int32_t result, const char *args, const char *why) {
    pthread_mutex_lock(&lock);
    size_t n=++calls[f], e=result<0 ? ++failures[f] : 0; errors+=result<0;
    pthread_mutex_unlock(&lock);
    if (n==1 || e==1 || tracing()) {
        if (result<0) printf("Runtime: %s(%s) = %#x: %s\n",names[f],args,(unsigned)result,why);
        else printf("Runtime: %s(%s) = %d\n",names[f],args,result);
    }
    return result;
}
static void check_codec(int f, uint32_t codec) {
    if (codec==CODEC_ATRAC9) return;
    fprintf(stderr,"STOP: %s: unsupported Audiodec codec %u (0x%x); only 1 (ATRAC9) is implemented\n",names[f],codec,codec);
    exit(21);
}
/* Only direct and flexible maps count, not permissions (runtime_memory.c): the game's blocks come
 * from its allocator (+0x12c430) and its thread objects. */
static int guest(const void *p, uint64_t size) { return p && runtime_memory_is_mapped((uintptr_t)p,size); }
static int slot_of(int32_t id) { /* lock held */
    for (int i=0;i<MAX_DECODERS;++i) if (table[i] && table[i]->id==id) return i;
    return -1;
}
static void dispose(Decoder *d) { if (d->handle) Atrac9ReleaseHandle(d->handle); pthread_mutex_destroy(&d->lock); free(d); }
static void destroy(Decoder *d) { dispose(d); pthread_mutex_lock(&lock); --objects; pthread_mutex_unlock(&lock); }
/* Lock held: takes slot i out of the table; returns the decoder if nobody holds it (free it then). */
static Decoder *unlink_slot(int i) { Decoder *d=table[i]; table[i]=NULL; d->live=0; return d->refs ? NULL : d; }
static Decoder *acquire(int32_t id) {
    pthread_mutex_lock(&lock);
    int i=slot_of(id); Decoder *d=i<0 ? NULL : table[i];
    if (d) ++d->refs;
    pthread_mutex_unlock(&lock);
    return d;
}
static void release(Decoder *d) {
    pthread_mutex_lock(&lock); int last=--d->refs==0 && !d->live; pthread_mutex_unlock(&lock);
    if (last) destroy(d);
}
static ABI int32_t init_library(uint32_t codec) {
    check_codec(F_INIT,codec);
    pthread_mutex_lock(&lock); int was=initialized; initialized=1; pthread_mutex_unlock(&lock);
    char args[32]; snprintf(args,sizeof args,"codec %u",codec);
    return finish(F_INIT,was ? ERR_ALREADY_INITIALIZED : 0,args,"already initialized");
}
/* Two call sites, both ignoring the result: the audio system's destructor (+0x10df70) and its vtable
   slot +0x18 (+0x10e864). Ids from before are errors; InitLibrary works again. */
static ABI int32_t term_library(uint32_t codec) {
    check_codec(F_TERM,codec);
    Decoder *gone[MAX_DECODERS]; int n=0;
    pthread_mutex_lock(&lock);
    int was=initialized; initialized=0;
    for (int i=0;i<MAX_DECODERS;++i) if (table[i] && (gone[n]=unlink_slot(i))) ++n;
    pthread_mutex_unlock(&lock);
    for (int i=0;i<n;++i) destroy(gone[i]);
    char args[32]; snprintf(args,sizeof args,"codec %u",codec);
    return finish(F_TERM,was ? 0 : ERR_NOT_INITIALIZED,args,"not initialized");
}
/* Reads what CreateDecoder needs (docs/audiodec.md 1.2) into c and p; returns why it cannot. */
static const char *read_create(const void *control, Ctrl *c, Params *p, int32_t *error) {
    *error=ERR_INVALID_PTR;
    if (!guest(control,sizeof *c)) return "ctrl is not mapped guest memory";
    memcpy(c,control,sizeof *c);
    if (!guest(c->params,sizeof *p)) return "params are not mapped guest memory";
    if (!guest(c->bsi,BSI_SIZE)) return "bsi is not mapped guest memory";
    memcpy(p,c->params,sizeof *p);
    *error=ERR_INVALID_SIZE;
    if (p->size!=sizeof *p) return "params size is not 0xC";
    if (c->bsi->size!=BSI_SIZE) return "bsi size is not 0x24";
    /* [H] 1 = 16-bit PCM (docs/audiodec.md 1.5): the game always writes 1. */
    *error=ERR_INVALID_WORD_LENGTH;
    if (p->word_length!=1) return "params word length is not 1";
    *error=ERR_INVALID_INIT_PARAM;
    if (p->config[0]!=0xFE) return "ATRAC9 config does not start with 0xFE";
    return NULL;
}
/* The game stores the result as the stream's id without checking it (+0x113177); a negative id
   makes it skip Decode and end the track (docs/audiodec.md 1.6). */
static ABI int32_t create_decoder(void *control, uint32_t codec) {
    check_codec(F_CREATE,codec);
    char args[64]; snprintf(args,sizeof args,"ctrl %p, codec %u",control,codec);
    Ctrl c; Params p; int32_t error;
    const char *why=read_create(control,&c,&p,&error);
    if (why) return finish(F_CREATE,error,args,why);
    Decoder *d=calloc(1,sizeof *d);
    if (!d) return finish(F_CREATE,ERR_OUT_OF_MEMORY,args,"out of host memory");
    pthread_mutex_init(&d->lock,NULL); memcpy(d->config,p.config,4);
    if (!(d->handle=Atrac9GetHandle()) || Atrac9InitDecoder(d->handle,d->config)) {
        dispose(d); return finish(F_CREATE,ERR_INVALID_INIT_PARAM,args,"LibAtrac9 rejects the ATRAC9 config");
    }
    Atrac9GetCodecInfo(d->handle,&d->info);
    d->superframe_remain=(uint32_t)d->info.superframeSize; d->live=1;
    uint32_t channels=(uint32_t)d->info.channels; /* d is not ours once it is in the table */
    pthread_mutex_lock(&lock);
    int slot=-1; int32_t id=0;
    for (int i=0;i<MAX_DECODERS && slot<0;++i) if (!table[i]) slot=i;
    if (!initialized) error=ERR_NOT_INITIALIZED, why="library not initialized";
    else if (slot<0 || next_id==INT32_MAX) error=ERR_ALL_HANDLES_IN_USE, why="decoder table full";
    else { d->id=id=next_id++; table[slot]=d; ++objects; ++created; }
    pthread_mutex_unlock(&lock);
    if (why) { dispose(d); return finish(F_CREATE,error,args,why); }
    /* The game reads the channels before the first Decode (+0x112926; docs/audiodec.md 1.4). */
    c.bsi->channels=channels;
    return finish(F_CREATE,id,args,NULL);
}
/* One live call site (+0x112d28), result ignored. */
static ABI int32_t delete_decoder(int32_t id) {
    char args[24]; snprintf(args,sizeof args,"id %d",id);
    Decoder *gone=NULL;
    pthread_mutex_lock(&lock);
    int i=slot_of(id);
    if (i>=0) gone=unlink_slot(i);
    pthread_mutex_unlock(&lock);
    if (gone) destroy(gone);
    return finish(F_DELETE,i>=0 ? 0 : ERR_INVALID_HANDLE,args,"no live decoder with this id");
}
static ABI int32_t decode(int32_t id, void *control) {
    char args[64]; snprintf(args,sizeof args,"id %d, ctrl %p",id,control);
    Decoder *d=acquire(id);
    if (!d) return finish(F_DECODE,ERR_INVALID_HANDLE,args,"no live decoder with this id");
    release(d);
    return finish(F_DECODE,ERR_INVALID_TYPE,args,"not implemented yet (Spec 4, task 4)");
}
static ABI int32_t clear_context(int32_t id) {
    char args[24]; snprintf(args,sizeof args,"id %d",id);
    Decoder *d=acquire(id);
    if (!d) return finish(F_CLEAR,ERR_INVALID_HANDLE,args,"no live decoder with this id");
    release(d);
    return finish(F_CLEAR,ERR_INVALID_TYPE,args,"not implemented yet (Spec 4, task 4)");
}
static const RuntimeExport exports[]={
    {"sceAudiodecInitLibrary",init_library}, {"sceAudiodecTermLibrary",term_library},
    {"sceAudiodecCreateDecoder",create_decoder}, {"sceAudiodecDeleteDecoder",delete_decoder},
    {"sceAudiodecDecode",decode}, {"sceAudiodecClearContext",clear_context},
};
uintptr_t runtime_audiodec_resolve(const char *name) { return RUNTIME_LOOKUP(exports,name); }
void runtime_audiodec_report(void) {
    pthread_mutex_lock(&lock);
    printf("Runtime: Audiodec decoders=%zu, frames=%zu, errors=%zu\n",created,frames_decoded,errors);
    pthread_mutex_unlock(&lock);
}
