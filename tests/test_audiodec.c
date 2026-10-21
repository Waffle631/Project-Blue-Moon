/* Spec 4a (docs/specs/2026-10-14-spec-4-audiodec.md; contract in docs/audiodec.md 1.8): libSceAudiodec
 * through runtime_resolve, with the control block laid out as the game builds it (docs/audiodec.md
 * 1.2) in guest direct memory. Includes the module to reach its decoder table (acquire/release).
 * Decoding uses the reference clips of tools/extract_audio_clips.py (game content, not versioned):
 * argv[1] or out/audiodec-clips; a missing clip prints SKIP: and its tests do not run. */
#define _GNU_SOURCE
#undef NDEBUG /* the checks are asserts; release builds define NDEBUG */
#include "../src/runtime_audiodec.c"
#include <assert.h>
#include <errno.h>
#include <sched.h>
#include <stdatomic.h>
#include <stddef.h>
#include <sys/wait.h>
#include <unistd.h>

/* The settings menu of the GPU library restarts through probe.c, which tests do not link. */
void runtime_restart(void) { abort(); }

typedef int32_t (ABI *Library)(uint32_t);
typedef int32_t (ABI *Create)(void *,uint32_t);
typedef int32_t (ABI *Handle)(int32_t);
typedef int32_t (ABI *Decode)(int32_t,void *);
typedef int32_t (ABI *Allocate)(int64_t,int64_t,uint64_t,uint64_t,int,int64_t *);
typedef int32_t (ABI *Map)(void **,uint64_t,int,int,int64_t,uint64_t);
typedef int32_t (ABI *Unmap)(void *,uint64_t);
#define GET(t,n) ((t)runtime_resolve(n,0))
#define E_API_FAIL 0x807F0000u
#define E_INVALID_INIT_PARAM 0x807F0002u
#define E_ALREADY_INITIALIZED 0x807F0003u
#define E_NOT_INITIALIZED 0x807F0005u
#define E_ALL_HANDLES_IN_USE 0x807F0007u
#define E_INVALID_PTR 0x807F0008u
#define E_INVALID_HANDLE 0x807F0009u
#define E_INVALID_WORD_LENGTH 0x807F000Cu
#define E_INVALID_SIZE 0x807F000Du
static struct { Library init,term; Create create; Handle remove,clear; Decode decode; } api;

/* The game's control block (NEXL_ATRAC9_CTRL, eboot +0x113118...+0x113167): the library gets &params. */
typedef struct { uint32_t size,pad; void *data; uint32_t bytes,pad2; } GameBuffer;
typedef struct {
    void *vtable;
    void *params,*bsi,*au,*pcm;
    GameBuffer au_desc,pcm_desc;
    uint32_t params_size,word_length; uint8_t config[4];
    uint32_t bsi_size,channels,bsi_rest[7];
} GameBlock;
_Static_assert(offsetof(GameBlock,au_desc)==0x28 && offsetof(GameBlock,pcm_desc)==0x40 && offsetof(GameBlock,params_size)==0x58 &&
               offsetof(GameBlock,bsi_size)==0x64 && sizeof(GameBlock)==0x88,"docs/audiodec.md 1.2");
/* The five ATRAC9 configurations of the dump (spec 2.2) and their channels. */
static const struct { uint32_t config,channels; } configs[]={
    {0xfe740ff0,2},{0xfe7409f0,2},{0xfe740bf0,2},{0xfe700bf0,1},{0xfe7004f0,1}};
#define GUEST_BYTES 0x10000
#define HOLE_BYTES 0x4000
#define EDGE_BYTES 0x1000
#define SLOT_BYTES 0x1000
#define SLOTS 40
/* hole: an unmapped page; edge: the bytes right before it, kept out of the block arena. stream_input
   and stream_pcm stand for the game's "at9 input" buffer (0x800) and its PCM buffer (channels x 512).
   slots: guest memory for the threads, SLOT_BYTES each (a block, an input and a PCM buffer). */
static uint8_t *guest_base,*guest_next,*edge,*hole,*stream_input,*stream_pcm,*slots;

static void fill(GameBlock *b,uint32_t config) {
    memset(b,0,sizeof *b);
    b->params=&b->params_size; b->bsi=&b->bsi_size; b->au=&b->au_desc; b->pcm=&b->pcm_desc;
    b->au_desc.size=b->pcm_desc.size=0x18; b->params_size=0xC; b->word_length=1; b->bsi_size=0x24;
    for (int i=0;i<4;++i) b->config[i]=(uint8_t)(config>>(24-8*i));
}
/* A new block in guest memory, aligned like the game's (0x40). */
static GameBlock *block(uint32_t config) {
    GameBlock *b=(GameBlock *)guest_next; guest_next+=0xC0;
    assert(guest_next<=edge); fill(b,config); return b;
}
static void setup(void) {
    runtime_start(1);
    api.init=GET(Library,"VjhsmxpcezI#libSceAudiodec"); api.term=GET(Library,"h5jSB2QIDV0#libSceAudiodec");
    api.create=GET(Create,"O3f1sLMWRvs#libSceAudiodec"); api.remove=GET(Handle,"Tp+ZEy69mLk#libSceAudiodec");
    api.decode=GET(Decode,"KHXHMDLkILw#libSceAudiodec"); api.clear=GET(Handle,"6Vf9WTLDoss#libSceAudiodec");
    Allocate alloc=GET(Allocate,"rTXw65xmLIA#libkernel");
    Map map=GET(Map,"L-Q3LEjIbgA#libkernel");
    Unmap unmap=GET(Unmap,"cQke9UuBQOk#libkernel");
    int64_t physical=-1; void *address=NULL;
    assert(alloc && map && unmap && alloc(0,536870912,GUEST_BYTES,HOLE_BYTES,0,&physical)==0);
    assert(map(&address,GUEST_BYTES,3,0,physical,HOLE_BYTES)==0);
    guest_base=address; physical=-1; address=NULL;
    /* Mapped before the hole is made, so that it cannot land there. */
    assert(alloc(0,536870912,SLOT_BYTES*SLOTS,HOLE_BYTES,0,&physical)==0 && map(&address,SLOT_BYTES*SLOTS,3,0,physical,HOLE_BYTES)==0);
    slots=address; stream_input=guest_base; stream_pcm=guest_base+0x800; guest_next=guest_base+0xC00;
    hole=guest_base+GUEST_BYTES-HOLE_BYTES; edge=hole-EDGE_BYTES;
    assert(unmap(hole,HOLE_BYTES)==0 && runtime_memory_is_mapped((uintptr_t)guest_base,8) && !runtime_memory_is_mapped((uintptr_t)hole,8));
}
static void resolves_all_six(void) {
    assert(api.init && api.term && api.create && api.remove && api.decode && api.clear);
}
/* Runs f with stdout sent to a temporary file; keeps the start of what it printed in captured and
   returns the number of lines. */
static char captured[1<<20];
static int printed_lines(void (*f)(void)) {
    fflush(stdout); FILE *tmp=tmpfile(); assert(tmp);
    int saved=dup(1); assert(saved>=0 && dup2(fileno(tmp),1)>=0);
    f(); fflush(stdout); dup2(saved,1); close(saved);
    rewind(tmp); int lines=0,c; size_t n=0;
    while ((c=fgetc(tmp))!=EOF) { lines+=c=='\n'; if (n<sizeof captured-1) captured[n++]=(char)c; }
    captured[n]=0; fclose(tmp); return lines;
}
/* Runs f in a child with stderr captured; it must stop with exit 21 and print STOP: and text. */
static void stops(void (*f)(void),const char *text) {
    int fd[2]; assert(pipe(fd)==0);
    fflush(stdout); fflush(stderr);
    pid_t child=fork(); assert(child>=0);
    if (!child) { dup2(fd[1],2); close(fd[0]); f(); _exit(0); }
    close(fd[1]);
    char out[512]={0}; size_t n=0; ssize_t r;
    while (n<sizeof out-1 && (r=read(fd[0],out+n,sizeof out-1-n))>0) n+=(size_t)r;
    close(fd[0]);
    int status=0; assert(waitpid(child,&status,0)==child);
    if (!WIFEXITED(status) || WEXITSTATUS(status)!=21 || !strstr(out,"STOP:") || !strstr(out,text)) {
        fprintf(stderr,"expected STOP with \"%s\", got status %#x: %s\n",text,status,out); abort();
    }
}
static void init_unknown(void) { api.init(0x1003); }
static void create_unknown(void) { api.create(&block(configs[0].config)->params,2); }
static void term_unknown(void) { api.term(0); }
/* Spec 3: an unsupported codec stops with its number, never silence. */
static void unknown_codec_stops(void) {
    stops(init_unknown,"codec 4099 (0x1003)");
    stops(create_unknown,"codec 2 (0x2)");
    stops(term_unknown,"codec 0 (0x0)");
}
/* docs/audiodec.md 1.4: CreateDecoder writes bsi +4 = channels (the game reads it before the first
   Decode, +0x112926) and leaves the rest of the block as the game left it. */
