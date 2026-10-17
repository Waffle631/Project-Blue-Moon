/* System services contracts: dialogs (progress bars run until closed), PlayGo, trophies. */
#undef NDEBUG /* the checks are asserts; release builds define NDEBUG */
#include "../src/runtime_services.c"
#include <assert.h>
#include <unistd.h>

uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *nid) { (void)table; (void)count; (void)nid; return 0; }
int gow3gpu_text_input_begin(const char *text, const char *prompt) { (void)text; (void)prompt; return 0; }
int gow3gpu_text_input_poll(char *text, uint64_t size) { (void)text; (void)size; return 0; }
int64_t runtime_file_open(const char *path, int flags, int mode) { (void)path; (void)flags; (void)mode; return -1; }
int64_t runtime_file_read(int fd, void *buffer, uint64_t size) { (void)fd; (void)buffer; (void)size; return -1; }
int64_t runtime_file_close(int fd) { (void)fd; return 0; }
static int choice_next=-1;
int gow3gpu_choice_begin(const char *title, const char *const *items, const char *const *images, int count, int focus) { (void)title; (void)items; (void)images; (void)focus; return count>0; }
int gow3gpu_choice_poll(void) { return choice_next; }
int runtime_savedata_param(int32_t user, const char *dir, void *param) { (void)user; (void)dir; (void)param; return -1; }
int runtime_savedata_icon(int32_t user, const char *dir, char *out, size_t size) { (void)user; (void)dir; (void)out; (void)size; return -1; }

/* Runs f with stdout sent to a temporary file and returns the number of lines it printed. */
static int printed_lines(void (*f)(void)) {
    fflush(stdout); FILE *tmp=tmpfile(); assert(tmp);
    int saved=dup(1); assert(saved>=0 && dup2(fileno(tmp),1)>=0);
    f(); fflush(stdout); dup2(saved,1); close(saved);
    rewind(tmp); int lines=0,c; while ((c=fgetc(tmp))!=EOF) lines+=c=='\n';
    fclose(tmp); return lines;
}
static void shareplay_twice(void) { assert(shareplay_initialize(NULL,0x1800)==0 && shareplay_initialize(NULL,0x1800)==0); }
static void live_streaming_twice(void) { assert(live_streaming_initialize(0x4000)==0 && live_streaming_initialize(0x4000)==0); }
/* The game's capture policy (eboot +0xee550) in mode 0, then mode 1; it ignores every result. */
static void capture_policy(void) {
    assert(screenshot_disable()==0 && screenshot_overlay("/app0/image/corp.png",0,0,3)==0 && screenshot_enable()==0);
    assert(live_streaming_enable(1)==0 && shareplay_prohibition(0)==0);
    uint32_t prohibited=0; assert(video_recording_info(0xd,&prohibited,4)==0);
    assert(screenshot_disable()==0 && live_streaming_enable(0)==0 && shareplay_prohibition(2)==0);
    prohibited=1; assert(video_recording_info(0xd,&prohibited,4)==0);
}

