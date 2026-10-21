/* libSceAudiodec, Spec 4a (docs/specs/2026-10-14-spec-4-audiodec.md): ATRAC9 decoders over LibAtrac9
 * (MIT, third_party/LibAtrac9). No public source describes the PS4 structures: the layout below is the
 * one the game builds (docs/audiodec.md 1.2), and every call checks it. The error values are the
 * Vita's libSceAudiodec ones (0x807F00xx, vitasdk psp2/audiodec.h); libSceAudiodecCpu uses 0x80F700xx
 * (shadPS4 PR 5289). Any error stops the game's track without a message (docs/audiodec.md 1.6), so
 * each one is logged with its cause: the first call and the first error of each function, or every
 * call with GOW3_TRACE_AUDIODEC=1 (400 lines; =2 has no cap), with the structures before and after.
 * GOW3_AUDIODEC_AU_DUMP=<dir> writes what each decoder consumed to <dir>/dec<id>.au. */
#define _GNU_SOURCE
#include "runtime.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "third_party/LibAtrac9/C/src/libatrac9.h"
#define ERR_API_FAIL ((int32_t)0x807F0000)
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
/* LibAtrac9 gets no input size (docs/audiodec.md 1.3): it decodes from a copy of the AU with zeros
   after it. AU_MAX is the largest ATRAC9 superframe (2048-byte frames << 3, decinit.c:45); a frame
   reads at most a few KB, however corrupt (256 coefficients x 8 channels of 16-bit fields), so it
   never gets past AU_ZEROS bytes that are never written. */
#define AU_MAX 0x4000
#define AU_ZEROS 0x4000
#define TRACE_LINES 400 /* like ajm_trace (runtime_ajm.c) */
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
    /* The frame within the superframe and the bytes LibAtrac9 has left of it: LibAtrac9 does not
       keep them (docs/audiodec.md 1.3). The lock covers them, the handle, au, pcm and dump. */
    uint32_t frames, superframe_remain;
    pthread_mutex_t lock;
    FILE *dump; int dump_opened;
    uint8_t au[AU_MAX+AU_ZEROS]; /* all zeros between calls */
    int16_t pcm[256*8];
} Decoder;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
/* Atrac9InitDecoder rewrites LibAtrac9's global tables (decinit.c:31-32) that every Atrac9Decode
   reads: CreateDecoder and ClearContext take it for writing, Decode for reading. TermLibrary takes it
   for writing too, so that it returns with no Decode in flight. Order: tables, a decoder's lock, lock. */
static pthread_rwlock_t tables=PTHREAD_RWLOCK_INITIALIZER;
static Decoder *table[MAX_DECODERS];
static int initialized;
static int32_t next_id=1; /* 0 is never an id: a zeroed field is not a decoder */
static size_t objects, created, frames_decoded, errors;
enum { F_INIT, F_TERM, F_CREATE, F_DELETE, F_DECODE, F_CLEAR, FUNCTIONS };
static const char *const names[FUNCTIONS]={"sceAudiodecInitLibrary","sceAudiodecTermLibrary","sceAudiodecCreateDecoder",
    "sceAudiodecDeleteDecoder","sceAudiodecDecode","sceAudiodecClearContext"};
static size_t calls[FUNCTIONS], failures[FUNCTIONS];
static size_t trace_lines; static int drift_logged;
/* 0 off, 1 capped at TRACE_LINES, 2 every call. */
static int tracing(void) {
    const char *v=getenv("GOW3_TRACE_AUDIODEC");
    return !v || !*v || !strcmp(v,"0") ? 0 : strcmp(v,"2") ? 1 : 2;
}
/* Whether one more traced line may be printed; the first one over the cap says so instead. */
static int trace_line(void) {
    int level=tracing();
    if (level!=1) return level;
    pthread_mutex_lock(&lock); size_t n=++trace_lines; pthread_mutex_unlock(&lock);
    if (n==TRACE_LINES+1) printf("Runtime: Audiodec trace stops after %d lines; GOW3_TRACE_AUDIODEC=2 has no cap\n",TRACE_LINES);
    return n<=TRACE_LINES;
}
/* Logs the call when it is the first one or the first error of f, or when traced. text is the cause
   of an error, or what the trace shows after a success (NULL: nothing). */
