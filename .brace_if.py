"""
Brace bracket-less if / else if / else bodies in Allman style.

For each unbraced if/else body, wraps it in `{ ... }` with the brace placement
matching the surrounding indentation. Skips third-party code, the user-listed
explicit-skip files, and a small set of judgement-call cases (control-flow
keywords as body, statement-end ambiguity).

Usage:
    python .brace_if.py                 # write
    python .brace_if.py --dry           # dry run
    python .brace_if.py --files <p1>... # only the given files
    python .brace_if.py --verbose       # also list skipped files
"""

from __future__ import annotations

import argparse
import os
from typing import List, Tuple


ROOT = r"C:\Users\panos\Desktop\spartan_engine\source"

EXPLICIT_SKIP = {
    os.path.normcase(os.path.normpath(os.path.join(ROOT, "runtime", "Resource", "ResourceCache.h"))),
    os.path.normcase(os.path.normpath(os.path.join(ROOT, "runtime", "Resource", "ResourceCache.cpp"))),
    os.path.normcase(os.path.normpath(os.path.join(ROOT, "runtime", "Rendering", "Material.cpp"))),
}

THIRD_PARTY_PATH_HINTS = [
    "editor\\imgui\\source",
    "imguizmo",
    "tinyddsloader.h",
    "bend_sss_cpu.h",
    "pugixml.hpp",
    "pugiconfig.hpp",
    "imgui_stdlib",
    "imstb_",
    "stb_sprintf",
    "imconfig",
    "third_party",
    "editor\\imgui\\texteditor",
    "editor\\imgui\\implementation\\imgui_impl_",
]


def gather_files(root: str) -> List[str]:
    out = []
    for dirpath, dirs, files in os.walk(root):
        for f in files:
            if f.endswith((".cpp", ".h", ".hpp")):
                out.append(os.path.join(dirpath, f))
    return out


def should_skip(path: str, content: str) -> Tuple[bool, str]:
    norm = os.path.normcase(os.path.normpath(path))
    if norm in EXPLICIT_SKIP:
        return True, "explicit skip"
    low = norm.replace("/", "\\").lower()
    for hint in THIRD_PARTY_PATH_HINTS:
        if hint.lower() in low:
            return True, f"third-party path ({hint})"
    head = content[:3000]
    if "Copyright" in head and "Panos Karabelas" not in head:
        return True, "third-party copyright header"
    if "Panos Karabelas" not in head and "editor\\imgui\\" in low:
        return True, "no panos header under editor\\imgui\\"
    return False, ""


def strip_noncode(s: str) -> str:
    """Replace strings, char literals, comments, raw strings, and preprocessor
    directives with spaces (preserving line breaks and original positions)."""
    n = len(s)
    out = list(s)
    i = 0
    while i < n:
        c = s[i]

        if c == "/" and i + 1 < n and s[i + 1] == "*":
            j = s.find("*/", i + 2)
            if j == -1:
                j = n
            else:
                j += 2
            for k in range(i, j):
                if out[k] not in "\r\n":
                    out[k] = " "
            i = j
            continue

        if c == "/" and i + 1 < n and s[i + 1] == "/":
            k = i
            while k < n and s[k] != "\n":
                if out[k] not in "\r\n":
                    out[k] = " "
                k += 1
            i = k
            continue

        if c == "R" and i + 1 < n and s[i + 1] == '"':
            prev_ok = i == 0 or not (s[i - 1].isalnum() or s[i - 1] == "_")
            if prev_ok:
                k = i + 2
                delim_start = k
                while k < n and s[k] != "(" and s[k] != "\n":
                    k += 1
                if k < n and s[k] == "(":
                    delim = s[delim_start:k]
                    end_marker = ")" + delim + '"'
                    j = s.find(end_marker, k + 1)
                    if j == -1:
                        j = n
                    else:
                        j += len(end_marker)
                    for kk in range(i, j):
                        if out[kk] not in "\r\n":
                            out[kk] = " "
                    i = j
                    continue

        if c == '"':
            k = i + 1
            while k < n:
                if s[k] == "\\" and k + 1 < n:
                    k += 2
                    continue
                if s[k] == '"':
                    k += 1
                    break
                if s[k] == "\n":
                    break
                k += 1
            for kk in range(i, k):
                if out[kk] not in "\r\n":
                    out[kk] = " "
            i = k
            continue

        if c == "'":
            k = i + 1
            while k < n:
                if s[k] == "\\" and k + 1 < n:
                    k += 2
                    continue
                if s[k] == "'":
                    k += 1
                    break
                if s[k] == "\n":
                    break
                k += 1
            for kk in range(i, k):
                if out[kk] not in "\r\n":
                    out[kk] = " "
            i = k
            continue

        if c == "#":
            line_start = s.rfind("\n", 0, i) + 1
            if s[line_start:i].strip() == "":
                k = i
                while k < n:
                    if s[k] == "\\" and k + 1 < n and (s[k + 1] == "\n" or (s[k + 1] == "\r" and k + 2 < n and s[k + 2] == "\n")):
                        if out[k] not in "\r\n":
                            out[k] = " "
                        k += 1
                        if k < n and s[k] == "\r":
                            k += 1
                        if k < n and s[k] == "\n":
                            k += 1
                        continue
                    if s[k] == "\n":
                        break
                    if out[k] not in "\r\n":
                        out[k] = " "
                    k += 1
                i = k
                continue

        i += 1

    return "".join(out)


