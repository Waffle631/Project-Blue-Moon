#define _GNU_SOURCE
#include "runtime.h"
#include <stdlib.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

typedef int (ABI *Register)(GuestCallback);
typedef int (ABI *RegisterCxa)(void (ABI *)(void *), void *, void *);
typedef int (ABI *Acquire)(uint64_t *);
typedef void (ABI *Guard)(uint64_t *);
typedef int32_t (ABI *AttrInit)(void **);
typedef int32_t (ABI *AttrType)(void **, int);
typedef int32_t (ABI *MutexInit)(void **, void **, const char *);
typedef int32_t (ABI *HandleOp)(void **);
typedef int32_t (ABI *Allocate)(int64_t, int64_t, uint64_t, uint64_t, int, int64_t *);
typedef int32_t (ABI *Map)(void **, uint64_t, int, int, int64_t, uint64_t);
typedef int32_t (ABI *Unmap)(void *, uint64_t);
typedef int32_t (ABI *Release)(uint64_t, uint64_t);
#define GET(type, name) ((type)runtime_resolve(name, 0))
typedef void *(ABI *TlsAddress)(const uint64_t *);
typedef void *(ABI *ThreadSelf)(void);
typedef int32_t (ABI *ThreadGetAttr)(void *,void **);
typedef int32_t (ABI *ThreadAffinity)(void **,uint64_t *);
static void *main_identity;
static void *tls_worker(void *unused) {
    (void)unused;
    const uint64_t index[]={2,0};
    unsigned char *p=GET(TlsAddress,"vNe1w4diLCs#libkernel")(index);
    assert(p[0]==42 && p[1]==17 && p[31]==0);
    p[0]=99;
    assert(GET(ThreadSelf,"aI+OeCz8xrQ#libkernel")()!=main_identity);
    return NULL;
}
static void libc_support(void) {
    static const unsigned char initial[]={42,17};
    runtime_set_libc_tls(initial,sizeof(initial),32);
    const uint64_t index[]={2,0}, last[]={2,31};
    TlsAddress tls=GET(TlsAddress,"vNe1w4diLCs#libkernel");
    unsigned char *p=tls(index);
    assert(p[0]==42 && p[1]==17 && p[2]==0 && p[31]==0);
    assert(tls(last)==p+31 && tls(index)==p);
    p[0]=23;
    main_identity=GET(ThreadSelf,"aI+OeCz8xrQ#libkernel")();
    pthread_t worker;
    assert(pthread_create(&worker,NULL,tls_worker,NULL)==0);
    assert(pthread_join(worker,NULL)==0);
    assert(p[0]==23 && initial[0]==42);
    void *attr=NULL;
    AttrInit init=GET(AttrInit,"nsYoNRywwNg#libkernel");
    ThreadGetAttr get=GET(ThreadGetAttr,"x1X76arYMxU#libkernel");
    ThreadAffinity affinity=GET(ThreadAffinity,"8+s5BzZjxSg#libkernel");
    HandleOp destroy=GET(HandleOp,"62KCwEMmzcM#libkernel");
    uint64_t mask=0;
    assert((uint32_t)init(NULL)==0x80020016);
    assert(init(&attr)==0 && get(main_identity,&attr)==0);
    assert(affinity(&attr,&mask)==0 && mask==0x7f);
    assert((uint32_t)get(&mask,&attr)==0x80020003);
    assert((uint32_t)affinity(&attr,NULL)==0x80020016);
    assert(destroy(&attr)==0 && !attr);
    assert((uint32_t)affinity(&attr,&mask)==0x80020016);
    uint64_t param[8]={64};
    runtime_set_procparam(param);
    assert(GET(ThreadSelf,"959qrazPIrg#libkernel")()==param);
    typedef void (ABI *SetHeap)(void **);
    void *heap[10]={0};
    GET(SetHeap,"p5EcQeEeJAE#libkernel")(heap);
    assert(runtime_application_heap_api()==heap);
    GET(SetHeap,"p5EcQeEeJAE#libkernel")(NULL);
    assert(runtime_application_heap_api()==NULL);
    runtime_set_procparam(NULL);
}
static int order[512], count;
static Register register_plain;
static ABI void first(void) { order[count++]=1; }
static ABI void second(void) { order[count++]=2; }
static ABI void with_arg(void *p) { order[count++]=(int)(uintptr_t)p; }
static ABI void add_handler(void) { order[count++]=4; assert(register_plain(first)==0); }

