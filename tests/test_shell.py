#!/usr/bin/env python3
"""Functional tests for msh. Run from the repository root with make test."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MSH = str(ROOT / "msh")


def physical(path):
    return subprocess.check_output(["pwd", "-P"], cwd=path, text=True).strip()


class ShellTests(unittest.TestCase):
    def run_script(self, script, env=None, timeout=10):
        merged = os.environ.copy()
        if env is not None:
            merged = env
        return subprocess.run(
            [MSH],
            input=script,
            text=True,
            capture_output=True,
            env=merged,
            timeout=timeout,
            cwd=str(ROOT),
        )

    def test_echo(self):
        result = self.run_script("echo hello\n")
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "hello\n")

    def test_quoted_argument(self):
        result = self.run_script('echo "hello world"\n')
        self.assertEqual(result.stdout, "hello world\n")

    def test_adjacent_quotes(self):
        result = self.run_script("""echo 'a'"b"\n""")
        self.assertEqual(result.stdout, "ab\n")

    def test_empty_line_and_comment(self):
        result = self.run_script("\n# ignored\n   \necho z\n")
        self.assertEqual(result.stdout, "z\n")
        self.assertEqual(result.returncode, 0)

    def test_pwd_matches_process(self):
        result = self.run_script("pwd\n")
        self.assertEqual(result.stdout.strip(), physical(str(ROOT)))

    def test_cd_and_external_see_directory(self):
        expected = physical("/tmp")
        result = self.run_script("cd /tmp\npwd\n/bin/pwd\n")
        lines = result.stdout.strip().splitlines()
        self.assertEqual(lines, [expected, expected])

    def test_cd_dash(self):
        tmp = physical("/tmp")
        root = physical("/")
        result = self.run_script("cd /tmp\ncd /\ncd -\npwd\n")
        lines = [line for line in result.stdout.splitlines() if line]
        self.assertEqual(lines[0], tmp)
        self.assertEqual(lines[1], tmp)
        self.assertNotEqual(root, tmp)

    def test_cd_missing(self):
        result = self.run_script("cd /no/such/msh/dir\necho $?\necho STILL\n")
        self.assertIn("No such file", result.stderr)
        self.assertEqual(result.stdout.splitlines(), ["1", "STILL"])

    def test_cd_too_many_arguments(self):
        result = self.run_script("cd /tmp /tmp\necho $?\n")
        self.assertIn("too many arguments", result.stderr)
        self.assertEqual(result.stdout.strip(), "1")

    def test_cd_to_file(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "file"
            target.write_text("x\n")
            result = self.run_script(f"cd {target}\necho STILL\n")
            self.assertIn("Not a directory", result.stderr)
            self.assertEqual(result.stdout.strip(), "STILL")

    def test_cd_without_home(self):
        env = {"PATH": os.environ["PATH"]}
        result = self.run_script("cd\necho STILL\n", env=env)
        self.assertIn("HOME not set", result.stderr)
        self.assertEqual(result.stdout.strip(), "STILL")

    def test_output_redirection_and_append(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "out.txt"
            script = f"echo one > {target}\necho two >> {target}\ncat {target}\n"
            result = self.run_script(script)
            self.assertEqual(result.stdout, "one\ntwo\n")
            self.assertEqual(target.read_text(), "one\ntwo\n")

    def test_input_redirection(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "in.txt"
            target.write_text("alpha\n")
            result = self.run_script(f"cat < {target}\n")
            self.assertEqual(result.stdout, "alpha\n")

    def test_empty_input_file(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "empty"
            target.write_text("")
            result = self.run_script(f"cat < {target}\necho $?\n")
            self.assertEqual(result.stdout, "0\n")

    def test_missing_input_file(self):
        result = self.run_script("cat < /no/such/msh/input\necho $?\necho DONE\n")
        self.assertIn("No such file", result.stderr)
        self.assertEqual(result.stdout.splitlines(), ["1", "DONE"])

    def test_builtin_redirection(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "pwd.txt"
            result = self.run_script(f"pwd > {target}\ncat {target}\n")
            self.assertEqual(result.stdout.strip(), physical(str(ROOT)))
            self.assertEqual(target.read_text().strip(), physical(str(ROOT)))

    def test_pipeline_sort(self):
        result = self.run_script("printf 'b\\nc\\na\\n' | sort\n")
        self.assertEqual(result.stdout, "a\nb\nc\n")

    def test_three_stage_pipeline(self):
        result = self.run_script("printf 'b\\na\\n' | sort | wc -l\n")
        self.assertEqual(result.stdout.strip(), "2")

    def test_long_pipeline_does_not_leak_descriptors(self):
        script = "echo hello" + (" | cat" * 30) + "\n"
        result = self.run_script(script)
        self.assertEqual(result.stdout.strip(), "hello")
        self.assertEqual(result.returncode, 0)

    def test_pipeline_redirection(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "sorted.txt"
            script = f"printf 'b\\na\\n' | sort > {target}\ncat {target}\n"
            result = self.run_script(script)
            self.assertEqual(result.stdout, "a\nb\n")

    def test_pipeline_status_is_last_command(self):
        true_false = self.run_script("true | false\necho $?\n")
        false_true = self.run_script("false | true\necho $?\n")
        self.assertEqual(true_false.stdout.strip(), "1")
        self.assertEqual(false_true.stdout.strip(), "0")

    def test_builtin_in_pipeline_flushes(self):
        result = self.run_script("pwd | cat\n")
        self.assertEqual(result.stdout.strip(), physical(str(ROOT)))

    def test_cd_in_pipeline_does_not_change_shell(self):
        here = physical(str(ROOT))
        result = self.run_script("cd /tmp | cat\npwd\n")
        self.assertEqual(result.stdout.strip(), here)

    def test_command_not_found(self):
        result = self.run_script("no_such_msh_command\necho $?\necho AFTER\n")
        self.assertIn("command not found", result.stderr)
        self.assertEqual(result.stdout.splitlines(), ["127", "AFTER"])

    def test_syntax_errors(self):
        cases = [
            "ls |\n",
            "| ls\n",
            "ls >\n",
            'echo "unterminated\n',
            "echo hi &\n",
            "echo a ;\n",
            "echo a 2> /tmp/msh-err\n",
            "> outfile\n",
        ]
        for script in cases:
            with self.subTest(script=script):
                result = self.run_script(script + "echo $?\n")
                self.assertIn("syntax error", result.stderr)
                self.assertEqual(result.stdout.strip(), "2")

    def test_duplicate_redirection(self):
        result = self.run_script("echo a > /tmp/msh-a > /tmp/msh-b\necho $?\n")
        self.assertIn("duplicate output redirection", result.stderr)
        self.assertEqual(result.stdout.strip(), "2")

    def test_exit_code(self):
        result = self.run_script("exit 4\necho NO\n")
        self.assertEqual(result.returncode, 4)
        self.assertNotIn("NO", result.stdout)

    def test_exit_keeps_previous_status(self):
        result = self.run_script("false\nexit\n")
        self.assertEqual(result.returncode, 1)

    def test_exit_rejects_bad_status_and_continues(self):
        result = self.run_script("exit no\necho STILL\n")
        self.assertIn("numeric status", result.stderr)
        self.assertEqual(result.stdout.strip(), "STILL")
        self.assertEqual(result.returncode, 0)

    def test_help(self):
        result = self.run_script("help\n")
        self.assertIn("fork", result.stdout)
        self.assertIn("cd", result.stdout)

    def test_question_status_not_expanded_when_quoted(self):
        result = self.run_script("false\necho '$?'\n")
        self.assertEqual(result.stdout.strip(), "$?")

    def test_final_status_on_eof(self):
        result = self.run_script("false\n")
        self.assertEqual(result.returncode, 1)

    def test_redirect_overrides_pipe(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "out.txt"
            edge = self.run_script(f"printf 'x\\n' > {target} | cat\necho SEP\ncat {target}\n")
            self.assertEqual(edge.stdout, "SEP\nx\n")
            middle = self.run_script(f"printf 'a\\n' | cat > {target} | cat\n")
            self.assertEqual(middle.stdout, "")
            self.assertEqual(target.read_text(), "a\n")


if __name__ == "__main__":
    unittest.main(verbosity=2)
