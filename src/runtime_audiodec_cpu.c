/* libSceAudiodecCpu, Spec 4b (docs/specs/2026-10-14-spec-4-audiodec.md): the HEVAG effects of vag.mrg
 * (codec 0x1008, shadPS4 PR 5289) on the game's "hevag decode thread", decoded with vgmstream's HEVAG
 * (third_party/vgmstream_hevag). No source describes the library itself: the contract is the one the
 * game shows (docs/audiodec.md 2.6). The decoder's state lives in the work memory the game hands over,
 * since AudiodecCpu has no Delete and the game reuses that memory voice after voice and frees it
 * itself. The first Decode after InitDecoder consumes the VAGp header; each later one decodes one
 * frame per channel. The error values are PR 5289's (all [H]). Logged like libSceAudiodec's
 * (runtime_audiodec.c): the first call and the first error of each function, or every call with
 * GOW3_TRACE_AUDIODEC=1 with the structures before and after it (its own 400 lines, so that the hevag
 * thread does not use up the ATRAC9 trace; =2 has no cap). The game ignores the three results
 * (docs/audiodec.md 2.3), so an error also leaves in the block what stops the voice: no work memory
 * from QueryMemSize, and from Decode no PCM and flag 7 (the end of the voice). */
#include "runtime.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "third_party/vgmstream_hevag/hevag.h"
#define ERR_API_FAIL ((int32_t)0x80F70000)
#define ERR_ARG ((int32_t)0x80F70002)
#define ERR_INVALID_PARAM_SIZE ((int32_t)0x80F70004)
#define ERR_INVALID_BSI_INFO_SIZE ((int32_t)0x80F70005)
#define ERR_INVALID_AU_INFO_SIZE ((int32_t)0x80F70006)
#define ERR_INVALID_PCM_ITEM_SIZE ((int32_t)0x80F70007)
#define ERR_INVALID_RESOURCE_SIZE ((int32_t)0x80F70008)
#define ERR_INVALID_CTRL_POINTER ((int32_t)0x80F70009)
#define ERR_INVALID_PARAM_POINTER ((int32_t)0x80F7000A)
#define ERR_INVALID_BSI_INFO_POINTER ((int32_t)0x80F7000B)
#define ERR_INVALID_AU_INFO_POINTER ((int32_t)0x80F7000C)
#define ERR_INVALID_PCM_ITEM_POINTER ((int32_t)0x80F7000D)
#define ERR_INVALID_RESOURCE_POINTER ((int32_t)0x80F7000E)
#define ERR_INVALID_AU_POINTER ((int32_t)0x80F7000F)
#define ERR_INVALID_PCM_POINTER ((int32_t)0x80F70010)
#define ERR_INVALID_WORK_MEM_POINTER ((int32_t)0x80F70011)
#define ERR_INVALID_WORD_LENGTH ((int32_t)0x80F70012)
#define ERR_INVALID_AU_SIZE ((int32_t)0x80F70013)
#define ERR_INVALID_PCM_SIZE ((int32_t)0x80F70014)
#define ERR_INVALID_WORK_MEM_SIZE ((int32_t)0x80F70015)
#define CODEC_HEVAG 0x1008 /* the game's only codec (+0x11565b, +0x115804, +0x115903) */
#define PARAMS_SIZE 0x40
#define BSI_SIZE 0x30
#define BSI_FLAG 0x28 /* the only bsi byte the game reads (+0x11595b): the frame's flag; 7 ends the voice */
#define HEADER_SIZE 0x30 /* the VAGp header, at the start of the first AU */
#define TRACE_LINES 400
/* The descriptors of the game's control block (docs/audiodec.md 2.2), as in libSceAudiodec. params
   (0x40 bytes) hold the VAGp header from +4, the channels in +0x22 and 1 in +0x34. */