static void create_returns_nonneg_id_with_each_of_five_configs(void) {
    for (size_t i=0;i<sizeof configs/sizeof *configs;++i) {
        GameBlock *b=block(configs[i].config),expected;
        fill(&expected,configs[i].config); expected.channels=configs[i].channels;
        int32_t id=api.create(&b->params,1);
        assert(id>=0);
        expected.params=b->params; expected.bsi=b->bsi; expected.au=b->au; expected.pcm=b->pcm;
        assert(!memcmp(b,&expected,sizeof expected));
        assert(api.remove(id)==0);
    }
}
static void create_writes_channels_to_bsi(void) {
    GameBlock *mono=block(0xfe700bf0),*stereo=block(0xfe740ff0);
    int32_t a=api.create(&mono->params,1),b=api.create(&stereo->params,1);
    assert(a>=0 && b>=0 && mono->channels==1 && stereo->channels==2);
    assert(api.remove(a)==0 && api.remove(b)==0);
}
/* An invalid configuration is an error with no id: no STOP, nothing written, nothing kept. */
static void expect_create_error(GameBlock *b,uint32_t error) {
    size_t before=objects;
    assert((uint32_t)api.create(&b->params,1)==error && b->channels==0 && objects==before);
}
static void create_bad_config_returns_error(void) {
    expect_create_error(block(0x00740ff0),E_INVALID_INIT_PARAM); /* no 0xFE marker */
    expect_create_error(block(0xfe750ff0),E_INVALID_INIT_PARAM); /* LibAtrac9 rejects the validation bit */
    GameBlock *b=block(0xfe740ff0); b->params_size=0x10; expect_create_error(b,E_INVALID_SIZE);
    b=block(0xfe740ff0); b->word_length=2; expect_create_error(b,E_INVALID_WORD_LENGTH);
    b=block(0xfe740ff0); b->bsi_size=0x20; expect_create_error(b,E_INVALID_SIZE);
}
static void create_null_or_unmapped_ctrl_returns_error(void) {
    GameBlock host; fill(&host,0xfe740ff0); /* valid, but not guest memory */
    assert((uint32_t)api.create(NULL,1)==E_INVALID_PTR);
    assert((uint32_t)api.create(&host.params,1)==E_INVALID_PTR && host.channels==0);
    GameBlock *b=block(0xfe740ff0); b->params=NULL; expect_create_error(b,E_INVALID_PTR);
    b=block(0xfe740ff0); b->params=&host.params_size; expect_create_error(b,E_INVALID_PTR);
    b=block(0xfe740ff0); b->bsi=NULL; expect_create_error(b,E_INVALID_PTR);
    b=block(0xfe740ff0); b->bsi=&host.bsi_size; expect_create_error(b,E_INVALID_PTR);
    /* A ctrl or bsi that crosses into the unmapped page is rejected before it is read. */
    void **edge=(void **)(hole-0x10); edge[0]=&b->params_size; edge[1]=&b->bsi_size;
    assert((uint32_t)api.create(edge,1)==E_INVALID_PTR);
    b=block(0xfe740ff0); b->bsi=hole-0x10; expect_create_error(b,E_INVALID_PTR);
}
static void stale_id_errors(void) {
    GameBlock *b=block(0xfe740ff0);
    int32_t id=api.create(&b->params,1);
    assert(id>=0 && api.remove(id)==0);
    assert((uint32_t)api.remove(id)==E_INVALID_HANDLE);
    assert((uint32_t)api.clear(id)==E_INVALID_HANDLE);
    assert((uint32_t)api.decode(id,&b->params)==E_INVALID_HANDLE);
    const int32_t never[]={-1,0,0x7fffff,INT32_MAX}; /* -1: the game's "no decoder"; ids start at 1 */
    for (size_t i=0;i<sizeof never/sizeof *never;++i)
        assert((uint32_t)api.remove(never[i])==E_INVALID_HANDLE && (uint32_t)api.clear(never[i])==E_INVALID_HANDLE &&
               (uint32_t)api.decode(never[i],&b->params)==E_INVALID_HANDLE);
}
static void ids_never_reused(void) {
    GameBlock *b=block(0xfe740ff0);
    int32_t previous=0;
    for (int i=0;i<8;++i) {
        int32_t id=api.create(&b->params,1);
        assert(id>previous && api.remove(id)==0); previous=id;
    }
    int32_t x=api.create(&b->params,1),y=api.create(&b->params,1);
    assert(x>previous && y>x && api.remove(x)==0 && api.remove(y)==0);
}
static void table_full_errors_without_id(void) {
    GameBlock *b=block(0xfe740ff0);
    int32_t ids[MAX_DECODERS];
    for (int i=0;i<MAX_DECODERS;++i) assert((ids[i]=api.create(&b->params,1))>=0);
    b->channels=0; expect_create_error(b,E_ALL_HANDLES_IN_USE);
    assert(api.remove(ids[7])==0 && (ids[7]=api.create(&b->params,1))>=0);
    for (int i=0;i<MAX_DECODERS;++i) assert(api.remove(ids[i])==0);
}
/* A Decode in flight holds a reference: DeleteDecoder makes the id stale at once but frees the
   decoder only when the last reference goes (plan, global constraints). */
static void delete_while_acquired_defers_free(void) {
    GameBlock *b=block(0xfe740ff0);
    int32_t id=api.create(&b->params,1);
    size_t before=objects;
    Decoder *d=acquire(id);
    assert(d && d->id==id && d->refs==1 && d->live);
    assert(api.remove(id)==0 && objects==before && !d->live && d->handle);
    assert(!acquire(id) && (uint32_t)api.remove(id)==E_INVALID_HANDLE);
    release(d);
    assert(objects==before-1);
}
static void term_while_acquired_defers_free(void) {
    GameBlock *b=block(0xfe740ff0);
    int32_t id=api.create(&b->params,1),other=api.create(&b->params,1);
    size_t before=objects;
    Decoder *d=acquire(id);
    assert(d && other>=0 && api.term(1)==0);
    assert(objects==before-1 && !d->live && d->handle && !acquire(id));
    release(d);
    assert(objects==before-2 && api.init(1)==0);
}
/* docs/audiodec.md 1.8: TermLibrary frees everything, ids from before are errors, and another
   InitLibrary works (TermLibrary has a second call site, +0x10e864). */
static void term_invalidates_ids_and_init_again_creates(void) {
    GameBlock *b=block(0xfe740ff0);
    int32_t a=api.create(&b->params,1),c=api.create(&b->params,1);
    assert(a>=0 && c>a && api.term(1)==0 && objects==0);
    assert((uint32_t)api.remove(a)==E_INVALID_HANDLE && (uint32_t)api.decode(c,&b->params)==E_INVALID_HANDLE);
    b->channels=0; expect_create_error(b,E_NOT_INITIALIZED);
    assert((uint32_t)api.term(1)==E_NOT_INITIALIZED);
    assert(api.init(1)==0 && (uint32_t)api.init(1)==E_ALREADY_INITIALIZED);
    int32_t d=api.create(&b->params,1);
    assert(d>c && b->channels==2 && objects==1 && api.remove(d)==0);
}
/* Spec 3: without GOW3_TRACE_AUDIODEC, only the first call and the first error of each function
   are logged; with it, every call, and CreateDecoder adds a line with the structures before it. */
static GameBlock *log_block,*log_bad;
static void create_good(void) { assert(api.remove(api.create(&log_block->params,1))==0); }
static void create_bad(void) { assert(api.create(&log_bad->params,1)<0); }
static void logs_first_call_and_first_error_only(void) {
    log_block=block(0xfe740ff0); log_bad=block(0x00740ff0);
    unsetenv("GOW3_TRACE_AUDIODEC");
    memset(calls,0,sizeof calls); memset(failures,0,sizeof failures);
    assert(printed_lines(create_good)==2); /* first Create, first Delete */
    assert(printed_lines(create_good)==0);
    assert(printed_lines(create_bad)==1);
    assert(printed_lines(create_bad)==0);
    setenv("GOW3_TRACE_AUDIODEC","1",1); trace_lines=0;
    assert(printed_lines(create_good)==3 && printed_lines(create_bad)==2);
    unsetenv("GOW3_TRACE_AUDIODEC");
}
/* A success line carries no error cause: a traced success is followed by what the trace shows. */
static GameBlock *lifecycle_block; static int32_t lifecycle_id;
static void lifecycle(void) {
    assert(api.term(1)==0 && api.init(1)==0);
    lifecycle_id=api.create(&lifecycle_block->params,1); assert(lifecycle_id>0 && api.remove(lifecycle_id)==0);
}
static void success_lines_carry_no_cause(void) {
    lifecycle_block=block(0xfe740ff0);
    memset(calls,0,sizeof calls); memset(failures,0,sizeof failures);
    assert(printed_lines(lifecycle)==4);
    char line[96]; snprintf(line,sizeof line,"sceAudiodecDeleteDecoder(id %d) = 0\n",lifecycle_id);
    assert(strstr(captured,"sceAudiodecTermLibrary(codec 1) = 0\n") && strstr(captured,"sceAudiodecInitLibrary(codec 1) = 0\n") && strstr(captured,line));
}

/* ---- Decode (task 4): the reference clips, the game's decode routine and LibAtrac9 straight ---- */
typedef struct { const char *name; uint32_t config,channels; uint8_t *file; const uint8_t *data; uint32_t data_size,block; } Clip;
static Clip clips[]={{"bgm_typ",0xfe740ff0,2,0,0,0,0},{"bgm_rare",0xfe7409f0,2,0,0,0,0},{"vse_stereo",0xfe740bf0,2,0,0,0,0},
                     {"vse_mono",0xfe700bf0,1,0,0,0,0},{"vse_mono_small",0xfe7004f0,1,0,0,0,0}};