static void exit_handlers(void) {
    register_plain=GET(Register,"8G2LB+A3rzg#libc");
    RegisterCxa cxa=GET(RegisterCxa,"tsvEmnenz48#libc");
    assert(register_plain && cxa);
    assert(register_plain(first)==0);
    assert(cxa(with_arg,(void *)3,(void *)11)==0);
    assert(register_plain(second)==0);
    runtime_finalize((void *)11);
    assert(count==1 && order[0]==3);
    runtime_finalize((void *)11); assert(count==1);
    runtime_finalize(NULL);
    assert(count==3 && order[1]==2 && order[2]==1);
    runtime_finalize(NULL); assert(count==3);
    assert(register_plain(add_handler)==0);
    runtime_finalize(NULL);
    assert(count==5 && order[3]==4 && order[4]==1);
    /* Exercise reallocating the registry and strict LIFO ordering. */
    count=0;
    for (uintptr_t i=0; i<200; ++i) assert(cxa(with_arg,(void *)i,(void *)12)==0);
    runtime_finalize((void *)12);
    assert(count==200);
    for (int i=0; i<200; ++i) assert(order[i]==199-i);
}
static void guards(void) {
    uint64_t state=0;
    Acquire acquire=GET(Acquire,"3GPpjQdAMTw#libc");
    Guard release=GET(Guard,"9rAeANT2tyE#libc"), abort_guard=GET(Guard,"2emaaluWzUw#libc");
    assert(acquire(&state)==1 && state==(UINT64_C(1)<<32));
    abort_guard(&state); assert(state==0);
    assert(acquire(&state)==1);
    release(&state); assert(state==1);
    assert(acquire(&state)==0);
}
static void mutexes(void) {
    AttrInit attr_init=GET(AttrInit,"F8bUHwAG284#libkernel");
    AttrType type=GET(AttrType,"iMp8QpE+XO4#libkernel");
    HandleOp attr_destroy=GET(HandleOp,"smWEktiyyG0#libkernel");
    MutexInit init=GET(MutexInit,"cmo1RIYva9o#libkernel");
    HandleOp lock=GET(HandleOp,"9UK1vLZQft4#libkernel"), unlock=GET(HandleOp,"tn3VlD0hG60#libkernel");
    HandleOp trylock=GET(HandleOp,"upoVrzMHFeE#libkernel"), destroy=GET(HandleOp,"2Of0f+3mhhE#libkernel");
    void *attr=NULL, *mutex=NULL;
    assert((uint32_t)attr_init(NULL)==0x80020016);
    assert(attr_init(&attr)==0);
    assert((uint32_t)type(&attr,99)==0x80020016);
    assert(type(&attr,2)==0 && init(&mutex,&attr,"recursive-test")==0);
    assert(attr_destroy(&attr)==0 && attr==NULL);
    assert(lock(&mutex)==0 && trylock(&mutex)==0);
    assert((uint32_t)destroy(&mutex)==0x80020010);
    assert(unlock(&mutex)==0 && unlock(&mutex)==0 && destroy(&mutex)==0);
    assert((uint32_t)lock(&mutex)==0x80020016);
    mutex=NULL;
    assert(lock(&mutex)==0); /* static initialization */
    assert((uint32_t)lock(&mutex)==0x8002000b);
    assert((uint32_t)trylock(&mutex)==0x80020010);
    assert(unlock(&mutex)==0 && destroy(&mutex)==0);
}
static void posix_mutexes(void) {
    AttrInit init=GET(AttrInit,"dQHWEsJtoE4#libkernel");
    AttrType type=GET(AttrType,"mDmgMOGVUqg#libkernel");
    HandleOp destroy_attr=GET(HandleOp,"HF7lK46xzjY#libkernel");
    typedef int32_t (ABI *PosixInit)(void **,void **);
    PosixInit mutex_init=GET(PosixInit,"ttHNfU+qDBU#libkernel");
    HandleOp lock=GET(HandleOp,"7H0iTOciTLo#libkernel");
    HandleOp unlock=GET(HandleOp,"2Z+PpY6CaJg#libkernel");
    HandleOp trylock=GET(HandleOp,"K-jXhbt2gn4#libkernel");
    HandleOp destroy=GET(HandleOp,"ltCfaGr2JGE#libkernel");
    void *a=NULL,*m=NULL;
    assert(init(NULL)==22 && init(&a)==0);
    assert(type(&a,99)==22 && type(&a,2)==0);
    assert(mutex_init(&m,&a)==0 && destroy_attr(&a)==0);
    assert(lock(&m)==0 && trylock(&m)==0 && destroy(&m)==16);
    assert(unlock(&m)==0 && unlock(&m)==0 && destroy(&m)==0);
    m=NULL;
    assert(lock(&m)==0 && lock(&m)==11 && trylock(&m)==16);
    assert(unlock(&m)==0 && destroy(&m)==0);
    assert(!runtime_resolve("dQHWEsJtoE4#libc",0));
}
static void wall_time(void) {
    typedef int (ABI *GetTime)(int64_t *,int32_t *);
    GetTime get=GET(GetTime,"n88vx3C5nW8#libkernel");
    struct { int64_t value[2]; uint64_t canary; } t={{0,0},0xabcdef};
    struct { int32_t value[2]; uint32_t canary; } z={{0,0},0x123456};
    time_t before=time(NULL);
    assert(get(t.value,z.value)==0);
    time_t after=time(NULL);
    assert(t.value[0]>=before && t.value[0]<=after);
    assert(t.value[1]>=0 && t.value[1]<1000000);
    assert(t.canary==0xabcdef && z.canary==0x123456);
    assert(get(NULL,NULL)==0 && get(NULL,z.value)==0);
    assert(!runtime_resolve("n88vx3C5nW8#libc",0));
}
/* Names that only existed in runtime_kernel.c's table: without import_names.inc they stopped the
   game (libc.prx calls _nanosleep while spinning). */
static void kernel_time_names(void) {
    typedef int32_t (ABI *Nanosleep)(const int64_t *,int64_t *);
    typedef uint64_t (ABI *Counter)(void);
    Nanosleep nap=GET(Nanosleep,"NhpspxdjEKU#libkernel");
    assert(nap && nap==GET(Nanosleep,"yS8U2TGCe1A#libkernel"));
    int64_t rq[2]={0,1000000},rem[2]={-1,-1};
    assert(nap(rq,rem)==0 && rem[0]==0 && rem[1]==0);
    int64_t bad[2]={0,1000000000};
    assert(nap(bad,NULL)==-1);
    Counter counter=GET(Counter,"fgxnMeTNUtY#libkernel"),hz=GET(Counter,"BNowx2l588E#libkernel");
    assert(counter && hz && hz()>0);
    uint64_t a=counter(); nap(rq,NULL); assert(counter()>a);
}
/* libc.prx init stops on the sanitizer queries if they are missing; NULL tables keep its own
   allocator. The game runs as a base PS4. */
static void sanitizer_and_neo(void) {
    typedef const void *(ABI *Table)(void);
    typedef int32_t (ABI *Flag)(void);
    Table m=GET(Table,"py6L8jiVAN8#libkernel"),n=GET(Table,"bnZxYgAFeA0#libkernel");
    Flag asan=GET(Flag,"jh+8XiK4LeE#libkernel"),neo=GET(Flag,"WslcK1FQcGI#libkernel");
    assert(m && n && asan && neo);
    assert(!m() && !m() && !n() && asan()==0 && neo()==0 && neo()==0);
}
static void heap_trace_info(void) {
    typedef int32_t (ABI *TraceInfo)(void *);
    TraceInfo f=GET(TraceInfo,"NWtTN10cJzE#libSceLibcInternalExt");
    assert(f);
    struct { uint64_t size; uint32_t version,trace; uint64_t *mask; void **mstates; } a={0x20,1,0,0,0},b=a,small={0x18,1,0,0,0};
    assert(f(&a)==0 && a.size==0x20 && a.version==1 && a.trace==0 && a.mask && a.mstates);
    assert(f(&b)==0 && b.mask==a.mask && b.mstates==a.mstates); /* one table for the process */
    *a.mask=0; a.mstates[127]=&a; assert(a.mstates[127]==&a); a.mstates[127]=0;
    assert((uint32_t)f(&small)==0x80020016 && !small.mask && (uint32_t)f(NULL)==0x80020016);
}
/* Spec 4a (docs/audiodec.md 1.8): InitLibrary(1) succeeds once; a second call is the Vita's
   ALREADY_INITIALIZED. The game ignores the result (eboot +0x10e211). The rest: tests/test_audiodec.c. */
static void audiodec_init(void) {
    typedef int32_t (ABI *Init)(uint32_t);
    Init init=GET(Init,"VjhsmxpcezI#libSceAudiodec");
    assert(init && init(1)==0 && (uint32_t)init(1)==0x807F0003);
}
/* Spec 3: sceSaveDataInitialize3 had code but no name, so it could not be reached. The game calls
   it with NULL (eboot +0xed2eb). */