def is_word_char(c: str) -> bool:
    return c.isalnum() or c == "_"


def find_keywords(stripped: str):
    """Yield (kw_start, kw_after_keyword, kind) for if / else if / else."""
    n = len(stripped)
    i = 0
    while i < n:
        c = stripped[i]
        if c == "i" and stripped[i:i + 2] == "if":
            after = i + 2
            before_ok = i == 0 or not is_word_char(stripped[i - 1])
            after_ok = after >= n or not is_word_char(stripped[after])
            if before_ok and after_ok:
                yield i, after, "if"
                i = after
                continue
        elif c == "e" and stripped[i:i + 4] == "else":
            after = i + 4
            before_ok = i == 0 or not is_word_char(stripped[i - 1])
            after_ok = after >= n or not is_word_char(stripped[after])
            if before_ok and after_ok:
                j = after
                while j < n and stripped[j] in " \t\r\n":
                    j += 1
                if stripped[j:j + 2] == "if" and (j + 2 >= n or not is_word_char(stripped[j + 2])):
                    yield i, j + 2, "else_if"
                    i = j + 2
                    continue
                yield i, after, "else"
                i = after
                continue
        i += 1


def find_paren_open_after_if(stripped: str, end_after_if_kw: int) -> int:
    n = len(stripped)
    j = end_after_if_kw
    while j < n and stripped[j] in " \t\r\n":
        j += 1
    if stripped[j:j + 9] == "constexpr" and (j + 9 >= n or not is_word_char(stripped[j + 9])):
        j += 9
        while j < n and stripped[j] in " \t\r\n":
            j += 1
    if j < n and stripped[j] == "(":
        return j
    return -1


def find_matching_paren(stripped: str, open_idx: int) -> int:
    n = len(stripped)
    depth = 0
    i = open_idx
    while i < n:
        c = stripped[i]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def find_first_significant(stripped: str, start: int) -> int:
    n = len(stripped)
    i = start
    while i < n and stripped[i] in " \t\r\n":
        i += 1
    return i if i < n else -1


def find_expression_statement_end(stripped: str, start: int) -> int:
    """Scan forward to the first ';' at depth-0 (paren/bracket only). Used when the body
    is known to be a simple expression / jump statement (not a control-flow statement)."""
    n = len(stripped)
    depth_paren = 0
    depth_bracket = 0
    i = start
    while i < n:
        c = stripped[i]
        if c == "(":
            depth_paren += 1
        elif c == ")":
            depth_paren -= 1
            if depth_paren < 0:
                return -1
        elif c == "[":
            depth_bracket += 1
        elif c == "]":
            depth_bracket -= 1
            if depth_bracket < 0:
                return -1
        elif c == ";" and depth_paren == 0 and depth_bracket == 0:
            return i + 1
        i += 1
    return -1