#define CLIPS (sizeof clips/sizeof *clips)
#define CLIP_SLACK 0x2000 /* zeros after the file: LibAtrac9 reads past the last frame (docs/audiodec.md 1.3) */
static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1]<<8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p+2)<<16; }
static void load_clips(const char *dir) {
    for (size_t i=0;i<CLIPS;++i) {
        Clip *c=&clips[i]; char path[512]; snprintf(path,sizeof path,"%s/%s.at9",dir,c->name);
        FILE *f=fopen(path,"rb");
        if (!f) { printf("SKIP: %s missing (python3 tools/extract_audio_clips.py --image Tsukihime/CUSA47999/image --out %s)\n",path,dir); continue; }
        fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
        assert(n>0x48 && (c->file=calloc(1,(size_t)n+CLIP_SLACK)) && fread(c->file,1,(size_t)n,f)==(size_t)n); fclose(f);
        /* The RIFF of spec 2.2: channels +0x16, block +0x20, config +0x40; data in its own chunk. */
        assert(!memcmp(c->file,"RIFF",4) && le16(c->file+0x16)==c->channels && le32(c->file+0x40)==__builtin_bswap32(c->config));
        c->block=le16(c->file+0x20);
        for (long at=12;at+8<=n;at+=8+(long)le32(c->file+at+4)+(le32(c->file+at+4)&1))
            if (!memcmp(c->file+at,"data",4)) { c->data=c->file+at+8; c->data_size=le32(c->file+at+4); }
        assert(c->data && c->data+c->data_size<=c->file+n && c->data_size>=c->block*4);
    }
}
static const Clip *need(const char *name, const char *test) {
    for (size_t i=0;i<CLIPS;++i) if (!strcmp(clips[i].name,name)) {
        if (clips[i].data) return &clips[i];
        printf("SKIP: %s: needs %s.at9\n",test,name); return NULL;
    }
    abort();
}
static uint32_t superframes(const Clip *c) { return c->data_size/c->block; }
static uint32_t frame_bytes(const Clip *c) { return 256*c->channels*2; }
static void set_au(GameBlock *b, void *data, uint32_t bytes) { b->au_desc.data=data; b->au_desc.bytes=bytes; }
static void set_pcm(GameBlock *b, void *data, uint32_t bytes) { b->pcm_desc.data=data; b->pcm_desc.bytes=bytes; }
/* The game's side of a stream (docs/audiodec.md 1.3): one superframe at a time in its input buffer,
   the AU at buffer + block - pending, pending minus what Decode says it consumed. */
typedef struct { const Clip *clip; GameBlock *b; int32_t id; uint32_t next,pending; uint8_t *input,*pcm; } Stream;
static void open_stream_at(Stream *s, const Clip *c, GameBlock *b, uint8_t *input, uint8_t *pcm) {
    s->clip=c; s->b=b; s->input=input; s->pcm=pcm; s->next=s->pending=0;
    s->id=api.create(&b->params,1); assert(s->id>0);
}
static void open_stream(Stream *s, const Clip *c) { open_stream_at(s,c,block(c->config),stream_input,stream_pcm); }
static GameBlock *slot_block(int i, uint32_t config) { GameBlock *b=(GameBlock *)(slots+(size_t)i*SLOT_BYTES); fill(b,config); return b; }
static void open_slot(Stream *s, const Clip *c, int i) { uint8_t *at=slots+(size_t)i*SLOT_BYTES; open_stream_at(s,c,slot_block(i,c->config),at+0x100,at+0x900); }
/* The loop-back of +0x113553/+0x113579: reading resumes at a superframe with nothing pending. */
static void seek_stream(Stream *s, uint32_t superframe) { s->next=superframe; s->pending=0; }
static int32_t game_decode(Stream *s) {
    const Clip *c=s->clip; GameBlock *b=s->b;
    if (!s->pending) { assert(s->next<superframes(c)); memcpy(s->input,c->data+(size_t)s->next++*c->block,c->block); s->pending=c->block; }
    set_au(b,s->input+c->block-s->pending,s->pending); set_pcm(b,s->pcm,c->channels*512);
    int32_t r=api.decode(s->id,&b->params);
    if (!r) s->pending-=b->au_desc.bytes;
    return r;
}
/* LibAtrac9 straight on the clip: the next frame starts where the last one ended, and a superframe's
   padding is skipped after its last frame. */
typedef struct { void *handle; const Clip *clip; uint32_t superframe; int frame,frames; const uint8_t *at; } Reference;
static void reference_open(Reference *r, const Clip *c, uint32_t superframe) {
    Atrac9CodecInfo info;
    assert((r->handle=Atrac9GetHandle()) && !Atrac9InitDecoder(r->handle,c->file+0x40) && !Atrac9GetCodecInfo(r->handle,&info));
    assert(info.superframeSize==(int)c->block && info.channels==(int)c->channels && info.frameSamples==256);
    r->clip=c; r->superframe=superframe; r->frame=0; r->frames=info.framesInSuperframe; r->at=c->data+(size_t)superframe*c->block;
}
static uint32_t reference_frame(Reference *r, int16_t *pcm) {
    int used=0; assert(!Atrac9Decode(r->handle,r->at,pcm,&used) && used>0); r->at+=used;
    if (++r->frame==r->frames) r->frame=0, r->at=r->clip->data+(size_t)++r->superframe*r->clip->block;
    return (uint32_t)used;
}
static int all(const uint8_t *p, uint8_t value, size_t n) { for (size_t i=0;i<n;++i) if (p[i]!=value) return 0; return 1; }
static void decode_matches_libatrac9_sample_by_sample(void) {
    for (size_t i=0;i<CLIPS;++i) {
        const Clip *c=&clips[i]; if (!c->data) continue;
        Stream s; open_stream(&s,c); Reference r; reference_open(&r,c,0);
        int16_t expected[256*2];
        for (uint32_t n=0;n<superframes(c)*(uint32_t)r.frames;++n) {
            uint32_t pending=s.pending ? s.pending : c->block,last=r.frame==r.frames-1,used=reference_frame(&r,expected);
            assert(game_decode(&s)==0 && s.b->channels==c->channels);
            assert(s.b->pcm_desc.bytes==frame_bytes(c) && !memcmp(stream_pcm,expected,frame_bytes(c)));
            assert(s.b->au_desc.bytes==(last ? pending : used));
        }
        assert(api.remove(s.id)==0); Atrac9ReleaseHandle(r.handle);
    }
}
/* docs/audiodec.md 1.3 [D]: the last frame of a superframe consumes what is left, padding included,
   so the game's pending bytes reach 0 exactly at each superframe's end. */
static void consumed_over_superframe_equals_block_size(void) {
    for (size_t i=0;i<CLIPS;++i) {
        const Clip *c=&clips[i]; if (!c->data) continue;
        Stream s; open_stream(&s,c);
        for (uint32_t sf=0;sf<superframes(c);++sf) {
            uint32_t sum=0;
            for (int k=0;k<4;++k) { assert(game_decode(&s)==0); sum+=s.b->au_desc.bytes; assert((s.pending==0)==(k==3)); }
            assert(sum==c->block);
        }
        assert(api.remove(s.id)==0);
    }
}
/* A decoder with the first superframe of c in stream_input as its AU and stream_pcm filled with 0xA5. */
static int32_t first_frame_ready(const Clip *c, GameBlock **b) {
    *b=block(c->config); int32_t id=api.create(&(*b)->params,1); assert(id>0);
    memcpy(stream_input,c->data,c->block); set_au(*b,stream_input,c->block);
    memset(stream_pcm,0xA5,0x400); set_pcm(*b,stream_pcm,frame_bytes(c));
    return id;
}
/* Decode returns error and changes nothing: not the control block, not the PCM buffer. */
static void expect_decode_error(int32_t id, void *control, GameBlock *b, uint32_t error) {
    GameBlock before=*b; uint8_t pcm[0x400]; memcpy(pcm,stream_pcm,sizeof pcm);
    uint32_t r=(uint32_t)api.decode(id,control);
    if (r!=error) { fprintf(stderr,"expected Decode error %#x, got %#x\n",error,r); abort(); }
    assert(!memcmp(b,&before,sizeof before) && !memcmp(pcm,stream_pcm,sizeof pcm));
}
/* After errors that LibAtrac9 never saw, the first frame still decodes as from a new decoder. Decode
   writes bsi +4 = channels again (docs/audiodec.md 1.4). */
static void expect_first_frame(const Clip *c, int32_t id, GameBlock *b, uint32_t capacity) {
    Reference r; reference_open(&r,c,0); int16_t expected[256*2]; uint32_t used=reference_frame(&r,expected);
    set_au(b,stream_input,c->block); set_pcm(b,stream_pcm,capacity); b->channels=0;
    assert(api.decode(id,&b->params)==0 && b->au_desc.bytes==used && b->pcm_desc.bytes==frame_bytes(c) && b->channels==c->channels);
    assert(!memcmp(stream_pcm,expected,frame_bytes(c)));
    Atrac9ReleaseHandle(r.handle);
}
static void output_capacity_too_small_errors_without_writing(void) {
    const Clip *c=need("vse_mono_small",__func__); if (!c) return;
    GameBlock *b; int32_t id=first_frame_ready(c,&b);
    b->pcm_desc.bytes=frame_bytes(c)-2; expect_decode_error(id,&b->params,b,E_INVALID_SIZE);
    /* More room than a frame: one frame is written and the rest of the buffer is left alone. */
    expect_first_frame(c,id,b,0x400);
    assert(all(stream_pcm+frame_bytes(c),0xA5,0x400-frame_bytes(c)));
    assert(api.remove(id)==0);
}
/* An AU shorter than its frame is an error (LibAtrac9 still decodes it from the zeros after the copy:
   the frame's size is in its header), and the runtime reads no byte past it: the AU ends where
   the unmapped page starts. A whole superframe there decodes, although LibAtrac9 reads ahead of its
   last frame (docs/audiodec.md 1.3): the runtime decodes from a copy with zeros after it. */