static void savedata_initialize3(void) {
    typedef int32_t (ABI *Init)(const void *);
    Init init=GET(Init,"TywrFKCoLGY#libSceSaveData");
    assert(init && init(NULL)==0);
}
/* Spec 3: sceSaveDataMount2 had code but no name either. The game mounts its save read-only with
   0x60 blocks at eboot +0x121716 and, on 0x809F0008 (no save yet), takes its "no save" path. */
static void savedata_mount2(void) {
    typedef struct { int32_t user,pad; const char *dir; uint64_t blocks; uint32_t mode; uint8_t reserved[36]; } Mount2;
    typedef int32_t (ABI *Mount)(const Mount2 *,void *);
    Mount mount=GET(Mount,"0z45PIH+SNI#libSceSaveData");
    char dir[32]="SPEC2_NO_SUCH_SAVE", result[64];
    Mount2 m={1,0,dir,0x60,1,{0}};
    assert(mount && (uint32_t)mount(&m,result)==0x809F0008 && (uint32_t)mount(NULL,result)==0x809F0000);
}
/* Spec 3 (4.4): sceSaveDataUmountWithBackup unmounts like sceSaveDataUmount and keeps no backup. The
   game calls it on the mount point it just created (eboot +0x1214e2) and ignores the result. */
static void savedata_umount_with_backup(void) {
    typedef struct { int32_t user,pad; const char *dir; uint64_t blocks; uint32_t mode; uint8_t reserved[36]; } Mount2;
    typedef int32_t (ABI *Mount)(const Mount2 *,void *);
    typedef int32_t (ABI *Umount)(const void *);
    Mount mount=GET(Mount,"0z45PIH+SNI#libSceSaveData");
    Umount backup=GET(Umount,"VwadwBBBJ80#libSceSaveData"),umount=GET(Umount,"BMR4F-Uek3E#libSceSaveData");
    char user[]="out/savedata-test-XXXXXX",dir[32]="savedata000",result[64],cmd[64];
    assert(mkdtemp(user)); runtime_file_configure("out",user);
    Mount2 m={1,0,dir,0x60,4,{0}}; /* CREATE */
    assert(mount && backup && umount && mount(&m,result)==0 && !strncmp(result,"/savedata",9));
    assert(backup(result)==0 && (uint32_t)backup(result)==0x809F0008 && (uint32_t)umount(result)==0x809F0008);
    snprintf(cmd,sizeof cmd,"rm -rf %s",user); assert(system(cmd)==0);
}
/* Spec 3 (4.4): UmountWithBackup keeps no copy, so CheckBackupData and RestoreBackupData find none. The
   game checks after a load fails as broken (eboot +0x121895) and offers a restore only on 0; it restores
   at +0x121bb2, +0x1224fd and +0x1257b8 and takes any other result as a failure. */
static void savedata_backup_queries(void) {
    typedef struct { int32_t user,pad; const char *title,*dir; uint8_t rest[48]; } Query; /* the 72 bytes the game zeroes */
    typedef int32_t (ABI *Backup)(const Query *);
    typedef int32_t (ABI *Init)(const void *);
    typedef int32_t (ABI *Term)(void);
    Backup check=GET(Backup,"RQOqDbk3bSU#libSceSaveData"),restore=GET(Backup,"lU9YRFsgwSU#libSceSaveData");
    Init init=GET(Init,"TywrFKCoLGY#libSceSaveData"); Term term=GET(Term,"yKDy8S5yLA0#libSceSaveData");
    char dir[32]="savedata000"; Query q={1,0,NULL,dir,{0}}, no_dir={1,0,NULL,NULL,{0}};
    assert(check && restore && term()==0);
    assert((uint32_t)check(&q)==0x809F0001 && (uint32_t)restore(&q)==0x809F0001); /* before Initialize3 */
    assert(init(NULL)==0);
    assert((uint32_t)check(&q)==0x809F0008 && (uint32_t)restore(&q)==0x809F0008);
    assert((uint32_t)check(&q)==0x809F0008 && (uint32_t)restore(&q)==0x809F0008);
    assert((uint32_t)check(NULL)==0x809F0000 && (uint32_t)restore(NULL)==0x809F0000);
    assert((uint32_t)check(&no_dir)==0x809F0000 && (uint32_t)restore(&no_dir)==0x809F0000);
}
/* stdout goes to a temporary file between capture_begin and capture_end, which returns what was printed. */
static FILE *capture_file; static int capture_saved;
static void capture_begin(void) {
    capture_file=tmpfile(); assert(capture_file);
    fflush(stdout); capture_saved=dup(1); assert(capture_saved>=0 && dup2(fileno(capture_file),1)==1);
}
static const char *capture_end(void) {
    static char text[65536];
    fflush(stdout); assert(dup2(capture_saved,1)==1); close(capture_saved);
    rewind(capture_file); size_t length=fread(text,1,sizeof(text)-1,capture_file); text[length]=0; fclose(capture_file);
    return text;
}
/* Spec 6 (5): the log shows the order of a save, mount -> param -> icon -> unmount. Unmounting a point
   that is no longer mounted prints nothing. */
static void savedata_call_log(void) {
    typedef struct { int32_t user,pad; const char *dir; uint64_t blocks; uint32_t mode; uint8_t reserved[36]; } Mount2;
    typedef struct { const void *buffer; uint64_t buffer_size,data_size; uint8_t reserved[32]; } Icon;
    typedef int32_t (ABI *Mount)(const Mount2 *,void *);
    typedef int32_t (ABI *Umount)(const void *);
    typedef int32_t (ABI *SetParam)(const void *,uint32_t,const void *,uint64_t);
    typedef int32_t (ABI *SaveIcon)(const void *,const Icon *);
    Mount mount=GET(Mount,"0z45PIH+SNI#libSceSaveData"); Umount umount=GET(Umount,"BMR4F-Uek3E#libSceSaveData");
    SetParam set_param=GET(SetParam,"85zul--eGXs#libSceSaveData"); SaveIcon save_icon=GET(SaveIcon,"c88Yy54Mx0w#libSceSaveData");
    assert(mount && umount && set_param && save_icon);
    char user[]="out/savedata-test-XXXXXX",dir[32]="savedata000",result[64],cmd[64],param[128],icon[128],unmounted[128];
    assert(mkdtemp(user)); runtime_file_configure("out",user);
    static const unsigned char png[8]={0x89,'P','N','G','\r','\n',0x1a,'\n'};
    Mount2 m={1,0,dir,0x60,4,{0}}; Icon i={png,sizeof png,sizeof png,{0}}; /* CREATE */
    capture_begin();
    int32_t mounted=mount(&m,result), titled=set_param(result,1,"Title",5), iconed=save_icon(result,&i);
    int32_t first=umount(result), second=umount(result);
    const char *text=capture_end();
    assert(!mounted && !titled && !iconed && !first && (uint32_t)second==0x809F0008);
    snprintf(param,sizeof param,"Runtime: save data param set (type 1) on %s\n",result);
    snprintf(icon,sizeof icon,"Runtime: save data icon saved on %s\n",result);
    snprintf(unmounted,sizeof unmounted,"Runtime: save data unmounted from %s\n",result);
    const char *a=strstr(text,"Runtime: save data 'savedata000' mounted at "), *b=strstr(text,param), *c=strstr(text,icon), *d=strstr(text,unmounted);
    assert(a && b && c && d && a<b && b<c && c<d);
    assert(!strstr(d+strlen(unmounted),"unmounted from")); /* the second unmount found nothing mounted */
    snprintf(cmd,sizeof cmd,"rm -rf %s",user); assert(system(cmd)==0);
}
/* Spec 6 (5): a save written in one run is mounted and read back in the next. The load half runs in a new
   process (--savedata-load), so only the host files carry over, not the mount slots. */
