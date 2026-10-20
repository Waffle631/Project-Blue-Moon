/* Offline AppContent provider for the configured base-game profile.
 * No package/DLC mounting, downloads, entitlement or license emulation. */
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#define INVALID_ID ((int32_t)0x805a1000)
#define NOT_LOADED ((int32_t)0x805a1001)
#define PARAMETER ((int32_t)0x80d90002)
#define BUSY ((int32_t)0x80d90003)
/* Spec 3 (4.2): there is no module info to hand out, and made-up info would corrupt unwinding. libc.prx
 * (+0xb852c) only tests for nonzero, so the exact code does not matter; INVALID_ID is this library's own
 * "no such module". */
#define UNWIND_UNAVAILABLE INVALID_ID
static unsigned refs, initialized, configured;
static uint32_t parameters[5];
static unsigned queries, lists;
void runtime_content_configure(const uint32_t values[5]) {
    if (refs || initialized || (values[0]!=1 && values[0]!=3)) {
        fputs("ERROR: invalid or late content profile\n",stderr); exit(1);
    }
    memcpy(parameters,values,sizeof(parameters)); configured=1;
}
static void require_provider(void) {
    if (!configured) { fputs("STOP: AppContent needs --content-profile\n",stderr); exit(21); }
}
/* System modules: every library this eboot imports is provided by the host
 * runtime, so loading one only tracks a reference count. AppContent (0xb4)
 * additionally requires the configured content profile. */
static unsigned module_refs[0x10000];
static ABI int32_t module_load(uint16_t id) {
    if (!id) return INVALID_ID;
    if (id==0xb4) require_provider();
    if (module_refs[id]==UINT32_MAX) { fputs("STOP: module reference overflow\n",stderr); exit(21); }
    ++module_refs[id];
    if (id==0xb4) { refs=module_refs[id]; printf("Runtime: AppContent host provider loaded; references=%u\n",refs); }
    else printf("Runtime: system module 0x%x loaded (host implementation)\n",id);
    return 0;
}
/* libSceFios2 loads 0xd7 (DiscMap) this way with argc=0, argv=NULL and unloads it with the public
 * sceSysmoduleUnloadModule: same ids and references. A host module has no start function, so its
 * start result is 0. Ids beyond 16 bits (internal 0x8000xxxx) have not been seen and stop. */
static ABI int32_t module_load_arg(uint32_t id,int32_t argc,const void *argv,uint64_t flags,int32_t *result) {
    if (id>0xffff) { fprintf(stderr,"STOP: unsupported internal system module id 0x%x\n",id); exit(21); }
    printf("Runtime: sceSysmoduleLoadModuleInternalWithArg(0x%x, argc=%d, argv=%p, flags=0x%llx)\n",id,argc,argv,(unsigned long long)flags);
    int32_t loaded=module_load((uint16_t)id);
    if (!loaded && result) *result=0;
    return loaded;
}
static ABI int32_t module_loaded(uint16_t id) {
    if (!id) return INVALID_ID;
    return module_refs[id] ? 0 : NOT_LOADED;
}
/* libc.prx's unwinder asks for the module that holds a pc (flags 1, info size 0x130). On an error it
 * reports no unwind info for that pc and never reads the info. */
static atomic_uint unwind_queries;
static ABI int32_t module_unwind_info(uintptr_t addr,int32_t flags,void *info) {
    (void)flags; (void)info;
    if (!atomic_fetch_add(&unwind_queries,1)) printf("Runtime: sceSysmoduleGetModuleInfoForUnwind(0x%llx) unavailable\n",(unsigned long long)addr);
    return UNWIND_UNAVAILABLE;
}
static ABI int32_t module_unload(uint16_t id) {
    int32_t result=module_loaded(id);
    if (result) return result;
    --module_refs[id];
    if (id==0xb4) { refs=module_refs[id]; if (!refs) initialized=0; }
    return 0;
}
static void require_loaded(void) {
    require_provider();
    if (!refs) { fputs("STOP: AppContent called before module load\n",stderr); exit(21); }
}
static ABI int32_t content_init(const unsigned char *init,unsigned char *boot) {
    require_loaded();
    if (!init || !boot) return PARAMETER;
    if (initialized) return BUSY;
    /* Only the reserved-zero init profile is implemented. */
    for (unsigned i=0;i<32;++i) if (init[i]) return PARAMETER;
    memset(boot,0,40); initialized=1;
    puts("Runtime: AppContent initialized; offline base-game profile, no mounted add-ons");
    return 0;
}
static void require_initialized(void) {
    require_loaded();
    if (!initialized) { fputs("STOP: AppContent query before initialization\n",stderr); exit(21); }
}
static ABI int32_t param_int(uint32_t id,int32_t *out) {
    require_initialized();
    if (id>4 || !out) return PARAMETER;
    memcpy(out,&parameters[id],4); ++queries;
    printf("Runtime: AppContent parameter %u = %d\n",id,*out);
    return 0;
}
static ABI int32_t addon_list(uint32_t service,void *list,uint32_t capacity,uint32_t *hits) {
    require_initialized();
    if (service) { fputs("STOP: nonzero AppContent service label unsupported\n",stderr); exit(21); }
    if ((!capacity || !list) && !hits) return PARAMETER;
    /* No add-ons are mounted by this provider. Never touch unused list slots. */
    if (hits) *hits=0;
    ++lists; return 0;
}
uintptr_t runtime_content_resolve(const char *name) {
    if (!strcmp(name,"g8cM39EUZ6o#libSceSysmodule")) return (uintptr_t)module_load;
    if (!strcmp(name,"hHrGoGoNf+s#libSceSysmodule")) return (uintptr_t)module_load_arg;
    if (!strcmp(name,"fMP5NHUOaMk#libSceSysmodule")) return (uintptr_t)module_loaded;
    if (!strcmp(name,"eR2bZFAAU0Q#libSceSysmodule")) return (uintptr_t)module_unload;
    if (!strcmp(name,"4fU5yvOkVG4#libSceSysmodule")) return (uintptr_t)module_unwind_info;
    if (!strcmp(name,"R9lA82OraNs#libSceAppContent")) return (uintptr_t)content_init;
    if (!strcmp(name,"99b82IKXpH4#libSceAppContent")) return (uintptr_t)param_int;
    if (!strcmp(name,"xnd8BJzAxmk#libSceAppContent")) return (uintptr_t)addon_list;
    return 0;
}
void runtime_content_report(void) {
    printf("Runtime: AppContent references=%u, initialized=%u, parameter queries=%u, list queries=%u, unwind info queries=%u\n",
           refs,initialized,queries,lists,atomic_load(&unwind_queries));
}