static void short_au_errors_without_overread(void) {
    const Clip *c=need("vse_stereo",__func__); if (!c) return;
    Reference r; reference_open(&r,c,0); int16_t expected[256*2];
    uint32_t used=reference_frame(&r,expected);
    GameBlock *b; int32_t id=first_frame_ready(c,&b);
    uint8_t *au=hole-(used-1); memcpy(au,c->data,used-1); set_au(b,au,used-1);
    GameBlock before=*b;
    uint32_t e=(uint32_t)api.decode(id,&b->params);
    assert(e==E_INVALID_SIZE && !memcmp(b,&before,sizeof before) && all(stream_pcm,0xA5,0x400));
    assert(api.clear(id)==0); Atrac9ReleaseHandle(r.handle);
    /* The first superframe without padding: its last frame ends at the block's last byte. */
    uint32_t sf=0;
    for (;;++sf) {
        assert(sf<superframes(c)); reference_open(&r,c,sf); uint32_t sum=0;
        for (int k=0;k<r.frames;++k) sum+=reference_frame(&r,expected);
        Atrac9ReleaseHandle(r.handle);
        if (sum==c->block) break;
    }
    au=hole-c->block; memcpy(au,c->data+(size_t)sf*c->block,c->block); reference_open(&r,c,sf);
    for (uint32_t pending=c->block,k=0;k<4;++k) {
        reference_frame(&r,expected); set_au(b,au+c->block-pending,pending); set_pcm(b,stream_pcm,frame_bytes(c));
        assert(api.decode(id,&b->params)==0 && !memcmp(stream_pcm,expected,frame_bytes(c)));
        pending-=b->au_desc.bytes;
    }
    assert(api.remove(id)==0); Atrac9ReleaseHandle(r.handle);
}
/* A frame LibAtrac9 rejects is an error with nothing written; after ClearContext the stream decodes
   from a superframe as from a new decoder. */
static void corrupt_frame_errors_and_clear_context_recovers(void) {
    const Clip *c=need("vse_mono_small",__func__); if (!c) return;
    Stream s; open_stream(&s,c);
    memset(stream_input,0xFF,c->block); set_au(s.b,stream_input,c->block);
    memset(stream_pcm,0xA5,0x400); set_pcm(s.b,stream_pcm,frame_bytes(c));
    expect_decode_error(s.id,&s.b->params,s.b,E_API_FAIL);
    assert(api.clear(s.id)==0);
    Reference r; reference_open(&r,c,0); int16_t expected[256];
    for (int n=0;n<8;++n) { reference_frame(&r,expected); assert(game_decode(&s)==0 && !memcmp(stream_pcm,expected,frame_bytes(c))); }
    assert(api.remove(s.id)==0); Atrac9ReleaseHandle(r.handle);
}
/* docs/audiodec.md 1.7: the game loops back with ClearContext after 1, 2 or 3 frames of a superframe
   and goes on at the start of another one; the next Decode must work (LibAtrac9 otherwise answers
   0x82000000) and give what a new decoder gives there. */
static void clear_context_mid_superframe_then_next_superframe_decodes(void) {
    const char *const vse[]={"vse_stereo","vse_mono","vse_mono_small"};
    for (size_t i=0;i<3;++i) {
        const Clip *c=need(vse[i],__func__); if (!c) continue;
        for (int k=1;k<=3;++k) {
            Stream s; open_stream(&s,c);
            for (int n=0;n<k;++n) assert(game_decode(&s)==0);
            assert(api.clear(s.id)==0);
            seek_stream(&s,1); Reference r; reference_open(&r,c,1); int16_t expected[256*2];
            for (int n=0;n<8;++n) {
                reference_frame(&r,expected);
                int32_t e=game_decode(&s);
                if (e) { fprintf(stderr,"%s: ClearContext after %d frames, Decode %d after it: %#x\n",c->name,k,n,(unsigned)e); abort(); }
                assert(!memcmp(stream_pcm,expected,frame_bytes(c)));
            }
            assert(api.remove(s.id)==0); Atrac9ReleaseHandle(r.handle);
        }
    }
}
static void unmapped_au_or_pcm_pointer_errors(void) {
    const Clip *c=need("vse_mono_small",__func__); if (!c) return;
    GameBlock *b; int32_t id=first_frame_ready(c,&b);
    GameBlock host; fill(&host,c->config); host.au_desc=b->au_desc; host.pcm_desc=b->pcm_desc; /* valid, but not guest memory */
    uint8_t host_data[0x400]; memcpy(host_data,c->data,c->block);
    expect_decode_error(id,NULL,b,E_INVALID_PTR);
    expect_decode_error(id,&host.params,b,E_INVALID_PTR);
    void **at_edge=(void **)(hole-0x10); at_edge[0]=b->params; at_edge[1]=b->bsi; /* ctrl crosses into the hole */
    expect_decode_error(id,at_edge,b,E_INVALID_PTR);
    void *au=b->au,*pcm=b->pcm,*bsi=b->bsi;
    b->au=NULL; expect_decode_error(id,&b->params,b,E_INVALID_PTR);
    b->au=&host.au_desc; expect_decode_error(id,&b->params,b,E_INVALID_PTR);
    b->au=au; b->pcm=NULL; expect_decode_error(id,&b->params,b,E_INVALID_PTR);
    b->pcm=&host.pcm_desc; expect_decode_error(id,&b->params,b,E_INVALID_PTR);
    b->pcm=pcm; b->bsi=NULL; expect_decode_error(id,&b->params,b,E_INVALID_PTR);
    b->bsi=&host.bsi_size; expect_decode_error(id,&b->params,b,E_INVALID_PTR);
    b->bsi=bsi;
    set_au(b,NULL,c->block); expect_decode_error(id,&b->params,b,E_INVALID_PTR);
    set_au(b,host_data,c->block); expect_decode_error(id,&b->params,b,E_INVALID_PTR);
    set_au(b,hole-c->block/2,c->block); expect_decode_error(id,&b->params,b,E_INVALID_PTR);
    set_au(b,stream_input,c->block);
    set_pcm(b,NULL,frame_bytes(c)); expect_decode_error(id,&b->params,b,E_INVALID_PTR);
    set_pcm(b,host_data,frame_bytes(c)); expect_decode_error(id,&b->params,b,E_INVALID_PTR);
    set_pcm(b,hole-frame_bytes(c)+2,frame_bytes(c)); expect_decode_error(id,&b->params,b,E_INVALID_PTR);
    expect_first_frame(c,id,b,frame_bytes(c));
    assert(api.remove(id)==0);
}
/* The descriptors the game writes (docs/audiodec.md 1.2): size 0x18, an AU of 1 to block bytes. */
static void bad_descriptor_sizes_error(void) {
    const Clip *c=need("vse_mono_small",__func__); if (!c) return;
    GameBlock *b; int32_t id=first_frame_ready(c,&b);
    b->au_desc.size=0x10; expect_decode_error(id,&b->params,b,E_INVALID_SIZE); b->au_desc.size=0x18;
    b->pcm_desc.size=0x20; expect_decode_error(id,&b->params,b,E_INVALID_SIZE); b->pcm_desc.size=0x18;
    set_au(b,stream_input,0); expect_decode_error(id,&b->params,b,E_INVALID_SIZE);
    set_au(b,stream_input,c->block+1); expect_decode_error(id,&b->params,b,E_INVALID_SIZE);
    expect_first_frame(c,id,b,frame_bytes(c));
    assert(api.remove(id)==0);
}
/* ---- Golden blocks (plan task 7): the control block as the game built it, not as fill() lays it out ----
   tests/data/audiodec_ctrl_<kind>.bin comes from the GOW3_TRACE_AUDIODEC=2 run of 2026-10-21
   (docs/audiodec.md 1.10): u64 ctrl address in the game, then four 0x80-byte copies of the block from
   ctrl (+0x08...+0x88), before and after CreateDecoder and before and after the decoder's first Decode.
   Structures only: the AU is the first superframe of the clip with the same configuration, so after
   Decode au.bytes is that clip's and not the game's track's (0x83 and 0x25 in the run). */
#define GOLDEN 0x80
typedef struct { uint64_t ctrl; uint8_t record[4][GOLDEN]; } Golden;
static void golden_load(Golden *g, const char *kind) {
    char path[128]; snprintf(path,sizeof path,"tests/data/audiodec_ctrl_%s.bin",kind);
    FILE *f=fopen(path,"rb"); assert(f && fread(g,1,sizeof *g,f)==sizeof *g && fgetc(f)==EOF); fclose(f);
}
/* Record i with its pointers moved: ctrl's four into block b, au.data and pcm.data (+0x28, +0x40 from
   ctrl) to stream_input and stream_pcm. */