typedef struct { uint32_t size, reserved; void *data; uint32_t bytes, reserved2; } Buffer;
typedef struct { uint8_t *params, *bsi; Buffer *au, *pcm; } Ctrl;
/* The work memory: this, then one HevagState per channel. header: the VAGp header has not come yet. */
typedef struct { char magic[4]; uint32_t channels, header, reserved; } Work;
_Static_assert(sizeof(Buffer)==0x18 && sizeof(Ctrl)==0x20 && sizeof(Work)==0x10,"docs/audiodec.md 2.2");
static const char work_magic[4]={'H','V','A','G'};
enum { F_QUERY, F_INIT, F_DECODE, FUNCTIONS };
static const char *const names[FUNCTIONS]={"sceAudiodecCpuQueryMemSize","sceAudiodecCpuInitDecoder","sceAudiodecCpuDecode"};
/* What each function reads of ctrl: params; params and bsi; all four (PR 5289's QueryCtrl, InitCtrl,
   DecodeCtrl). The game passes the same 0x20 bytes to all three. */
static const size_t ctrl_sizes[FUNCTIONS]={8,0x10,0x20};
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static size_t calls[FUNCTIONS], failures[FUNCTIONS], decoders, frames_decoded, errors, trace_lines;
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
static void check_codec(int f, uint32_t codec) {
    if (codec==CODEC_HEVAG) return;
    fprintf(stderr,"STOP: %s: unsupported AudiodecCpu codec %u (0x%x); the game uses only 0x1008 (HEVAG)\n",names[f],codec,codec);
    exit(21);
}
/* Only direct and flexible maps count, not permissions (runtime_memory.c): all the game's buffers come
   from its allocator and the loaded vag.mrg (docs/audiodec.md 2.6). */
static int guest(const void *p, uint64_t size) { return p && runtime_memory_is_mapped((uintptr_t)p,size); }
static uint32_t u32_at(const uint8_t *p) { uint32_t v; memcpy(&v,p,4); return v; }
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
/* What a success left, for the trace: the resource and, after InitDecoder and Decode, the work memory;
   after Decode, the bsi, both descriptors and the first PCM bytes. */
static void put_after(Line *l, int f, const Ctrl *c, const Buffer *r) {
    put(l,"after:");
    if (f==F_DECODE) {
        put_hex(l,"bsi",c->bsi,BSI_SIZE); put_hex(l,"au",c->au,sizeof(Buffer)); put_hex(l,"pcm",c->pcm,sizeof(Buffer));
        put_hex(l,"pcm.data",c->pcm->data,c->pcm->bytes<16 ? c->pcm->bytes : 16);
    }
    put_hex(l,"resource",r,sizeof(Buffer));
    if (f!=F_QUERY) put_hex(l,"work",r->data,r->bytes);
}
static int32_t finish(int f, const char *args, const Ctrl *c, const Buffer *r) {
    if (!tracing()) return logged(f,0,args,NULL);
    Line l={{0},0}; put_after(&l,f,c,r);
    return logged(f,0,args,l.text);
}
#define WHY 160
#define FAIL(e,...) return snprintf(why,WHY,__VA_ARGS__), (e)
/* PR 5289's dispatcher checks, in its order (docs/audiodec.md 2.6): the pointers f reads, then the
   sizes of what they point to. On success c holds ctrl and *r the resource descriptor. */