static int32_t logged(int f, int32_t result, const char *args, const char *text) {
    pthread_mutex_lock(&lock);
    size_t n=++calls[f], e=result<0 ? ++failures[f] : 0; errors+=result<0;
    pthread_mutex_unlock(&lock);
    if (n==1 || e==1 || trace_line()) {
        if (result<0) printf("Runtime: %s(%s) = %#x: %s\n",names[f],args,(unsigned)result,text);
        else if (text) printf("Runtime: %s(%s) = %d; %s\n",names[f],args,result,text);
        else printf("Runtime: %s(%s) = %d\n",names[f],args,result);
    }
    return result;
}
/* why: the cause, if result is an error. */
static int32_t finish(int f, int32_t result, const char *args, const char *why) { return logged(f,result,args,result<0 ? why : NULL); }
static void check_codec(int f, uint32_t codec) {
    if (codec==CODEC_ATRAC9) return;
    fprintf(stderr,"STOP: %s: unsupported Audiodec codec %u (0x%x); only 1 (ATRAC9) is implemented\n",names[f],codec,codec);
    exit(21);
}
/* Only direct and flexible maps count, not permissions (runtime_memory.c): the game's blocks come
 * from its allocator (+0x12c430) and its thread objects. */
static int guest(const void *p, uint64_t size) { return p && runtime_memory_is_mapped((uintptr_t)p,size); }
/* A traced line: guest structures in hex, at most 256 bytes each (spec 3). */
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
/* ctrl and what it points to, read only where it is guest memory. */
static void put_ctrl(Line *l, const void *control) {
    put_hex(l,"ctrl",control,sizeof(Ctrl));
    if (!guest(control,sizeof(Ctrl))) return;
    const Ctrl *c=control;
    put_hex(l,"params",c->params,sizeof(Params)); put_hex(l,"bsi",c->bsi,BSI_SIZE);
    put_hex(l,"au",c->au,sizeof(Buffer)); put_hex(l,"pcm",c->pcm,sizeof(Buffer));
}
static void trace_before(int f, const char *args, const void *control) {
    if (!trace_line()) return;
    Line l={{0},0}; put_ctrl(&l,control);
    printf("Runtime: %s(%s) before:%s\n",names[f],args,l.text);
}
static int slot_of(int32_t id) { /* lock held */
    for (int i=0;i<MAX_DECODERS;++i) if (table[i] && table[i]->id==id) return i;
    return -1;
}
static void dispose(Decoder *d) {
    if (d->handle) Atrac9ReleaseHandle(d->handle);
    if (d->dump) fclose(d->dump);
    pthread_mutex_destroy(&d->lock); free(d);
}
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
    pthread_rwlock_wrlock(&tables); pthread_mutex_lock(&lock);
    int was=initialized; initialized=0;
    for (int i=0;i<MAX_DECODERS;++i) if (table[i] && (gone[n]=unlink_slot(i))) ++n;
    pthread_mutex_unlock(&lock); pthread_rwlock_unlock(&tables);
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
    if (tracing()) trace_before(F_CREATE,args,control);
    Ctrl c; Params p; int32_t error;
    const char *why=read_create(control,&c,&p,&error);
    if (why) return finish(F_CREATE,error,args,why);
    Decoder *d=calloc(1,sizeof *d);
    if (!d) return finish(F_CREATE,ERR_OUT_OF_MEMORY,args,"out of host memory");
    pthread_mutex_init(&d->lock,NULL); memcpy(d->config,p.config,4);
    pthread_rwlock_wrlock(&tables);
    int rejected=!(d->handle=Atrac9GetHandle()) || Atrac9InitDecoder(d->handle,d->config);
    pthread_rwlock_unlock(&tables);
    if (rejected) { dispose(d); return finish(F_CREATE,ERR_INVALID_INIT_PARAM,args,"LibAtrac9 rejects the ATRAC9 config"); }
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
    if (!tracing()) return finish(F_CREATE,id,args,NULL);
    Line l={{0},0}; put(&l,"after:"); put_ctrl(&l,control);
    return logged(F_CREATE,id,args,l.text);
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
/* Reads what Decode needs (docs/audiodec.md 1.2-1.3) into c, au and pcm; returns why it cannot. */
static const char *read_decode(const void *control, const Decoder *d, uint32_t produced, Ctrl *c, Buffer *au, Buffer *pcm, int32_t *error) {
    *error=ERR_INVALID_PTR;
    if (!guest(control,sizeof *c)) return "ctrl is not mapped guest memory";
    memcpy(c,control,sizeof *c);
    if (!guest(c->bsi,BSI_SIZE)) return "bsi is not mapped guest memory";
    if (!guest(c->au,sizeof *au)) return "AU descriptor is not mapped guest memory";
    if (!guest(c->pcm,sizeof *pcm)) return "PCM descriptor is not mapped guest memory";
    memcpy(au,c->au,sizeof *au); memcpy(pcm,c->pcm,sizeof *pcm);
    *error=ERR_INVALID_SIZE;
    if (au->size!=sizeof *au) return "AU descriptor size is not 0x18";
    if (pcm->size!=sizeof *pcm) return "PCM descriptor size is not 0x18";
    /* The game sends what is left of the superframe it read (docs/audiodec.md 1.3). */
    if (!au->bytes || au->bytes>(uint32_t)d->info.superframeSize) return "AU is empty or larger than a superframe";
    /* One frame per call, 16-bit interleaved (docs/audiodec.md 1.5): the game's capacity is one frame. */
    if (pcm->bytes<produced) return "PCM capacity is less than one frame";
    *error=ERR_INVALID_PTR;
    if (!guest(au->data,au->bytes)) return "AU data is not mapped guest memory";
    if (!guest(pcm->data,produced)) return "PCM data is not mapped guest memory";
    return NULL;
}
/* One frame (d->lock held). Writes nothing to the guest unless it succeeds; after a LibAtrac9 error
   the decoder is out of step until ClearContext, and the game deletes it anyway (docs/audiodec.md 1.6). */