typedef struct { int32_t user,pad; const char *dir; uint64_t blocks; uint32_t mode; uint8_t reserved[36]; } SaveMount2;
typedef int32_t (ABI *SaveMount)(const SaveMount2 *,void *);
typedef int32_t (ABI *SaveUmount)(const void *);
static void save_pattern(unsigned char *bytes) { for (int i=0;i<4096;++i) bytes[i]=(unsigned char)(i*31+7); }
static uint32_t save_status(const char *result) { uint32_t status; memcpy(&status,result+28,4); return status; } /* MountResult.status */
static void savedata_load(const char *user) {
    typedef int32_t (ABI *Init)(const void *);
    SaveMount mount=GET(SaveMount,"0z45PIH+SNI#libSceSaveData"); SaveUmount umount=GET(SaveUmount,"BMR4F-Uek3E#libSceSaveData");
    Init init=GET(Init,"TywrFKCoLGY#libSceSaveData");
    runtime_file_configure("out",user); assert(mount && umount && init && init(NULL)==0);
    char dir[32]="savedata000",missing[32]="savedata999",result[64],path[80];
    SaveMount2 m={1,0,dir,0x60,1,{0}}, none={1,0,missing,0x60,1,{0}}; /* RDONLY with 0x60 blocks, as the game loads (eboot +0x121716) */
    assert(mount(&m,result)==0 && save_status(result)==0); /* existing, not created */
    unsigned char expected[4096], bytes[4096]; save_pattern(expected);
    snprintf(path,sizeof path,"%s/x",result); int fd=(int)runtime_file_open(path,0,0);
    assert(fd>=0 && runtime_file_read(fd,bytes,sizeof bytes)==(int64_t)sizeof bytes && !memcmp(bytes,expected,sizeof bytes));
    assert(runtime_file_read(fd,bytes,1)==0 && !runtime_file_close(fd)); /* exactly 4096 bytes */
    assert(umount(result)==0 && (uint32_t)mount(&none,result)==0x809F0008);
}
static void savedata_survives_remount(const char *self) {
    SaveMount mount=GET(SaveMount,"0z45PIH+SNI#libSceSaveData"); SaveUmount umount=GET(SaveUmount,"BMR4F-Uek3E#libSceSaveData");
    char user[]="out/savedata-test-XXXXXX",dir[32]="savedata000",result[64],path[80],cmd[160];
    assert(mount && umount && mkdtemp(user)); runtime_file_configure("out",user);
    SaveMount2 m={1,0,dir,0x60,32,{0}}; /* CREATE2: created when missing */
    assert(mount(&m,result)==0 && save_status(result)==1);
    unsigned char bytes[4096]; save_pattern(bytes);
    snprintf(path,sizeof path,"%s/x",result); int fd=(int)runtime_file_open(path,1|0x200|0x400,0644); /* O_WRONLY|O_CREAT|O_TRUNC */
    assert(fd>=0 && runtime_file_write(fd,bytes,sizeof bytes)==(int64_t)sizeof bytes && !runtime_file_close(fd) && umount(result)==0);
    snprintf(cmd,sizeof cmd,"%s --savedata-load %s",self,user); fflush(stdout); assert(system(cmd)==0);
    snprintf(cmd,sizeof cmd,"rm -rf %s",user); assert(system(cmd)==0);
}
/* Spec 3: fJgP+wqifno is DiscMap 0x7C980FFB0AA27E7A (the NID's bytes). libSceFios2.prx asks it for a
   file's LBA package location (call at +0x3411a) and takes its fallback path on any nonzero result.
   Like its twin ioKMruft1ek, it reports that no disc bitmap exists and leaves the outputs alone. */
