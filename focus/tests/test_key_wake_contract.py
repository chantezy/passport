#!/usr/bin/env python3
"""Static contracts for the deep-sleep key-wake guard wiring in the main component.

Which file holds the button callback is the application's choice: the baseline
demo wires it in `main/main.c`, while an application that owns its own screens
and tasks wires it in its own translation unit. This contract therefore looks for
the callback across `main/*.c` instead of pinning one filename, and asserts the
same ordering property either way.
"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]

CALLBACK = "on_key"
DEFINITION = re.compile(rf"\b{CALLBACK}\s*\([^;]*?\)\s*\{{")


def function_body(source: str, name: str) -> str:
    match = DEFINITION.search(source)
    if not match:
        raise AssertionError(f"function not found: {name}")
    start = match.end() - 1
    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start + 1:index]
    raise AssertionError(f"function is unterminated: {name}")


class KeyWakeGuardWiringTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.sources = {
            path.relative_to(ROOT).as_posix(): path.read_text(encoding="utf-8")
            for path in sorted((ROOT / "main").glob("*.c"))
        }

    def test_on_key_consumes_the_guard_before_the_readiness_check(self) -> None:
        # A release can arrive after the post-init voltage sample but before input
        # dispatch is ready. on_key must run the guard first so that release still
        # disarms it, instead of being discarded by the readiness early-return.
        bodies = [
            (path, function_body(source, CALLBACK))
            for path, source in self.sources.items()
            if DEFINITION.search(source)
        ]
        self.assertEqual(
            [path for path, _ in bodies],
            ["main/focus_app.c"],
            "exactly one translation unit in main/ should define the key callback",
        )
        body = bodies[0][1]
        self.assertIn("key_guard_consume(", body)
        readiness = [body.index(token) for token in ("!s_input_ready", "s_input_ready") if token in body]
        self.assertTrue(readiness, "the callback must gate dispatch on input readiness")
        self.assertLess(body.index("key_guard_consume("), min(readiness))


if __name__ == "__main__":
    unittest.main()