static int32_t decode_frame(Decoder *d, void *control, char *why, size_t why_size, Line *trace) {
    Ctrl c; Buffer au, pcm; int32_t error;
    uint32_t produced=(uint32_t)(d->info.frameSamples*d->info.channels)*2, frames=(uint32_t)d->info.framesInSuperframe;
    const char *bad=read_decode(control,d,produced,&c,&au,&pcm,&error);
    if (bad) { snprintf(why,why_size,"%s",bad); return error; }
    /* Drift: how far the game's position in the superframe is from LibAtrac9's, accumulated since
       the superframe started; 0 when the game follows what Decode consumed (docs/audiodec.md 1.9). */
    long long drift=(long long)au.bytes-(long long)d->superframe_remain;
    if (drift) {
        pthread_mutex_lock(&lock); int first=!drift_logged; drift_logged=1; pthread_mutex_unlock(&lock);
        if (first) printf("Runtime: sceAudiodecDecode(id %d): the AU has %u bytes and LibAtrac9 has %u left in the superframe (drift %lld); later drifts only with GOW3_TRACE_AUDIODEC\n",
                          d->id,au.bytes,d->superframe_remain,drift);
    }
    memcpy(d->au,au.data,au.bytes);
    int used=0, e=Atrac9Decode(d->handle,d->au,d->pcm,&used);
    uint32_t consumed=(uint32_t)used, frame=d->frames+1;
    if (e) error=ERR_API_FAIL, snprintf(why,why_size,"LibAtrac9 rejects frame %u/%u: %#x",frame,frames,(unsigned)e);
    else if (consumed>au.bytes) error=ERR_INVALID_SIZE, snprintf(why,why_size,"the frame needs %u bytes and the AU has %u",consumed,au.bytes);
    else {
        error=0;
        d->superframe_remain=consumed<d->superframe_remain ? d->superframe_remain-consumed : 0;
        /* docs/audiodec.md 1.3 [D]: the last frame consumes what is left, padding included, so the
           game's pending bytes reach 0 and it reads the next superframe. */
        if (frame==frames) consumed=au.bytes, d->frames=0, d->superframe_remain=(uint32_t)d->info.superframeSize;
        else d->frames=frame;
        if (!d->dump_opened) {
            const char *dir=getenv("GOW3_AUDIODEC_AU_DUMP"); d->dump_opened=1;
            if (dir && *dir) {
                char path[1024]; snprintf(path,sizeof path,"%s/dec%d.au",dir,d->id);
                if (!(d->dump=fopen(path,"wb"))) printf("Runtime: GOW3_AUDIODEC_AU_DUMP: cannot write %s\n",path);
            }
        }
        if (d->dump) fwrite(d->au,1,consumed,d->dump);
        memcpy(pcm.data,d->pcm,produced);
        c.au->bytes=consumed; c.pcm->bytes=produced; c.bsi->channels=(uint32_t)d->info.channels;
        if (trace) {
            put(trace,"after:"); put_ctrl(trace,control);
            put_hex(trace,"au.data",au.data,au.bytes<16 ? au.bytes : 16); put_hex(trace,"pcm.data",pcm.data,16);
            put(trace,"; frame %u/%u, used %d, drift %lld",frame,frames,used,drift);
        }
    }
    /* Zeros again: what a frame reads past its AU never depends on the AU before, so an offline
       replay of the dump decodes the same (LibAtrac9 does not use those bits for a valid frame). */
    memset(d->au,0,au.bytes);
    return error;
}
/* Decode with a decoder taken by acquire; releases it. DeleteDecoder or TermLibrary may take it out of
   the table before this gets its locks: then it errors and writes nothing, so no Decode reaches the
   guest after TermLibrary has returned. */