static void discmap_location(void) {
    typedef int32_t (ABI *Location)(const char *,int64_t,int64_t,uint64_t *,uint64_t *,uint64_t *);
    const char *nids[]={"fJgP+wqifno#libSceDiscMap","ioKMruft1ek#libSceDiscMap"};
    for (int i=0;i<2;++i) {
        Location f=GET(Location,nids[i]);
        uint64_t where=7,lba=7,size=7;
        assert(f && (uint32_t)f("/app0/image/allscr.mrg",0,1,&where,&lba,&size)==0x81100004 && where==7 && lba==7 && size==7);
    }
}
static void direct_memory(void) {
    Allocate alloc=GET(Allocate,"rTXw65xmLIA#libkernel");
    Map map=GET(Map,"L-Q3LEjIbgA#libkernel");
    Unmap unmap=GET(Unmap,"cQke9UuBQOk#libkernel");
    Release release=GET(Release,"MBuItvba6z8#libkernel");
    int64_t a=-1,b=-1,c=-1;
    const uint64_t pool=536870912, length=16384;
    assert((uint32_t)alloc(0,pool,1,0,0,&a)==0x80020016 && a==-1);
    assert((uint32_t)alloc(0,8192,length,0,0,&a)==0x80020023);
    assert(alloc(0,pool,length,2097152,0,&a)==0 && a==0);
    assert(alloc(0,pool,length,2097152,0,&b)==0 && b==2097152);
    assert(alloc(0,pool,length,16384,0,&c)==0 && c==16384);
    void *x=NULL,*y=NULL,*invalid=NULL;
    assert((uint32_t)map(&invalid,length*2,3,0,b,0)==0x80020016);
    assert(map(&x,length,3,0,a,2097152)==0 && !((uintptr_t)x%2097152));
    assert(map(&y,length,3,0,a,0)==0);
    assert(*(uint64_t *)x==0);
    *(uint64_t *)x=UINT64_C(0xabcdef0123456789);
    assert(*(uint64_t *)y==UINT64_C(0xabcdef0123456789)); /* real shared backing */
    assert((uint32_t)unmap((char *)x+4096,length)==0x80020016); /* unaligned */
    assert(unmap(x,length)==0 && unmap(y,length)==0);
    assert(release(a,length)==0 && release(b,length)==0 && release(c,length)==0);
    assert(alloc(0,pool,length,0,0,&a)==0 && a==0);
    x=NULL; assert(map(&x,length,3,0,a,0)==0);
    assert(*(uint64_t *)x==0); /* released allocation does not leak previous data */
    assert(release(a,length)==0); /* release also unmaps owned mapping */
}
static void memory_primitives(void) {
    typedef void *(ABI *Set)(void *,int,size_t);
    typedef void *(ABI *Copy)(void *,const void *,size_t);
    typedef int (ABI *Compare)(const void *,const void *,size_t);
    char a[16],b[16];
    assert(GET(Set,"8zTFvBIAIN8#libc")(a,0x5a,sizeof(a))==a);
    assert(GET(Copy,"Q3VBxCXhUHs#libc")(b,a,sizeof(a))==b);
    assert(GET(Compare,"DfivPArhucg#libc")(a,b,sizeof(a))==0);
    memcpy(a,"abcdef",7);
    GET(Copy,"+P6FRGH4LfA#libc")(a+1,a,6);
    assert(memcmp(a,"aabcdef",7)==0);
    typedef size_t (ABI *Length)(const char *);
    Length length=GET(Length,"j4ViWNHEgww#libc");
    assert(length("")==0 && length("abc\0def")==3 && length("\xff\x80")==2);
}

static HandleOp rw_read, rw_write, rw_tryread, rw_trywrite, rw_unlock, rw_destroy;
typedef struct { int64_t seconds, nanoseconds; } TestTime;
typedef int32_t (ABI *TimedLock)(void **, const TestTime *);
static void rw_setup(void) {
    rw_read=GET(HandleOp,"Ox9i0c7L5w0#libkernel"); rw_write=GET(HandleOp,"mqdNorrB+gI#libkernel");
    rw_tryread=GET(HandleOp,"XD3mDeybCnk#libkernel"); rw_trywrite=GET(HandleOp,"bIHoZCTomsI#libkernel");
    rw_unlock=GET(HandleOp,"+L98PIbGttk#libkernel"); rw_destroy=GET(HandleOp,"BB+kb08Tl9A#libkernel");
    assert(rw_read && rw_write && rw_tryread && rw_trywrite && rw_unlock && rw_destroy);
}
static void rw_lifecycle(void) {
    rw_setup();
    MutexInit init=GET(MutexInit,"6ULAa0fq4jA#libkernel");
    void *lock;
    assert((uint32_t)init(NULL,NULL,NULL)==0x80020016);
    assert(init(&lock,NULL,"rw-test")==0);
    assert(rw_read(&lock)==0 && rw_read(&lock)==0);
    assert((uint32_t)rw_trywrite(&lock)==0x80020010);
    assert((uint32_t)rw_write(&lock)==0x8002000b);
    assert((uint32_t)rw_destroy(&lock)==0x80020010);
    assert(rw_unlock(&lock)==0 && rw_unlock(&lock)==0);
    assert((uint32_t)rw_unlock(&lock)==0x80020001);
    assert(rw_write(&lock)==0);
    assert((uint32_t)rw_tryread(&lock)==0x80020010);
    assert((uint32_t)rw_read(&lock)==0x8002000b);
    assert(rw_unlock(&lock)==0 && rw_destroy(&lock)==0);
    assert((uintptr_t)lock==1);
    assert((uint32_t)rw_read(&lock)==0x80020016);
    assert((uint32_t)rw_destroy(&lock)==0x80020016);
    lock=NULL;
    assert(rw_destroy(&lock)==0);
    assert(rw_read(&lock)==0 && rw_unlock(&lock)==0 && rw_destroy(&lock)==0);
    lock=(void *)(uintptr_t)0xdead;
    assert((uint32_t)rw_tryread(&lock)==0x80020016);
}
typedef struct {
    void *lock;
    pthread_barrier_t ready, release, writer_ready;
    _Atomic int value;
} RwFixture;
static void barrier(pthread_barrier_t *b) {
    int e=pthread_barrier_wait(b); assert(e==0 || e==PTHREAD_BARRIER_SERIAL_THREAD);
}
static void *reader_worker(void *context) {
    RwFixture *f=context;
    assert(rw_read(&f->lock)==0);
    barrier(&f->ready); barrier(&f->release);
    assert(atomic_load(&f->value)==0);
    assert(rw_unlock(&f->lock)==0); return NULL;
}
static void *writer_worker(void *context) {
    RwFixture *f=context;
    assert((uint32_t)rw_trywrite(&f->lock)==0x80020010);
    barrier(&f->writer_ready);
    assert(rw_write(&f->lock)==0);
    atomic_store(&f->value,1);
    assert(rw_unlock(&f->lock)==0); return NULL;
}
static void *holding_writer(void *context) {
    RwFixture *f=context;
    assert(rw_write(&f->lock)==0);
    barrier(&f->ready); barrier(&f->release);
    assert(rw_unlock(&f->lock)==0); return NULL;
}
static void fixture_init(RwFixture *f) {
    assert(pthread_barrier_init(&f->ready,NULL,2)==0);
    assert(pthread_barrier_init(&f->release,NULL,2)==0);
    assert(pthread_barrier_init(&f->writer_ready,NULL,2)==0);
}
static void fixture_destroy(RwFixture *f) {
    assert(rw_destroy(&f->lock)==0);
    assert(pthread_barrier_destroy(&f->ready)==0);
    assert(pthread_barrier_destroy(&f->release)==0);
    assert(pthread_barrier_destroy(&f->writer_ready)==0);
}
static void rw_concurrency(void) {
    rw_setup();
    RwFixture f={0}; fixture_init(&f);
    pthread_t reader,writer;
    assert(rw_read(&f.lock)==0);
    assert(pthread_create(&reader,NULL,reader_worker,&f)==0);
    barrier(&f.ready); /* two threads now hold read locks at once */
    assert(rw_unlock(&f.lock)==0);
    assert(pthread_create(&writer,NULL,writer_worker,&f)==0);
    barrier(&f.writer_ready);
    assert((uint32_t)rw_destroy(&f.lock)==0x80020010);
    assert(atomic_load(&f.value)==0);
    barrier(&f.release);
    assert(pthread_join(reader,NULL)==0 && pthread_join(writer,NULL)==0);
    assert(atomic_load(&f.value)==1);
    fixture_destroy(&f);
}
static void rw_timeouts(void) {
    rw_setup();
    TimedLock read_timed=GET(TimedLock,"iPtZRWICjrM#libkernel"), write_timed=GET(TimedLock,"adh--6nIqTk#libkernel");
    RwFixture f={0}; fixture_init(&f);
    pthread_t writer; assert(pthread_create(&writer,NULL,holding_writer,&f)==0);
    barrier(&f.ready);
    TestTime expired={0,0},invalid={0,1000000000};
    assert((uint32_t)read_timed(&f.lock,&expired)==0x8002003c);
    assert((uint32_t)write_timed(&f.lock,&expired)==0x8002003c);
    assert((uint32_t)read_timed(&f.lock,&invalid)==0x80020016);
    assert((uint32_t)rw_unlock(&f.lock)==0x80020001);
    barrier(&f.release); assert(pthread_join(writer,NULL)==0);
    assert(read_timed(&f.lock,&invalid)==0); /* available lock ignores deadline */
    assert(rw_unlock(&f.lock)==0);
    fixture_destroy(&f);
}
/* libc.prx registers its per-thread atexit runner with _sceKernelSetThreadDtors; the kernel calls
   it on the exiting thread, before TSD key destructors (docs/specs/2026-10-14-spec-2-kernel.md, 5.1). */
