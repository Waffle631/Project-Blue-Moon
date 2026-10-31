from paths import ROOT  # also puts scripts/ on the path
import re
import struct
import tempfile
import unittest
from pathlib import Path

import game_identity
from game_identity import IDS, TITLE_ID, game_sfo, game_title_id, is_game, is_game_folder


def game_folder(tmp, **values):
    """A game folder whose sce_sys/param.sfo (PSF 1.1) holds string values; without values it has none."""
    folder = Path(tmp) / 'game'
    (folder / 'sce_sys').mkdir(parents=True)
    if values:
        entries, keys, data = b'', b'', b''
        for key, value in values.items():
            text = value.encode() + b'\0'
            entries += struct.pack('<HHIII', len(keys), 0x204, len(text), len(text), len(data))
            keys += key.encode() + b'\0'
            data += text
        keys_offset = 20 + len(entries)
        (folder / 'sce_sys/param.sfo').write_bytes(
            struct.pack('<4sIIII', b'\0PSF', 0x101, keys_offset, keys_offset + len(keys), len(values))
            + entries + keys + data)
    return folder


class GameIdentityTests(unittest.TestCase):
    def test_values(self):
        self.assertEqual(TITLE_ID, 'CUSA47999')
        self.assertEqual(game_identity.TITLE, 'TSUKIHIME -A piece of blue glass moon-')
        self.assertEqual(game_identity.APP_VERSION, '01.00')
        self.assertEqual(IDS, {'CUSA47999'})

    def test_is_game_by_title_id(self):
        self.assertTrue(is_game('CUSA47999'))
        self.assertFalse(is_game('CUSA01623'))
        self.assertFalse(is_game(None))
        self.assertFalse(is_game(''))

    def test_folders(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = game_folder(tmp, TITLE_ID='CUSA47999', APP_VER='01.00')
            self.assertTrue(is_game_folder(folder))
            self.assertEqual(game_title_id(folder), 'CUSA47999')
            self.assertEqual(game_sfo(folder)['APP_VER'], '01.00')
        with tempfile.TemporaryDirectory() as tmp:
            # Another game, even one whose title names this one: src/probe.c stops it with code 30.
            self.assertFalse(is_game_folder(game_folder(tmp, TITLE_ID='CUSA99999', TITLE=game_identity.TITLE)))
        with tempfile.TemporaryDirectory() as tmp:
            folder = game_folder(tmp)
            self.assertFalse(is_game_folder(folder))  # no param.sfo
            self.assertIsNone(game_title_id(folder))
            self.assertEqual(game_sfo(folder), {})
        self.assertFalse(is_game_folder(None))
        self.assertEqual(game_sfo(None), {})

    def test_probe_runs_the_same_game(self):
        # src/probe.c is C and cannot import this module: its copy must match.
        source = (ROOT / 'src/probe.c').read_text()
        self.assertEqual(re.search(r'#define SUPPORTED_TITLE_ID "(\w+)"', source).group(1), TITLE_ID)


if __name__ == '__main__':
    unittest.main()