static void golden_place(uint8_t *out, const Golden *g, int i, GameBlock *b) {
    uint8_t *ctrl=(uint8_t *)&b->params; memcpy(out,g->record[i],GOLDEN);
    for (size_t at=0;at<0x20;at+=8) {
        uint64_t p; memcpy(&p,out+at,8); assert(p-g->ctrl<GOLDEN);
        uint8_t *q=ctrl+(p-g->ctrl); memcpy(out+at,&q,8);
    }
    const struct { size_t at; uint8_t *to; } data[]={{0x28,stream_input},{0x40,stream_pcm}};
    for (size_t k=0;k<2;++k) { uint64_t p; memcpy(&p,out+data[k].at,8); uint8_t *q=p ? data[k].to : NULL; memcpy(out+data[k].at,&q,8); }
}
static void golden_block(const char *kind, const char *clip) {
    Golden g; golden_load(&g,kind);
    GameBlock *b=block(0); uint8_t *ctrl=(uint8_t *)&b->params,expected[GOLDEN];
    golden_place(ctrl,&g,0,b);
    int32_t id=api.create(ctrl,1); assert(id>0);
    golden_place(expected,&g,1,b); assert(!memcmp(ctrl,expected,GOLDEN));
    char test[48]; snprintf(test,sizeof test,"golden_block(%s)",kind);
    const Clip *c=need(clip,test);
    if (c) {
        golden_place(ctrl,&g,2,b);
        assert(!memcmp(b->config,c->file+0x40,4) && b->au_desc.bytes==c->block && b->pcm_desc.bytes==c->channels*512);
        memcpy(stream_input,c->data,c->block); memset(stream_pcm,0xA5,0x400);
        Reference r; reference_open(&r,c,0); int16_t pcm[256*2]; uint32_t used=reference_frame(&r,pcm);
        assert(api.decode(id,ctrl)==0);
        golden_place(expected,&g,3,b); memcpy(expected+0x30,&used,4); /* au.bytes */
        assert(!memcmp(ctrl,expected,GOLDEN) && !memcmp(stream_pcm,pcm,frame_bytes(c)));
        Atrac9ReleaseHandle(r.handle);
    }
    assert(api.remove(id)==0);
}
static void golden_blocks_from_the_game(void) { golden_block("stereo","bgm_typ"); golden_block("mono","vse_mono"); }
/* ---- The tracer (spec 3, plan task 4) ---- */
static Stream trace_stream;
static void trace_one(void) { assert(game_decode(&trace_stream)==0); }
static void trace_250(void) { for (int n=0;n<250;++n) trace_one(); }
static void hexof(char *out, const void *p, size_t n) { for (size_t i=0;i<n;++i) sprintf(out+2*i,"%02x",((const uint8_t *)p)[i]); }
/* The game sends one byte less than LibAtrac9 has left: the AU still starts at the frame. */
static void trace_short_by_one(void) {
    Stream *s=&trace_stream; GameBlock *b=s->b;
    set_au(b,stream_input+s->clip->block-s->pending,s->pending-1); set_pcm(b,stream_pcm,frame_bytes(s->clip));
    assert(api.decode(s->id,&b->params)==0); s->pending-=b->au_desc.bytes;
}
static void trace_dumps_structures_and_drift(void) {
    const Clip *c=need("vse_mono_small",__func__); if (!c) return;
    open_stream(&trace_stream,c); GameBlock *b=trace_stream.b;
    setenv("GOW3_TRACE_AUDIODEC","1",1); trace_lines=0;
    assert(printed_lines(trace_one)==2 && strstr(captured,"sceAudiodecDecode(id ") && strstr(captured,"before:") && strstr(captured,"after:"));
    /* Before: the AU descriptor as the game filled it; after: what Decode wrote back. */
    GameBuffer au={0x18,0,stream_input,c->block,0}; char text[160]; hexof(text,&au,sizeof au);
    assert(strstr(captured,text));
    hexof(text,&b->au_desc,sizeof b->au_desc); assert(strstr(captured,text));
    hexof(text,c->data,16); assert(strstr(captured,text)); /* the first bytes of the AU */
    hexof(text,stream_pcm,16); assert(strstr(captured,text)); /* and of the PCM */
    assert(strstr(captured,"frame 1/4") && strstr(captured,"drift 0"));
    /* A drift is logged once without the variable (the capped trace could miss it), always with it. */
    unsetenv("GOW3_TRACE_AUDIODEC"); drift_logged=0;
    assert(printed_lines(trace_short_by_one)==1 && strstr(captured,"drift -1"));
    assert(printed_lines(trace_short_by_one)==0);
    setenv("GOW3_TRACE_AUDIODEC","1",1);
    assert(printed_lines(trace_short_by_one)==2 && strstr(captured,"drift -1") && strstr(captured,"frame 4/4"));
    unsetenv("GOW3_TRACE_AUDIODEC");
    assert(api.remove(trace_stream.id)==0);
}
/* GOW3_TRACE_AUDIODEC=1 stops after 400 lines (as ajm_trace in runtime_ajm.c) and says so; =2 has no cap. */
static void trace_caps_at_400_lines_unless_level_2(void) {
    const Clip *c=need("vse_mono",__func__); if (!c) return;
    open_stream(&trace_stream,c);
    trace_one(); /* not the first Decode, which is logged apart from the cap */
    setenv("GOW3_TRACE_AUDIODEC","1",1); trace_lines=0;
    assert(printed_lines(trace_250)==401 && strstr(captured,"GOW3_TRACE_AUDIODEC=2"));
    setenv("GOW3_TRACE_AUDIODEC","2",1);
    assert(printed_lines(trace_250)==500);
    unsetenv("GOW3_TRACE_AUDIODEC");
    assert(api.remove(trace_stream.id)==0);
}
/* GOW3_AUDIODEC_AU_DUMP=<dir>: <dir>/dec<id>.au holds the stream as the game fed it, what each
   Decode consumed one after another; for a whole superframe, the clip's bytes. */
static void au_dump_writes_the_consumed_stream(void) {
    const Clip *c=need("vse_mono_small",__func__); if (!c) return;
    char dir[]="/tmp/audiodec-au-XXXXXX"; assert(mkdtemp(dir));
    setenv("GOW3_AUDIODEC_AU_DUMP",dir,1);
    Stream s; open_stream(&s,c);
    for (int n=0;n<12;++n) assert(game_decode(&s)==0);
    assert(api.remove(s.id)==0); unsetenv("GOW3_AUDIODEC_AU_DUMP");
    char path[64]; snprintf(path,sizeof path,"%s/dec%d.au",dir,s.id);
    FILE *f=fopen(path,"rb"); assert(f);
    uint8_t got[0x1000]; size_t n=fread(got,1,sizeof got,f); fclose(f);
    assert(n==3*c->block && !memcmp(got,c->data,n));
    assert(!unlink(path) && !rmdir(dir));
}
/* ---- Counters (spec 3): the Audiodec line of runtime_report ---- */
typedef struct { size_t decoders,frames,errors; } Counts;
static void report(void) { runtime_report(); }
static Counts counts(void) {
    Counts n={0,0,0}; printed_lines(report);
    const char *at=strstr(captured,"Runtime: Audiodec decoders=");
    assert(at && sscanf(at,"Runtime: Audiodec decoders=%zu, frames=%zu, errors=%zu",&n.decoders,&n.frames,&n.errors)==3);
    return n;
}
/* decoders: created; frames: decoded; errors: failed calls of any function. */
static void report_counts(void) {
    const Clip *c=need("vse_mono_small",__func__); if (!c) return;
    Counts before=counts(); Stream s; open_stream(&s,c);
    for (int n=0;n<6;++n) assert(game_decode(&s)==0);
    assert(api.remove(s.id)==0 && (uint32_t)api.remove(s.id)==E_INVALID_HANDLE && (uint32_t)api.decode(s.id,&s.b->params)==E_INVALID_HANDLE);
    Counts after=counts();
    assert(after.decoders==before.decoders+1 && after.frames==before.frames+6 && after.errors==before.errors+2);
}
/* ---- Threads and restart (task 5): the game's 32 "at9 threads" (docs/audiodec.md 1.1) ---- */
/* What LibAtrac9 straight gives for a whole clip: each frame's PCM and the AU bytes Decode reports. Made
   before any thread starts: Atrac9InitDecoder rewrites the global tables outside the runtime's lock. */
typedef struct { int16_t *pcm; uint32_t *consumed,frames; } Decoded;
static Decoded decoded[CLIPS];
static const Decoded *decoded_clip(size_t i) {
    Decoded *e=&decoded[i]; const Clip *c=&clips[i];
    if (e->pcm) return e;
    Reference r; reference_open(&r,c,0); e->frames=superframes(c)*(uint32_t)r.frames;
    assert((e->pcm=malloc((size_t)e->frames*frame_bytes(c))) && (e->consumed=malloc(e->frames*sizeof *e->consumed)));
    for (uint32_t n=0,pending=c->block;n<e->frames;++n) {
        int last=r.frame==r.frames-1; uint32_t used=reference_frame(&r,e->pcm+(size_t)n*frame_bytes(c)/2);
        e->consumed[n]=last ? pending : used; pending=last ? c->block : pending-used;
    }
    Atrac9ReleaseHandle(r.handle); return e;
}
/* One "at9 thread": its clip from the start, every frame checked, once or (loop) round and round as the
   game loops (ClearContext, docs/audiodec.md 1.7) until an error; after an error, three more calls. */