typedef int32_t (ABI *SetCallback)(void *);
typedef int32_t (ABI *ThreadCreate)(void **,void **,void *(ABI *)(void *),void *,const char *);
typedef int32_t (ABI *ThreadJoin)(void *,void **);
typedef int32_t (ABI *KeyCreate)(uint32_t *,void (ABI *)(void *));
typedef int32_t (ABI *KeySet)(uint32_t,void *);
typedef int32_t (ABI *DsoCount)(void *);
static uint32_t dtor_key;
static _Atomic int dtor_calls, key_calls, dtor_order, key_order, sequence;
static pthread_t entry_thread, dtor_thread;
static ABI void thread_dtors(void) { dtor_thread=pthread_self(); dtor_order=++sequence; ++dtor_calls; }
static ABI void key_destructor(void *value) { (void)value; key_order=++sequence; ++key_calls; }
static ABI int count_unused(int module) { (void)module; abort(); }
static ABI void report_unused(int module) { (void)module; abort(); }
static ABI void *returning_entry(void *argument) {
    entry_thread=pthread_self();
    assert(GET(KeySet,"+BzXYkqYeLE#libkernel")(dtor_key,argument)==0);
    return argument;
}
static ABI void *exiting_entry(void *argument) {
    entry_thread=pthread_self();
    assert(GET(KeySet,"+BzXYkqYeLE#libkernel")(dtor_key,argument)==0);
    ((void (ABI *)(void *))GET(void *,"3kg7rT0NQIs#libkernel"))(argument);
    abort();
}
static void run_guest_thread(void *(ABI *entry)(void *)) {
    void *thread=NULL, *result=NULL; static int value;
    dtor_calls=key_calls=0; sequence=0;
    assert(GET(ThreadCreate,"6UgtwV+0zb4#libkernel")(&thread,NULL,entry,&value,"dtors")==0);
    assert(GET(ThreadJoin,"onNY9Byn-W8#libkernel")(thread,&result)==0 && result==&value);
}
static void thread_dtors_contract(void) {
    SetCallback set_dtors=GET(SetCallback,"rNhWz+lvOMU#libkernel");
    SetCallback set_count=GET(SetCallback,"pB-yGZ2nQ9o#libkernel");
    SetCallback set_report=GET(SetCallback,"WhCc1w3EhSI#libkernel");
    DsoCount increment=GET(DsoCount,"Tz4RNUCBbGI#libkernel"), decrement=GET(DsoCount,"8OnWXlgQlvo#libkernel");
    assert(set_dtors && set_count && set_report && increment && decrement);
    assert(GET(KeyCreate,"geDaqgH9lTg#libkernel")(&dtor_key,key_destructor)==0);
    /* Before libc registers anything, a thread exit runs only the key destructors. */
    run_guest_thread(returning_entry);
    assert(dtor_calls==0 && key_calls==1);
    /* Count and Report only serve module unloading, which never happens: stored, never called. */
    assert(set_dtors((void *)thread_dtors)==0 && set_count((void *)count_unused)==0 && set_report((void *)report_unused)==0);
    run_guest_thread(returning_entry);
    assert(dtor_calls==1 && key_calls==1 && pthread_equal(dtor_thread,entry_thread) && dtor_order<key_order);
    run_guest_thread(exiting_entry);
    assert(dtor_calls==1 && key_calls==1 && pthread_equal(dtor_thread,entry_thread) && dtor_order<key_order);
    static int dso;
    assert(increment(&dso)==0 && increment(&dso)==0 && decrement(&dso)==0 && decrement(&dso)==0);
}
/* Spec 2 (block D): libc's module table lock (libc +0x128f60, a static initializer, so NULL) goes
   through the POSIX names; same objects as scePthreadRwlock*, positive errno values. */
static void posix_rwlocks(void) {
    rw_setup();
    HandleOp rd=GET(HandleOp,"iGjsr1WAtI0#libkernel"),wr=GET(HandleOp,"sIlRvQqsN2Y#libkernel"),un=GET(HandleOp,"EgmLo6EWgso#libkernel");
    assert(rd && wr && un);
    void *lock=NULL;
    assert(rd(&lock)==0 && lock && rd(&lock)==0);
    assert(wr(&lock)==11 && (uint32_t)rw_trywrite(&lock)==0x80020010); /* EDEADLK: this thread reads */
    assert(un(&lock)==0 && un(&lock)==0 && un(&lock)==1);                /* EPERM: nothing held */
    assert(wr(&lock)==0 && rd(&lock)==11 && un(&lock)==0);
    assert(rd(NULL)==22 && wr(NULL)==22 && un(NULL)==22);
    assert(rw_destroy(&lock)==0 && wr(&lock)==22);
}
/* Spec 2 (block D): scePthreadGetname is only called by libc's per-thread atexit report (+0xb63a0,
   module unloading only) with a 32-byte stack buffer; the API takes no size. */
