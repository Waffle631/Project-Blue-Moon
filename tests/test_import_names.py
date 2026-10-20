from paths import ROOT
import base64
import re
import unittest
from prepare import nid

# Spec 3 (docs/specs/2026-10-14-spec-3-servicios.md, section 3): runtime_lookup (src/runtime.c) only
# reaches an export through its "NID#library" line in src/import_names.inc, so each of these must be there.
SPEC3=[
    ('sceSaveDataInitialize3','libSceSaveData','TywrFKCoLGY'),
    ('sceSaveDataMount2','libSceSaveData','0z45PIH+SNI'),
    ('sceSaveDataUmountWithBackup','libSceSaveData','VwadwBBBJ80'),
    ('sceSaveDataCheckBackupData','libSceSaveData','RQOqDbk3bSU'),
    ('sceSaveDataRestoreBackupData','libSceSaveData','lU9YRFsgwSU'),
    ('sceScreenShotEnable','libSceScreenShot','2xxUtuC-RzE'),
    ('sceScreenShotDisable','libSceScreenShot','tIYf0W5VTi8'),
    ('sceScreenShotSetOverlayImageWithOrigin','libSceScreenShot','73WQ4Jj0nJI'),
    ('sceSharePlayInitialize','libSceSharePlay','isruqthpYcw'),
    ('sceSharePlayTerminate','libSceSharePlay','UaLjloJinow'),
    ('sceSharePlaySetProhibition','libSceSharePlay','co2NCj--pnc'),
    ('sceGameLiveStreamingInitialize','libSceGameLiveStreaming','kvYEw2lBndk'),
    ('sceGameLiveStreamingTerminate','libSceGameLiveStreaming','9yK6Fk8mKOQ'),
    ('sceGameLiveStreamingEnableLiveStreaming','libSceGameLiveStreaming','dWM80AX39o4'),
    ('sceVideoRecordingSetInfo','libSceVideoRecording','Fc8qxlKINYQ'),
    ('sceSysmoduleLoadModuleInternalWithArg','libSceSysmodule','hHrGoGoNf+s'),
    ('sceSysmoduleGetModuleInfoForUnwind','libSceSysmodule','4fU5yvOkVG4'),
    ('sceSystemServiceParamGetString','libSceSystemService','SsC-m-S9JTA'),
    ('sceSystemServiceReenableMusicPlayer','libSceSystemService','9kPCz7Or+1Y'),
    ('sceUserServiceGetUserColor','libSceUserService','lUoqwTQu4Go'),
    ('sceMsgDialogGetResult','libSceMsgDialog','Lr8ovHH9l6A'),
    ('sceDiscMap_7C980FFB0AA27E7A','libSceDiscMap','fJgP+wqifno'),
    ('sceLibcHeapGetTraceInfo','libSceLibcInternalExt','NWtTN10cJzE'),
]
# Spec 4a (docs/specs/2026-10-14-spec-4-audiodec.md, 2.1): the six libSceAudiodec imports of the eboot.
SPEC4=[
    ('sceAudiodecInitLibrary','libSceAudiodec','VjhsmxpcezI'),
    ('sceAudiodecTermLibrary','libSceAudiodec','h5jSB2QIDV0'),
    ('sceAudiodecCreateDecoder','libSceAudiodec','O3f1sLMWRvs'),
    ('sceAudiodecDeleteDecoder','libSceAudiodec','Tp+ZEy69mLk'),
    ('sceAudiodecDecode','libSceAudiodec','KHXHMDLkILw'),
    ('sceAudiodecClearContext','libSceAudiodec','6Vf9WTLDoss'),
]

def import_names():
    text=(ROOT/'src/import_names.inc').read_text(encoding='ascii')
    return {(n,lib):name for n,lib,name in re.findall(r'\{"([^"#]+)#([^"]+)","([^"]+)"\}',text)}

class ImportNamesTests(unittest.TestCase):
    def test_nid_matches_name(self):
        for name,lib,n in SPEC3+SPEC4:
            with self.subTest(name):
                raw=name.rpartition('_')[2]
                # No public name hashes to this NID: the name carries the NID's own bytes (spec 4.3).
                if re.fullmatch(r'[0-9A-F]{16}',raw): self.assertEqual(base64.b64decode(n.replace('-','/')+'=').hex().upper(),raw)
                else: self.assertEqual(nid(name),n)

    def test_every_name_is_in_import_names_inc(self):
        table=import_names()
        for name,lib,n in SPEC3+SPEC4:
            with self.subTest(name): self.assertEqual(table.get((n,lib)),name,f'{n}#{lib}')

if __name__=='__main__':
    unittest.main()
