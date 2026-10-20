#include "runtime.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef int32_t (ABI *Module)(uint16_t);
typedef int32_t (ABI *Init)(const void *,void *);
typedef int32_t (ABI *Param)(uint32_t,int32_t *);
typedef int32_t (ABI *List)(uint32_t,void *,uint32_t,uint32_t *);
typedef int32_t (ABI *LoadArg)(uint32_t,int32_t,const void *,uint64_t,int32_t *);
typedef int32_t (ABI *Unwind)(uintptr_t,int32_t,void *);
#define GET(t,n) ((t)runtime_content_resolve(n))
/* libc.prx's unwinder asks for the unwind info of a pc (libc +0xb852c: flags 1, size 0x130 in the
 * first field) and, on any nonzero result, reports that it has none without reading the info. So the
 * answer is an error that never writes, whatever the arguments. */
static void unwind_unavailable(void) {
    Unwind unwind=GET(Unwind,"4fU5yvOkVG4#libSceSysmodule");
    unsigned char info[0x130]; memset(info,0xa5,sizeof(info));
    assert(unwind && (uint32_t)unwind((uintptr_t)unwind_unavailable,1,info)==0x805a1000);
    assert((uint32_t)unwind((uintptr_t)unwind_unavailable,0,info)==0x805a1000);
    for (unsigned i=0;i<sizeof(info);++i) assert(info[i]==0xa5);
    assert((uint32_t)unwind((uintptr_t)unwind_unavailable,1,NULL)==0x805a1000);
}
int main(int argc,char **argv) {
    Module load=GET(Module,"g8cM39EUZ6o#libSceSysmodule");
    Module loaded=GET(Module,"fMP5NHUOaMk#libSceSysmodule");
    Module unload=GET(Module,"eR2bZFAAU0Q#libSceSysmodule");
    Init init=GET(Init,"R9lA82OraNs#libSceAppContent");
    Param param=GET(Param,"99b82IKXpH4#libSceAppContent");
    List list=GET(List,"xnd8BJzAxmk#libSceAppContent");
    assert(load && loaded && unload && init && param && list);
    assert(!runtime_content_resolve("g8cM39EUZ6o#libkernel"));
    if (argc>1 && !strcmp(argv[1],"--missing")) { load(0xb4); return 99; }
    if (argc>1 && !strcmp(argv[1],"--unwind")) { unwind_unavailable(); puts("PASS: no unwind info"); return 0; }
    unwind_unavailable();
    const uint32_t profile[]={1,13,0x80000000,0,7};
    runtime_content_configure(profile);
    if (argc>1 && !strcmp(argv[1],"--unknown")) {
        /* Other system modules are host-provided: reference counted, independent. */
        assert((uint32_t)loaded(0xb5)==0x805a1001);
        assert(load(0xb5)==0 && loaded(0xb5)==0 && (uint32_t)loaded(0xb4)==0x805a1001);
        assert(unload(0xb5)==0 && (uint32_t)loaded(0xb5)==0x805a1001);
        puts("PASS: independent module references"); return 0;
    }
    assert((uint32_t)loaded(0)==0x805a1000);
    assert((uint32_t)loaded(0xb4)==0x805a1001);
    assert((uint32_t)unload(0xb4)==0x805a1001);
    assert(load(0xb4)==0 && load(0xb4)==0 && loaded(0xb4)==0);
    unsigned char initial[32]={0};
    struct { unsigned char boot[40]; uint32_t canary; } b;
    memset(&b,0xa5,sizeof(b));
    assert((uint32_t)init(NULL,b.boot)==0x80d90002);
    initial[31]=1; assert((uint32_t)init(initial,b.boot)==0x80d90002); initial[31]=0;
    assert(b.boot[0]==0xa5 && init(initial,b.boot)==0);
    for (unsigned i=0;i<40;++i) assert(b.boot[i]==0);
    assert(b.canary==0xa5a5a5a5);
    b.boot[0]=0xcc; assert((uint32_t)init(initial,b.boot)==0x80d90003 && b.boot[0]==0xcc);
    struct { int32_t value; uint32_t canary; } result={-1,0x1234};
    for (unsigned i=0;i<5;++i) {
        assert(param(i,&result.value)==0 && (uint32_t)result.value==profile[i]);
        assert(result.canary==0x1234);
    }
    result.value=77;
    assert((uint32_t)param(5,&result.value)==0x80d90002 && result.value==77);
    assert((uint32_t)param(0,NULL)==0x80d90002);
    unsigned char entries[48]; memset(entries,0xaa,sizeof(entries));
    struct { uint32_t hits,canary; } h={99,0xabcdef};
    assert(list(0,NULL,0,&h.hits)==0 && h.hits==0 && h.canary==0xabcdef);
    assert(list(0,entries,2,&h.hits)==0 && h.hits==0);
    for (unsigned i=0;i<sizeof(entries);++i) assert(entries[i]==0xaa);
    assert((uint32_t)list(0,NULL,0,NULL)==0x80d90002);
    assert(unload(0xb4)==0 && loaded(0xb4)==0);
    assert((uint32_t)init(initial,b.boot)==0x80d90003);
    assert(unload(0xb4)==0 && (uint32_t)loaded(0xb4)==0x805a1001);
    assert(load(0xb4)==0 && init(initial,b.boot)==0 && unload(0xb4)==0);
    /* libSceFios2 loads 0xd7 with no arguments and unloads it with the public call on failure:
     * same id space and reference count; the start result goes to *result. */
    LoadArg load_arg=GET(LoadArg,"hHrGoGoNf+s#libSceSysmodule");
    assert(load_arg);
    struct { int32_t value; uint32_t canary; } start={-1,0x5678};
    assert(load_arg(0xd7,0,NULL,0,&start.value)==0 && start.value==0 && start.canary==0x5678);
    assert(loaded(0xd7)==0 && unload(0xd7)==0 && (uint32_t)loaded(0xd7)==0x805a1001);
    assert(load_arg(0xd7,0,NULL,0,NULL)==0 && unload(0xd7)==0);
    assert((uint32_t)load_arg(0,0,NULL,0,&start.value)==0x805a1000);
    puts("PASS: AppContent lifecycle, profile and output boundaries");
    return 0;
}
