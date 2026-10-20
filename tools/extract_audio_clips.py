"""Extract the Audiodec reference clips (Spec 4, docs/specs/2026-10-14-spec-4-audiodec.md, §8).

  python3 tools/extract_audio_clips.py --image Tsukihime/CUSA47999/image --out out/audiodec-clips

Writes one RIFF ATRAC9 clip per configuration the game uses (tests/test_audiodec.c compares the
runtime against LibAtrac9 on each) and the first real HEVAG effect of vag.mrg (4b). Game content is
copyrighted: the clips stay in out/ and are never committed. Exit status 1 if any clip is missing."""
import argparse
import struct
import sys
from pathlib import Path

SECTOR=0x800
ATRAC9_GUID=bytes.fromhex('d242e147ba368d4d88fc61654f8c836c')  # 47E142D2-36BA-4D8D-88FC-61654F8C836C
# (file, archive, ATRAC9 config at RIFF +0x40, block size at +0x20); first matching entry wins.
CLIPS=[('bgm_typ.at9','bgm','fe740ff0',512),('bgm_rare.at9','bgm','fe7409f0',320),
       ('vse_stereo.at9','vse','fe740bf0',384),('vse_mono.at9','vse','fe700bf0',384),
       ('vse_mono_small.at9','vse','fe7004f0',160)]
VAG_CLIP='vag0.vagp'


# .hed entry: {u32 sector; u16 sectors; u16 size & 0xffff}; the top nibble of `sector` holds bits
# 16-19 of the sector number. All-0xff entries (the last two of vse.hed and vag.hed) have no data.
def entries(path):
    data=path.read_bytes()
    for index in range(len(data)//8):
        value,sectors,_=struct.unpack_from('<IHH',data,index*8)
        if value!=0xffffffff: yield index,((value&0xffff)|(value>>28)<<16)*SECTOR,sectors*SECTOR

def read_hed(path):
    return [(offset,length) for _,offset,length in entries(path)]

def read_blob(mrg,offset,head_size,size_of):
    with open(mrg,'rb') as f:
        f.seek(offset); head=f.read(head_size)
        size=size_of(head) if len(head)==head_size else None
        if size is None or size<head_size: return None
        data=head+f.read(size-head_size)
    return data if len(data)==size else None

def extract_riff(mrg,offset):
    return read_blob(mrg,offset,12,lambda h:struct.unpack_from('<I',h,4)[0]+8
                     if h[:4]==b'RIFF' and h[8:]==b'WAVE' else None)

# VAGp header is 0x30 bytes, big-endian data size at +0x0c, name at +0x20.
def extract_vagp(mrg,offset):
    return read_blob(mrg,offset,0x30,lambda h:0x30+struct.unpack_from('>I',h,12)[0] if h[:4]==b'VAGp' else None)

# (config hex, block size, channels) of a WAVE_FORMAT_EXTENSIBLE ATRAC9 RIFF, or None.
def atrac9_info(riff):
    if len(riff)<0x44 or riff[12:16]!=b'fmt ' or struct.unpack_from('<H',riff,0x14)[0]!=0xfffe: return None
    if riff[0x2c:0x3c]!=ATRAC9_GUID: return None
    return riff[0x40:0x44].hex(),struct.unpack_from('<H',riff,0x20)[0],struct.unpack_from('<H',riff,0x16)[0]

def write(out,name,blob,where,detail):
    (out/name).write_bytes(blob); print(f'{name}: {where}, {detail}, {len(blob)} bytes')

def extract_atrac9(image,out,archive):
    wanted={(config,block):name for name,arch,config,block in CLIPS if arch==archive}
    if (image/f'{archive}.hed').exists():
        for index,offset,_ in entries(image/f'{archive}.hed'):
            if not wanted: break
            riff=extract_riff(image/f'{archive}.mrg',offset); info=riff and atrac9_info(riff)
            if info and info[:2] in wanted:
                write(out,wanted.pop(info[:2]),riff,f'{archive}.mrg entry {index}',
                      f'config {info[0]}, block {info[1]}, {info[2]} ch')
    for (config,block),name in wanted.items(): print(f'{name}: missing (no {archive} entry with config {config}, block {block})')
    return not wanted

# Entry 0 of vag.mrg is "dummy", 1984 bytes of silence: useless as a decoder reference.
def extract_vag(image,out):
    if (image/'vag.hed').exists():
        for index,offset,_ in entries(image/'vag.hed'):
            vag=extract_vagp(image/'vag.mrg',offset)
            name=vag and vag[0x20:0x30].split(b'\0')[0].decode('ascii','replace')
            if vag and name!='dummy':
                write(out,VAG_CLIP,vag,f'vag.mrg entry {index}',
                      f'{name}, version {vag[4:8].hex()}, {struct.unpack_from(">I",vag,16)[0]} Hz'); return True
    print(f'{VAG_CLIP}: missing (no VAGp effect in vag.mrg)'); return False

def main(argv=None):
    root=Path(__file__).resolve().parent.parent
    p=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--image',type=Path,default=root/'Tsukihime/CUSA47999/image')
    p.add_argument('--out',type=Path,default=root/'out/audiodec-clips')
    a=p.parse_args(argv); a.out.mkdir(parents=True,exist_ok=True)
    found=[extract_atrac9(a.image,a.out,'bgm'),extract_atrac9(a.image,a.out,'vse'),extract_vag(a.image,a.out)]
    return 0 if all(found) else 1

if __name__=='__main__':
    sys.exit(main())
