#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Dependency graph and porting analysis for upstream firmware unity builds.

Finds the unity root (.c that #includes all others), extracts the symbols each
file defines, cross-references identifier use, and classifies each file's
dependencies against modd's contract headers. Output is meant for an LLM that
has to write a HOST wrapper (stubs for the symbols a file needs but cannot get).

Five dependency classes are tracked (the first version saw only functions):
  1. Header symbols     - #define constants / function-like macros and static
                          (inline) functions from the .h files the unity build
                          includes ("macros", "inlines"; source = the header).
  2. Global variables   - file-scope `static` variables defined in one .c and
                          used in another ("globals").
  3. Function signatures - parameter counts are captured; when a contract
                          header provides a symbol the upstream also defines,
                          differing arg counts are `signature_warnings`.
  4. Transitive includes - a .c that `#include`s other .c files inherits their
                          external deps; they are merged into the parent and
                          reported as `from_sub_includes`.
  5. Rich output        - per file: sub_includes, deps (contract_satisfied,
                          signature_warnings, needs_stub, independent,
                          circular_*), stub_estimate, confidence, atomic_block.

Per-file recommendation (based on stub_estimate.total, threshold 30):
  ADAPTER  - nothing to stub
  HOST     - stub_estimate.total <= HOST_STUB_THRESHOLD
  FORK     - more than that

Confidence: high = only function deps; medium = macro/global/inline deps
(regex-based, may have false positives); low = a needed name is also a struct
field name in the file (ambiguous match).

Usage:
    python tools/debug/dep_graph.py firmware/upstream/felucca
    python tools/debug/dep_graph.py firmware/upstream/felucca --json
    python tools/debug/dep_graph.py firmware/upstream/felucca --file seq.c [--json]
    python tools/debug/dep_graph.py firmware/upstream/felucca --root firmware/src/felucca.c
    python tools/debug/dep_graph.py firmware/upstream/melodee --contracts firmware/contracts
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from collections import defaultdict
from pathlib import Path
from typing import NamedTuple

C_KEYWORDS = frozenset({
    "auto", "break", "case", "char", "const", "continue", "default", "do",
    "double", "else", "enum", "extern", "float", "for", "goto", "if", "inline",
    "int", "long", "register", "return", "short", "signed", "sizeof", "static",
    "struct", "switch", "typedef", "union", "unsigned", "void", "volatile",
    "while", "_Bool", "_Complex", "_Imaginary",
    "NULL", "true", "false", "bool",
    "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "int8_t", "int16_t", "int32_t", "int64_t", "size_t", "ssize_t",
    "NELEM", "offsetof", "__attribute__",
})

COMMENT_RE = re.compile(r'//[^\n]*|/\*.*?\*/', re.DOTALL)
STRING_RE = re.compile(r'"(?:[^"\\\n]|\\.)*"')
CHAR_RE = re.compile(r"'(?:[^'\\\n]|\\.)*'")
PREPROC_RE = re.compile(
    r'^\s*#\s*(?:include|ifdef|ifndef|if|elif|else|endif|pragma|error|warning|undef|line)\b[^\n]*',
    re.MULTILINE,
)

# Parameter list with one level of nested parentheses (function pointers).
PARAMS = r'\(((?:[^()]|\([^()]*\))*)\)'

FUNC_DEF_RE = re.compile(
    r'^[a-zA-Z_][\w\s*]+?'
    r'\b(\w+)'
    r'\s*' + PARAMS +
    r'\s*\{',
    re.MULTILINE,
)

# File-scope static variable: static [volatile|const] <type> [*] name [N]... ; | = | ,
GLOBAL_RE = re.compile(
    r'^static\s+(?:(?:volatile|const)\s+)*'
    r'((?:unsigned\s+|signed\s+)?(?:struct\s+\w+|union\s+\w+|\w+)(?:\s+(?:long|int))*)'
    r'[\s*]*?(?:const\s+)?(\*[\s*]*)?\b(\w+)\s*(?:\[[^\]]*\]\s*)*[;=,]',
    re.MULTILINE,
)

INCLUDE_C_RE = re.compile(r'#\s*include\s+"([^"]+\.c)"')
INCLUDE_ANY_RE = re.compile(r'#\s*include\s+"([^"]+\.[ch])"')
IDENT_RE = re.compile(r'\b([a-zA-Z_]\w+)\b')
FIELD_RE = re.compile(r'(?:\.|->)\s*([a-zA-Z_]\w+)')

# Contract headers: declarations / definitions (name only, old broad match),
# plus an anchored match that also yields the parameter list.
FUNC_DECL_RE = re.compile(r'\b(\w+)\s*\([^)]*\)\s*[{;]')
FUNC_DECL_ARGS_RE = re.compile(
    r'^(?:(?:static|extern|inline)\s+|__attribute__\(\(.*?\)\)\s+)*'
    r'[a-zA-Z_][\w \t*]*?\b(\w+)\s*' + PARAMS + r'\s*[{;]',
    re.MULTILINE,
)
MACRO_RE = re.compile(
    r'^[ \t]*#[ \t]*define[ \t]+(\w+)(\([^)\n]*\))?[ \t]*((?:[^\n\\]|\\\n|\\.)*)',
    re.MULTILINE,
)

HOST_STUB_THRESHOLD = 30
# Rough stub size in lines per symbol kind.
LINES_PER = {"functions": 6, "macros": 1, "globals": 2, "inlines": 2}
KIND_PLURAL = {"function": "functions", "macro": "macros", "global": "globals", "inline": "inlines"}


class Sym(NamedTuple):
    name: str
    kind: str          # function | macro | global | inline
    source: Path
    line: int
    args: int | None
    type: str | None
    value: str | None


def _blank(m: re.Match) -> str:
    """Replace a match by spaces, keeping newlines so line numbers survive."""
    return re.sub(r'[^\n]', ' ', m.group(0))


def strip_comments(text: str) -> str:
    return COMMENT_RE.sub(_blank, text)


def strip_noise(text: str) -> str:
    text = strip_comments(text)
    text = STRING_RE.sub('""', text)
    text = CHAR_RE.sub("' '", text)
    return text


def count_params(params: str) -> int | None:
    """Number of parameters; None when variadic."""
    p = params.strip()
    if not p or p == "void":
        return 0
    if "..." in p:
        return None
    depth = 0
    n = 1
    for ch in p:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        elif ch == "," and depth == 0:
            n += 1
    return n


def find_unity_root(upstream_dir: Path) -> Path | None:
    best, best_count = None, 0
    src_dirs = [
        upstream_dir / "firmware" / "src",
        upstream_dir / "src",
        upstream_dir,
    ]
    for src_dir in src_dirs:
        if not src_dir.is_dir():
            continue
        for f in src_dir.glob("*.c"):
            text = f.read_text(errors="replace")
            count = len(INCLUDE_C_RE.findall(text))
            if count > best_count:
                best, best_count = f, count
    return best


def resolve_include(include_path: str, from_file: Path, search_dirs: list[Path]) -> Path | None:
    candidate = from_file.parent / include_path
    if candidate.is_file():
        return candidate.resolve()
    for d in search_dirs:
        candidate = d / include_path
        if candidate.is_file():
            return candidate.resolve()
    return None


def collect_included_files(root: Path, search_dirs: list[Path]) -> list[Path]:
    seen: set[Path] = set()
    ordered: list[Path] = []

    def _walk(filepath: Path):
        rp = filepath.resolve()
        if rp in seen:
            return
        seen.add(rp)
        text = filepath.read_text(errors="replace")
        for inc in INCLUDE_C_RE.findall(text):
            resolved = resolve_include(inc, filepath, search_dirs)
            if resolved and resolved not in seen:
                _walk(resolved)
        ordered.append(rp)

    _walk(root)
    return ordered


def collect_headers(files: list[Path], search_dirs: list[Path]) -> list[Path]:
    """Quoted-include .h files reachable from the unity files (transitively)."""
    seen: set[Path] = set()
    ordered: list[Path] = []

    def _walk(filepath: Path):
        text = strip_comments(filepath.read_text(errors="replace"))
        for inc in INCLUDE_ANY_RE.findall(text):
            if not inc.endswith(".h"):
                continue
            resolved = resolve_include(inc, filepath, search_dirs)
            if resolved and resolved not in seen:
                seen.add(resolved)
                ordered.append(resolved)
                _walk(resolved)

    for f in files:
        _walk(f)
    return ordered


def direct_c_includes(filepath: Path, search_dirs: list[Path]) -> list[Path]:
    text = strip_comments(filepath.read_text(errors="replace"))
    out = []
    for inc in INCLUDE_C_RE.findall(text):
        r = resolve_include(inc, filepath, search_dirs)
        if r:
            out.append(r)
    return out


def extract_symbols(filepath: Path, is_header: bool) -> list[Sym]:
    """All symbols a file defines: functions, globals, macros."""
    text = filepath.read_text(errors="replace")
    nocomment = strip_comments(text)
    clean = PREPROC_RE.sub(lambda m: re.sub(r'[^\n]', ' ', m.group(0)), strip_noise(text))
    syms: list[Sym] = []

    for m in MACRO_RE.finditer(nocomment):
        name, params, value = m.group(1), m.group(2), m.group(3)
        if name in C_KEYWORDS or len(name) < 2:
            continue
        value = re.sub(r'\s+', ' ', value.replace('\\\n', ' ')).strip()
        line = nocomment[:m.start()].count('\n') + 1
        if params is not None:
            syms.append(Sym(name, "macro", filepath, line, count_params(params[1:-1]), None, value[:80]))
        else:
            if len(name) <= 2 or (not value and name.endswith(("_H", "_H_", "_INCLUDED"))):
                continue
            syms.append(Sym(name, "macro", filepath, line, None, None, value[:80]))

    for m in FUNC_DEF_RE.finditer(clean):
        name = m.group(1)
        if name in C_KEYWORDS or len(name) < 2:
            continue
        line = clean[:m.start()].count('\n') + 1
        syms.append(Sym(name, "inline" if is_header else "function", filepath, line,
                        count_params(m.group(2)), None, None))

    for m in GLOBAL_RE.finditer(clean):
        typ, ptr, name = m.group(1), m.group(2), m.group(3)
        if name in C_KEYWORDS or len(name) < 2 or typ in ("inline", "return"):
            continue
        line = clean[:m.start()].count('\n') + 1
        t = re.sub(r'\s+', ' ', typ).strip() + (" *" if ptr else "")
        syms.append(Sym(name, "global", filepath, line, None, t, None))
    return syms


def extract_identifiers(filepath: Path) -> tuple[set[str], set[str]]:
    """Returns (identifiers used outside struct-field position, field names)."""
    text = filepath.read_text(errors="replace")
    clean = strip_noise(text)
    clean = PREPROC_RE.sub('', clean)
    fields = set(FIELD_RE.findall(clean))
    ids = set(IDENT_RE.findall(FIELD_RE.sub(' ', clean)))
    ids -= C_KEYWORDS
    return ids, fields


class ContractSym(NamedTuple):
    header: str
    kind: str
    args: int | None


def _contract_header_regex(h: Path, symbols: dict[str, ContractSym]) -> None:
    text = h.read_text(errors="replace")
    clean = strip_noise(text)
    header = h.name
    anchored: dict[str, int | None] = {}
    for m in FUNC_DECL_ARGS_RE.finditer(clean):
        if m.group(1) not in C_KEYWORDS:
            anchored[m.group(1)] = count_params(m.group(2))
    for m in FUNC_DECL_RE.finditer(clean):
        name = m.group(1)
        if name not in C_KEYWORDS and len(name) > 1:
            symbols[name] = ContractSym(header, "function", anchored.get(name))
    _contract_macros_regex(h, symbols)


def _contract_macros_regex(h: Path, symbols: dict[str, ContractSym]) -> None:
    text = h.read_text(errors="replace")
    header = h.name
    for m in MACRO_RE.finditer(strip_comments(text)):
        name = m.group(1)
        if name in C_KEYWORDS or len(name) <= 1:
            continue
        if m.group(2) is not None:
            symbols.setdefault(name, ContractSym(header, "macro", count_params(m.group(2)[1:-1])))
        elif len(name) > 2:
            symbols.setdefault(name, ContractSym(header, "macro", None))


def extract_contract_symbols(contracts_dir: Path) -> dict[str, ContractSym]:
    """Symbols provided by contract headers: {symbol: ContractSym}."""
    symbols: dict[str, ContractSym] = {}
    if not contracts_dir.is_dir():
        return symbols
    for h in sorted(contracts_dir.glob("*.h")):
        _contract_header_regex(h, symbols)
    return symbols


# ---------------------------------------------------------------------------
# pycparser-based parsing (headers and contracts only; falls back to regex)
# ---------------------------------------------------------------------------

try:
    from pycparser import CParser, c_ast, c_generator
except Exception:  # pycparser not installed
    CParser = c_ast = c_generator = None

FAKE_INCLUDE_DIR = Path(__file__).resolve().parent / "dep_graph_includes"
USE_PYCPARSER = True


def _find_clang() -> str | None:
    return shutil.which("clang")


def pycparser_available() -> bool:
    return USE_PYCPARSER and CParser is not None and _find_clang() is not None and FAKE_INCLUDE_DIR.is_dir()


# Host-provided sizes that contract headers require before inclusion.
HOST_DEFINES = ["NVOICE=8", "NPART=4", "NTRK=8", "NSTEP=16", "NENGINES=16", "CTL=32"]


def _preprocess(filepath: Path, search_dirs: list[Path], fake_include_dir: Path,
                defines: list[str] = ()) -> str | None:
    clang = _find_clang()
    if not clang:
        return None
    cmd = [clang, "-E", "-nostdinc", f"-I{fake_include_dir}", f"-I{filepath.parent}"]
    cmd += [f"-I{d}" for d in search_dirs]
    cmd += [f"-D{d}" for d in defines]
    cmd += ["-D__attribute__(x)=", "-D__asm__(...)=", "-D__builtin_expect(x,y)=(x)",
            "-D__extension__=", "-D__inline__=inline", "-D__inline=inline",
            "-Wno-everything", str(filepath)]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    except (OSError, subprocess.SubprocessError):
        return None
    if r.returncode != 0:
        return None
    return r.stdout


def _stub_host_types(text: str) -> str:
    """Typedef every `*_t` name that is used but never declared (host types such as track_t)."""
    body = re.sub(r'^#.*$', '', text, flags=re.MULTILINE)
    used = set(re.findall(r'\b([A-Za-z_]\w*_t)\b', body))
    missing = [n for n in sorted(used)
               if not re.search(r'(typedef[^;{]*?\b%s\s*;|}\s*%s\s*;)' % (n, n), body)]
    if not missing:
        return text
    return "".join(f"typedef int {n};\n" for n in missing) + text


def _parse_ast(filepath: Path, search_dirs: list[Path], fake_include_dir: Path,
               defines: list[str] = ()):
    """Preprocess + parse; returns the pycparser FileAST or None."""
    if CParser is None:
        return None
    text = _preprocess(filepath, search_dirs, fake_include_dir, defines)
    if text is None:
        return None
    text = _stub_host_types(text)
    try:
        return CParser().parse(text, filename=str(filepath))
    except Exception:
        return None


def _func_decl_of(type_node):
    """The FuncDecl inside a Decl's type chain, if the declared name is a function."""
    return type_node if isinstance(type_node, c_ast.FuncDecl) else None


def _func_args(fd) -> int | None:
    if fd.args is None:
        return 0
    params = fd.args.params
    if any(isinstance(p, c_ast.EllipsisParam) for p in params):
        return None
    if len(params) == 1 and isinstance(params[0], c_ast.Typename) and \
            isinstance(params[0].type, c_ast.TypeDecl) and \
            isinstance(params[0].type.type, c_ast.IdentifierType) and \
            params[0].type.type.names == ["void"]:
        return 0
    return len(params)


def _type_str(gen, type_node) -> str:
    try:
        return re.sub(r"\s+", " ", gen._generate_type(type_node)).strip()
    except Exception:
        return "?"


def _same_file(coord_file: str | None, filepath: Path) -> bool:
    if not coord_file:
        return False
    try:
        return Path(coord_file).resolve() == filepath
    except OSError:
        return False


def parse_header_pycparser(filepath: Path, search_dirs: list[Path],
                           fake_include_dir: Path = FAKE_INCLUDE_DIR) -> list[Sym] | None:
    """Functions (as inline), globals and typedefs defined by a header, via clang -E + pycparser.

    Typedefs are walked but not emitted (the model has no typedef kind).
    Returns None when preprocessing or parsing fails.
    """
    filepath = filepath.resolve()
    ast = _parse_ast(filepath, search_dirs, fake_include_dir)
    if ast is None:
        return None
    gen = c_generator.CGenerator()
    syms: list[Sym] = []
    typedefs: dict[str, str] = {}

    class V(c_ast.NodeVisitor):
        def visit_FuncDef(self, node):
            c = node.decl.coord
            if c and _same_file(c.file, filepath) and len(node.decl.name) >= 2:
                fd = _func_decl_of(node.decl.type)
                syms.append(Sym(node.decl.name, "inline", filepath, c.line,
                                _func_args(fd) if fd else None, _type_str(gen, node.decl.type.type), None))

        def visit_Typedef(self, node):
            c = node.coord
            if c and _same_file(c.file, filepath):
                typedefs[node.name] = _type_str(gen, node.type)

        def visit_Decl(self, node):
            c = node.coord
            if not (c and _same_file(c.file, filepath)) or not node.name or len(node.name) < 2:
                return
            if "extern" in (node.storage or []) or "typedef" in (node.storage or []):
                return
            if isinstance(node.type, (c_ast.FuncDecl, c_ast.Struct, c_ast.Union, c_ast.Enum)):
                return
            syms.append(Sym(node.name, "global", filepath, c.line, None, _type_str(gen, node.type), None))

    # Only top-level declarations: function bodies hold locals we do not want.
    v = V()
    for ext in ast.ext:
        v.visit(ext) if not isinstance(ext, c_ast.FuncDef) else v.visit_FuncDef(ext)
    return syms


def parse_contracts_pycparser(contracts_dir: Path,
                              fake_include_dir: Path = FAKE_INCLUDE_DIR) -> dict[str, ContractSym] | None:
    """Contract symbols with accurate argument counts. None when pycparser cannot run at all.

    A header that fails to parse falls back to the regex scan for that header only;
    macros always come from the regex scan (pycparser sees post-expansion text).
    """
    if not pycparser_available() or not contracts_dir.is_dir():
        return None
    symbols: dict[str, ContractSym] = {}
    ok = 0
    search = [contracts_dir, contracts_dir.parent, contracts_dir.parent / "hal"]
    for h in sorted(contracts_dir.glob("*.h")):
        ast = _parse_ast(h.resolve(), search, fake_include_dir, HOST_DEFINES)
        if ast is None:
            print(f"warning: pycparser failed on contract {h.name}; using regex", file=sys.stderr)
            _contract_header_regex(h, symbols)
            continue
        ok += 1
        hp = h.resolve()
        for ext in ast.ext:
            decl = ext.decl if isinstance(ext, c_ast.FuncDef) else ext
            if not isinstance(decl, c_ast.Decl) or not decl.name or len(decl.name) < 2:
                continue
            if not (decl.coord and _same_file(decl.coord.file, hp)):
                continue
            if isinstance(decl.type, c_ast.FuncDecl):
                symbols[decl.name] = ContractSym(h.name, "function", _func_args(decl.type))
        _contract_macros_regex(h, symbols)
    return symbols if ok else None


def tarjan_scc(nodes: list[Path], graph: dict[Path, dict[Path, list[str]]]) -> list[list[Path]]:
    sys.setrecursionlimit(max(sys.getrecursionlimit(), 10000))
    node_set = set(nodes)
    index_counter = [0]
    stack: list[Path] = []
    on_stack: set[Path] = set()
    index: dict[Path, int] = {}
    lowlink: dict[Path, int] = {}
    result: list[list[Path]] = []

    def strongconnect(v: Path):
        index[v] = index_counter[0]
        lowlink[v] = index_counter[0]
        index_counter[0] += 1
        stack.append(v)
        on_stack.add(v)
        for w in graph.get(v, {}):
            if w not in node_set:
                continue
            if w not in index:
                strongconnect(w)
                lowlink[v] = min(lowlink[v], lowlink[w])
            elif w in on_stack:
                lowlink[v] = min(lowlink[v], index[w])
        if lowlink[v] == index[v]:
            component: list[Path] = []
            while True:
                w = stack.pop()
                on_stack.discard(w)
                component.append(w)
                if w == v:
                    break
            result.append(component)

    for v in nodes:
        if v not in index:
            strongconnect(v)
    return result


def file_lines(filepath: Path) -> int:
    with filepath.open(errors="replace") as fh:
        return sum(1 for _ in fh)


def short_path(filepath: Path, base: Path) -> str:
    try:
        return str(filepath.relative_to(base))
    except ValueError:
        return str(filepath)


class Model(NamedTuple):
    files: list[Path]
    headers: list[Path]
    syms: dict[Path, list[Sym]]            # per file/header, own definitions
    refs: dict[Path, set[str]]
    fields: dict[Path, set[str]]
    subs: dict[Path, list[Path]]           # direct .c sub-includes
    closure: dict[Path, set[Path]]         # transitive sub-includes (excl. self)
    providers: dict[str, list[Sym]]        # name -> definitions (files first, headers after)


def _header_symbols(h: Path, search_dirs: list[Path]) -> list[Sym]:
    """pycparser symbols plus regex macros; plain regex when pycparser is unavailable or fails."""
    regex = extract_symbols(h, True)
    if not pycparser_available():
        return regex
    parsed = parse_header_pycparser(h, search_dirs)
    if parsed is None:
        print(f"warning: pycparser failed on {h.name}; using regex", file=sys.stderr)
        return regex
    return [s for s in regex if s.kind == "macro"] + parsed


def build_model(files: list[Path], headers: list[Path], search_dirs: list[Path]) -> Model:
    syms: dict[Path, list[Sym]] = {}
    refs: dict[Path, set[str]] = {}
    fields: dict[Path, set[str]] = {}
    for f in files:
        syms[f] = extract_symbols(f, False)
        refs[f], fields[f] = extract_identifiers(f)
    for h in headers:
        syms[h] = _header_symbols(h, search_dirs)
        refs[h], fields[h] = extract_identifiers(h)

    fileset = set(files)
    subs = {f: [s for s in direct_c_includes(f, search_dirs) if s in fileset and s != f] for f in files}
    closure: dict[Path, set[Path]] = {}

    def _clos(f: Path, trail: frozenset = frozenset()) -> set[Path]:
        if f in closure:
            return closure[f]
        out: set[Path] = set()
        for s in subs[f]:
            if s in trail or s == f:
                continue
            out.add(s)
            out |= _clos(s, trail | {f})
        closure[f] = out
        return out

    for f in files:
        _clos(f)

    providers: dict[str, list[Sym]] = defaultdict(list)
    for f in files + headers:
        for s in syms[f]:
            providers[s.name].append(s)
    return Model(files, headers, syms, refs, fields, subs, closure, providers)


def _own(model: Model, f: Path) -> set[Path]:
    return {f} | model.closure[f]


def resolve_deps(model: Model, f: Path) -> dict[str, Sym]:
    """External symbols f needs: {name: providing Sym}, closure-aware."""
    own = _own(model, f)
    own_names = {s.name for p in own for s in model.syms[p]}
    refs = set().union(*(model.refs[p] for p in own))
    out: dict[str, Sym] = {}
    for name in refs - own_names:
        cands = [s for s in model.providers.get(name, []) if s.source not in own]
        if not cands:
            continue
        cfiles = [s for s in cands if s.source in model.syms and s.source not in model.headers]
        out[name] = (cfiles or cands)[0]
    return out


def classify_all(model: Model, contract_syms: dict[str, ContractSym], base: Path) -> list[dict]:
    deps = {f: resolve_deps(model, f) for f in model.files}

    # Reduced graph for SCCs: file edges, contract-satisfied symbols removed.
    reduced: dict[Path, dict[Path, list[str]]] = defaultdict(lambda: defaultdict(list))
    for f, d in deps.items():
        for name, s in d.items():
            if name in contract_syms or s.source in model.headers:
                continue
            reduced[f][s.source].append(name)
    sccs = tarjan_scc(model.files, reduced)
    scc_of: dict[Path, int] = {}
    for i, scc in enumerate(sccs):
        if len(scc) > 1:
            for p in scc:
                scc_of[p] = i

    results = []
    for f in model.files:
        own = _own(model, f)
        own_defs = {s.name for s in model.syms[f]}
        direct_refs = model.refs[f]
        own_fields = set().union(*(model.fields[p] for p in own))

        contract: dict[str, list[dict]] = defaultdict(list)
        sig_warn: list[dict] = []
        needs: dict[str, list[dict]] = {"functions": [], "macros": [], "globals": [], "inlines": []}
        independent: dict[str, list[str]] = defaultdict(list)
        circ: dict[str, dict[str, list[str]]] = {k: defaultdict(list) for k in needs}
        from_sub: list[str] = []
        collisions: list[str] = []

        for name, s in sorted(deps[f].items()):
            src = short_path(s.source, base)
            is_hdr = s.source in model.headers
            via_sub = name not in direct_refs or name in own_defs
            if via_sub:
                from_sub.append(name)
            cs = contract_syms.get(name)
            if cs:
                entry = {"name": name, "type": s.kind if s.kind != "inline" else "function", "args": cs.args}
                contract[cs.header].append(entry)
                if (s.kind in ("function", "inline") and cs.kind == "function"
                        and s.args is not None and cs.args is not None and s.args != cs.args):
                    sig_warn.append({"name": name, "contract_args": cs.args,
                                     "upstream_args": s.args, "header": cs.header,
                                     "upstream_source": src})
                continue
            if name in own_fields:
                collisions.append(name)
            circular = False
            if is_hdr:
                # A header is circular for f when it refers back to what f (or its subs) define.
                hdefs = model.refs[s.source]
                circular = bool(hdefs & {x.name for p in own for x in model.syms[p]
                                         if x.kind in ("function", "global")})
            else:
                circular = scc_of.get(s.source, -2) == scc_of.get(f, -1) and f in scc_of
            if not is_hdr and not circular:
                independent[src].append(name)
                continue
            plural = KIND_PLURAL[s.kind]
            item: dict = {"name": name, "source": src}
            if s.kind in ("function", "inline"):
                item["args"] = s.args
            if s.kind == "macro":
                item["value"] = s.value
                if s.args is not None:
                    item["args"] = s.args
            if s.kind == "global":
                item["type"] = s.type
            if via_sub:
                item["via_sub_include"] = True
            needs[plural].append(item)
            if circular:
                circ[plural][src].append(name)

        est = {k: len(v) for k, v in needs.items()}
        est["total"] = sum(est.values())
        est["estimated_lines"] = sum(est[k] * LINES_PER[k] for k in LINES_PER)

        if est["total"] == 0:
            rec = "ADAPTER"
        elif est["total"] <= HOST_STUB_THRESHOLD:
            rec = "HOST"
        else:
            rec = "FORK"

        if collisions:
            conf = "low"
        elif est["macros"] or est["globals"] or est["inlines"]:
            conf = "medium"
        else:
            conf = "high"

        block = None
        if f in scc_of:
            block = sorted(short_path(p, base) for p in sccs[scc_of[f]] if p != f)

        results.append({
            "file": short_path(f, base),
            "lines": file_lines(f),
            "recommendation": rec,
            "confidence": conf,
            "sub_includes": [short_path(s, base) for s in model.subs[f]],
            "deps": {
                "contract_satisfied": {k: v for k, v in sorted(contract.items())},
                "signature_warnings": sig_warn,
                "needs_stub": needs,
                "independent": {k: sorted(v) for k, v in sorted(independent.items())},
                "circular_functions": {k: sorted(v) for k, v in circ["functions"].items()},
                "circular_globals": {k: sorted(v) for k, v in circ["globals"].items()},
                "circular_macros": {k: sorted(v) for k, v in circ["macros"].items()},
                "circular_inlines": {k: sorted(v) for k, v in circ["inlines"].items()},
                "from_sub_includes": sorted(from_sub),
                "field_name_collisions": sorted(collisions),
            },
            "stub_estimate": est,
            "atomic_block": block,
        })
    return results


def _more(items: list[str], n: int = 5) -> str:
    return ", ".join(items[:n]) + (f" (+{len(items) - n})" if len(items) > n else "")


def report_text(results: list[dict], base: Path) -> str:
    lines: list[str] = []
    lines.append(f"=== Porting analysis: {base.name} ===")
    lines.append(f"    (contract-aware; {len(results)} files; HOST threshold {HOST_STUB_THRESHOLD} stubs)\n")
    by_rec: dict[str, list[dict]] = defaultdict(list)
    for r in results:
        by_rec[r["recommendation"]].append(r)

    for rec, label, desc in [
        ("ADAPTER", "ADAPTER - import with thin adapter", "nothing to stub"),
        ("HOST", "HOST - import with stub wiring", f"stub_estimate.total <= {HOST_STUB_THRESHOLD}"),
        ("FORK", "FORK - too many stubs", "needs fork or full block import"),
    ]:
        group = by_rec.get(rec, [])
        if not group:
            continue
        lines.append(f"--- {label} ({len(group)} files) ---")
        lines.append(f"    {desc}\n")
        for r in sorted(group, key=lambda x: x["file"]):
            d = r["deps"]
            e = r["stub_estimate"]
            lines.append(f"  {r['file']:50s} {r['lines']:>5} lines  [{r['confidence']}]"
                         f"  stubs {e['total']} (~{e['estimated_lines']} lines)")
            if d["contract_satisfied"]:
                total_c = sum(len(v) for v in d["contract_satisfied"].values())
                lines.append(f"    contract: {total_c} symbols via {', '.join(d['contract_satisfied'])}")
            for w in d["signature_warnings"]:
                lines.append(f"    SIGNATURE {w['name']}: contract {w['contract_args']} args, "
                             f"upstream {w['upstream_args']} ({w['header']})")
            for dep, syms in d["independent"].items():
                lines.append(f"    needs {dep.split('/')[-1]}: {_more(syms)}")
            for kind in ("functions", "macros", "globals", "inlines"):
                circ = d.get("circular_" + kind, {})
                for dep, syms in circ.items():
                    lines.append(f"    circular {kind} {dep.split('/')[-1]}: {_more(syms)}")
            if r["sub_includes"]:
                lines.append(f"    sub-includes: {_more([p.split('/')[-1] for p in r['sub_includes']], 8)}")
            if r["atomic_block"]:
                lines.append(f"    block: {_more([p.split('/')[-1] for p in r['atomic_block']], 8)}")
        lines.append("")

    counts = {rec: len(by_rec.get(rec, [])) for rec in ("ADAPTER", "HOST", "FORK")}
    lines.append(f"Summary: {counts['ADAPTER']} ADAPTER, {counts['HOST']} HOST, {counts['FORK']} FORK")
    return "\n".join(lines)


def report_file_text(r: dict) -> str:
    d = r["deps"]
    e = r["stub_estimate"]
    out = [f"=== {r['file']} ({r['lines']} lines) ===",
           f"recommendation: {r['recommendation']}  confidence: {r['confidence']}",
           f"sub_includes: {', '.join(r['sub_includes']) or '-'}",
           f"stub_estimate: {json.dumps(e)}"]
    for hdr, items in d["contract_satisfied"].items():
        out.append(f"contract {hdr}: " + ", ".join(f"{i['name']}({i['args']})" if i['args'] is not None
                                                    else i['name'] for i in items))
    for w in d["signature_warnings"]:
        out.append(f"SIGNATURE MISMATCH {w['name']}: contract {w['contract_args']} vs "
                   f"upstream {w['upstream_args']} ({w['header']}, {w['upstream_source']})")
    for kind, items in d["needs_stub"].items():
        if not items:
            continue
        out.append(f"needs_stub {kind} ({len(items)}):")
        for i in items:
            extra = " ".join(f"{k}={v}" for k, v in i.items() if k not in ("name", "source"))
            out.append(f"  {i['name']:30s} {i['source']}  {extra}")
    for dep, syms in d["independent"].items():
        out.append(f"independent {dep}: {', '.join(syms)}")
    if d["from_sub_includes"]:
        out.append(f"from sub-includes: {', '.join(d['from_sub_includes'])}")
    if d["field_name_collisions"]:
        out.append(f"field-name collisions (low confidence): {', '.join(d['field_name_collisions'])}")
    if r["atomic_block"]:
        out.append(f"atomic block: {', '.join(r['atomic_block'])}")
    return "\n".join(out)


def report_json(results: list[dict], base: Path) -> str:
    return json.dumps({
        "upstream": base.name,
        "files": results,
        "summary": {
            "adapter": sum(1 for r in results if r["recommendation"] == "ADAPTER"),
            "host": sum(1 for r in results if r["recommendation"] == "HOST"),
            "fork": sum(1 for r in results if r["recommendation"] == "FORK"),
        },
    }, indent=2)


def main():
    parser = argparse.ArgumentParser(
        description="Dependency graph and porting analysis for upstream firmware unity builds."
    )
    parser.add_argument("upstream_dir", type=Path, help="Path to upstream directory")
    parser.add_argument("--root", type=Path, default=None,
                        help="Unity root .c file (auto-detected if omitted)")
    parser.add_argument("--contracts", type=Path, default=None,
                        help="Path to contract headers dir (default: firmware/contracts)")
    parser.add_argument("--json", action="store_true", help="JSON output")
    parser.add_argument("--file", default=None, metavar="FILENAME",
                        help="Detailed analysis for one file (name or path suffix)")
    parser.add_argument("--no-pycparser", action="store_true",
                        help="Skip clang+pycparser parsing of headers/contracts; use regex only")
    args = parser.parse_args()
    global USE_PYCPARSER
    USE_PYCPARSER = not args.no_pycparser
    if USE_PYCPARSER and not pycparser_available():
        print("pycparser/clang unavailable; using regex only.", file=sys.stderr)

    upstream = args.upstream_dir.resolve()
    if not upstream.is_dir():
        print(f"Error: {upstream} is not a directory", file=sys.stderr)
        sys.exit(1)

    contracts_dir = args.contracts
    if contracts_dir is None:
        for parent in [upstream.parent.parent, upstream.parent.parent.parent, Path.cwd()]:
            candidate = parent / "firmware" / "contracts"
            if candidate.is_dir():
                contracts_dir = candidate
                break
    if contracts_dir:
        contracts_dir = contracts_dir.resolve()

    root = args.root
    if root is None:
        root = find_unity_root(upstream)
        if root is None:
            print("Error: could not find unity root. Use --root.", file=sys.stderr)
            sys.exit(1)
        print(f"Unity root: {short_path(root, upstream)}", file=sys.stderr)
    else:
        root = root.resolve()

    search_dirs = [
        root.parent,
        upstream / "firmware" / "src",
        upstream / "firmware",
        upstream / "src",
        upstream,
    ]

    files = collect_included_files(root, search_dirs)
    if not files:
        print("No .c files found in the unity build.", file=sys.stderr)
        sys.exit(1)
    headers = collect_headers(files, search_dirs)

    contract_syms: dict[str, ContractSym] = {}
    if contracts_dir and contracts_dir.is_dir():
        parsed_contracts = parse_contracts_pycparser(contracts_dir) if pycparser_available() else None
        if parsed_contracts is None:
            if pycparser_available():
                print("warning: pycparser contract parsing failed; using regex", file=sys.stderr)
            parsed_contracts = extract_contract_symbols(contracts_dir)
        contract_syms = parsed_contracts
        print(f"Contracts: {len(contract_syms)} symbols from {contracts_dir.name}/", file=sys.stderr)
    else:
        print("No contracts dir found; running without contract awareness.", file=sys.stderr)

    print(f"Analyzing {len(files)} files, {len(headers)} headers...", file=sys.stderr)

    model = build_model(files, headers, search_dirs)
    results = classify_all(model, contract_syms, upstream)

    if args.file:
        want = args.file
        match = [r for r in results if r["file"] == want or r["file"].endswith("/" + want)]
        if not match:
            print(f"Error: no analyzed file matches {want!r}", file=sys.stderr)
            sys.exit(1)
        for r in match:
            print(json.dumps(r, indent=2) if args.json else report_file_text(r))
        return

    if args.json:
        print(report_json(results, upstream))
    else:
        print(report_text(results, upstream))


if __name__ == "__main__":
    main()
