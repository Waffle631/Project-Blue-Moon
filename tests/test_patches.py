from paths import ROOT  # also puts scripts/ on the path
import contextlib
import io
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import patches
from patches import compile_patches, external_patches, external_selection, compile_external, game_profile

# A game that does not exist: the patch engine is tested without a real one.
SYNTH_ID = 'CUSA99990'
SYNTH_IDS = {SYNTH_ID}
SEGMENTS = [(0, 0x6000000)]

# A plain site and a site found by its signature (compile_patches).
SYNTH_XML = """<?xml version="1.0"?>
<Patch>
  <TitleID><ID>CUSA99990</ID></TitleID>
  <Metadata Title="Synthetic Game" Name="Bug Fix - Texture Corruption Fix" AppVer="01.00" AppElf="eboot.bin" isEnabled="true">
    <PatchList><Line Type="bytes" Address="0x00401000" Value="9090"/></PatchList>
  </Metadata>
  <Metadata Title="Synthetic Game" Name="Skip Videos" AppVer="01.00" AppElf="eboot.bin" isEnabled="true">
    <PatchList><Line Type="bytes" Address="0x00402004" Value="9090" Pattern="11 22 ?? 44 55 66" At="1"/></PatchList>
  </Metadata>
</Patch>"""


EXTERNAL = """<?xml version="1.0"?>
<Patch>
  <TitleID><ID>CUSA99990</ID></TitleID>
  <Metadata Title="Synthetic Game" Name="On" Author="x" PatchVer="1.0" AppVer="01.00" AppElf="eboot.bin" isEnabled="true">
    <PatchList><Line Type="bytes" Address="0x00401000" Value="9090"/></PatchList>
  </Metadata>
  <Metadata Title="Synthetic Game" Name="Off" Author="x" PatchVer="1.0" AppVer="01.00" AppElf="eboot.bin">
    <PatchList><Line Type="bytes32" Address="0x00402000" Value="0x12345678"/></PatchList>
  </Metadata>
  <Metadata Title="Synthetic Game" Name="Mask" Author="x" PatchVer="1.0" AppVer="01.00" AppElf="eboot.bin" isEnabled="true">
    <PatchList><Line Type="mask" Value="90 ?? 90" Offset="0"/></PatchList>
  </Metadata>
  <Metadata Title="Synthetic Game" Name="Old" Author="x" PatchVer="1.0" AppVer="00.90" AppElf="eboot.bin" isEnabled="true">
    <PatchList><Line Type="bytes" Address="0x00403000" Value="90"/></PatchList>
  </Metadata>
</Patch>"""


def synthetic_xml(test):
    """SYNTH_XML in a temporary folder, removed after the test."""
    directory = tempfile.TemporaryDirectory()
    test.addCleanup(directory.cleanup)
    xml = Path(directory.name) / 'Synthetic_Game.xml'
    xml.write_text(SYNTH_XML)
    return xml


def tiny_elf(size):
    """An eboot.elf with one PT_LOAD of size zero bytes at vaddr 0 (eboot_segments, eboot_image)."""
    header = bytearray(0x40)
    header[:4] = b'\x7fELF'
    struct.pack_into('<Q', header, 0x20, 0x40)
    struct.pack_into('<HH', header, 0x36, 56, 1)
    load = struct.pack('<IIQQQQQQ', 1, 5, 0x100, 0, 0, size, size, 0x4000)
    return bytes(header) + load + bytes(0x100 - 0x78) + bytes(size)