static void thread_get_name(void) {
    typedef int32_t (ABI *Rename)(void *,const char *);
    typedef int32_t (ABI *GetName)(void *,char *);
    Rename rename_thread=GET(Rename,"GBUY7ywdULE#libkernel");
    GetName get_name=GET(GetName,"How7B8Oet6k#libkernel");
    assert(rename_thread && get_name);
    void *self=GET(ThreadSelf,"aI+OeCz8xrQ#libkernel")();
    struct { char name[32]; uint64_t canary; } b;
    memset(&b,0x55,sizeof(b)); b.canary=0xfeedface;
    assert(rename_thread(self,"task_game")==0 && get_name(self,b.name)==0 && !strcmp(b.name,"task_game"));
    assert(rename_thread(self,"a-thread-name-longer-than-thirty-one-characters")==0 && get_name(self,b.name)==0);
    assert(strlen(b.name)==31 && !strncmp(b.name,"a-thread-name-longer-than-thirt",31) && b.canary==0xfeedface);
    assert((uint32_t)get_name(self,NULL)==0x80020016 && (uint32_t)get_name(&b,b.name)==0x80020003);
    assert(rename_thread(self,"host")==0);
}
/* Spec 2 (block C): libc's localtime_r (+0x1740), mktime (+0xc0a0) and strftime %z (+0x1fb00) convert
   through the kernel with a 16-byte {t, west, dst} record: local = utc + west + dst. The offset is the
   host zone's at that instant with daylight saving folded in, like sceKernelGettimezone. */
static void time_conversion(void) {
    typedef struct { int64_t t; int32_t west, dst; } Timesec;
    typedef int32_t (ABI *ToLocal)(int64_t,int64_t *,Timesec *,uint64_t *);
    typedef int32_t (ABI *ToUtc)(int64_t,int32_t,int64_t *,Timesec *,int32_t *);
    ToLocal to_local=GET(ToLocal,"-o5uEDpN+oY#libkernel");
    ToUtc to_utc=GET(ToUtc,"0NTHN1NKONI#libkernel");
    assert(to_local && to_utc);
    assert(!setenv("TZ","EST5EDT,M3.2.0,M11.1.0",1)); tzset();
    const int64_t winter=1700000000, summer=1690000000, twice=1699147800; /* twice: 2023-11-05 01:30 */
    int64_t local=0,utc=0; Timesec ts={0,7,7}; uint64_t dst=7; int32_t dst_seconds=7;
    assert(to_local(winter,&local,&ts,&dst)==0 && local==winter-5*3600);
    assert(ts.t==winter && ts.west==-5*3600 && ts.dst==0 && dst==0);
    assert(to_local(summer,&local,&ts,NULL)==0 && local==summer-4*3600 && ts.t==summer && ts.west==-4*3600 && ts.dst==0);
    assert(to_local(summer,&local,NULL,NULL)==0 && local==summer-4*3600);
    assert((uint32_t)to_local(winter,NULL,&ts,NULL)==0x80020016);
    for (int32_t hint=-1;hint<=1;++hint) { /* tm_isdst from mktime: the folded offset already decides */
        ts=(Timesec){0,7,7}; dst_seconds=7;
        assert(to_utc(winter-5*3600,hint,&utc,&ts,&dst_seconds)==0 && utc==winter);
        assert(ts.t==winter && ts.west==-5*3600 && ts.dst==0 && dst_seconds==0);
        assert(to_utc(summer-4*3600,hint,&utc,NULL,NULL)==0 && utc==summer);
    }
    assert(to_utc(twice,0,&utc,&ts,NULL)==0 && utc+ts.west==twice && to_local(utc,&local,NULL,NULL)==0 && local==twice);
    /* 03:00 that day is EST, but read as UTC it is still EDT: one step would be an hour off. */
    assert(to_utc(twice+5400,0,&utc,&ts,NULL)==0 && utc==twice+5400+5*3600 && ts.west==-5*3600);
    /* 2023-03-12 02:30 does not exist: the offset after the change. */
    assert(to_utc(1678588200,1,&utc,&ts,NULL)==0 && utc==1678588200+4*3600 && ts.west==-4*3600);
    assert((uint32_t)to_utc(winter,0,NULL,&ts,NULL)==0x80020016);
}
/* Spec 2 (block F): libc's <filesystem> reads directories with sceKernelGetdents (libc +0x95141) and
   sets times and permissions with sceKernelUtimes (+0x959c8, timeval[2]) and sceKernelChmod (+0x95cb6).
   Game files stay read-only; writable mounts change the host file. */
static void file_metadata(void) {
    typedef int32_t (ABI *Getdents)(int,char *,uint64_t);
    typedef int32_t (ABI *Utimes)(const char *,const int64_t *);
    typedef int32_t (ABI *Chmod)(const char *,uint16_t);
    Getdents getdents=GET(Getdents,"j2AIqSqJP0w#libkernel");
    Utimes set_times=GET(Utimes,"0Cq8ipKr9n0#libkernel");
    Chmod set_mode=GET(Chmod,"fgIsQ10xYVA#libkernel");
    assert(getdents && set_times && set_mode);
    char root[]="out/file-metadata-XXXXXX",game[64],user[64],host[96],cmd[96];
    assert(mkdtemp(root));
    snprintf(game,sizeof game,"%s/game",root); snprintf(user,sizeof user,"%s/user",root);
    assert(!mkdir(game,0755));
    snprintf(host,sizeof host,"%s/asset.bin",game); FILE *f=fopen(host,"w"); assert(f && !fclose(f));
    runtime_file_configure(game,user);
    snprintf(host,sizeof host,"%s/data/meta.txt",user); f=fopen(host,"w"); assert(f && !fclose(f));
    int dir=(int)runtime_file_open("/app0",0x20000,0); assert(dir>=3);
    char entries[1024]; int32_t n=getdents(dir,entries,sizeof entries),found=0;
    assert(n>0);
    for (int32_t p=0;p<n;) {
        uint16_t length; memcpy(&length,entries+p+4,2); assert(length);
        found|=(!strcmp(entries+p+8,"asset.bin") && entries[p+6]==8); p+=length;
    }
    assert(found && getdents(dir,entries,sizeof entries)==0); /* end of the directory */
    assert((uint32_t)getdents(dir,NULL,sizeof entries)==0x8002000e && (uint32_t)getdents(dir,entries,256)==0x80020016);
    assert(runtime_file_close(dir)==0 && (uint32_t)getdents(dir,entries,sizeof entries)==0x80020009);
    const int64_t times[4]={1000000000,250000,1100000000,500000},bad[4]={0,1000000,0,0};
    struct stat s;
    assert(set_times("/data/meta.txt",times)==0 && !stat(host,&s));
    assert(s.st_atim.tv_sec==1000000000 && s.st_atim.tv_nsec==250000000 && s.st_mtim.tv_sec==1100000000 && s.st_mtim.tv_nsec==500000000);
    time_t before=time(NULL);
    assert(set_times("/data/meta.txt",NULL)==0 && !stat(host,&s) && s.st_mtim.tv_sec>=before); /* NULL: now */
    assert((uint32_t)set_times("/data/meta.txt",bad)==0x80020016);
    assert((uint32_t)set_times("/app0/asset.bin",times)==0x8002001e && (uint32_t)set_times("/data/missing",times)==0x80020002);
    assert(set_mode("/data/meta.txt",0600)==0 && !stat(host,&s) && (s.st_mode&0777)==0600);
    assert(set_mode("/data/meta.txt",0644)==0 && !stat(host,&s) && (s.st_mode&0777)==0644);
    assert((uint32_t)set_mode("/app0/asset.bin",0644)==0x8002001e && (uint32_t)set_mode("/data/missing",0644)==0x80020002);
    snprintf(cmd,sizeof cmd,"rm -rf %s",root); assert(system(cmd)==0);
}
/* Spec 2, section 9: these stay traps until the game calls them (scePthreadCancel: destructors of the
   eboot's thread class, +0x12af2e; the other two: libc's unwinder and a heap path never taken). Named
   so the STOP line says which API it is, never resolved. */
