/* HEVAG frame decoder (third_party/vgmstream_hevag; Spec 4b, docs/specs/2026-10-14-spec-4-audiodec.md).
 * The hand-computed cases pin the frame layout and both halves of the table, and are worked out with
 * vgmstream's formula: they cannot tell whether Sony rounds the same way (spec 11). matches_vgmstream_cli
 * compares a whole effect of vag.mrg (argv[1], or out/audiodec-clips/vag0.vagp from
 * tools/extract_audio_clips.py, game content) with an independent build of vgmstream-cli: $VGMSTREAM_CLI,
 * out/vgmstream-cli or vgmstream-cli on PATH (docs/ENTORNO.md). Without the clip or the tool it prints
 * SKIP: and does not run. */
#define _DEFAULT_SOURCE
#undef NDEBUG /* the checks are asserts; release builds define NDEBUG */
#include "third_party/vgmstream_hevag/hevag.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* A frame as the encoder lays it out: index split between the high nibbles of bytes 0 and 1, shift in
   the low nibble of byte 0, flag in the low nibble of byte 1, then the codes, low nibble first. */
static void frame_of(uint8_t f[HEVAG_FRAME_BYTES], int index, int shift, int flag, const int codes[HEVAG_FRAME_SAMPLES]) {
    f[0]=(uint8_t)((index&0xf)<<4|shift); f[1]=(uint8_t)((index&0xf0)|flag);
    for (int i=0;i<HEVAG_FRAME_SAMPLES;i+=2) f[2+i/2]=(uint8_t)((codes[i]&0xf)|(codes[i+1]&0xf)<<4);
}

/* Index 2 is the classic VAG pair 115/64, -52/64: hist3 and hist4 must not count. Shift 12 makes each
   code its own value. The classic integer formula ((h1*115 - h2*52) >> 6) gives the same samples here
   (the predictions are positive; on negative ones its floor and vgmstream's truncation differ by 1). */
static void low_index_matches_classic_vag(void) {
    int codes[HEVAG_FRAME_SAMPLES]={3,-2};
    uint8_t f[HEVAG_FRAME_BYTES]; frame_of(f,2,12,0,codes);
    HevagState s={{100,50,12345,-12345}}, plain={{100,50,0,0}};
    int16_t out[HEVAG_FRAME_SAMPLES], ref[HEVAG_FRAME_SAMPLES];
    assert(hevag_decode_frame(&s,f,out)==0);
    /* 3 + trunc(100*1.796875 - 50*0.8125) = 3 + trunc(139.0625); -2 + trunc(142*1.796875 - 100*0.8125)
       = -2 + trunc(173.90625); trunc(307.265625 - 115.375); trunc(343.203125 - 138.9375) */
    assert(out[0]==142 && out[1]==171 && out[2]==191 && out[3]==204);
    assert(hevag_decode_frame(&plain,f,ref)==0 && !memcmp(out,ref,sizeof out));
    assert(s.hist[0]==out[27] && s.hist[1]==out[26] && s.hist[2]==out[25] && s.hist[3]==out[24]);
}

/* Index 0x5a needs both nibbles (0xa in byte 0, 0x5 in byte 1) and all four history samples:
   {1.4737549, -0.22900391, -0.24621582, -0.073364258}. */
static void high_index_uses_extended_table(void) {
    int codes[HEVAG_FRAME_SAMPLES]={0};
    uint8_t f[HEVAG_FRAME_BYTES]; frame_of(f,0x5a,12,0,codes);
    assert(f[0]==0xac && f[1]==0x50);
    HevagState s={{1000,2000,3000,4000}};
    int16_t out[HEVAG_FRAME_SAMPLES];
    assert(hevag_decode_frame(&s,f,out)==0);
    /* trunc(1473.7549 - 458.00782 - 738.64746 - 293.45703) = trunc(-16.357);
       trunc(-16*1.4737549 - 229.00391 - 492.43164 - 220.09277) = trunc(-965.108) */
    assert(out[0]==-16 && out[1]==-965);
}

static void silence_frame_is_zero(void) {
    static const int16_t zero[HEVAG_FRAME_SAMPLES];
    int16_t out[HEVAG_FRAME_SAMPLES];
    uint8_t blank[HEVAG_FRAME_BYTES]={0};
    HevagState s={{0}};
    assert(hevag_decode_frame(&s,blank,out)==0 && !memcmp(out,zero,sizeof out));
    /* The sector padding after each VAGp of vag.mrg (docs/audiodec.md 2.6). */
    uint8_t padding[HEVAG_FRAME_BYTES]={0x0c};
    assert(hevag_decode_frame(&s,padding,out)==0 && !memcmp(out,zero,sizeof out));
    /* The end frame: whatever its codes and the history, silence, and the history runs out. */
    int codes[HEVAG_FRAME_SAMPLES]={7,-8,5,1,7,7};
    uint8_t end[HEVAG_FRAME_BYTES]; frame_of(end,0x5a,0,HEVAG_FLAG_END,codes);
    HevagState busy={{500,400,300,200}};
    assert(hevag_decode_frame(&busy,end,out)==HEVAG_FLAG_END && !memcmp(out,zero,sizeof out));
    assert(!busy.hist[0] && !busy.hist[1] && !busy.hist[2] && !busy.hist[3]);
}