def find_statement_end(stripped: str, start: int, max_depth: int = 32) -> int:
    """Find end (exclusive) of a single complete C++ statement starting at start.

    Recurses into nested if/else chains so the body of a nested if (whether
    braced or not, with or without else) is correctly delimited. Compound
    bodies end at the matching '}'. Other control-flow keywords are not
    recursed into; we fall back to ';' termination there.
    """
    if max_depth <= 0:
        return -1
    n = len(stripped)
    i = start
    while i < n and stripped[i] in " \t\r\n":
        i += 1
    if i >= n:
        return -1

    if stripped[i] == "{":
        depth = 1
        i += 1
        while i < n and depth > 0:
            c = stripped[i]
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
            i += 1
        if depth != 0:
            return -1
        return i

    before_ok = i == 0 or not is_word_char(stripped[i - 1])
    if before_ok and stripped[i:i + 2] == "if" and (i + 2 >= n or not is_word_char(stripped[i + 2])):
        i += 2
        while i < n and stripped[i] in " \t\r\n":
            i += 1
        if stripped[i:i + 9] == "constexpr" and (i + 9 >= n or not is_word_char(stripped[i + 9])):
            i += 9
            while i < n and stripped[i] in " \t\r\n":
                i += 1
        if i >= n or stripped[i] != "(":
            return -1
        depth = 1
        i += 1
        while i < n and depth > 0:
            if stripped[i] == "(":
                depth += 1
            elif stripped[i] == ")":
                depth -= 1
            i += 1
        if depth != 0:
            return -1
        body_end = find_statement_end(stripped, i, max_depth - 1)
        if body_end == -1:
            return -1
        j = body_end
        while j < n and stripped[j] in " \t\r\n":
            j += 1
        if stripped[j:j + 4] == "else" and (j + 4 >= n or not is_word_char(stripped[j + 4])):
            j += 4
            else_body_end = find_statement_end(stripped, j, max_depth - 1)
            if else_body_end == -1:
                return -1
            return else_body_end
        return body_end

    return find_expression_statement_end(stripped, i)


def line_indent(s: str, pos: int) -> str:
    line_start = s.rfind("\n", 0, pos) + 1
    j = line_start
    while j < len(s) and s[j] in " \t":
        j += 1
    return s[line_start:j]


def line_start_index(s: str, pos: int) -> int:
    return s.rfind("\n", 0, pos) + 1


def detect_newline(s: str) -> str:
    crlf = s.count("\r\n")
    lf = s.count("\n") - crlf
    return "\r\n" if crlf > lf else "\n"