class ExternalPatchTests(unittest.TestCase):
    def test_selection_and_unsupported_lines(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            (folder / 'extra.xml').write_text(EXTERNAL)
            (folder / 'other.xml').write_text(EXTERNAL.replace(SYNTH_ID, 'CUSA99999'))
            (folder / 'broken.xml').write_text('<Patch>')
            found = external_patches(folder, '01.00', SYNTH_IDS)
            self.assertEqual([key for key, _, _ in found],
                             ['extra.xml/On', 'extra.xml/Off', 'extra.xml/Mask'])
            # The file's isEnabled; the mask patch is skipped as a whole.
            writes = compile_external(external_selection(found, None), SEGMENTS)
            self.assertEqual(writes, [(0x1000, bytes.fromhex('9090'))])
            config = folder / 'patches.json'
            config.write_text('{"enabled": ["extra.xml/Off"], "disabled": ["extra.xml/On"]}')
            writes = compile_external(external_selection(found, config), SEGMENTS)
            self.assertEqual(writes, [(0x2000, (0x12345678).to_bytes(4, 'little'))])

    def test_missing_folder(self):
        self.assertEqual(external_patches(Path(tempfile.gettempdir()) / 'no such folder', '01.00', SYNTH_IDS), [])


class CompilePatchesTests(unittest.TestCase):
    def test_reference_addresses_and_signatures(self):
        xml = synthetic_xml(self)
        names = ['Bug Fix - Texture Corruption Fix', 'Skip Videos']
        # Without the image: the reference Address values.
        self.assertEqual(compile_patches(xml, names, '01.00', SEGMENTS),
                         [(0x1000, b'\x90\x90'), (0x2004, b'\x90\x90')])
        # With it: the signature finds the moved site; a line without one keeps its Address.
        image = bytearray(0x10000)
        image[0x3000:0x3006] = bytes.fromhex('112233445566')
        self.assertEqual(compile_patches(xml, names, None, [(0, 0x10000)], bytes(image), {}),
                         [(0x1000, b'\x90\x90'), (0x3001, b'\x90\x90')])


class GameProfileTests(unittest.TestCase):
    """What the launcher's built-in patch page still reads (docs/specs/2026-10-30-spec-8-alpha.md §5)."""
    def setUp(self):
        self.xml = synthetic_xml(self)
        games = mock.patch.object(patches, 'GAMES', [(SYNTH_IDS, self.xml, '01.00', 'Synthetic Game')])
        games.start()
        self.addCleanup(games.stop)

    def test_profiles_by_title_id(self):
        self.assertEqual(game_profile(SYNTH_ID)[1:3], (self.xml, '01.00'))
        self.assertIsNone(game_profile('CUSA99999'))
        self.assertIsNone(game_profile(None))

    def test_profile_by_title_for_any_region(self):
        self.assertEqual(game_profile('CUSA99999', 'Synthetic Game® Remastered')[1], self.xml)
        self.assertIsNone(game_profile('CUSA99999', 'Another Game'))
        with mock.patch.object(patches, 'game_sfo', return_value={'TITLE_ID': 'CUSA77777', 'TITLE': 'Synthetic Game'}):
            self.assertTrue(patches.game_patchable('folder'))
        self.assertFalse(patches.game_patchable(None))
        with tempfile.TemporaryDirectory() as folder:
            self.assertFalse(patches.game_patchable(folder))  # no param.sfo: unknown game


class BuiltInProfileTests(unittest.TestCase):
    def test_without_profile_nothing_applies(self):
        self.assertEqual(patches.GAMES, [])
        self.assertIsNone(game_profile('CUSA47999'))
        self.assertIsNone(game_profile('CUSA47999', 'TSUKIHIME -A piece of blue glass moon-'))


class PatchesMainTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.out = self.root / 'out'
        self.out.mkdir()

    def run_main(self, *args, title_id=None):
        """patches.py's main() with these arguments and the param.sfo TITLE_ID: (its output, the number
        of writes in patches.bin, the game folders it read)."""
        seen = []
        output = io.StringIO()
        with mock.patch.object(sys, 'argv', ['patches.py', '--out', str(self.out), *args]), \
                mock.patch.dict(os.environ), \
                mock.patch.object(patches, 'game_title_id', side_effect=lambda game: seen.append(game) or title_id), \
                contextlib.redirect_stdout(output):
            os.environ.pop('TSUKI_GAME_DIR', None)
            patches.main()
        return output.getvalue(), struct.unpack_from('<8sQQ', (self.out / 'patches.bin').read_bytes())[2], seen

    def test_default_game_folder(self):
        _, _, seen = self.run_main()
        self.assertEqual(Path(seen[0]), ROOT / 'Tsukihime' / 'CUSA47999')

    def test_no_patch_folder(self):
        # The data folder may have no patches/; then not even eboot.elf is read.
        output, writes, _ = self.run_main('--game-dir', str(self.root / 'game'),
                                          '--patches-dir', str(self.root / 'missing'), title_id='CUSA47999')
        self.assertIn('Patches: CUSA47999; 0 writes from 0 third-party patches', output)
        self.assertEqual(writes, 0)

    def patch_folder(self, title_id):
        (self.out / 'eboot.elf').write_bytes(tiny_elf(0x10000))
        folder = self.root / 'external'
        folder.mkdir()
        (folder / 'extra.xml').write_text(EXTERNAL.replace(SYNTH_ID, title_id))
        return folder

    def test_third_party_patches_of_this_game(self):
        folder = self.patch_folder('CUSA47999')
        output, writes, _ = self.run_main('--game-dir', str(self.root / 'game'), '--patches-dir', str(folder),
                                          title_id='CUSA47999')
        self.assertIn('Patches: external extra.xml/On (1 writes)', output)
        self.assertEqual(writes, 1)

    def test_another_game_gets_none(self):
        # src/probe.c stops it with exit code 30; its own patch files are not applied before that.
        for title_id in ('CUSA99999', None):
            with self.subTest(title_id=title_id):
                folder = self.patch_folder('CUSA99999') if title_id else self.root / 'external'
                output, writes, _ = self.run_main('--game-dir', str(self.root / 'game'), '--patches-dir', str(folder),
                                                  title_id=title_id)
                self.assertIn(f'Patches: {title_id or "no title ID"} is not CUSA47999: none applied', output)
                self.assertEqual(writes, 0)


class SignatureTests(unittest.TestCase):
    XML = """<?xml version="1.0"?>
<Patch>
  <Metadata Name="Found" AppVer="01.02">
    <PatchList><Line Type="bytes" Address="0x401004" Value="9090" Pattern="11 22 ?? 44 55 66" At="1"/></PatchList>
  </Metadata>
  <Metadata Name="Call" AppVer="01.02">
    <PatchList><Line Type="bytes" Address="0x401010" Value="e8{rel:there:0}" Pattern="aa bb cc dd ee" At="0"/></PatchList>
    <Target Name="there" Address="0x401100" Pattern="e9 ?? ?? ?? ?? 77 88" At="1" Ref="1"/>
  </Metadata>
  <Metadata Name="Missing" AppVer="01.02">
    <PatchList>
      <Line Type="bytes" Address="0x401004" Value="9090" Pattern="11 22 ?? 44 55 66" At="1"/>
      <Line Type="bytes" Address="0x401020" Value="90" Pattern="de ad be ef" At="0"/>
    </PatchList>
  </Metadata>
</Patch>"""

    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.xml = Path(directory.name) / 'test.xml'
        self.xml.write_text(self.XML)
        image = bytearray(0x400)
        image[0x40:0x46] = bytes.fromhex('112233445566')
        image[0x80:0x85] = bytes.fromhex('aabbccddee')
        image[0x200:0x207] = bytes.fromhex('e9') + (0x300 - 0x205).to_bytes(4, 'little', signed=True) + bytes.fromhex('7788')
        self.image = bytes(image)

    def test_sites_found_anywhere_and_rel32_recomputed(self):
        skipped = {}
        writes = compile_patches(self.xml, ['Found', 'Call', 'Missing'], None, [(0, 0x400)], self.image, skipped)
        call = (0x300 - (0x80 + 5)).to_bytes(4, 'little', signed=True)
        self.assertEqual(writes, [(0x41, b'\x90\x90'), (0x80, b'\xe8' + call)])
        # One site of the patch is missing: the whole patch is left out, with the reason.
        self.assertEqual(skipped, {'Missing': 'signature not found'})

    def test_ambiguous_signature_is_skipped(self):
        image = bytearray(self.image)
        image[0x100:0x106] = image[0x40:0x46]
        skipped = {}
        self.assertEqual(compile_patches(self.xml, ['Found'], None, [(0, 0x400)], bytes(image), skipped), [])
        self.assertEqual(skipped, {'Found': 'signature found more than once'})

    def test_without_the_image_the_reference_addresses_are_used(self):
        writes = compile_patches(self.xml, ['Found', 'Call'], '01.02', [(0, 0x10000)])
        self.assertEqual(writes[0], (0x1004, b'\x90\x90'))
        self.assertEqual(writes[1][0], 0x1010)


if __name__ == '__main__':
    unittest.main()