/* Frame B has only zero codes: all it outputs comes from the history frame A left. */
static void history_carries_between_frames(void) {
    int codes_a[HEVAG_FRAME_SAMPLES]={7}, codes_b[HEVAG_FRAME_SAMPLES]={0};
    uint8_t a[HEVAG_FRAME_BYTES], b[HEVAG_FRAME_BYTES];
    frame_of(a,1,8,0,codes_a); frame_of(b,2,12,1,codes_b);
    HevagState s={{0}}, fresh={{0}};
    int16_t out_a[HEVAG_FRAME_SAMPLES], out_b[HEVAG_FRAME_SAMPLES], alone[HEVAG_FRAME_SAMPLES];
    assert(hevag_decode_frame(&s,a,out_a)==0);
    /* 7 << 12 >> 8 = 112, then trunc(x * 0.9375) down to 14 */
    assert(out_a[0]==112 && out_a[1]==105 && out_a[27]==14);
    assert(s.hist[0]==14 && s.hist[1]==15 && s.hist[2]==16 && s.hist[3]==18);
    assert(hevag_decode_frame(&s,b,out_b)==1);
    /* trunc(14*1.796875 - 15*0.8125) = trunc(12.96875) */
    assert(out_b[0]==12 && out_b[1]==10);
    assert(hevag_decode_frame(&fresh,b,alone)==1);
    for (int i=0;i<HEVAG_FRAME_SAMPLES;++i) assert(alone[i]==0);
}

/* The output saturates to 16 bits, the history does not ("no clamping here" in vgmstream). */
static void output_clamps_but_history_does_not(void) {
    int up[HEVAG_FRAME_SAMPLES]={7}, down[HEVAG_FRAME_SAMPLES]={-8};
    uint8_t f[HEVAG_FRAME_BYTES]; frame_of(f,1,0,0,up);
    HevagState s={{30000}};
    int16_t out[HEVAG_FRAME_SAMPLES];
    hevag_decode_frame(&s,f,out);
    /* 28672 + trunc(30000*0.9375) = 56797, then trunc(56797*0.9375) = 53247: a clamped history would
       give trunc(32767*0.9375) = 30719 */
    assert(out[0]==32767 && out[1]==32767);
    frame_of(f,1,0,0,down); s=(HevagState){{-30000}};
    hevag_decode_frame(&s,f,out);
    assert(out[0]==-32768 && out[1]==-32768);
}

/* The index has 8 bits and the table 128 rows: vgmstream reads row 0 for the rest, which predicts
   nothing, so the samples are the codes themselves. Frames come from the guest. */
static void index_over_127_reads_row_0(void) {
    int codes[HEVAG_FRAME_SAMPLES]={1,2,3,-4,5,-6,7,-8};
    uint8_t high[HEVAG_FRAME_BYTES], zero[HEVAG_FRAME_BYTES];
    frame_of(high,0xf5,12,0,codes); frame_of(zero,0,12,0,codes);
    HevagState a={{1000,2000,3000,4000}}, b=a;
    int16_t out[HEVAG_FRAME_SAMPLES], ref[HEVAG_FRAME_SAMPLES];
    hevag_decode_frame(&a,high,out); hevag_decode_frame(&b,zero,ref);
    assert(!memcmp(out,ref,sizeof out) && out[0]==1 && out[3]==-4 && out[7]==-8 && out[8]==0);
}

static unsigned char *load(const char *path, long *size) {
    FILE *f=fopen(path,"rb");
    if (!f) return NULL;
    fseek(f,0,SEEK_END); *size=ftell(f); rewind(f);
    unsigned char *data=malloc((size_t)*size);
    assert(data && fread(data,1,(size_t)*size,f)==(size_t)*size);
    fclose(f);
    return data;
}
static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static uint32_t le32(const unsigned char *p) { return (uint32_t)p[3]<<24|(uint32_t)p[2]<<16|(uint32_t)p[1]<<8|p[0]; }