static int32_t read_call(int f, const void *control, void *resource, Ctrl *c, Buffer **r, char *why) {
    memset(c,0,sizeof *c);
    if (!guest(control,ctrl_sizes[f])) FAIL(ERR_INVALID_CTRL_POINTER,"ctrl is not mapped guest memory");
    memcpy(c,control,ctrl_sizes[f]);
    if (!guest(c->params,PARAMS_SIZE)) FAIL(ERR_INVALID_PARAM_POINTER,"params are not mapped guest memory");
    if (f!=F_QUERY && !guest(c->bsi,BSI_SIZE)) FAIL(ERR_INVALID_BSI_INFO_POINTER,"bsi is not mapped guest memory");
    if (f==F_DECODE && !guest(c->au,sizeof(Buffer))) FAIL(ERR_INVALID_AU_INFO_POINTER,"AU descriptor is not mapped guest memory");
    if (f==F_DECODE && !guest(c->pcm,sizeof(Buffer))) FAIL(ERR_INVALID_PCM_ITEM_POINTER,"PCM descriptor is not mapped guest memory");
    if (!guest(resource,sizeof(Buffer))) FAIL(ERR_INVALID_RESOURCE_POINTER,"resource descriptor is not mapped guest memory");
    *r=resource;
    if (u32_at(c->params)!=PARAMS_SIZE) FAIL(ERR_INVALID_PARAM_SIZE,"params size is not 0x40");
    if (f!=F_QUERY && u32_at(c->bsi)!=BSI_SIZE) FAIL(ERR_INVALID_BSI_INFO_SIZE,"bsi size is not 0x30");
    if (f==F_DECODE && c->au->size!=sizeof(Buffer)) FAIL(ERR_INVALID_AU_INFO_SIZE,"AU descriptor size is not 0x18");
    if (f==F_DECODE && c->pcm->size!=sizeof(Buffer)) FAIL(ERR_INVALID_PCM_ITEM_SIZE,"PCM descriptor size is not 0x18");
    if ((*r)->size!=sizeof(Buffer)) FAIL(ERR_INVALID_RESOURCE_SIZE,"resource descriptor size is not 0x18");
    return 0;
}
/* params +0x22, the VAGp header's channels; 0 counts as 1, as the game (+0x115824) and vgmstream count it. */
static uint32_t channels_of(const uint8_t *params) { return params[0x22] ? params[0x22] : 1; }
static uint32_t work_size(uint32_t channels) { return (uint32_t)(sizeof(Work)+channels*sizeof(HevagState)); }
/* A descriptor the runtime may write after an error: mapped and of the size the game gives it (0x18). */
static Buffer *descriptor(void *p) { Buffer *b=p; return guest(b,sizeof *b) && b->size==sizeof *b ? b : NULL; }
static ABI int32_t query_mem_size(void *control, void *resource, uint32_t codec) {
    check_codec(F_QUERY,codec);
    char args[96], why[WHY]; snprintf(args,sizeof args,"ctrl %p, resource %p, codec %#x",control,resource,codec);
    if (tracing()) trace_before(F_QUERY,args,control,resource);
    Ctrl c; Buffer *r=NULL;
    int32_t error=read_call(F_QUERY,control,resource,&c,&r,why);
    if (error) {
        /* The game allocates this size when it is more than it has (+0x115795): 0, nothing, and the
           InitDecoder that follows fails on the memory. */
        Buffer *d=descriptor(resource);
        if (d) d->bytes=0;
        return logged(F_QUERY,error,args,why);
    }
    r->bytes=work_size(channels_of(c.params));
    return finish(F_QUERY,args,&c,r);
}
/* InitDecoder's checks after read_call: the work memory for the params' channels, then the params. */
static int32_t check_init(const Ctrl *c, const Buffer *r, uint32_t channels, char *why) {
    uint32_t size=work_size(channels);
    if (!r->data) FAIL(ERR_INVALID_WORK_MEM_POINTER,"work memory is null");
    if (r->bytes<size) FAIL(ERR_INVALID_WORK_MEM_SIZE,"work memory is %u bytes, %u needed for %u channels",r->bytes,size,channels);
    if (!guest(r->data,size)) FAIL(ERR_INVALID_WORK_MEM_POINTER,"work memory is not mapped guest memory");
    /* The memory is the decoder's from here: a voice that fails now must not go on decoding with the
       state the voice before left in it. */
    memset(r->data,0,sizeof work_magic);
    if (memcmp(c->params+4,"VAGp",4)) FAIL(ERR_ARG,"params do not hold a VAGp header");
    /* [H] 1 = 16-bit PCM, like ATRAC9's word length (docs/audiodec.md 2.6): the game always writes 1. */
    if (u32_at(c->params+0x34)!=1) FAIL(ERR_INVALID_WORD_LENGTH,"params word length (+0x34) is %u, not 1",u32_at(c->params+0x34));
    return 0;
}
/* After a successful QueryMemSize the game passes resource.bytes as they came from it (+0x11578d) and
   the stream's memory: a new block when that size is more than it had, else the one of the voice before. */
