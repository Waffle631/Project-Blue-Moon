from paths import ROOT
import subprocess
import tempfile
import unittest
from pathlib import Path

SCRIPT=ROOT/'tools/check_rename.sh'

# Spec 7 §6 (docs/specs/2026-10-14-spec-7-renombrado.md): check_rename.sh fails on any old name left in the tracked
# tree, in a file's contents or in its path, except in the history folders, the submodules, pgo/, the branch
# mention in CLAUDE.md and the roadmap, and lines marked "rename-check: ok". Since §11 it also fails on the previous
# game's name and serials, except in the player READMEs (rewritten for the Alpha) and "gow3 contributors" credits.
class CheckRenameTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(); self.repo=Path(self.tmp.name)
        subprocess.run(['git','init','-q',str(self.repo)],check=True)
        self.write('README.md','Tsukihime port.\n')

    def tearDown(self): self.tmp.cleanup()

    def write(self,path,text):
        p=self.repo/path; p.parent.mkdir(parents=True,exist_ok=True); p.write_text(text,encoding='utf-8')
        subprocess.run(['git','-C',str(self.repo),'add',path],check=True) # git grep and ls-files see tracked files only

    def check(self):
        return subprocess.run(['bash',str(SCRIPT),str(self.repo)],capture_output=True,text=True,timeout=60)

    def test_clean_tree_passes(self):
        r=self.check()
        self.assertEqual((r.returncode,r.stdout),(0,''),r.stderr)

    def test_content_match_fails(self):
        self.write('src/a.c','int x;\nconst char *v=getenv("GOW3_X");\n')
        r=self.check()
        self.assertEqual(r.returncode,1,r.stderr)
        self.assertIn('src/a.c:2:',r.stdout)

    def test_name_match_fails(self):
        self.write('gpu/shim/gow3_foo.h','')
        r=self.check()
        self.assertEqual(r.returncode,1,r.stderr)
        self.assertIn('gpu/shim/gow3_foo.h',r.stdout)

    def test_case_insensitive(self):
        self.write('docs/a.md','A port of GoW3.\n')
        r=self.check()
        self.assertEqual(r.returncode,1,r.stderr)
        self.assertIn('docs/a.md:1:',r.stdout)

    def test_exceptions_pass(self):
        for path in ('docs/gow3/a.md','docs/plans/a.md','docs/specs/a.md','gpu/third_party/x/a.c','pgo/a.txt'):
            self.write(path,'GOW3_GAME_DIR gow3\n')
        r=self.check()
        self.assertEqual((r.returncode,r.stdout),(0,''),r.stderr)

    def test_branch_mention_only_in_claude_and_roadmap(self):
        line='History stays in the rama `gow3`, branch gow3 and branch `gow3`; see [docs/gow3/ROADMAP.md](../gow3/ROADMAP.md).\n'
        self.write('CLAUDE.md',line); self.write('docs/tsuki/ROADMAP.md',line)
        r=self.check()
        self.assertEqual((r.returncode,r.stdout),(0,''),r.stderr)
        self.write('docs/a.md',line)
        r=self.check()
        self.assertEqual(r.returncode,1,r.stderr)
        self.assertIn('docs/a.md:1:',r.stdout)
        self.write('docs/a.md','Tsukihime port.\n')
        self.write('CLAUDE.md',line+'Run with `GOW3_GAME_DIR`; branch `gow3`.\n')
        r=self.check()
        self.assertEqual(r.returncode,1,r.stderr)
        self.assertEqual(r.stdout,'CLAUDE.md:2:Run with `GOW3_GAME_DIR`; branch `gow3`.\n')

    def test_game_name_and_serials_fail(self):
        self.write('src/a.c','// God of War III draws\n')
        self.write('tests/b.py',"sfo(TITLE_ID='CUSA01623')\n")
        self.write('docs/c.md','Region cusa01715.\n')
        r=self.check()
        self.assertEqual(r.returncode,1,r.stderr)
        for match in ('src/a.c:1:','tests/b.py:1:','docs/c.md:1:'): self.assertIn(match,r.stdout)

    def test_player_readmes_excepted(self):
        text='God of War III Remastered (CUSA01623): run bin\\gow3-probe.exe\n'
        for path in ('README.md','README.pt-BR.md','packaging/windows/README-Windows.txt'): self.write(path,text)
        r=self.check()
        self.assertEqual((r.returncode,r.stdout),(0,''),r.stderr)
        self.write('packaging/windows/README.md',text) # a developer README follows the rename
        r=self.check()
        self.assertEqual(r.returncode,1,r.stderr)
        self.assertEqual(r.stdout,'packaging/windows/README.md:1:'+text)

    def test_contributors_credit_excepted(self):
        self.write('gpu/x/LICENSE.txt','Copyright (c) 2026 gow3 contributors\n')
        self.write('gpu/x/a.h','// SPDX-FileCopyrightText: Copyright 2026 IFreemz, gow3 contributors\n')
        r=self.check()
        self.assertEqual((r.returncode,r.stdout),(0,''),r.stderr)
        self.write('gpu/x/b.h','// Copyright 2026 gow3 contributors; see gow3gpu.h\n') # another gow3 still counts
        r=self.check()
        self.assertEqual(r.returncode,1,r.stderr)
        self.assertIn('gpu/x/b.h:1:',r.stdout)

    def test_marker_line_ignored(self):
        self.write('tests/test_lang.py','env={"GOW3_LANGUAGE":"8"} # rename-check: ok\n')
        r=self.check()
        self.assertEqual((r.returncode,r.stdout),(0,''),r.stderr)

    def test_not_a_repo_is_an_error(self):
        with tempfile.TemporaryDirectory() as d:
            r=subprocess.run(['bash',str(SCRIPT),d],capture_output=True,text=True,timeout=60)
        self.assertEqual(r.returncode,2,r.stdout); self.assertTrue(r.stderr)

if __name__=='__main__': unittest.main()
