from paths import ROOT
import contextlib
import io
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0,str(ROOT/'tools'))
import extract_audio_clips as eac

SECTOR=0x800
GUID=bytes.fromhex('d242e147ba368d4d88fc61654f8c836c')

# Same layout as the dump's RIFF ATRAC9 (bgm.mrg entry 1): fmt extensible at +0x0c, config at +0x40.
def at9(config,block,channels,payload=b'\x55'*100):
    fmt=struct.pack('<HHIIHHHHI16sI4sI',0xfffe,channels,48000,48000*block//256,block,0,0x22,256,3,GUID,1,
                    bytes.fromhex(config),0)
    body=b'WAVE'+b'fmt '+struct.pack('<I',len(fmt))+fmt+b'data'+struct.pack('<I',len(payload))+payload
    return b'RIFF'+struct.pack('<I',len(body))+body

def vagp(name,data=b'\x00'*32):
    return (b'VAGp'+struct.pack('>IIII',0x00020001,0,len(data),48000)+b'\0'*12+name.ljust(16,b'\0')+data)

def entry(sector,sectors,size):
    return struct.pack('<IHH',(sector&0xffff)|(sector>>16)<<28,sectors,size&0xffff)

SENTINEL=b'\xff'*8

# Writes .hed/.mrg with each blob at a sector boundary and trailing garbage after it.
def archive(base,blobs,sentinels=0):
    hed=b'';mrg=b''
    for blob in blobs:
        sectors=(len(blob)+16+SECTOR-1)//SECTOR
        hed+=entry(len(mrg)//SECTOR,sectors,len(blob))
        mrg+=(blob+b'\xaa'*16).ljust(sectors*SECTOR,b'\xcc')
    base.with_suffix('.hed').write_bytes(hed+SENTINEL*sentinels); base.with_suffix('.mrg').write_bytes(mrg)

class ExtractAudioClipsTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(); self.dir=Path(self.tmp.name)

    def tearDown(self): self.tmp.cleanup()

    def test_read_hed_high_nibble(self):
        hed=self.dir/'a.hed'
        hed.write_bytes(struct.pack('<IHH',0,15,30248)+struct.pack('<IHH',0x10000581,4,1234))
        self.assertEqual(eac.read_hed(hed),[(0,15*SECTOR),(0x10581*SECTOR,4*SECTOR)])

    def test_extract_riff_trims_to_riff_size(self):
        clip=at9('fe740ff0',512,2); archive(self.dir/'bgm',[at9('fe7409f0',320,2,b'\1'*5000),clip])
        offset=eac.read_hed(self.dir/'bgm.hed')[1][0]
        self.assertEqual(eac.extract_riff(self.dir/'bgm.mrg',offset),clip)

    def test_rejects_non_riff_returns_none(self):
        archive(self.dir/'x',[b'\0'*64,vagp(b'moon'),b'RIFF'+struct.pack('<I',4)+b'AVI '])
        for offset,_ in eac.read_hed(self.dir/'x.hed'):
            self.assertIsNone(eac.extract_riff(self.dir/'x.mrg',offset))

    def test_truncated_riff_returns_none(self):
        clip=at9('fe740ff0',512,2); (self.dir/'t.mrg').write_bytes(clip[:-1])
        self.assertIsNone(eac.extract_riff(self.dir/'t.mrg',0))

    # vse.hed ends with two all-0xff entries (1327, 1328): no data behind them.
    def test_sentinel_entries_skipped(self):
        archive(self.dir/'vse',[at9('fe7004f0',160,1),at9('fe740bf0',384,2)],sentinels=2)
        entries=eac.read_hed(self.dir/'vse.hed')
        self.assertEqual(len(entries),2)
        for offset,_ in entries: self.assertIsNotNone(eac.extract_riff(self.dir/'vse.mrg',offset))

    def test_extract_vagp_trims_to_data_size(self):
        clip=vagp(b'moon_se00001_L',bytes(range(48))); archive(self.dir/'vag',[vagp(b'dummy'),clip])
        self.assertEqual(eac.extract_vagp(self.dir/'vag.mrg',eac.read_hed(self.dir/'vag.hed')[1][0]),clip)
        self.assertIsNone(eac.extract_vagp(self.dir/'vag.mrg',SECTOR*99))

    def test_atrac9_info_reads_config_block_and_channels(self):
        self.assertEqual(eac.atrac9_info(at9('fe700bf0',384,1)),('fe700bf0',384,1))
        self.assertIsNone(eac.atrac9_info(at9('fe700bf0',384,1).replace(GUID,b'\0'*16)))

    def run_main(self,*args):
        out=io.StringIO()
        with contextlib.redirect_stdout(out): code=eac.main(['--image',str(self.dir),'--out',str(self.dir/'clips'),*args])
        return code,out.getvalue()

    # First entry of each kind wins; a missing kind is reported and the rest are still written.
    def test_main_writes_first_clip_of_each_kind_and_reports_missing(self):
        typ=at9('fe740ff0',512,2,b'\2'*3000); stereo=at9('fe740bf0',384,2); real=vagp(b'moon_se00001_L')
        archive(self.dir/'bgm',[typ,at9('fe740ff0',512,2,b'\3'*10)])
        archive(self.dir/'vse',[at9('fe741bf0',384,2),stereo],sentinels=2)
        archive(self.dir/'vag',[vagp(b'dummy'),real,vagp(b'moon_se00001_R')],sentinels=2)
        code,log=self.run_main()
        clips=self.dir/'clips'
        self.assertEqual(code,1)
        self.assertEqual((clips/'bgm_typ.at9').read_bytes(),typ)
        self.assertEqual((clips/'vse_stereo.at9').read_bytes(),stereo)
        self.assertEqual((clips/'vag0.vagp').read_bytes(),real)
        self.assertEqual(sorted(p.name for p in clips.iterdir()),['bgm_typ.at9','vag0.vagp','vse_stereo.at9'])
        self.assertIn('bgm_typ.at9: bgm.mrg entry 0, config fe740ff0, block 512, 2 ch',log)
        for name in ('bgm_rare.at9','vse_mono.at9','vse_mono_small.at9'): self.assertIn(f'{name}: missing',log)

    def test_main_reports_missing_archive(self):
        archive(self.dir/'bgm',[at9('fe740ff0',512,2),at9('fe7409f0',320,2)])
        code,log=self.run_main()
        self.assertEqual(code,1)
        self.assertIn('vse_stereo.at9: missing',log); self.assertIn('vag0.vagp: missing',log)
        self.assertTrue((self.dir/'clips/bgm_rare.at9').exists())

if __name__=='__main__':
    unittest.main()
