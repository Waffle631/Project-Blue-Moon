"""Compile the selected third-party shadPS4/GoldHEN XML patches of this game (game_identity) into
out/patches.bin for the loader.

Patch addresses are PS4 virtual addresses (eboot base 0x400000); the loader's
image places eboot vaddr 0 at image offset 0. Only literal writes are supported
(bytes, bytes16/32/64, float32/64, utf8, utf16); pattern ("mask") patches are rejected.
compile_patches also reads lines with signatures (Pattern/At, tools/make_signatures.py) that find
each site in any build.
"""
import argparse
import json
import os
import re
import struct
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

from game_identity import APP_VERSION, IDS, TITLE_ID, game_sfo, game_title_id, is_game

EBOOT_BASE=0x400000


def eboot_segments(elf):
    phoff,=struct.unpack_from('<Q',elf,0x20)
    phentsize,phnum=struct.unpack_from('<HH',elf,0x36)
    segments=[]
    for i in range(phnum):
        kind,_,_,vaddr,_,_,memsz,_=struct.unpack_from('<IIQQQQQQ',elf,phoff+i*phentsize)
        if kind==1: segments.append((vaddr,vaddr+memsz))
    return segments


def eboot_image(elf):
    """The PT_LOAD file bytes of eboot.elf at their virtual addresses (image offsets)."""
    phoff,=struct.unpack_from('<Q',elf,0x20)
    phentsize,phnum=struct.unpack_from('<HH',elf,0x36)
    loads=[struct.unpack_from('<IIQQQQQQ',elf,phoff+i*phentsize) for i in range(phnum)]
    loads=[(vaddr,offset,filesz) for kind,_,offset,vaddr,_,filesz,_,_ in loads if kind==1]
    image=bytearray(max(vaddr+filesz for vaddr,_,filesz in loads))
    for vaddr,offset,filesz in loads: image[vaddr:vaddr+filesz]=elf[offset:offset+filesz]
    return bytes(image)


# Signatures find a patch site in any build whose code matches, wherever the code moved: the
# original bytes around the site, "??" where a build may differ (rip-relative displacements,
# rel32 branch targets). tools/make_signatures.py writes them from the reference build.
def parse_pattern(text):
    return [None if b=='??' else int(b,16) for b in text.split()]


def find_pattern(image, pattern, limit=2):
    """Image offsets where pattern matches, at most limit of them."""
    runs,start=[],None
    for i,b in enumerate(pattern+[None]):
        if b is not None and start is None: start=i
        if b is None and start is not None: runs.append((start,i)); start=None
    if not runs: raise ValueError('pattern without fixed bytes')
    a,b=max(runs,key=lambda r:r[1]-r[0])
    anchor=bytes(pattern[a:b])
    found,at=[],image.find(anchor)
    while at>=0 and len(found)<limit:
        s=at-a
        if s>=0 and s+len(pattern)<=len(image) and all(p is None or image[s+i]==p for i,p in enumerate(pattern)):
            found.append(s)
        at=image.find(anchor,at+1)
    return found


def locate(image, element, prefix=''):
    """Image offset of a Line's or Target's site: its Pattern (unique) plus At, else Address."""
    pattern=element.get(prefix+'Pattern')
    if not pattern:
        return int(element.get('Address'),0)-EBOOT_BASE
    found=find_pattern(image,parse_pattern(pattern))
    if len(found)!=1:
        raise LookupError('signature not found' if not found else 'signature found more than once')
    return found[0]+int(element.get(prefix+'At','0'),0)


def resolve_targets(image, meta, sites):
    """Image offsets of a patch's named Targets: a site (Pattern/At), or the address a rip-relative
    or rel32 operand points to (Ref: the operand's offset in its Pattern; Tail: the instruction's
    bytes after the operand)."""
    targets={}
    for target in meta.iter('Target'):
        name=target.get('Name')
        if target.get('Line'):
            targets[name]=sites[int(target.get('Line'))]
            continue
        at=locate(image,target)
        if target.get('Ref'):
            disp,=struct.unpack_from('<i',image,at)
            at=at+4+int(target.get('Tail','0'))+disp
        targets[name]=at
    return targets


