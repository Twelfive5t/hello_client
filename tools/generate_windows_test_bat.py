"""Generate hello_client_test.bat by scanning *_test.cpp under the test directory.

Usage (called automatically by cmake install, or manually):
    python generate_windows_test_bat.py
"""

import glob
import os
import re


def find_test_cases(test_dir):
    """Auto-scan all *_test.cpp under test_dir.

    Returns {category: [(fixture, test_name), ...]}
    where category is the name of the subdirectory containing the file.
    """
    tests = {}
    for cpp_file in sorted(glob.glob(os.path.join(test_dir, "**", "*_test.cpp"), recursive=True)):
        category = os.path.basename(os.path.dirname(cpp_file))
        # Skip the entry-point directory (src/)
        if category == "src":
            continue
        entries = []
        with open(cpp_file, "r", encoding="utf-8") as f:
            for line in f:
                m = re.match(r"TEST_F\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)", line.strip())
                if m:
                    entries.append((m.group(1), m.group(2)))
        if entries:
            tests[category] = entries
    return tests


def generate_bat(tests, output_path):
    """Generate hello_client_test.bat: asks for server address then shows a test menu."""
    lines = []

    def e(s=""):
        lines.append(s)

    e("@echo off")
    e("setlocal enabledelayedexpansion")
    e()
    e("goto :SERVER_INPUT")
    e()

    # flatten: [(category, fixture, name), ...]
    flat = [(cat, fix, name) for cat, entries in tests.items() for fix, name in entries]

    e(":MENU")
    e("cls")
    e("echo Server: !SERVER!")
    e("echo ============================================")
    e("echo   HelloClient Test Menu")
    e("echo ============================================")
    e("echo.")
    prev_cat = None
    for i, (cat, fix, name) in enumerate(flat, 1):
        if cat != prev_cat:
            if prev_cat is not None:
                e("echo.")
            e(f"echo  [{cat}]")
            prev_cat = cat
        display = name[9:] + " (DISABLED)" if name.startswith("DISABLED_") else name
        e(f"echo    {i}.  {display}")
    e("echo.")
    e("echo    C.  Change Server")
    e("echo    0.  Exit")
    e("echo ============================================")
    e("set \"TEST=\"")
    e("set \"FIXTURE=\"")
    e("set /p CHOICE=Select: ")
    e()
    e("if /I \"!CHOICE!\"==\"C\" goto :SERVER_INPUT")
    e("if \"!CHOICE!\"==\"0\" goto :EOF")
    e()

    for i, (cat, fix, name) in enumerate(flat, 1):
        e(f"if \"!CHOICE!\"==\"{i}\" ( set \"TEST={name}\" & set \"FIXTURE={fix}\" )")
    e()
    e("if \"!TEST!\"==\"\" (")
    e("    echo Invalid selection.")
    e("    pause")
    e("    goto :MENU")
    e(")")
    e()
    e("echo.")
    e("echo [RUN] !FIXTURE!.!TEST!")
    e("echo --------------------------------------------")
    e("\"%~dp0main_test.exe\" --gtest_filter=!FIXTURE!.!TEST! --server=!SERVER! --gtest_color=yes")
    e("echo.")
    e("pause")
    e("goto :MENU")
    e()
    e(":SERVER_INPUT")
    e("set /p \"INPUT_IP=Enter server IP   [127.0.0.1]: \"")
    e("if \"!INPUT_IP!\"==\"\" set \"INPUT_IP=127.0.0.1\"")
    e("set /p \"INPUT_PORT=Enter server port [50051]:    \"")
    e("if \"!INPUT_PORT!\"==\"\" set \"INPUT_PORT=50051\"")
    e("set \"SERVER=!INPUT_IP!:!INPUT_PORT!\"")
    e("goto :MENU")

    content = "\r\n".join(lines)
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    with open(output_path, "w", encoding="ascii", newline="") as f:
        f.write(content)
    print(f"Generated: {output_path}")


if __name__ == "__main__":
    script_dir = os.path.dirname(os.path.abspath(__file__))
    test_dir = os.path.join(script_dir, "..", "test")
    output_path = os.path.join(script_dir, "hello_client_test.bat")

    tests = find_test_cases(test_dir)
    for cat, entries in tests.items():
        print(f"  [{cat}]: {len(entries)} tests")
    generate_bat(tests, output_path)
