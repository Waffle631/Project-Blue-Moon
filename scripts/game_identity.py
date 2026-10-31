"""The one game this build runs: TSUKIHIME -A piece of blue glass moon- (PS4, CUSA47999).

run.py, the launcher and the packaging read the identity from here. src/probe.c is C and keeps its own
copy (SUPPORTED_TITLE_ID, exit code 30 for any other game); tests/test_game_identity.py checks they match.
"""
from pathlib import Path

TITLE_ID='CUSA47999'
TITLE='TSUKIHIME -A piece of blue glass moon-'
APP_VERSION='01.00'
IDS={TITLE_ID}  # title IDs third-party patch files (patches.external_patches) must name


def game_sfo(game):
    """The values of the game folder's param.sfo, or {} when it cannot be read (prepare.read_sfo)."""
    if not game:
        return {}
    try:
        from prepare import read_sfo
    except ImportError:
        return {}
    return read_sfo(Path(game) / 'sce_sys/param.sfo') or {}


def game_title_id(game):
    return game_sfo(game).get('TITLE_ID')


def is_game(title_id):
    """Whether a param.sfo TITLE_ID is this game's: src/probe.c starts no other."""
    return title_id in IDS


def is_game_folder(folder):
    return is_game(game_title_id(folder))