int main(void) {
    unsigned char param[128]={0}, result[64]={0}; /* the game zeroes the pointers it does not want */
    uint32_t value;

    /* MsgDialog: a user message finishes with OK; a progress bar runs until closed. */
    assert(msg_init()==0);
    memset(param+56,0,4); param[56]=1;
    assert(msg_open(param)==0 && msg_status()==DIALOG_FINISHED);
    assert(msg_result(result)==0);
    memcpy(&value,result+4,4); assert(value==1);
    param[56]=2;
    assert(msg_open(param)==0 && msg_status()==DIALOG_RUNNING);
    assert(msg_result(result)==DIALOG_NOT_FINISHED);
    assert(msg_progress(0,50)==0 && msg_progress(1,50)==DIALOG_PARAM_INVALID);
    assert(msg_close()==0 && msg_status()==DIALOG_FINISHED && msg_close()==DIALOG_NOT_RUNNING);
    assert(msg_progress(0,100)==DIALOG_NOT_RUNNING);
    assert(msg_result(NULL)==DIALOG_ARG_NULL);
    assert(msg_term()==0 && msg_open(NULL)==DIALOG_ARG_NULL);

    /* SaveDataDialog: mode at offset 52 (5 = progress bar); result after the mode field. */
    assert(save_init()==0 && save_ready()==1);
    param[52]=5;
    assert(save_open(param)==0 && save_status()==DIALOG_RUNNING && save_progress(0,10)==0);
    assert(save_close()==0 && save_result(result)==0);
    memcpy(&value,result+8,4); assert(value==0); /* closed by the game: no button */
    param[52]=2;
    assert(save_open(param)==0 && save_status()==DIALOG_FINISHED && save_result(result)==0);
    memcpy(&value,result+8,4); assert(value==1);
    assert(save_term()==0);

    /* Save list: the player's choice comes back as the save directory. */
    char names[2][32]={"SAVE0001","SAVE0002"}, chosen[32];
    unsigned char items[64]={0}, list[128]={0}, out[64]={0};
    const char *names_ptr=names[0]; uint32_t name_count=2; const char *new_title="New";
    memcpy(items+16,&names_ptr,8); memcpy(items+24,&name_count,4); memcpy(items+32,&new_title,8);
    unsigned char *items_ptr=items; char *chosen_ptr=chosen;
    list[52]=1; list[56]=2; memcpy(list+72,&items_ptr,8);
    memcpy(out+16,&chosen_ptr,8);
    assert(save_init()==0 && save_open(list)==0 && save_status()==DIALOG_RUNNING);
    choice_next=2; /* entries: New save, SAVE0001, SAVE0002 */
    assert(save_status()==DIALOG_FINISHED && save_result(out)==0 && !strcmp(chosen,"SAVE0002"));
    memcpy(&value,out+4,4); assert(value==0);
    memcpy(&value,out+8,4); assert(value==1);
    assert(save_open(list)==0);
    choice_next=0;
    assert(save_status()==DIALOG_FINISHED && save_result(out)==0 && chosen[0]==0); /* new save */
    assert(save_open(list)==0);
    choice_next=-2;
    assert(save_status()==DIALOG_FINISHED && save_result(out)==0);
    memcpy(&value,out+4,4); assert(value==SAVE_RESULT_CANCELED);
    assert(save_term()==0);

    /* PlayGo: installed, full speed, progress never divides by zero. */
    int32_t handle=0; uint64_t progress[2]; int32_t speed; uint16_t chunk=0;
    assert(playgo_init(NULL)==0 && playgo_open(&handle,NULL)==0);
    assert(playgo_get_speed(handle,&speed)==0 && speed==2);
    assert(playgo_progress(handle,&chunk,1,progress)==0 && progress[0]==progress[1] && progress[1]);
    assert(playgo_progress(handle,&chunk,0,progress)==PLAYGO_BAD_SIZE);
    assert(playgo_todo(handle,&chunk,1)==0 && playgo_todo(handle,NULL,1)==PLAYGO_BAD_POINTER);
    assert(playgo_close(handle)==0 && playgo_close(handle)==PLAYGO_BAD_HANDLE && playgo_terminate()==0);

    /* Trophies: nothing unlocked. */
    uint32_t flags[4]={~0u,~0u,~0u,~0u}, count=7;
    assert(trophy_unlock_state(1,1,flags,&count)==0 && !flags[0] && !flags[3] && count==0);
    assert(trophy_unlock_state(1,1,NULL,&count)==TROPHY_INVALID && trophy_release(0)==TROPHY_INVALID);

    float ratio=0; assert(safe_area(&ratio)==0 && ratio==1.0f && safe_area(NULL)==SYSTEM_PARAMETER);

    /* System name (id 6): the game asks with a 65-byte buffer and ignores the result. */
    char system_name[65]; memset(system_name,'x',sizeof system_name);
    assert(system_param_string(6,system_name,sizeof system_name)==0 && !strcmp(system_name,"PS4"));
    assert(system_param_string(6,system_name,3)==SYSTEM_PARAMETER && system_param_string(6,NULL,65)==SYSTEM_PARAMETER);
    assert(system_param_string(99,system_name,sizeof system_name)==SYSTEM_PARAMETER);
    /* "No effect" services (Spec 3, 4.1): 0, a log line on the first call only, every call counted. */
    assert(printed_lines(shareplay_twice)==1 && no_effect_calls[SHAREPLAY_INITIALIZE]==2);
    assert(printed_lines(shareplay_twice)==0 && no_effect_calls[SHAREPLAY_INITIALIZE]==4);
    assert(printed_lines(live_streaming_twice)==1 && no_effect_calls[LIVE_STREAMING_INITIALIZE]==2);
    assert(printed_lines(live_streaming_twice)==0 && no_effect_calls[LIVE_STREAMING_INITIALIZE]==4 && no_effect_calls[SHAREPLAY_INITIALIZE]==4);
    assert(printed_lines(capture_policy)==6 && no_effect_calls[SCREENSHOT_DISABLE]==2 && no_effect_calls[SCREENSHOT_OVERLAY]==1);
    assert(no_effect_calls[SCREENSHOT_ENABLE]==1 && no_effect_calls[LIVE_STREAMING_ENABLE]==2);
    assert(no_effect_calls[SHAREPLAY_PROHIBITION]==2 && no_effect_calls[VIDEO_RECORDING_INFO]==2);
    assert(printed_lines(capture_policy)==0 && no_effect_calls[SCREENSHOT_DISABLE]==4);
    puts("services test: ok");
    return 0;
}