typedef struct { Stream s; size_t clip; int loop; atomic_uint frames; uint32_t wrong; int32_t error; int errors_after; } Worker;
static void *work(void *arg) {
    Worker *w=arg; const Decoded *e=&decoded[w->clip]; uint32_t bytes=frame_bytes(w->s.clip);
    for (uint32_t n=0;n<e->frames || w->loop;++n) {
        if (n==e->frames) { if ((w->error=api.clear(w->s.id))) break; seek_stream(&w->s,0); n=0; }
        int32_t r=game_decode(&w->s);
        if (r) { w->error=r; break; }
        if (w->s.b->au_desc.bytes!=e->consumed[n] || memcmp(w->s.pcm,e->pcm+(size_t)n*bytes/2,bytes)) ++w->wrong;
        atomic_fetch_add(&w->frames,1);
    }
    if (w->error) for (int k=0;k<3;++k) w->errors_after+=game_decode(&w->s)==w->error;
    return NULL;
}
#define AT9_THREADS 32
static Worker workers[AT9_THREADS]; static pthread_t threads[AT9_THREADS];
/* The i-th of the clips that loaded, round and round; CLIPS if none did. */
static size_t clip_for(int i) {
    size_t loaded[CLIPS],n=0;
    for (size_t k=0;k<CLIPS;++k) if (clips[k].data) loaded[n++]=k;
    return n ? loaded[(size_t)i%n] : CLIPS;
}
/* n workers in slots 0..n-1, each on clip_for(i), all opened before any starts. */
static void start_workers(int n, int loop) {
    for (int i=0;i<n;++i) {
        Worker *w=&workers[i]; memset(w,0,sizeof *w); w->clip=clip_for(i); w->loop=loop;
        decoded_clip(w->clip); open_slot(&w->s,&clips[w->clip],i);
    }
    for (int i=0;i<n;++i) assert(!pthread_create(&threads[i],NULL,work,&workers[i]));
}
static void join_workers(int n) { for (int i=0;i<n;++i) assert(!pthread_join(threads[i],NULL)); }
/* Each worker decoded its whole clip right; then its decoder goes. */
static void expect_whole_clips(int n) {
    for (int i=0;i<n;++i) {
        Worker *w=&workers[i];
        if (w->error || w->wrong || w->frames!=decoded[w->clip].frames) {
            fprintf(stderr,"thread %d (%s): error %#x, %u wrong of %u frames\n",i,clips[w->clip].name,(unsigned)w->error,w->wrong,(unsigned)w->frames); abort();
        }
        assert(api.remove(w->s.id)==0);
    }
}
/* Spec 8: 32 threads, one decoder each, all at once: every frame as LibAtrac9 gives it alone, and the
   counters add up. */
static void many_threads_decode_independent_streams(void) {
    if (clip_for(0)==CLIPS) { printf("SKIP: %s: no clip\n",__func__); return; }
    Counts before=counts(); size_t total=0;
    start_workers(AT9_THREADS,0); join_workers(AT9_THREADS);
    for (int i=0;i<AT9_THREADS;++i) total+=workers[i].frames;
    expect_whole_clips(AT9_THREADS);
    Counts after=counts();
    assert(after.decoders==before.decoders+AT9_THREADS && after.frames==before.frames+total && after.errors==before.errors);
}
static atomic_int busy; static unsigned rounds;
/* Creates and clears decoders of every configuration, in a loop, until busy is 0. */
static void *create_and_clear(void *arg) {
    (void)arg;
    for (unsigned n=0;atomic_load(&busy);rounds=++n) {
        GameBlock *b=slot_block(AT9_THREADS,configs[n%(sizeof configs/sizeof *configs)].config); int32_t id=api.create(&b->params,1);
        assert(id>0 && api.clear(id)==0 && api.clear(id)==0 && api.remove(id)==0);
    }
    return NULL;
}
/* CreateDecoder and ClearContext rewrite LibAtrac9's global tables (Atrac9InitDecoder) that every Decode
   reads: with a thread doing them in a loop, 8 decoders of different configurations give the PCM they give
   alone. The tables get the same values each time, so on x86 a missing lock leaves the PCM as it is; the
   race is ThreadSanitizer's to see (spec 8). */
static void create_and_clear_while_others_decode_is_bit_exact(void) {
    if (clip_for(0)==CLIPS) { printf("SKIP: %s: no clip\n",__func__); return; }
    start_workers(8,0); atomic_store(&busy,1); rounds=0;
    pthread_t t; assert(!pthread_create(&t,NULL,create_and_clear,NULL));
    join_workers(8); atomic_store(&busy,0); assert(!pthread_join(t,NULL));
    expect_whole_clips(8);
    assert(rounds>0);
}
static Stream inflight; static int32_t inflight_result,term_result; static atomic_int term_returned;
static void *decode_inflight(void *arg) { (void)arg; inflight_result=game_decode(&inflight); return NULL; }
static void *term_in_thread(void *arg) { (void)arg; term_result=api.term(1); atomic_store(&term_returned,1); return NULL; }
/* Until a Decode in another thread holds the tables for reading: it is past acquire. */
static void wait_for_reader(void) {
    for (int r;(r=pthread_rwlock_trywrlock(&tables))!=EBUSY;) { assert(!r); pthread_rwlock_unlock(&tables); sched_yield(); }
}
static size_t live_objects(void) { pthread_mutex_lock(&lock); size_t n=objects; pthread_mutex_unlock(&lock); return n; }
/* inflight on c in slot 0, its PCM buffer filled with 0xA5, and the frame it decodes first. */
static Decoder *inflight_decoder(const Clip *c, int16_t *first) {
    open_slot(&inflight,c,0); memset(inflight.pcm,0xA5,0x400);
    Reference r; reference_open(&r,c,0); reference_frame(&r,first); Atrac9ReleaseHandle(r.handle);
    Decoder *d=acquire(inflight.id); release(d); /* allocated while it is in the table */
    return d;
}
/* docs/audiodec.md 1.8: TermLibrary waits for a Decode in flight, which ends as if TermLibrary came after
   it; then everything is freed and the old id is an error. */
static void term_waits_for_inflight_decode_and_later_calls_error(void) {
    const Clip *c=need("vse_mono_small",__func__); if (!c) return;
    int16_t first[256]; Decoder *d=inflight_decoder(c,first); size_t before=live_objects();
    pthread_mutex_lock(&d->lock); /* the Decode waits for it, holding the tables */
    pthread_t a,t; assert(!pthread_create(&a,NULL,decode_inflight,NULL)); wait_for_reader();
    atomic_store(&term_returned,0); assert(!pthread_create(&t,NULL,term_in_thread,NULL));
    usleep(100000); int early=atomic_load(&term_returned); size_t during=live_objects();
    pthread_mutex_unlock(&d->lock); assert(!pthread_join(a,NULL) && !pthread_join(t,NULL));
    if (early) { fprintf(stderr,"TermLibrary returned while a Decode was in flight\n"); abort(); }
    assert(during==before && inflight_result==0 && !memcmp(inflight.pcm,first,frame_bytes(c)));
    assert(term_result==0 && live_objects()==0);
    GameBlock *b=inflight.b;
    assert((uint32_t)api.decode(inflight.id,&b->params)==E_INVALID_HANDLE && (uint32_t)api.clear(inflight.id)==E_INVALID_HANDLE &&
           (uint32_t)api.remove(inflight.id)==E_INVALID_HANDLE && (uint32_t)api.create(&b->params,1)==E_NOT_INITIALIZED);
    assert(api.init(1)==0);
}
/* A Decode that took its decoder just before TermLibrary (acquire done, not its locks yet) gets the locks
   after TermLibrary has returned: it errors and writes nothing, so nothing reaches the guest after it. */
static void decode_taken_before_term_writes_nothing_after_it(void) {
    const Clip *c=need("vse_mono_small",__func__); if (!c) return;
    GameBlock *b; int32_t id=first_frame_ready(c,&b); Decoder *d=acquire(id);
    assert(d && api.term(1)==0 && live_objects()==1);
    GameBlock saved=*b; uint8_t pcm[0x400]; memcpy(pcm,stream_pcm,sizeof pcm);
    uint32_t e=(uint32_t)decode_acquired(d,&b->params,"id taken before TermLibrary");
    if (e!=E_INVALID_HANDLE) { fprintf(stderr,"Decode after TermLibrary: %#x\n",e); abort(); }
    assert(!memcmp(b,&saved,sizeof saved) && !memcmp(pcm,stream_pcm,sizeof pcm) && live_objects()==0);
    assert(api.init(1)==0);
}
/* DeleteDecoder while a Decode of the same id waits for the decoder's lock: the id is stale at once, the
   decoder is freed only when that Decode lets it go, and the Decode errors without writing. */
static void delete_during_decode_does_not_free_under_it(void) {
    const Clip *c=need("vse_mono_small",__func__); if (!c) return;
    int16_t first[256]; Decoder *d=inflight_decoder(c,first); size_t before=live_objects();
    pthread_mutex_lock(&d->lock);
    pthread_t a; assert(!pthread_create(&a,NULL,decode_inflight,NULL)); wait_for_reader();
    int32_t removed=api.remove(inflight.id); size_t during=live_objects(); int handle=d->handle!=NULL;
    pthread_mutex_unlock(&d->lock); assert(!pthread_join(a,NULL));
    assert(removed==0 && during==before && handle && !acquire(inflight.id));
    if ((uint32_t)inflight_result!=E_INVALID_HANDLE) { fprintf(stderr,"Decode after DeleteDecoder: %#x\n",(unsigned)inflight_result); abort(); }
    assert(all(inflight.pcm,0xA5,0x400) && live_objects()==before-1);
}
/* docs/audiodec.md 1.8: TermLibrary while 8 threads decode in a loop. Each gets its frames right and then
   only INVALID_HANDLE, none writes PCM after TermLibrary has returned, and InitLibrary and CreateDecoder
   work again: the new decoder decodes from the start. */