static const char *vgmstream_cli(void) {
    const char *env=getenv("VGMSTREAM_CLI");
    if (env && *env) return env;
    if (!access("out/vgmstream-cli",X_OK)) return "out/vgmstream-cli";
    return system("command -v vgmstream-cli >/dev/null 2>&1")==0 ? "vgmstream-cli" : NULL;
}

/* The same rules as vgmstream's meta (src/meta/vag.c, versions 0x00020001 and 0x00030000): data at 0x30,
   size at 0x0c, channels at 0x1e (0 is mono), channels interleaved every frame. */
static void matches_vgmstream_cli(const char *clip) {
    long size; unsigned char *vag=load(clip,&size);
    if (!vag) { printf("SKIP: %s missing (python3 tools/extract_audio_clips.py --image Tsukihime/CUSA47999/image --out out/audiodec-clips)\n",clip); return; }
    const char *cli=vgmstream_cli();
    if (!cli) { printf("SKIP: matches_vgmstream_cli: no vgmstream-cli ($VGMSTREAM_CLI, out/vgmstream-cli or PATH; docs/ENTORNO.md)\n"); free(vag); return; }
    assert(size>=0x30 && !memcmp(vag,"VAGp",4) && (be32(vag+4)==0x00020001 || be32(vag+4)==0x00030000));
    uint32_t channels=vag[0x1e] ? vag[0x1e] : 1, data=be32(vag+0x0c);
    assert(0x30+(long)data<=size && data%(HEVAG_FRAME_BYTES*channels)==0);
    uint32_t frames=data/HEVAG_FRAME_BYTES/channels, samples=frames*HEVAG_FRAME_SAMPLES;
    int16_t *mine=malloc((size_t)samples*channels*sizeof *mine);
    HevagState *state=calloc(channels,sizeof *state);
    assert(mine && state);
    for (uint32_t f=0;f<frames;++f)
        for (uint32_t c=0;c<channels;++c) {
            int16_t out[HEVAG_FRAME_SAMPLES];
            hevag_decode_frame(&state[c],vag+0x30+(f*channels+c)*HEVAG_FRAME_BYTES,out);
            for (int i=0;i<HEVAG_FRAME_SAMPLES;++i) mine[(f*HEVAG_FRAME_SAMPLES+(uint32_t)i)*channels+c]=out[i];
        }
    /* vgmstream picks its parser by extension, and .vag is one of them. */
    char in[]="/tmp/hevag-test-XXXXXX.vag", wav[sizeof in+4];
    int fd=mkstemps(in,4); assert(fd>=0);
    assert(write(fd,vag,(size_t)size)==(ssize_t)size); close(fd);
    snprintf(wav,sizeof wav,"%s.wav",in);
    char command[512]; snprintf(command,sizeof command,"\"%s\" -o \"%s\" \"%s\" >/dev/null 2>&1",cli,wav,in);
    int status=system(command);
    long wav_size; unsigned char *w=load(wav,&wav_size);
    unlink(in); unlink(wav);
    if (status || !w) { fprintf(stderr,"FAIL: %s did not decode %s (status %d)\n",cli,clip,status); exit(1); }
    assert(wav_size>=12 && !memcmp(w,"RIFF",4) && !memcmp(w+8,"WAVE",4));
    const unsigned char *pcm=NULL; uint32_t pcm_bytes=0;
    for (long at=12;at+8<=wav_size;) {
        uint32_t length=le32(w+at+4);
        if (!memcmp(w+at,"fmt ",4)) assert((w[at+10]|w[at+11]<<8)==(int)channels && (w[at+22]|w[at+23]<<8)==16);
        if (!memcmp(w+at,"data",4)) { pcm=w+at+8; pcm_bytes=length; break; }
        at+=8+length+(length&1);
    }
    assert(pcm && pcm_bytes==samples*channels*2);
    for (uint32_t i=0;i<samples*channels;++i) {
        int16_t theirs=(int16_t)(pcm[2*i]|pcm[2*i+1]<<8);
        if (theirs!=mine[i]) { fprintf(stderr,"FAIL: %s sample %u: vgmstream %d, hevag %d\n",clip,i,theirs,mine[i]); exit(1); }
    }
    printf("hevag: %s matches %s (%u samples x %u channels)\n",clip,cli,samples,channels);
    free(w); free(state); free(mine); free(vag);
}

int main(int argc, char **argv) {
    low_index_matches_classic_vag();
    high_index_uses_extended_table();
    silence_frame_is_zero();
    history_carries_between_frames();
    output_clamps_but_history_does_not();
    index_over_127_reads_row_0();
    matches_vgmstream_cli(argc>1 ? argv[1] : "out/audiodec-clips/vag0.vagp");
    puts("PASS: HEVAG decoder");
    return 0;
}