static int32_t decode_acquired(Decoder *d, void *control, const char *args) {
    int traced=tracing()!=0;
    if (traced) trace_before(F_DECODE,args,control);
    char why[160]; Line after={{0},0};
    pthread_rwlock_rdlock(&tables); pthread_mutex_lock(&d->lock);
    pthread_mutex_lock(&lock); int live=d->live; pthread_mutex_unlock(&lock);
    int32_t result=live ? decode_frame(d,control,why,sizeof why,traced ? &after : NULL) : ERR_INVALID_HANDLE;
    pthread_mutex_unlock(&d->lock); pthread_rwlock_unlock(&tables);
    release(d);
    if (result) return finish(F_DECODE,result,args,live ? why : "decoder deleted or library terminated during the call");
    pthread_mutex_lock(&lock); ++frames_decoded; pthread_mutex_unlock(&lock);
    return logged(F_DECODE,0,args,traced ? after.text : NULL);
}
static ABI int32_t decode(int32_t id, void *control) {
    char args[64]; snprintf(args,sizeof args,"id %d, ctrl %p",id,control);
    Decoder *d=acquire(id);
    return d ? decode_acquired(d,control,args) : finish(F_DECODE,ERR_INVALID_HANDLE,args,"no live decoder with this id");
}
/* At the end of a loop (+0x11353f, result ignored): back to a new decoder's state, with a new
   LibAtrac9 handle, since Atrac9InitDecoder on the same one leaves its frame index (docs/audiodec.md 1.7). */
static ABI int32_t clear_context(int32_t id) {
    char args[24]; snprintf(args,sizeof args,"id %d",id);
    Decoder *d=acquire(id);
    if (!d) return finish(F_CLEAR,ERR_INVALID_HANDLE,args,"no live decoder with this id");
    void *fresh=Atrac9GetHandle();
    pthread_rwlock_wrlock(&tables);
    int failed=!fresh || Atrac9InitDecoder(fresh,d->config);
    pthread_mutex_lock(&d->lock);
    if (!failed) {
        Atrac9ReleaseHandle(d->handle); d->handle=fresh; fresh=NULL;
        d->frames=0; d->superframe_remain=(uint32_t)d->info.superframeSize;
    }
    pthread_mutex_unlock(&d->lock); pthread_rwlock_unlock(&tables);
    if (fresh) Atrac9ReleaseHandle(fresh);
    release(d);
    return failed ? finish(F_CLEAR,ERR_OUT_OF_MEMORY,args,"no new LibAtrac9 handle") : finish(F_CLEAR,0,args,NULL);
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