static void term_init_create_again_works(void) {
    if (clip_for(0)==CLIPS) { printf("SKIP: %s: no clip\n",__func__); return; }
    size_t clip=clip_for(0); const Clip *c=&clips[clip]; int32_t last=0;
    start_workers(8,1);
    for (int i=0;i<8;++i) { while (atomic_load(&workers[i].frames)<16) sched_yield(); if (workers[i].s.id>last) last=workers[i].s.id; }
    assert(api.term(1)==0);
    static uint8_t pcm[8][0x400]; for (int i=0;i<8;++i) memcpy(pcm[i],workers[i].s.pcm,sizeof pcm[i]);
    join_workers(8);
    for (int i=0;i<8;++i) {
        Worker *w=&workers[i];
        if (memcmp(pcm[i],w->s.pcm,sizeof pcm[i])) { fprintf(stderr,"thread %d wrote PCM after TermLibrary returned\n",i); abort(); }
        if (w->wrong || (uint32_t)w->error!=E_INVALID_HANDLE || w->errors_after!=3) {
            fprintf(stderr,"thread %d: %u wrong of %u frames, error %#x, then %d of 3 the same\n",i,w->wrong,(unsigned)w->frames,(unsigned)w->error,w->errors_after); abort();
        }
    }
    assert(api.init(1)==0);
    Stream s; open_slot(&s,c,0); assert(s.id>last);
    const Decoded *e=&decoded[clip];
    for (uint32_t n=0;n<e->frames && n<64;++n) assert(game_decode(&s)==0 && s.b->au_desc.bytes==e->consumed[n] && !memcmp(s.pcm,e->pcm+(size_t)n*frame_bytes(c)/2,frame_bytes(c)));
    assert(api.remove(s.id)==0);
}

/* ---- libSceAudiodecCpu (task 6): explicit errors until HEVAG exists (tasks 10-12; docs/audiodec.md 2) ---- */
typedef int32_t (ABI *CpuCall)(void *,void *,uint32_t);
#define E_CPU_API_FAIL 0x80F70000u
#define HEVAG 0x1008
static struct { CpuCall query,init,decode; } cpu;
/* The game's HEVAG control block (eboot +0x1155b0...+0x11577f), 0xe0 bytes from its allocator
   (+0x1154ce): the library gets &params and &resource, the work memory descriptor. */
typedef struct {
    void *vtable;
    void *params,*bsi,*au,*pcm;
    GameBuffer au_desc,pcm_desc;
    uint32_t params_size; uint8_t params_rest[0x3c];
    uint32_t bsi_size; uint8_t bsi_rest[0x2c];
    GameBuffer resource;
} HevagBlock;
_Static_assert(offsetof(HevagBlock,au_desc)==0x28 && offsetof(HevagBlock,pcm_desc)==0x40 && offsetof(HevagBlock,params_size)==0x58 &&
               offsetof(HevagBlock,bsi_size)==0x98 && offsetof(HevagBlock,resource)==0xc8 && sizeof(HevagBlock)==0xe0,"docs/audiodec.md 2.2");
/* As the game fills it before QueryMemSize: params +4 hold the VAGp header without its reserved bytes. */
static void hevag_fill(HevagBlock *b, const uint8_t *vag) {
    memset(b,0,sizeof *b);
    b->params=&b->params_size; b->bsi=&b->bsi_size; b->au=&b->au_desc; b->pcm=&b->pcm_desc;
    b->au_desc.size=b->pcm_desc.size=b->resource.size=0x18; b->params_size=0x40; b->bsi_size=0x30;
    memcpy(b->params_rest,vag,8); memcpy(b->params_rest+0xc,vag+0xc,8); b->params_rest[0x1e]=vag[0x1e];
    memcpy(b->params_rest+0x20,vag+0x20,0x10); b->params_rest[0x30]=1;
}
static HevagBlock *hevag_block(const uint8_t *vag) {
    HevagBlock *b=(HevagBlock *)guest_next; guest_next+=0x100;
    assert(guest_next<=edge); hevag_fill(b,vag); return b;
}
/* An effect as vag.mrg holds them (spec 2.2): VAGp 0x00020001 (HEVAG), 48 kHz, frames of 16 bytes
   per channel; the data are not decoded yet. */
static uint8_t *hevag_effect(uint32_t frames, uint8_t channels) {
    uint8_t *v=guest_next; guest_next+=0x100; uint32_t size=frames*16*channels;
    assert(guest_next<=edge && 0x30+size<=0x100);
    memset(v,0,0x100); memcpy(v,"VAGp\0\2\0\1",8);
    v[0xc]=(uint8_t)(size>>24); v[0xd]=(uint8_t)(size>>16); v[0xe]=(uint8_t)(size>>8); v[0xf]=(uint8_t)size;
    v[0x12]=0xbb; v[0x13]=0x80; v[0x1e]=channels; memcpy(v+0x20,"moon_se_test",12);
    for (uint32_t i=0;i<size;++i) v[0x30+i]=(uint8_t)(i%16==1 ? 0 : 0x11*(i%16));
    return v;
}
static uint32_t effect_size(const uint8_t *vag) { return 0x30+((uint32_t)vag[0xc]<<24 | (uint32_t)vag[0xd]<<16 | (uint32_t)vag[0xe]<<8 | vag[0xf]); }
static void cpu_resolves_all_three(void) {
    cpu.query=GET(CpuCall,"ktD2w3D4G2U#libSceAudiodecCpu"); cpu.init=GET(CpuCall,"hdFsxo3MFu8#libSceAudiodecCpu");
    cpu.decode=GET(CpuCall,"lSVTiWV5wLc#libSceAudiodecCpu");
    assert(cpu.query && cpu.init && cpu.decode);
}
/* Logged like libSceAudiodec (spec 3): without GOW3_TRACE_AUDIODEC, the first call of each function
   (its first error too); with it, a line with the structures before each call and one with its result.
   Runs before any other AudiodecCpu call of this process. */
static HevagBlock *cpu_log_block;
static void cpu_each(void) {
    HevagBlock *b=cpu_log_block;
    cpu.query(&b->params,&b->resource,HEVAG); cpu.init(&b->params,&b->resource,HEVAG); cpu.decode(&b->params,&b->resource,HEVAG);
}
static void cpu_logs_first_call_only_unless_traced(void) {
    uint8_t *vag=hevag_effect(1,1); HevagBlock *b=cpu_log_block=hevag_block(vag);
    b->au_desc.data=vag; b->au_desc.bytes=0x40; b->pcm_desc.data=stream_pcm; b->pcm_desc.bytes=56;
    unsetenv("GOW3_TRACE_AUDIODEC");
    assert(printed_lines(cpu_each)==3 && strstr(captured,"sceAudiodecCpuQueryMemSize(") && strstr(captured,"sceAudiodecCpuInitDecoder(") &&
           strstr(captured,"sceAudiodecCpuDecode(") && strstr(captured,"codec 0x1008) = 0x80f70000: ") && strstr(captured,"Spec 4b"));
    assert(printed_lines(cpu_each)==0);
    setenv("GOW3_TRACE_AUDIODEC","1",1);
    assert(printed_lines(cpu_each)==6 && strstr(captured,"sceAudiodecCpuDecode(") && strstr(captured,"before:"));
    char text[0x100]; hexof(text,&b->params_size,0x40); assert(strstr(captured,text)); /* params */
    hexof(text,&b->bsi_size,0x30); assert(strstr(captured,text));
    hexof(text,&b->resource,0x18); assert(strstr(captured,text));
    hexof(text,&b->pcm_desc,0x18); assert(strstr(captured,text));
    /* The first bytes of the AU, the VAGp header (params +4 hold most of it too). */
    char au_data[0x120]="au.data "; hexof(au_data+8,vag,16); assert(strstr(captured,au_data));
    unsetenv("GOW3_TRACE_AUDIODEC");
}
/* Its own 400-line cap: the hevag thread's errors do not use up libSceAudiodec's trace, nor the
   other way round; =2 has no cap. */
static void cpu_decode_250(void) { for (int n=0;n<250;++n) cpu.decode(&cpu_log_block->params,&cpu_log_block->resource,HEVAG); }
static void cpu_trace_has_its_own_cap(void) {
    unsetenv("GOW3_TRACE_AUDIODEC");
    cpu.decode(&cpu_log_block->params,&cpu_log_block->resource,HEVAG); /* not the first Decode, logged apart from the cap */
    setenv("GOW3_TRACE_AUDIODEC","1",1); trace_lines=TRACE_LINES+1;
    int lines=printed_lines(cpu_decode_250);
    assert(lines>300 && lines<=TRACE_LINES+1 && strstr(captured,"AudiodecCpu trace stops after 400 lines"));
    assert(printed_lines(cpu_decode_250)==0);
    setenv("GOW3_TRACE_AUDIODEC","2",1);
    assert(printed_lines(cpu_decode_250)==500);
    unsetenv("GOW3_TRACE_AUDIODEC"); trace_lines=0;
}
static void cpu_query_unknown(void) { HevagBlock *b=hevag_block(hevag_effect(1,1)); cpu.query(&b->params,&b->resource,0x1001); }
static void cpu_init_unknown(void) { HevagBlock *b=hevag_block(hevag_effect(1,1)); cpu.init(&b->params,&b->resource,1); }
static void cpu_decode_unknown(void) { HevagBlock *b=hevag_block(hevag_effect(1,1)); cpu.decode(&b->params,&b->resource,0); }
/* Spec 3: the game passes only 0x1008 (+0x11565b, +0x115804, +0x115903); any other codec stops. */
static void cpu_unknown_codec_stops(void) {
    stops(cpu_query_unknown,"codec 4097 (0x1001)");
    stops(cpu_init_unknown,"codec 1 (0x1)");
    stops(cpu_decode_unknown,"codec 0 (0x0)");
}
/* The game ignores all three results (docs/audiodec.md 2.3), so the error says what it leaves: no work
   memory (QueryMemSize writes size 0), and from Decode no PCM with the AU consumed as it came. */
