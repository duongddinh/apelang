# CLI regression coverage, including inputs that previously hung/crashed.
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

EXE = os.environ.get("APELANG_EXE", str(pathlib.Path(__file__).resolve().parents[1] / "apeslang"))

class LanguageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)

    def command(self, *args, input="", expected=0):
        p = subprocess.run([EXE, *map(str, args)], input=input, text=True,
                           capture_output=True, cwd=self.root, timeout=15)
        self.assertEqual(p.returncode, expected, p.stdout + p.stderr)
        return p

    def run_source(self, source):
        path = self.root / "test.ape"
        path.write_text(source)
        self.command("compile", path)
        return self.command("run", path.with_suffix(".apb"))

    def test_arithmetic_recursion_and_scopes(self):
        p = self.run_source("""ape global = 5
tribe fact(n) { if (n < 2) { give 1 } give n eek fact(n aah 1) }
tree fact(6)
{ ape global = 42 tree global }
tree global tree -3 eek 2
""")
        self.assertIn("720\n42\n5\n-6\n", p.stdout)

    def test_zero_negative_and_nested_loops(self):
        p = self.run_source("""swing 0 { tree "BAD" } swing -2 { tree "BAD" }
ape n = 0 swing 3 { ape x = 2 swing x { n = n ooh 1 } }
tree n
tribe early() { swing 4 { give 17 } } swing 2 { tree early() }
""")
        self.assertNotIn("BAD", p.stdout)
        self.assertIn("6\n17\n17\n", p.stdout)

    def test_catch_type_errors_in_every_string_operation(self):
        expressions = ['tally(12)', 'graft(1, 2)', 'slice(1, 0, 1)', 'scan(1, 2)',
                       'shed(12)', '1 aah "x"', '1 ook 0', 'forage(12)',
                       'inscribe(1, 2)', '[1]["x"]', '{}[1]', '12[0]']
        for expr in expressions:
            with self.subTest(expr=expr):
                p = self.run_source('tumble { tree '+expr+' } catch (e) { tree "CAUGHT" } tree "OK"')
                self.assertIn("CAUGHT\nOK\n", p.stdout)
                self.assertEqual(p.stderr, "")

    def test_error_unwinds_call_and_loop_state(self):
        p = self.run_source("""tribe bad() { swing 3 { tumble { give tally(1) } catch (e) { tree 1 ook 0 } } }
swing 3 { tumble { bad() } catch (e) { tree "CAUGHT" } }
tree "DONE"
""")
        self.assertEqual(p.stdout.count("CAUGHT\n"), 3)
        self.assertIn("DONE\n", p.stdout)

    def test_map_growth(self):
        code='ape m = {}\n'+''.join(f'm["k{i}"] = {i}\n' for i in range(100))
        p=self.run_source(code+'tree m["k99"] tree m["k0"]')
        self.assertIn("99\n0\n", p.stdout)

    def test_empty_strings_and_io(self):
        p=self.run_source('tree [] tree tally(shed("")) tree tally(shed("   ")) tree inscribe("data.txt", "banana") tree forage("data.txt")')
        self.assertIn("0\n0\ntrue\nbanana\n", p.stdout)

    def test_relative_modules_nested_functions_and_ownership(self):
        sub=self.root/'lib'; sub.mkdir()
        (sub/'nested.ape').write_text('tribe double(n) { give n eek 2 }')
        (sub/'helper.ape').write_text('summon "nested.ape" tribe outer(n) { tribe inner(x) { give x ooh 1 } give inner(n) }')
        p=self.root/'main.ape'; p.write_text('summon "lib/helper.ape" tree outer(double(20))')
        self.command('compile',p)
        p=self.command('run',p.with_suffix('.apb'))
        self.assertIn('41\n',p.stdout)

    def test_circular_modules_fail_fast(self):
        (self.root/'a.ape').write_text('summon "b.ape"')
        (self.root/'b.ape').write_text('summon "a.ape"')
        p=self.command('compile','a.ape',expected=65)
        self.assertIn('Circular',p.stderr)

    def test_syntax_error_in_block_does_not_hang(self):
        for source in ['tribe f() { ape = 1 }', 'if (true) { tree @ }', 'ape x = [']:
            (self.root/'bad.ape').write_text(source)
            self.command('compile','bad.ape',expected=65)
            self.assertFalse((self.root/'bad.apb').exists())

    def test_long_literals_fail_before_truncating_operands(self):
        for source in ['tree "'+'a'*256+'"', 'ape '+'x'*256+' = 1']:
            (self.root/'bad.ape').write_text(source)
            self.command('compile','bad.ape',expected=65)

    def test_keyword_prefix_is_an_identifier(self):
        p=self.run_source('ape oohbanana = 42 tree oohbanana')
        self.assertIn('42\n',p.stdout)

    def test_repl_retains_functions_and_recovers_after_error(self):
        p=self.command('repl',input='tribe twice(n) { give n eek 2 }\ntree twice(21)\ntree tally(12)\ntree twice(3)\nexit\n')
        self.assertIn('42\n',p.stdout); self.assertIn('6\n',p.stdout)
        self.assertIn('Runtime Error',p.stderr)

    def test_gc_keeps_live_objects_and_repl_functions(self):
        p=self.command('repl',input='tribe f(n) { give n ooh 1 }\nape live = ["alive"]\nswing 100000 { ape tmp = graft("banana", "split") }\ntree live[0]\ntree f(41)\nexit\n')
        self.assertIn('alive\n',p.stdout); self.assertIn('42\n',p.stdout)
        self.assertNotIn('GC Cycles: 0',p.stdout)

    def test_corrupt_bytecode_rejected(self):
        for code in [b'',b'\xff',b'\x00\x02',b'\x0e\xff\xff',b'\x04\x04\x04', b'\x15\xff']:
            (self.root/'bad.apb').write_bytes(code)
            self.command('run','bad.apb',expected=70)

    def test_application_builtins(self):
        p=self.run_source('tree number(" -12.5 ") tree inscribe("note.txt", "42") tree scan(files(), "note.txt") > -1 tree erase("note.txt") tree forage("note.txt")')
        self.assertIn('-12.5\ntrue\ntrue\ntrue\nnil\n',p.stdout)
        p=self.command('repl',input='tree asktext()\n123\nexit\n')
        self.assertIn('123\n',p.stdout)

    def test_nesting_and_numeric_index_limits(self):
        for source in ['tree '+'('*80+'1'+')'*80, '{'*40+'tree 1'+'}'*40]:
            (self.root/'bad.ape').write_text(source)
            self.command('compile','bad.ape',expected=65)
        p=self.run_source('tree [1][1000000000000000000000] tumble { tree slice("a", 0, 1000000000000000000000) } catch (e) { tree "CAUGHT" } ape a = [nil] a[0] = a tree a')
        self.assertIn('nil\nCAUGHT\n',p.stdout)
        self.assertIn('<...>',p.stdout)

    def test_game_builtins(self):
        p=self.run_source('swing 1000 { ape r = random(30) if (r < 0 yellow r >= 30) { tree "BAD" } } tree "RNG PASS" tumble { paint(0, 0, 320, 200, 0) } catch (e) { tree "HOST CHECK" }')
        self.assertNotIn('BAD',p.stdout)
        self.assertIn('RNG PASS\nHOST CHECK\n',p.stdout)
        for expr in ['random(0)', 'random(1.5)', 'paint(0, 0, 1, 1, 99)', 'wait(0)']:
            p=self.run_source('tumble { '+expr+' } catch (e) { tree "CAUGHT" }')
            self.assertIn('CAUGHT',p.stdout)

    def test_original_noninteractive_examples(self):
        examples=pathlib.Path(EXE).parent/'example'
        shutil.copytree(examples,self.root/'example')
        for name in ['math','stringoperation','test_io','memtest']:
            with self.subTest(name=name):
                path=self.root/'example'/f'{name}.ape'
                self.command('compile',path)
                self.command('run',path.with_suffix('.apb'))

if __name__=='__main__':
    unittest.main(verbosity=2)