def transform_file(content: str) -> Tuple[str, int, List[str]]:
    """Return (new_content, num_fixes, list of skip-reasons for sites we left alone).

    Per call, only the OUTERMOST eligible sites are emitted; nested unbraced
    ifs whose keyword falls within an outer site's body range are deferred.
    The caller is expected to iterate until the file reaches a fixed point.
    """
    stripped = strip_noncode(content)
    nl = detect_newline(content)
    skipped_notes: List[str] = []

    keywords = list(find_keywords(stripped))

    sites = []
    immediate_edits: List[Tuple[int, int, str]] = []

    for kw_start, kw_after, kind in keywords:
        if kind == "else":
            pre_end = kw_after
        else:
            paren_open = find_paren_open_after_if(stripped, kw_after)
            if paren_open == -1:
                continue
            paren_close = find_matching_paren(stripped, paren_open)
            if paren_close == -1:
                continue
            pre_end = paren_close + 1

        body_start = find_first_significant(stripped, pre_end)
        if body_start == -1:
            continue

        bc = stripped[body_start]

        if bc == "{":
            continue

        if bc == ";":
            indent = line_indent(content, kw_start)
            seg = content[pre_end:body_start + 1]
            if "\n" in seg:
                first_nl = seg.find("\n")
                replacement = seg[:first_nl + 1] + indent + "{" + nl + indent + "}"
            else:
                replacement = nl + indent + "{" + nl + indent + "}"
            immediate_edits.append((pre_end, body_start + 1, replacement))
            continue

        if bc.isalpha() or bc == "_":
            j = body_start
            while j < len(stripped) and is_word_char(stripped[j]):
                j += 1
            first_word = stripped[body_start:j]
            if first_word in {"do", "switch", "try", "for", "while", "case", "default"}:
                line_no = content.count("\n", 0, kw_start) + 1
                skipped_notes.append(
                    f"  line {line_no}: body starts with control-flow keyword '{first_word}', skipped (judgement)"
                )
                continue

        body_end = find_statement_end(stripped, body_start)
        if body_end == -1:
            line_no = content.count("\n", 0, kw_start) + 1
            skipped_notes.append(f"  line {line_no}: could not find statement end, skipped")
            continue

        sites.append((kw_start, pre_end, body_start, body_end))

    body_ranges = [(s[2], s[3]) for s in sites]
    edits: List[Tuple[int, int, str]] = list(immediate_edits)
    fixes = len(immediate_edits)

    for idx, (kw_start, pre_end, body_start, body_end) in enumerate(sites):
        nested = False
        for j, (bs, be) in enumerate(body_ranges):
            if j == idx:
                continue
            if bs <= kw_start < be:
                nested = True
                break
        if nested:
            continue

        indent = line_indent(content, kw_start)
        body_indent = indent + "    "

        seg = content[pre_end:body_start]
        first_nl = seg.find("\n")
        if first_nl == -1:
            head_replacement = nl + indent + "{" + nl + body_indent
            tail_insert = nl + indent + "}"
            new_text = head_replacement + content[body_start:body_end] + tail_insert
            edits.append((pre_end, body_end, new_text))
        else:
            ls = line_start_index(content, body_start)
            insert_open = indent + "{" + nl
            tail_insert = nl + indent + "}"
            edits.append((ls, ls, insert_open))
            edits.append((body_end, body_end, tail_insert))

        fixes += 1

    if not edits:
        return content, 0, skipped_notes

    edits.sort(key=lambda e: (e[0], e[1]), reverse=True)
    out = content
    for start, end, repl in edits:
        out = out[:start] + repl + out[end:]
    return out, fixes, skipped_notes


def process(paths, dry_run: bool, verbose: bool):
    touched = []
    skipped = []
    notes_summary = []
    for p in paths:
        try:
            with open(p, "r", encoding="utf-8", newline="") as f:
                content = f.read()
        except UnicodeDecodeError:
            with open(p, "r", encoding="latin-1", newline="") as f:
                content = f.read()
        skip, why = should_skip(p, content)
        if skip:
            skipped.append((p, why))
            continue
        new_content, fixes, notes = transform_file(content)
        if fixes > 0:
            touched.append((p, fixes))
            if notes:
                notes_summary.append((p, notes))
            if not dry_run:
                with open(p, "w", encoding="utf-8", newline="") as f:
                    f.write(new_content)
        elif notes:
            notes_summary.append((p, notes))

    print("=" * 60)
    print(f"Mode: {'DRY RUN' if dry_run else 'WRITE'}")
    print(f"Files touched: {len(touched)}")
    total = 0
    for p, c in touched:
        rel = os.path.relpath(p, os.path.dirname(ROOT))
        print(f"  {rel}: {c} site(s)")
        total += c
    print(f"Total fixes: {total}")
    print(f"Skipped (third-party / explicit): {len(skipped)}")
    if verbose:
        for p, why in skipped:
            rel = os.path.relpath(p, os.path.dirname(ROOT))
            print(f"  - {rel}: {why}")
    if notes_summary:
        print("\nJudgement-call sites (left alone, please review):")
        for p, ns in notes_summary:
            rel = os.path.relpath(p, os.path.dirname(ROOT))
            print(f"  {rel}:")
            for n in ns:
                print(n)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--files", nargs="*", default=None)
    args = ap.parse_args()

    if args.files:
        paths = [os.path.abspath(p) for p in args.files]
    else:
        paths = gather_files(ROOT)

    process(paths, dry_run=args.dry, verbose=args.verbose)


if __name__ == "__main__":
    main()