static void cpu_calls_answer_api_fail(void) {
    uint8_t *vag=hevag_effect(2,1); HevagBlock *b=hevag_block(vag),expected;
    b->resource.bytes=0xdeadbeef;
    assert((uint32_t)cpu.query(&b->params,&b->resource,HEVAG)==E_CPU_API_FAIL && b->resource.bytes==0);
    memcpy(&expected,b,sizeof expected);
    assert((uint32_t)cpu.init(&b->params,&b->resource,HEVAG)==E_CPU_API_FAIL && !memcmp(b,&expected,sizeof expected));
    memset(stream_pcm,0x5a,0x380);
    b->au_desc.data=vag; b->au_desc.bytes=0x40; b->pcm_desc.data=stream_pcm; b->pcm_desc.bytes=56;
    memcpy(&expected,b,sizeof expected); expected.pcm_desc.bytes=0;
    assert((uint32_t)cpu.decode(&b->params,&b->resource,HEVAG)==E_CPU_API_FAIL && !memcmp(b,&expected,sizeof expected) && all(stream_pcm,0x5a,0x380));
}
/* Null or unmapped pointers, and descriptors that are not 0x18 bytes: the same error, nothing written. */
static void cpu_bad_pointers_write_nothing(void) {
    uint8_t *vag=hevag_effect(1,1); HevagBlock *b=hevag_block(vag);
    for (int f=0;f<3;++f) {
        CpuCall call=f==0 ? cpu.query : f==1 ? cpu.init : cpu.decode;
        assert((uint32_t)call(NULL,NULL,HEVAG)==E_CPU_API_FAIL && (uint32_t)call(hole,hole,HEVAG)==E_CPU_API_FAIL);
    }
    /* A descriptor whose size field is mapped and whose bytes field is not. */
    GameBuffer *tail=(GameBuffer *)(hole-8); tail->size=0x18;
    assert((uint32_t)cpu.query(&b->params,tail,HEVAG)==E_CPU_API_FAIL);
    b->pcm=tail; assert((uint32_t)cpu.decode(&b->params,&b->resource,HEVAG)==E_CPU_API_FAIL);
    b->pcm=hole; assert((uint32_t)cpu.decode(&b->params,&b->resource,HEVAG)==E_CPU_API_FAIL);
    b->pcm=&b->pcm_desc; b->resource.size=b->pcm_desc.size=0x20; b->resource.bytes=7; b->pcm_desc.bytes=56;
    assert((uint32_t)cpu.query(&b->params,&b->resource,HEVAG)==E_CPU_API_FAIL && b->resource.bytes==7);
    assert((uint32_t)cpu.decode(&b->params,&b->resource,HEVAG)==E_CPU_API_FAIL && b->pcm_desc.bytes==56);
    /* Decode's answer needs only the PCM descriptor: no PCM, whatever params hold. */
    b->params=hole; b->resource.size=b->pcm_desc.size=0x18;
    assert((uint32_t)cpu.decode(&b->params,&b->resource,HEVAG)==E_CPU_API_FAIL && b->pcm_desc.bytes==0);
}
/* The game's HEVAG voice: prepare (+0x1155b0), decode (+0x115880) and the mixer loop that calls it
   (+0x115f50). None of them looks at what the library returns. */
typedef struct { HevagBlock *b; const uint8_t *vag; uint32_t remaining,offset,calls,channels; } Voice;
static void voice_prepare(Voice *v, HevagBlock *b, const uint8_t *vag) {
    memset(v,0,sizeof *v); v->b=b; v->vag=vag; v->remaining=effect_size(vag);
    cpu.query(&b->params,&b->resource,HEVAG);
    /* The game allocates resource.bytes when they exceed what it has (+0x115795): none so far. */
    assert(!b->resource.bytes); b->resource.data=NULL;
    cpu.init(&b->params,&b->resource,HEVAG);
    v->channels=vag[0x1e] ? vag[0x1e] : 1;
}
/* *samples: the samples asked for (at least 28, one frame), then the samples made; *end: the voice is over. */
static void voice_decode(Voice *v, int16_t *out, uint32_t *samples, int *end) {
    HevagBlock *b=v->b;
    if (!v->remaining) { *samples=0; *end=1; return; }
    uint32_t want=*samples>28 ? *samples : 28, pcm=v->channels*want*2;
    b->au_desc.data=(void *)(v->vag+v->offset); b->au_desc.bytes=v->channels*16+(v->calls ? 0 : 0x30);
    b->pcm_desc.data=out; b->pcm_desc.bytes=pcm<0x380 ? pcm : 0x380;
    cpu.decode(&b->params,&b->resource,HEVAG);
    *samples=v->calls ? b->pcm_desc.bytes/(v->channels*2) : 0; /* the first call is the header's */
    v->offset+=b->au_desc.bytes; v->remaining-=b->au_desc.bytes; *end=0;
    if (b->bsi_rest[0x24]==7) *samples=0, *end=1; /* bsi +0x28, the frame's flags: 7 ends the voice */
    ++v->calls;
}
/* Decodes until it has frames samples or the voice ends; returns the samples it mixed. */
static uint32_t voice_mix(Voice *v, int16_t *out, uint32_t frames, int *end) {
    uint32_t mixed=0,samples=0; int64_t need=frames; *end=0;
    while (need>0) {
        if (v->calls>10000) { fprintf(stderr,"the game's mixer loop never ends: %u Decode calls\n",v->calls); abort(); }
        voice_decode(v,out+(size_t)mixed*v->channels,&samples,end); mixed+=samples;
        if (*end) break;
        need-=samples;
    }
    return mixed;
}
/* With the errors, an effect plays in silence to the end of its data and the voice stops: with
   au.bytes 0 the loop would never end, and with pcm.bytes untouched it would mix 28 samples of
   whatever its buffer holds on each call. */
static void cpu_errors_end_the_voice_in_silence(void) {
    for (uint8_t channels=1;channels<=2;++channels) {
        uint8_t *vag=hevag_effect(6,channels); HevagBlock *b=hevag_block(vag); Voice v;
        voice_prepare(&v,b,vag);
        memset(stream_pcm,0x5a,0x380);
        int end=0; uint32_t mixed=voice_mix(&v,(int16_t *)stream_pcm,256,&end);
        assert(end && !mixed && v.calls==6 && v.offset==effect_size(vag) && all(stream_pcm,0x5a,0x380));
    }
}
/* The AudiodecCpu line of runtime_report: nothing is created or decoded until tasks 10-12. */
static Counts cpu_counts(void) {
    Counts n={0,0,0}; printed_lines(report);
    const char *at=strstr(captured,"Runtime: AudiodecCpu decoders=");
    assert(at && sscanf(at,"Runtime: AudiodecCpu decoders=%zu, frames=%zu, errors=%zu",&n.decoders,&n.frames,&n.errors)==3);
    return n;
}
static void cpu_report_counts(void) {
    Counts before=cpu_counts(); cpu_each();
    Counts after=cpu_counts();
    assert(!after.decoders && !after.frames && after.errors==before.errors+3);
}
int main(int argc, char **argv) {
    setup();
    resolves_all_six();
    assert(api.init(1)==0);
    unknown_codec_stops();
    create_returns_nonneg_id_with_each_of_five_configs();
    create_writes_channels_to_bsi();
    create_bad_config_returns_error();
    create_null_or_unmapped_ctrl_returns_error();
    stale_id_errors();
    ids_never_reused();
    table_full_errors_without_id();
    delete_while_acquired_defers_free();
    term_while_acquired_defers_free();
    term_invalidates_ids_and_init_again_creates();
    logs_first_call_and_first_error_only();
    success_lines_carry_no_cause();
    load_clips(argc>1 ? argv[1] : "out/audiodec-clips");
    decode_matches_libatrac9_sample_by_sample();
    consumed_over_superframe_equals_block_size();
    output_capacity_too_small_errors_without_writing();
    short_au_errors_without_overread();
    corrupt_frame_errors_and_clear_context_recovers();
    clear_context_mid_superframe_then_next_superframe_decodes();
    unmapped_au_or_pcm_pointer_errors();
    bad_descriptor_sizes_error();
    golden_blocks_from_the_game();
    trace_dumps_structures_and_drift();
    trace_caps_at_400_lines_unless_level_2();
    au_dump_writes_the_consumed_stream();
    report_counts();
    many_threads_decode_independent_streams();
    create_and_clear_while_others_decode_is_bit_exact();
    term_waits_for_inflight_decode_and_later_calls_error();
    decode_taken_before_term_writes_nothing_after_it();
    delete_during_decode_does_not_free_under_it();
    term_init_create_again_works();
    cpu_resolves_all_three();
    cpu_logs_first_call_only_unless_traced();
    cpu_trace_has_its_own_cap();
    cpu_unknown_codec_stops();
    cpu_calls_answer_api_fail();
    cpu_bad_pointers_write_nothing();
    cpu_errors_end_the_voice_in_silence();
    cpu_report_counts();
    puts("PASS: Audiodec contracts");
    return 0;
}
