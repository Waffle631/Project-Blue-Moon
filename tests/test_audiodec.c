/* Spec 4a (docs/specs/2026-10-14-spec-4-audiodec.md; contract in docs/audiodec.md 1.8): libSceAudiodec
 * through runtime_resolve, with the control block laid out as the game builds it (docs/audiodec.md
 * 1.2) in guest direct memory. Includes the module to reach its decoder table (acquire/release). */
#define _GNU_SOURCE
#undef NDEBUG /* the checks are asserts; release builds define NDEBUG */
#include "../src/runtime_audiodec.c"
#include <assert.h>
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
#define E_INVALID_TYPE 0x807F0001u
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
static uint8_t *guest_base,*guest_next,*hole; /* hole: an unmapped page right after the blocks */

static void fill(GameBlock *b,uint32_t config) {
    memset(b,0,sizeof *b);
    b->params=&b->params_size; b->bsi=&b->bsi_size; b->au=&b->au_desc; b->pcm=&b->pcm_desc;
    b->au_desc.size=b->pcm_desc.size=0x18; b->params_size=0xC; b->word_length=1; b->bsi_size=0x24;
    for (int i=0;i<4;++i) b->config[i]=(uint8_t)(config>>(24-8*i));
}
/* A new block in guest memory, aligned like the game's (0x40). */
static GameBlock *block(uint32_t config) {
    GameBlock *b=(GameBlock *)guest_next; guest_next+=0xC0;
    assert(guest_next<=hole); fill(b,config); return b;
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
    guest_base=guest_next=address; hole=guest_base+GUEST_BYTES-HOLE_BYTES;
    assert(unmap(hole,HOLE_BYTES)==0 && runtime_memory_is_mapped((uintptr_t)guest_base,8) && !runtime_memory_is_mapped((uintptr_t)hole,8));
}
static void resolves_all_six(void) {
    assert(api.init && api.term && api.create && api.remove && api.decode && api.clear);
}
/* Runs f with stdout sent to a temporary file and returns the number of lines it printed. */
static int printed_lines(void (*f)(void)) {
    fflush(stdout); FILE *tmp=tmpfile(); assert(tmp);
    int saved=dup(1); assert(saved>=0 && dup2(fileno(tmp),1)>=0);
    f(); fflush(stdout); dup2(saved,1); close(saved);
    rewind(tmp); int lines=0,c; while ((c=fgetc(tmp))!=EOF) lines+=c=='\n';
    fclose(tmp); return lines;
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
/* Until task 4 of the plan, Decode and ClearContext on a live decoder are explicit errors. */
static void live_decode_and_clear_answer_explicit_error(void) {
    GameBlock *b=block(0xfe740ff0);
    int32_t id=api.create(&b->params,1);
    assert(id>=0 && (uint32_t)api.decode(id,&b->params)==E_INVALID_TYPE && (uint32_t)api.clear(id)==E_INVALID_TYPE);
    assert(api.remove(id)==0);
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
   are logged; with it, every call. */
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
    setenv("GOW3_TRACE_AUDIODEC","1",1);
    assert(printed_lines(create_good)==2 && printed_lines(create_bad)==1);
    unsetenv("GOW3_TRACE_AUDIODEC");
}
int main(void) {
    setup();
    resolves_all_six();
    assert(api.init(1)==0);
    unknown_codec_stops();
    create_returns_nonneg_id_with_each_of_five_configs();
    create_writes_channels_to_bsi();
    create_bad_config_returns_error();
    create_null_or_unmapped_ctrl_returns_error();
    stale_id_errors();
    live_decode_and_clear_answer_explicit_error();
    ids_never_reused();
    table_full_errors_without_id();
    delete_while_acquired_defers_free();
    term_while_acquired_defers_free();
    term_invalidates_ids_and_init_again_creates();
    logs_first_call_and_first_error_only();
    puts("PASS: Audiodec contracts");
    return 0;
}