def fill(value, at, targets):
    """Value bytes with each {rel:NAME:TAIL} replaced by the rel32 to target NAME, measured from the
    end of the instruction (TAIL bytes after the operand)."""
    out=bytearray()
    for part in re.split(r'(\{[^}]*\})',value.replace(' ','')):
        if not part.startswith('{'):
            out+=bytes.fromhex(part)
            continue
        _,name,tail=part[1:-1].split(':')
        out+=struct.pack('<i',targets[name]-(at+len(out)+4+int(tail)))
    return bytes(out)


def encode(line):
    kind,value=line.get('Type'),line.get('Value')
    if kind=='bytes': return bytes.fromhex(value.replace(' ',''))
    if kind in ('bytes16','bytes32','bytes64'):
        return int(value,0).to_bytes(int(kind[5:])//8,'little')
    if kind=='float32': return struct.pack('<f',float(value))
    if kind=='float64': return struct.pack('<d',float(value))
    if kind=='utf8': return value.encode()+b'\0'
    if kind=='utf16': return value.encode('utf-16-le')+b'\0\0'
    raise ValueError(f'unsupported patch type {kind!r}')


def compile_patches(xml, names, app_version, segments, image=None, skipped=None):
    """Writes of the named patches. With the game's image, each site is found by its signature
    (any build); without it, at the reference build's Address. A patch whose sites are not all
    found is left out whole, with the reason in skipped[name]. app_version None: any version."""
    found={}
    for meta in ET.parse(xml).getroot().iter('Metadata'):
        if meta.get('Name') in names and app_version in (None,meta.get('AppVer')) and meta.get('AppElf','eboot.bin')=='eboot.bin':
            found[meta.get('Name')]=meta
    missing=[n for n in names if n not in found]
    if missing: raise ValueError(f'patches not found for app version {app_version}: {missing}')
    writes=[]
    for name in names:
        lines=list(found[name].iter('Line'))
        try:
            sites=[locate(image,line) if image is not None else int(line.get('Address'),0)-EBOOT_BASE
                   for line in lines]
            targets=(resolve_targets(image,found[name],sites) if image is not None else
                     {t.get('Name'):int(t.get('Address'),0)-EBOOT_BASE for t in found[name].iter('Target')})
        except LookupError as error:
            if skipped is None: raise ValueError(f'{name}: {error}')
            skipped[name]=str(error)
            continue
        for line,offset in zip(lines,sites):
            data=fill(line.get('Value'),offset,targets) if '{' in line.get('Value','') else encode(line)
            if not any(start<=offset and offset+len(data)<=end for start,end in segments):
                raise ValueError(f'{name}: address {offset+EBOOT_BASE:#x} is outside the eboot')
            writes.append((offset,data))
    return writes


# There is no built-in patch profile any more: main() reads only third-party files. Until the launcher's
# built-in patch page goes too (docs/specs/2026-10-30-spec-8-alpha.md §5), it still imports GAMES (empty),
# game_profile*, game_patchable, patch_status (nothing writes patch_status.json now) and EXCLUSIVE_PREFIX.
GAMES=[]
EXCLUSIVE_PREFIX='Resolution Patch'


def game_profile(title_id, title=None):
    """(title IDs, built-in XML, metadata AppVer, title) for a known game, else None."""
    title=(title or '').replace('®','').replace('™','')
    return next((g for g in GAMES if title_id in g[0] or (title and g[3] in title)),None)


def game_profile_for(game):
    """The profile of a game folder, by title ID or by the title in its param.sfo."""
    values=game_sfo(game) if game else {}
    return game_profile(values.get('TITLE_ID'),values.get('TITLE'))


def game_patchable(game):
    """Whether the built-in patches are tried on this game folder (any region and version)."""
    return bool(game) and bool(game_profile_for(game))


def patch_status(out):
    """The last start's patch report (patches.py writes it): {'applied': [...], 'skipped': {...}}."""
    try:
        return json.loads((Path(out)/'patch_status.json').read_text())
    except (OSError, ValueError):
        return None


def external_patches(directory, app_version, ids):
    """[(key, file, metadata)] of eboot patches for this version in directory/*.xml (third-party
    shadPS4/GoldHEN files in the data directory's patches/ folder) whose files name one of ids, or none.
    key is "<file name>/<patch name>" (the launcher's selection, patches.json)."""
    found=[]
    directory=Path(directory)
    for path in sorted(directory.glob('*.xml')) if directory.is_dir() else []:
        try:
            root=ET.parse(path).getroot()
        except ET.ParseError as error:
            print(f'Patches: {path.name}: {error}',file=sys.stderr)
            continue
        file_ids={e.text.strip() for e in root.iter('ID') if e.text}
        if file_ids and not file_ids&ids: continue
        for meta in root.iter('Metadata'):
            if meta.get('AppVer')==app_version and meta.get('AppElf','eboot.bin')=='eboot.bin':
                found.append((f'{path.name}/{meta.get("Name")}',path,meta))
    return found


def external_selection(found, config):
    """Selected external patches: patches.json {"enabled": [...], "disabled": [...]} overrides
    each file's isEnabled."""
    settings={}
    if config and Path(config).is_file():
        settings=json.loads(Path(config).read_text())
    enabled,disabled=set(settings.get('enabled',[])),set(settings.get('disabled',[]))
    return [(key,path,meta) for key,path,meta in found
            if key in enabled or (key not in disabled and meta.get('isEnabled','false').lower()=='true')]


def compile_external(selected, segments):
    """Writes of the selected external patches; a patch with unsupported lines is skipped whole."""
    writes=[]
    for key,_,meta in selected:
        try:
            ours=[]
            for line in meta.iter('Line'):
                offset=int(line.get('Address') or '',0)-EBOOT_BASE
                data=encode(line)
                if not any(start<=offset and offset+len(data)<=end for start,end in segments):
                    raise ValueError(f'address {line.get("Address")} is outside the eboot')
                ours.append((offset,data))
        except ValueError as error:
            print(f'Patches: skipped {key}: {error}',file=sys.stderr)
            continue
        writes+=ours
        print(f'Patches: external {key} ({len(ours)} writes)')
    return writes


def write_patches(out, writes):
    # G3PATCH2: the patch base, so the loader can rebase pointers the patches write into
    # relocated slots.
    blob=struct.pack('<8sQQ',b'G3PATCH2',EBOOT_BASE,len(writes))
    for offset,data in writes: blob+=struct.pack('<QQ',offset,len(data))+data
    (out/'patches.bin').write_bytes(blob)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--patches-dir',type=Path,help='third-party patch XML files (shadPS4 format)')
    p.add_argument('--patches-config',type=Path,help='patches.json: enabled/disabled external patches')
    p.add_argument('--out',type=Path,default=Path(__file__).resolve().parent.parent/'out')
    p.add_argument('--game-dir',type=Path,default=Path(os.environ.get('TSUKI_GAME_DIR',
                   Path(__file__).resolve().parent.parent/'Tsukihime'/TITLE_ID)))
    a=p.parse_args()
    title=game_title_id(a.game_dir)
    if not is_game(title):
        # src/probe.c stops another game with exit code 30; nothing to patch before that.
        write_patches(a.out,[])
        print(f'Patches: {title or "no title ID"} is not {TITLE_ID}: none applied')
        return
    # Third-party files: absolute addresses of version APP_VERSION, written as they are (no signatures).
    found=external_patches(a.patches_dir,APP_VERSION,IDS) if a.patches_dir else []
    selected=external_selection(found,a.patches_config)
    writes=compile_external(selected,eboot_segments((a.out/'eboot.elf').read_bytes())) if selected else []
    write_patches(a.out,writes)
    print(f'Patches: {title}; {len(writes)} writes from {len(selected)} third-party patches')

if __name__=='__main__':
    main()