static ABI int32_t init_decoder(void *control, void *resource, uint32_t codec) {
    check_codec(F_INIT,codec);
    char args[96], why[WHY]; snprintf(args,sizeof args,"ctrl %p, resource %p, codec %#x",control,resource,codec);
    if (tracing()) trace_before(F_INIT,args,control,resource);
    Ctrl c; Buffer *r=NULL;
    int32_t error=read_call(F_INIT,control,resource,&c,&r,why);
    uint32_t channels=error ? 0 : channels_of(c.params);
    if (!error) error=check_init(&c,r,channels,why);
    if (error) return logged(F_INIT,error,args,why);
    Work w={{0},channels,1,0}; memcpy(w.magic,work_magic,sizeof w.magic);
    memset(r->data,0,work_size(channels)); memcpy(r->data,&w,sizeof w);
    pthread_mutex_lock(&lock); ++decoders; pthread_mutex_unlock(&lock);
    return finish(F_INIT,args,&c,r);
}
/* The header of the decoder in r's work memory, checked against the size the game says it has. */
static int32_t read_work(const Buffer *r, Work *w, char *why) {
    if (!r->data) FAIL(ERR_INVALID_WORK_MEM_POINTER,"work memory is null");
    if (r->bytes<sizeof *w) FAIL(ERR_INVALID_WORK_MEM_SIZE,"work memory is %u bytes",r->bytes);
    if (!guest(r->data,r->bytes)) FAIL(ERR_INVALID_WORK_MEM_POINTER,"work memory is not mapped guest memory");
    memcpy(w,r->data,sizeof *w);
    if (memcmp(w->magic,work_magic,sizeof w->magic)) FAIL(ERR_API_FAIL,"work memory holds no decoder (no InitDecoder succeeded on it)");
    if (!w->channels || w->channels>255) FAIL(ERR_API_FAIL,"work memory holds %u channels",w->channels);
    if (r->bytes<work_size(w->channels)) FAIL(ERR_INVALID_WORK_MEM_SIZE,"work memory is %u bytes, %u needed for %u channels",r->bytes,work_size(w->channels),w->channels);
    return 0;
}
/* [D] (docs/audiodec.md 2.6): the game drops what the first call makes, so it consumes the VAGp header
   alone, with no PCM, and no frame is lost. */
static int32_t decode_header(const Ctrl *c, const Buffer *r, Work *w, char *why) {
    const uint8_t *h=c->au->data;
    if (c->au->bytes<HEADER_SIZE) FAIL(ERR_INVALID_AU_SIZE,"the first AU has %u bytes, less than the VAGp header (0x30)",c->au->bytes);
    if (!guest(h,HEADER_SIZE)) FAIL(ERR_INVALID_AU_POINTER,"AU data are not mapped guest memory");
    if (memcmp(h,"VAGp",4)) FAIL(ERR_ARG,"the first AU does not start with a VAGp header");
    if ((h[0x1e] ? h[0x1e] : 1u)!=w->channels) FAIL(ERR_ARG,"the VAGp header has %u channels and InitDecoder had %u",h[0x1e],w->channels);
    w->header=0; memcpy(r->data,w,sizeof *w);
    c->au->bytes=HEADER_SIZE; c->pcm->bytes=0; c->bsi[BSI_FLAG]=0;
    return 0;
}
/* [D] One frame per channel per call, whatever room the AU and the PCM have: the game sends one, and so
   the flag in the bsi is one frame's. Channels are interleaved every 16 bytes in, every sample out. */