static void decided_traps(void) {
    static const char *const traps[][2]={{"qBDmpCyGssE#libkernel","scePthreadCancel"},
        {"RpQJJVKTiFM#libkernel","sceKernelGetModuleInfoForUnwind"},
        {"-YTW+qXc3CQ#libkernel","sceKernelInternalMemoryGetModuleSegmentInfo"}};
    for (int i=0;i<3;++i) assert(!strcmp(runtime_import_name(traps[i][0]),traps[i][1]) && !runtime_resolve(traps[i][0],0));
}
/* runtime_report ends with the GPU: line (hito H2); nothing ran on the GPU, so all zero. */
static void gpu_stats_line(void) {
    char line[160];
    size_t n=runtime_gpu_stats_line(line,sizeof(line));
    assert(n==strlen(line) && !strcmp(line,"GPU: submits=0 draws=0 dispatches=0 flips=0 unimplemented=0\n"));
    char small[8]; assert(runtime_gpu_stats_line(small,sizeof(small))==7 && !strcmp(small,"GPU: su"));
    capture_begin(); runtime_report();
    const char *last=strstr(capture_end(),"GPU: submits=");
    assert(last && !strcmp(last,line));
}
int main(int argc,char **argv) {
    runtime_start(0); assert(runtime_resolve("bzQExy189ZI#libc",0)==0);
    runtime_start(1);
    assert(runtime_resolve("bzQExy189ZI#wrong#module",0)==0);
    assert(runtime_resolve("unknown",0)==0);
    /* Keys are NID#library for every game; libScePosix shares libkernel's contracts. */
    assert(runtime_resolve("n88vx3C5nW8#libScePosix",0) &&
           runtime_resolve("n88vx3C5nW8#libScePosix",0)==runtime_resolve("n88vx3C5nW8#libkernel",0));
    assert(!strcmp(runtime_import_name("n88vx3C5nW8#libScePosix"),"gettimeofday"));
    assert(runtime_resolve("n88vx3C5nW8#libScePosixX",0)==0);
    assert(runtime_resolve("8zTFvBIAIN8#libc",1)==0);
    assert(runtime_resolve("f7uOxY9mM1U#libkernel",0)==0);
    uint64_t *canary=(uint64_t *)runtime_resolve("f7uOxY9mM1U#libkernel",1);
    assert(canary && *canary && !(*canary & 255));
    if (argc>1 && !strcmp(argv[1],"--guard-recursion")) {
        uint64_t guard=0; Acquire acquire=GET(Acquire,"3GPpjQdAMTw#libc");
        acquire(&guard); acquire(&guard); return 99;
    }
    if (argc>1 && !strcmp(argv[1],"--bad-tls")) {
        static const char data[4]={0};
        runtime_set_libc_tls(data,4,8);
        const uint64_t index[]={2,8};
        GET(TlsAddress,"vNe1w4diLCs#libkernel")(index); return 99;
    }
    if (argc>1 && !strcmp(argv[1],"--stack-failure")) {
        GET(GuestCallback,"Ou3iL1abvng#libkernel")(); return 99;
    }
    if (argc>1 && !strcmp(argv[1],"--rwlock-lifecycle")) { rw_lifecycle(); return 0; }
    if (argc>1 && !strcmp(argv[1],"--rwlock-concurrency")) { rw_concurrency(); return 0; }
    if (argc>1 && !strcmp(argv[1],"--rwlock-timeouts")) { rw_timeouts(); return 0; }
    if (argc>1 && !strcmp(argv[1],"--thread-dtors")) { thread_dtors_contract(); puts("PASS: thread dtors"); return 0; }
    if (argc>1 && !strcmp(argv[1],"--posix-rwlocks")) { posix_rwlocks(); return 0; }
    if (argc>1 && !strcmp(argv[1],"--thread-name")) { thread_get_name(); return 0; }
    if (argc>1 && !strcmp(argv[1],"--time-conversion")) { time_conversion(); return 0; }
    if (argc>1 && !strcmp(argv[1],"--file-metadata")) { file_metadata(); return 0; }
    if (argc>1 && !strcmp(argv[1],"--decided-traps")) { decided_traps(); return 0; }
    if (argc>2 && !strcmp(argv[1],"--savedata-load")) { savedata_load(argv[2]); return 0; }
    if (argc>1 && !strcmp(argv[1],"--rtld-underflow")) {
        static int dso; GET(DsoCount,"8OnWXlgQlvo#libkernel")(&dso); return 99;
    }
    libc_support(); posix_mutexes(); wall_time(); kernel_time_names(); sanitizer_and_neo(); heap_trace_info(); audiodec_init(); savedata_initialize3(); savedata_mount2(); savedata_umount_with_backup(); savedata_backup_queries(); savedata_call_log(); savedata_survives_remount(argv[0]); discmap_location(); gpu_stats_line(); exit_handlers(); guards(); mutexes(); direct_memory(); memory_primitives();
    rw_lifecycle(); rw_concurrency(); rw_timeouts(); posix_rwlocks(); thread_get_name(); file_metadata(); decided_traps();
    time_conversion(); /* last: changes TZ */
    puts("PASS: callback lifecycle, guard ABI, mutex errors, shared direct memory, memory primitives, resolver scope, "
         "POSIX rwlocks, thread names, file metadata, time conversion, GPU stats line");
    return 0;
}