static int32_t decode_frames(const Ctrl *c, const Buffer *r, const Work *w, char *why) {
    uint32_t channels=w->channels, in=16*channels, out=HEVAG_FRAME_SAMPLES*2*channels;
    uint8_t *au=c->au->data, *pcm=c->pcm->data, *states=(uint8_t *)r->data+sizeof *w;
    if (c->au->bytes<in) FAIL(ERR_INVALID_AU_SIZE,"the AU has %u bytes, less than a frame per channel (%u)",c->au->bytes,in);
    /* More than 16 channels do not fit in the game's 0x380 (docs/audiodec.md 2.6). */
    if (c->pcm->bytes<out) FAIL(ERR_INVALID_PCM_SIZE,"PCM capacity %u is less than a frame per channel (%u)",c->pcm->bytes,out);
    if (!guest(au,in)) FAIL(ERR_INVALID_AU_POINTER,"AU data are not mapped guest memory");
    if (!guest(pcm,out)) FAIL(ERR_INVALID_PCM_POINTER,"PCM data are not mapped guest memory");
    int flag=0;
    for (uint32_t ch=0;ch<channels;++ch) {
        HevagState s; uint8_t frame[HEVAG_FRAME_BYTES]; int16_t samples[HEVAG_FRAME_SAMPLES];
        memcpy(&s,states+ch*sizeof s,sizeof s); memcpy(frame,au+16*ch,sizeof frame);
        int f=hevag_decode_frame(&s,frame,samples);
        memcpy(states+ch*sizeof s,&s,sizeof s);
        /* [D] 7 when any channel ends, else the first channel's flag. */
        if (!ch || f==HEVAG_FLAG_END) flag=f;
        for (int i=0;i<HEVAG_FRAME_SAMPLES;++i) memcpy(pcm+((size_t)i*channels+ch)*2,&samples[i],2);
    }
    c->au->bytes=in; c->pcm->bytes=out; c->bsi[BSI_FLAG]=(uint8_t)flag;
    return 0;
}
/* After an error the game, which ignores the result (+0x115911), adds pcm.bytes of samples, moves past
   au.bytes and ends the voice on flag 7 (+0x115920...+0x115970): no PCM, the AU as it came, and the end. */
static void stop_voice(void *control) {
    if (!guest(control,sizeof(Ctrl))) return;
    Ctrl c; memcpy(&c,control,sizeof c);
    Buffer *pcm=descriptor(c.pcm);
    if (pcm) pcm->bytes=0;
    if (guest(c.bsi,BSI_SIZE) && u32_at(c.bsi)==BSI_SIZE) c.bsi[BSI_FLAG]=HEVAG_FLAG_END;
}
static ABI int32_t decode(void *control, void *resource, uint32_t codec) {
    check_codec(F_DECODE,codec);
    char args[96], why[WHY]; snprintf(args,sizeof args,"ctrl %p, resource %p, codec %#x",control,resource,codec);
    if (tracing()) trace_before(F_DECODE,args,control,resource);
    Ctrl c; Buffer *r=NULL; Work w={{0},0,0,0};
    int32_t error=read_call(F_DECODE,control,resource,&c,&r,why);
    if (!error) error=read_work(r,&w,why);
    int header=!error && w.header;
    if (!error) error=header ? decode_header(&c,r,&w,why) : decode_frames(&c,r,&w,why);
    if (error) { stop_voice(control); return logged(F_DECODE,error,args,why); }
    if (!header) { pthread_mutex_lock(&lock); ++frames_decoded; pthread_mutex_unlock(&lock); }
    return finish(F_DECODE,args,&c,r);
}
/* One call site each, all three results ignored: +0x115788, +0x115811, +0x11590c. */
static const RuntimeExport exports[]={
    {"sceAudiodecCpuQueryMemSize",query_mem_size}, {"sceAudiodecCpuInitDecoder",init_decoder}, {"sceAudiodecCpuDecode",decode},
};
uintptr_t runtime_audiodec_cpu_resolve(const char *name) { return RUNTIME_LOOKUP(exports,name); }
/* decoders: voices InitDecoder set up; frames: Decode calls that made PCM (not the header's). */
void runtime_audiodec_cpu_report(void) {
    pthread_mutex_lock(&lock);
    printf("Runtime: AudiodecCpu decoders=%zu, frames=%zu, errors=%zu\n",decoders,frames_decoded,errors);
    pthread_mutex_unlock(&lock);
}
