#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Generate the REST API's OpenAPI 3.1 document from the firmware's C sources.

What the C gives (firmware/src/rpc/api_ctl.c, api_pages.c, rpc_server.c):
  - the routes and their #if guards: API_ROUTES[] in api_ctl.c,
  - each {param}: the api_arg_*() call that checks it (name, bounds, names),
  - each reply: the JSON its handler writes, replayed through the helpers it calls
    (rpc_tx_key_*, rpc_tx_raw/char literals, api_stream_*, api_tx_jstr), with each
    field's #if guard and whether every reply has it,
  - the error codes: rpc_http_error(<code>) / api_arg_fail(<code>) on the way.
What it does not give lives in docs/api/openapi.overlay.yaml: summaries, prose,
units, examples. A guard becomes `x-kconfig: [expr, ...]` on the route or field;
the per-build filter keeps it when every expression holds for that build.

    gen_openapi.py --config build/<dc>/gen/config.h --version V \\
        --json build/<dc>/openapi.json [--header build/<dc>/gen/openapi_json.h]
    gen_openapi.py --check            # read the sources and the overlay, print problems
    gen_openapi.py --full FILE        # the document for every build, guards kept

Strict: a route, a parameter, a JSON write or an overlay key it cannot place stops
it with the file, the line, the reason and the fix (exit 1, every problem in one
run). Rules for a handler it can read: docs/TESTING.md, "Writing a REST handler".
"""
from __future__ import annotations

import argparse
import ast
import copy
import difflib
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[2]
RPC = ROOT / 'firmware' / 'src' / 'rpc'
SOURCES = [RPC / 'api_ctl.c', RPC / 'api_pages.c', RPC / 'rpc_server.c']
REST_ONLY = ('api_ctl.c', 'api_pages.c')   # every JSON write in these belongs to a route
OVERLAY = ROOT / 'docs' / 'api' / 'openapi.overlay.yaml'
CONST_DIRS = [ROOT / 'firmware' / 'adapters', ROOT / 'firmware' / 'hal', ROOT / 'firmware' / 'src']
RULES = 'docs/TESTING.md#writing-a-rest-handler'
ROUTES_TABLE = 'API_ROUTES'


@dataclass
class Config:
    """What the C does not say by itself. Each entry is named in an error's fix."""
    # Non-Kconfig macros in #if lines, in Kconfig terms.
    macros: dict = field(default_factory=lambda: {
        'UI_SHELL': 'UI_SHELL_MODD',            # modd.c: themes with shell_* entry points
        'WEATHER_COLORS_H': 'LWIP',            # a shell's page_weather.c (built with LWIP) includes it
    })
    # Calls through a function pointer: the table of {"NAME", function} it comes from.
    dispatch: dict = field(default_factory=lambda: {'jfn': 'api_page_json_table'})
    # Functions in api_ctl.c / api_pages.c that write JSON outside any route.
    ignore: set = field(default_factory=lambda: {'api_usb_audio_ring', 'api_usb_audio_ring_arm', 'api_usb_audio_ring_disarm', 'api_usb_regs'})


# --- what the JSON writers are -------------------------------------------------------------

KEY_WRITERS = {'rpc_tx_key_str': 'string', 'rpc_tx_key_int': 'integer',
               'rpc_tx_key_uint': 'uint', 'rpc_tx_key_bool': 'boolean'}       # (tx, "key", value)
VALUE_WRITERS = {'rpc_tx_int': ('integer', 1), 'rpc_tx_uint': ('uint', 1),
                 'api_stream_int': ('integer', 0), 'api_stream_uint': ('uint', 0),
                 'api_stream_jstr': ('qstring', 0), 'api_tx_jstr': ('qstring', 1),
                 'api_stream_b64': ('b64', 0)}                               # kind, value arg
TEXT_WRITERS = {'rpc_tx_raw': 1, 'rpc_tx_char': 1, 'api_stream_str': 0, 'api_stream_put': 0}
ERROR_CALLS = {'rpc_http_error': 0, 'api_arg_fail': 0}                       # status arg
ARG_CHECKS = ('api_arg_str', 'api_arg_int', 'api_arg_int_or', 'api_arg_enum', 'api_arg_hex32',
              'api_arg_hexbytes', 'api_arg_notes')
NOT_WALKED = (set(KEY_WRITERS) | set(VALUE_WRITERS) | set(TEXT_WRITERS) | set(ERROR_CALLS)
              | {'api_stream_begin', 'api_stream_end', 'api_stream_flush', 'rpc_tx_init',
                 'rpc_http_respond', 'api_ctl_route'})
WRITERS = set(KEY_WRITERS) | set(VALUE_WRITERS) | set(TEXT_WRITERS)
KEYWORDS = {'if', 'for', 'while', 'switch', 'return', 'sizeof', 'do', 'else', 'case', 'default',
            'break', 'continue', 'goto', 'typedef', 'struct', 'static', 'const'}


# --- problems -----------------------------------------------------------------------------

@dataclass
class Problem:
    where: str          # file:line
    what: str           # the call or key, quoted
    why: str
    fixes: list

    def __str__(self):
        out = [f'gen_openapi: {self.where}: {self.what}', f'  {self.why}']
        if self.fixes:
            out.append('  Fix one of:' if len(self.fixes) > 1 else '  Fix:')
            for f in self.fixes:
                lines = f.splitlines()
                out.append(f'    - {lines[0]}')
                out += [f'      {l}' for l in lines[1:]]
        out.append(f'  Rules for handlers: {RULES}')
        return '\n'.join(out)


class GenError(Exception):
    def __init__(self, problems):
        self.problems = problems
        super().__init__('\n\n'.join(str(p) for p in problems))


def rel(path):
    try:
        return str(Path(path).resolve().relative_to(ROOT))
    except ValueError:
        return str(path)


# --- C reading ----------------------------------------------------------------------------

@dataclass
class Tok:
    kind: str     # id num str chr op
    v: str        # text; for str/chr the unescaped value
    line: int
    raw: str = ''


TOKEN_RE = re.compile(r'''
    (?P<ws>\s+) | (?P<lc>//[^\n]*) | (?P<bc>/\*.*?\*/)
  | (?P<str>"(?:\\.|[^"\\\n])*") | (?P<chr>'(?:\\.|[^'\\\n])+')
  | (?P<num>0[xX][0-9a-fA-F]+[uUlL]*|\d+[uUlL]*)
  | (?P<id>[A-Za-z_]\w*)
  | (?P<op>->|&&|\|\||==|!=|<=|>=|<<|>>|\+\+|--|[-+*/%&|^!~<>=?:;,.(){}\[\]#\\])
''', re.S | re.X)


def c_unescape(body):
    out, i = [], 0
    simple = {'n': '\n', 'r': '\r', 't': '\t', '0': '\0', '\\': '\\', '"': '"', "'": "'"}
    while i < len(body):
        c = body[i]
        if c == '\\' and i + 1 < len(body):
            n = body[i + 1]
            if n == 'x':
                m = re.match(r'[0-9a-fA-F]{1,2}', body[i + 2:])
                out.append(chr(int(m.group(0), 16)))
                i += 2 + len(m.group(0))
                continue
            out.append(simple.get(n, n))
            i += 2
            continue
        out.append(c)
        i += 1
    return ''.join(out)


class CFile:
    def __init__(self, name, text, config, problems):
        self.name, self.text, self.config, self.problems = name, text, config, problems
        self.lines = text.split('\n')
        self.terms = self._guards()               # line -> frozenset of guard expressions
        self.toks = self._tokens()
        self.functions = {}                       # name -> (body tokens, line)
        self._functions()

    # #if guards per line
    def _expr(self, text, line):
        text = re.sub(r'/\*.*?\*/|//.*', '', text).strip()
        text = re.sub(r'defined\s*\(\s*(\w+)\s*\)', r'\1', text)
        text = re.sub(r'defined\s+(\w+)', r'\1', text)
        parts = re.findall(r'\w+|&&|\|\||[!()]', text)
        out = []
        for p in parts:
            if p in ('&&', '||', '!', '(', ')'):
                out.append(p)
            elif p.isdigit():
                out.append('true' if int(p) else 'false')
            elif p.startswith('CONFIG_'):
                out.append(p[len('CONFIG_'):])
            elif p in self.config.macros:
                out.append('(' + self.config.macros[p] + ')')
            else:
                self.problems.append(Problem(
                    f'{rel(self.name)}:{line}', f'#if ... {p} ...',
                    f'{p} is not a Kconfig symbol (CONFIG_*) and gen_openapi.py does not know '
                    f'which builds define it, so it cannot tell which builds have the routes '
                    f'and fields under it.',
                    [f'say which Kconfig symbols {p} stands for, in Config.macros in '
                     f'tools/codegen/gen_openapi.py:\n'
                     f"    '{p}': 'SYMBOL_A || SYMBOL_B',   # where {p} is defined",
                     f'or guard the code with the CONFIG_* symbol itself']))
                out.append('false')
        return norm_expr(' '.join(out))

    def _guards(self):
        terms, stack = {}, []        # frame: [current terms(list), previous exprs(list)]
        i = 0
        while i < len(self.lines):
            line = self.lines[i]
            m = re.match(r'\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)', line)
            cur = frozenset(t for fr in stack for t in fr[0])
            terms[i + 1] = cur
            if m:
                kind, rest = m.group(1), m.group(2)
                nxt = self.lines[i + 1] if i + 1 < len(self.lines) else ''
                if kind == 'ifndef' and re.match(rf'\s*#\s*define\s+{re.escape(rest.split()[0])}\b', nxt):
                    stack.append([[], ['true']])          # include guard or a default value
                elif kind in ('ifdef', 'ifndef', 'if'):
                    e = self._expr(rest if kind == 'if' else rest.split()[0], i + 1)
                    if kind == 'ifndef':
                        e = norm_expr(f'!({e})')
                    stack.append([split_and(e), [e]])
                elif kind == 'elif' and stack:
                    e = self._expr(rest, i + 1)
                    prev = stack[-1][1]
                    stack[-1] = [[norm_expr(f'!({p})') for p in prev] + split_and(e), prev + [e]]
                elif kind == 'else' and stack:
                    prev = stack[-1][1]
                    stack[-1] = [[norm_expr(f'!({p})') for p in prev], prev]
                elif kind == 'endif' and stack:
                    stack.pop()
            i += 1
        return terms

    def _tokens(self):
        # blank preprocessor lines (and their continuations), keep the line count
        src, cont = [], False
        for line in self.lines:
            if cont or re.match(r'\s*#', line):
                cont = line.rstrip().endswith('\\')
                src.append('')
            else:
                src.append(line)
        text = '\n'.join(src)
        toks, line, pos = [], 1, 0
        while pos < len(text):
            m = TOKEN_RE.match(text, pos)
            if not m:
                raise ValueError(f'{self.name}:{line}: cannot tokenize {text[pos:pos + 20]!r}')
            kind, s = m.lastgroup, m.group(0)
            if kind == 'str':
                v = c_unescape(s[1:-1])
                if toks and toks[-1].kind == 'str':      # "a" "b"
                    toks[-1].v += v
                else:
                    toks.append(Tok('str', v, line, s))
            elif kind == 'chr':
                toks.append(Tok('chr', c_unescape(s[1:-1]), line, s))
            elif kind in ('id', 'num', 'op'):
                toks.append(Tok(kind, s, line, s))
            line += s.count('\n')
            pos = m.end()
        return toks

    def _functions(self):
        t, i, depth = self.toks, 0, 0
        while i < len(t):
            v = t[i].v
            if t[i].kind == 'op' and v in '([':
                depth += 1
            elif t[i].kind == 'op' and v in ')]':
                depth -= 1
            elif t[i].kind == 'op' and v == '{':
                if depth == 0 and i and t[i - 1].v == ')':
                    o = match_back(t, i - 1)
                    if o > 0 and t[o - 1].kind == 'id' and t[o - 1].v not in KEYWORDS:
                        close = match(t, i)
                        self.functions.setdefault(t[o - 1].v, (t[i + 1:close], t[o - 1].line))
                        i = close + 1
                        continue
                i = match(t, i) + 1        # an initializer or a struct body
                continue
            i += 1

    def table(self, name):
        """Initializer of `name[...] = { ... }`: [(line, [tokens of one entry or element])]."""
        t = self.toks
        for i in range(len(t) - 2):
            if t[i].kind == 'id' and t[i].v == name and t[i + 1].v == '[':
                j = match(t, i + 1) + 1
                if j < len(t) and t[j].v == '=' and t[j + 1].v == '{':
                    close = match(t, j + 1)
                    return t[j + 2:close]
        return None


def match(t, i):
    """Index of the bracket closing t[i]."""
    pairs = {'(': ')', '{': '}', '[': ']'}
    o, c, d = t[i].v, pairs[t[i].v], 0
    for j in range(i, len(t)):
        if t[j].kind == 'op':
            if t[j].v == o:
                d += 1
            elif t[j].v == c:
                d -= 1
                if d == 0:
                    return j
    raise ValueError(f'unbalanced {o} at line {t[i].line}')


def match_back(t, i):
    pairs = {')': '(', '}': '{', ']': '['}
    c, o, d = t[i].v, pairs[t[i].v], 0
    for j in range(i, -1, -1):
        if t[j].kind == 'op':
            if t[j].v == c:
                d += 1
            elif t[j].v == o:
                d -= 1
                if d == 0:
                    return j
    return -1


def split_top(t, sep=','):
    out, cur, d = [], [], 0
    for x in t:
        if x.kind == 'op' and x.v in '([{':
            d += 1
        elif x.kind == 'op' and x.v in ')]}':
            d -= 1
        if d == 0 and x.kind == 'op' and x.v == sep:
            out.append(cur)
            cur = []
        else:
            cur.append(x)
    if cur or out:
        out.append(cur)
    return out


def norm_expr(e):
    """One spelling per expression: 'A && !(B || C)'."""
    parts = re.findall(r'\w+|&&|\|\||[!()]', e)
    s = ''
    for p in parts:
        if p in ('&&', '||'):
            s += f' {p} '
        else:
            s += p
    s = s.strip()
    while True:     # !(X) for a single symbol -> !X ; (X) -> X
        n = re.sub(r'!\((\w+)\)', r'!\1', s)
        n = re.sub(r'^\(([^()]*)\)$', r'\1', n)
        n = re.sub(r'!!(\w+)', r'\1', n)
        if n == s:
            return s
        s = n


def split_and(e):
    """Top-level && parts of an expression (so terms compare one by one)."""
    parts, d, cur, toks = [], 0, '', re.findall(r'\w+|&&|\|\||[!()]', e)
    if '||' in [t for t in toks]:
        # split only at && outside parentheses, if there is no top-level ||
        top_or = False
        for t in toks:
            d += t == '('
            d -= t == ')'
            if d == 0 and t == '||':
                top_or = True
        if top_or:
            return [norm_expr(e)]
    d = 0
    for t in toks:
        if t == '(':
            d += 1
        elif t == ')':
            d -= 1
        if d == 0 and t == '&&':
            parts.append(cur)
            cur = ''
        else:
            cur += (' ' if cur and t not in ('(', ')', '!') and not cur.endswith(('(', '!')) else '') + t
    parts.append(cur)
    return [norm_expr(p) for p in parts if p.strip() and norm_expr(p) != 'true']


def simplify(terms):
    """Sorted terms without the ones another implies: 'A || B' goes when A is a term."""
    terms = set(terms)
    out = []
    for t in terms:
        disj = [d.strip() for d in t.split('||')] if '(' not in t and '&&' not in t else []
        if len(disj) > 1 and any(d in terms for d in disj):
            continue
        out.append(t)
    return sorted(out)


def kconfig_true(expr, on):
    """Evaluate a guard expression for a build whose Kconfig symbols `on` are set."""
    py = []
    for p in re.findall(r'\w+|&&|\|\||[!()]', expr):
        if p == '&&':
            py.append(' and ')
        elif p == '||':
            py.append(' or ')
        elif p == '!':
            py.append(' not ')
        elif p in ('(', ')'):
            py.append(p)
        elif p == 'true':
            py.append('True')
        elif p == 'false':
            py.append('False')
        else:
            py.append(repr(p in on))
    return bool(eval(''.join(py), {'__builtins__': {}}, {}))


# --- constants ----------------------------------------------------------------------------

_DEFINES = None


def defines():
    global _DEFINES
    if _DEFINES is None:
        _DEFINES = {}
        for d in CONST_DIRS:
            for p in sorted(d.rglob('*')):
                if p.suffix not in ('.h', '.c') or 'upstream' in p.parts or 'vendor' in p.parts:
                    continue
                for m in re.finditer(r'^\s*#\s*define\s+(\w+)[ \t]+([^\n]*?)\s*(?:/[*/].*)?$',
                                     p.read_text(errors='replace'), re.M):
                    _DEFINES.setdefault(m.group(1), m.group(2))
    return _DEFINES


def const_value(toks, extra=None):
    """Integer value of a constant expression of literals and #defines, or None."""
    text = ' '.join(t.raw or t.v for t in toks)
    names = dict(defines())
    names.update(extra or {})
    for _ in range(8):
        text2 = re.sub(r'\b([A-Za-z_]\w*)\b',
                       lambda m: f'({names[m.group(1)]})' if m.group(1) in names else m.group(1), text)
        if text2 == text:
            break
        text = text2
    text = re.sub(r'\b(0[xX][0-9a-fA-F]+|\d+)[uUlL]+\b', r'\1', text)
    text = re.sub(r'\((?:u?int\d+_t|unsigned|int|long)\)', '', text)
    try:
        node = ast.parse(text.strip(), mode='eval')
        for n in ast.walk(node):
            if not isinstance(n, (ast.Expression, ast.BinOp, ast.UnaryOp, ast.Constant, ast.operator,
                                  ast.unaryop)):
                return None
        v = eval(compile(node, '<const>', 'eval'), {'__builtins__': {}}, {})
        return int(v)
    except Exception:
        return None


# --- replaying a handler ------------------------------------------------------------------

@dataclass
class Piece:
    kind: str          # lit hole members
    text: str = ''
    schema: dict = None
    branches: list = None
    terms: frozenset = frozenset()
    cond: int = 0
    where: str = ''
    owner: str = ''       # the function that writes it (for rpc_http_json_ok: its caller)


@dataclass
class Ctx:
    file: CFile
    fn: str
    extra: frozenset
    cond: int
    stack: tuple


@dataclass
class Info:
    returns: bool = False
    errors: bool = False
    early: bool = False


class Reader:
    def __init__(self, sources, config):
        self.config = config
        self.problems = []
        self.files = [CFile(name, text, config, self.problems) for name, text in sources]
        self.functions = {}
        for f in self.files:
            for name, (body, line) in f.functions.items():
                self.functions.setdefault(name, (f, body, line))
        self.visited = set()      # (file name, line) of JSON writes on a route's path

    def fail(self, where, what, why, fixes):
        self.problems.append(Problem(where, what, why, fixes))

    # one route
    def walk_route(self, path, fn_name, entry_where):
        self.pieces, self.params, self.codes = [], [], set()
        if fn_name not in self.functions:
            self.fail(entry_where, f'{{ "{path}", {fn_name} }}',
                      f'the handler {fn_name}() of {path} is not defined in '
                      f'{", ".join(rel(f.name) for f in self.files)}.',
                      [f'define it in firmware/src/rpc/api_ctl.c:\n'
                       f'    static void {fn_name}(const char *const *a) {{ ... }}',
                       'or point the entry at the handler that exists'])
            return None
        f, body, line = self.functions[fn_name]
        ctx = Ctx(f, fn_name, frozenset(), 0, (fn_name,))
        self.seq(body, 0, len(body), ctx)
        return self.pieces, self.params, self.codes

    def seq(self, t, i, end, ctx):
        info, bonus = Info(), 0
        while i < end:
            c = Ctx(ctx.file, ctx.fn, ctx.extra, ctx.cond + bonus, ctx.stack)
            i, si = self.stmt(t, i, end, c)
            info.returns |= si.returns
            info.errors |= si.errors
            if si.early:
                bonus += 1
        return info

    def stmt(self, t, i, end, ctx):
        x = t[i]
        if x.v == ';':
            return i + 1, Info()
        if x.v == '{':
            j = match(t, i)
            return j + 1, self.seq(t, i + 1, j, ctx)
        if x.kind == 'id' and x.v == 'if':
            j = match(t, i + 1)
            ci = self.expr(t[i + 2:j], ctx)
            inner = Ctx(ctx.file, ctx.fn, ctx.extra, ctx.cond + 1, ctx.stack)
            k, a = self.stmt(t, j + 1, end, inner)
            b = Info()
            if k < end and t[k].v == 'else':
                k, b = self.stmt(t, k + 1, end, inner)
            checks = any(y.v in ARG_CHECKS for y in t[i + 2:j])
            early = any(br.returns and not br.errors for br in (a, b)) and not checks
            return k, Info(a.returns or b.returns, ci.errors or a.errors or b.errors, early)
        if x.kind == 'id' and x.v in ('for', 'while'):
            j = match(t, i + 1)
            self.expr(t[i + 2:j], ctx)
            return self.stmt(t, j + 1, end, ctx)
        if x.kind == 'id' and x.v == 'do':
            k, a = self.stmt(t, i + 1, end, ctx)
            j = match(t, k + 1)                    # while ( ... )
            return j + 2, a
        if x.kind == 'id' and x.v == 'switch':
            j = match(t, i + 1)
            self.expr(t[i + 2:j], ctx)
            return self.stmt(t, j + 1, end, Ctx(ctx.file, ctx.fn, ctx.extra, ctx.cond + 1, ctx.stack))
        if x.kind == 'id' and x.v in ('case', 'default'):
            while t[i].v != ':':
                i += 1
            return i + 1, Info()
        if x.kind == 'id' and i + 1 < end and t[i + 1].v == ':' and x.v not in KEYWORDS:
            return i + 2, Info()                     # label
        j, d = i, 0
        while j < end:
            if t[j].kind == 'op' and t[j].v in '([{':
                d += 1
            elif t[j].kind == 'op' and t[j].v in ')]}':
                d -= 1
            elif d == 0 and t[j].v == ';':
                break
            j += 1
        if x.kind == 'id' and x.v == 'return':
            ei = self.expr(t[i + 1:j], ctx)
            return j + 1, Info(True, ei.errors)
        if x.kind == 'id' and x.v in ('break', 'continue', 'goto'):
            return j + 1, Info()
        return j + 1, self.expr(t[i:j], ctx)

    def expr(self, t, ctx):
        info, i = Info(), 0
        while i < len(t):
            x = t[i]
            if (x.kind == 'id' and i + 1 < len(t) and t[i + 1].v == '(' and x.v not in KEYWORDS
                    and not (i and t[i - 1].v in ('.', '->'))):
                j = match(t, i + 1)
                args = split_top(t[i + 2:j])
                for a in args:
                    ai = self.expr(a, ctx)
                    info.errors |= ai.errors
                ci = self.call(x, args, ctx)
                info.errors |= ci.errors
                i = j + 1
                continue
            i += 1
        return info

    def where(self, ctx, line):
        return f'{rel(ctx.file.name)}:{line}'

    def terms(self, ctx, line):
        return ctx.file.terms.get(line, frozenset()) | ctx.extra

    def emit(self, ctx, line, **kw):
        owner = ctx.stack[-2] if ctx.fn == 'rpc_http_json_ok' and len(ctx.stack) > 1 else ctx.fn
        self.pieces.append(Piece(terms=self.terms(ctx, line), cond=ctx.cond,
                                 where=self.where(ctx, line), owner=owner, **kw))
        self.visited.add((ctx.file.name, line))

    def call(self, x, args, ctx):
        name, line = x.v, x.line
        quoted = f'{name}({", ".join(" ".join(y.raw or y.v for y in a) for a in args)})'
        if name in KEY_WRITERS:
            key = args[1] if len(args) > 2 else []
            if len(key) != 1 or key[0].kind != 'str':
                self.fail(self.where(ctx, line), quoted,
                          f'the key of {name}() is not a string literal, so the field has no name '
                          f'in the document.',
                          [f'write the key as a literal: {name}(tx, "field_name", value)'])
                return Info()
            self.emit(ctx, line, kind='lit', text=json.dumps(key[0].v) + ':')
            self.emit(ctx, line, kind='hole', schema=value_schema(KEY_WRITERS[name], args[2]))
            return Info()
        if name in VALUE_WRITERS:
            kind, _ = VALUE_WRITERS[name]
            self.emit(ctx, line, kind='hole', schema={'x-hole': kind})
            return Info()
        if name in TEXT_WRITERS:
            arg = args[TEXT_WRITERS[name]] if len(args) > TEXT_WRITERS[name] else []
            if len(arg) == 1 and arg[0].kind in ('str', 'chr'):
                self.emit(ctx, line, kind='lit', text=arg[0].v)
            elif name == 'api_stream_put':
                self.emit(ctx, line, kind='hole', schema={'x-hole': 'raw'})
            else:
                self.fail(self.where(ctx, line), quoted,
                          f'{name}() writes text that is not a literal, so the shape of the JSON '
                          f'cannot be read from the code.',
                          ['for a value with a key: rpc_tx_key_str(tx, "key", value) '
                           '(or _int, _uint, _bool)',
                           'for a string value alone (an array item): api_tx_jstr(tx, value), '
                           'or api_stream_jstr(value) in a streamed reply',
                           'for JSON punctuation: a literal, rpc_tx_raw(tx, "\\"key\\":[")'])
            return Info()
        if name in ERROR_CALLS:
            a = args[ERROR_CALLS[name]] if args else []
            if len(a) == 1 and a[0].kind == 'num':
                self.codes.add(int(a[0].v.rstrip('uUlL'), 0))
            return Info(errors=True)
        if name in ARG_CHECKS:
            self.arg_check(name, args, ctx, line, quoted)
        if name in self.config.dispatch:
            return self.dispatch(name, ctx, line)
        if name in self.functions and name not in NOT_WALKED:
            if name in ctx.stack:
                return Info()
            f, body, _ = self.functions[name]
            inner = Ctx(f, name, self.terms(ctx, line), ctx.cond, ctx.stack + (name,))
            before = len(self.codes)
            si = self.seq(body, 0, len(body), inner)
            return Info(errors=si.errors or len(self.codes) > before)
        if name not in self.functions and self.is_pointer(name, ctx):
            self.fail(self.where(ctx, line), quoted,
                      f'{name} is a function pointer, so which serializers this call runs is not '
                      f'in the code here.',
                      [f'name the table of {{"NAME", function}} it comes from, in Config.dispatch '
                       f'in tools/codegen/gen_openapi.py:\n'
                       f"    dispatch = {{..., '{name}': 'your_table'}}",
                       'or call the functions directly'])
        return Info()

    def is_pointer(self, name, ctx):
        body = self.functions.get(ctx.fn, (None, [], 0))[1]
        return any(body[k].v == name and k + 1 < len(body) and body[k + 1].v == '='
                   for k in range(len(body)))

    def dispatch(self, name, ctx, line):
        table_name = self.config.dispatch[name]
        for f in self.files:
            entries = f.table(table_name)
            if entries is None:
                continue
            branches = []
            for grp in [g for g in split_top(entries) if g]:
                inner = [y for y in grp if y.v not in ('{', '}')]
                parts = split_top(inner)
                if len(parts) != 2 or len(parts[0]) != 1 or parts[0][0].kind != 'str':
                    continue                     # { 0, 0 } terminator
                title, fn = parts[0][0].v, parts[1][0].v
                if fn not in self.functions:
                    self.fail(f'{rel(f.name)}:{parts[1][0].line}', f'{{ "{title}", {fn} }}',
                              f'{fn}() of {table_name}[] is not defined.',
                              [f'define it or remove the entry'])
                    continue
                saved = self.pieces
                self.pieces = []
                bf, body, _ = self.functions[fn]
                bterms = self.terms(ctx, line) | f.terms.get(parts[0][0].line, frozenset())
                self.seq(body, 0, len(body), Ctx(bf, fn, bterms, ctx.cond, ctx.stack + (fn,)))
                branches.append((title, bterms, self.pieces))
                self.pieces = saved
            self.emit(ctx, line, kind='members', branches=branches)
            return Info()
        self.fail(self.where(ctx, line), f'{name}(...)',
                  f'Config.dispatch says {name} comes from {table_name}[], which is not in the sources.',
                  [f'fix the table name in Config.dispatch in tools/codegen/gen_openapi.py'])
        return Info()

    def arg_check(self, name, args, ctx, line, quoted):
        where = self.where(ctx, line)
        a0 = args[0] if args else []
        if not (len(a0) == 4 and a0[1].v == '[' and a0[2].kind == 'num' and a0[3].v == ']'):
            self.fail(where, quoted,
                      f'the first argument of {name}() must be a route argument a[N], so the check '
                      f'can be matched to the {{param}} of the route.',
                      [f'{name}(a[0], ...) for the first {{param}} of the route, a[1] for the second'])
            return
        idx = int(a0[2].v.rstrip('uUlL'), 0)
        if len(args) < 2 or len(args[1]) != 1 or args[1][0].kind != 'str':
            self.fail(where, quoted, f'the name of the parameter is not a string literal.',
                      [f'{name}(a[{idx}], "param_name", ...) with the name the route uses: {{param_name}}'])
            return
        pname = args[1][0].v

        def num(k, what):
            v = const_value(args[k]) if len(args) > k else None
            if v is None:
                self.fail(where, quoted,
                          f'the {what} of {pname} is not a number or a #define of one, so the '
                          f'parameter has no range in the document.',
                          [f'use a literal or a #define NAME <number> for the {what} '
                           f'(found: {" ".join(y.raw or y.v for y in args[k]) if len(args) > k else "nothing"})'])
            return v

        schema = None
        if name == 'api_arg_str':
            schema = {'type': 'string'}
        elif name == 'api_arg_int':
            lo, hi = num(2, 'lower bound'), num(3, 'upper bound')
            schema = {'type': 'integer', 'minimum': lo, 'maximum': hi}
        elif name == 'api_arg_int_or':
            lo, hi = num(2, 'lower bound'), num(3, 'upper bound')
            word = args[4][0].v if len(args) > 4 and len(args[4]) == 1 and args[4][0].kind == 'str' else None
            if word is None:
                self.fail(where, quoted, 'the word of api_arg_int_or() is not a string literal.',
                          ['api_arg_int_or(a[i], "name", lo, hi, "word", value, &out)'])
            schema = {'anyOf': [{'type': 'integer', 'minimum': lo, 'maximum': hi},
                                {'type': 'string', 'const': word}]}
        elif name == 'api_arg_enum':
            tname = args[2][0].v if len(args) > 2 and len(args[2]) == 1 else None
            table = None
            for f in self.files:
                tt = f.table(tname) if tname else None
                if tt is not None:
                    table = [y.v for y in tt if y.kind == 'str']
                    break
            if table is None:
                self.fail(where, quoted,
                          f'the names of {pname} are not a static const char *const table in the '
                          f'sources, so the document cannot list them.',
                          [f'pass a table: static const char *const {tname or "API_NAMES"}[N] = '
                           '{ "a", "b" }; in api_ctl.c'])
                return
            n = num(3, 'table length')
            if n is not None and n != len(table):
                self.fail(where, quoted,
                          f'{tname}[] has {len(table)} names but the call says {n}.',
                          [f'pass {len(table)}u, or sizeof {tname} / sizeof {tname}[0]'])
            schema = {'anyOf': [{'type': 'string', 'enum': table},
                                {'type': 'integer', 'minimum': 0, 'maximum': len(table) - 1}]}
        elif name == 'api_arg_hex32':
            schema = {'type': 'string', 'pattern': '^(0[xX])?[0-9a-fA-F]{1,8}$'}
        elif name == 'api_arg_hexbytes':
            mx = num(3, 'byte count')
            schema = {'type': 'string', 'pattern': f'^([0-9a-fA-F]{{2}}){{1,{mx}}}$'}
        elif name == 'api_arg_notes':
            schema = {'type': 'string', 'pattern': '^[0-9]{2}(,[0-9]{2})*$'}
        self.params.append((idx, pname, schema, where))


def value_schema(kind, arg):
    lit = arg[0] if len(arg) == 1 else None
    if kind == 'string':
        s = {'type': 'string'}
        if lit is not None and lit.kind == 'str':
            s['const'] = lit.v
        return s
    if kind == 'boolean':
        s = {'type': 'boolean'}
        if lit is not None and lit.kind == 'num' and lit.v in ('0', '1'):
            s['const'] = lit.v == '1'
        return s
    s = {'type': 'integer'}
    if kind == 'uint':
        s['minimum'] = 0
    if lit is not None and lit.kind == 'num':
        s = {'type': 'integer', 'const': int(lit.v.rstrip('uUlL'), 0)}
    return s


# --- the written JSON -> a schema ---------------------------------------------------------

class Shape:
    """Reads the pieces a handler writes as JSON with holes."""

    def __init__(self, pieces, route_terms, reader, route):
        self.reader, self.route = reader, route
        self.route_terms = route_terms
        self.toks = self.lex(pieces)
        self.i = 0

    def lex(self, pieces):
        chars = []
        for p in pieces:
            if p.kind == 'lit':
                chars += [(c, p) for c in p.text]
            else:
                chars.append((None, p))
        toks, i = [], 0
        while i < len(chars):
            c, p = chars[i]
            if c is None:
                toks.append(('members' if p.kind == 'members' else 'hole', p, p))
                i += 1
            elif c.isspace():
                i += 1
            elif c in '{}[]:,':
                toks.append((c, None, p))
                i += 1
            elif c == '"':
                parts, j = [], i + 1
                while j < len(chars) and chars[j][0] != '"':
                    cj, pj = chars[j]
                    if cj == '\\' and j + 1 < len(chars):
                        parts.append(chars[j + 1][0] or '')
                        j += 2
                        continue
                    parts.append(cj if cj is not None else pj)
                    j += 1
                toks.append(('str', parts, p))
                i = j + 1
            elif c == '-' or c.isdigit():
                j = i + 1
                while j < len(chars) and chars[j][0] is not None and chars[j][0].isdigit():
                    j += 1
                toks.append(('num', ''.join(ch for ch, _ in chars[i:j]), p))
                i = j
            elif c.isalpha():
                j = i
                while j < len(chars) and chars[j][0] is not None and chars[j][0].isalpha():
                    j += 1
                toks.append(('word', ''.join(ch for ch, _ in chars[i:j]), p))
                i = j
            else:
                self.bad(p, f'{c!r} is not JSON')
                i += 1
        return toks

    def bad(self, piece, why):
        self.reader.fail(piece.where if piece else self.route, 'the JSON reply of ' + self.route,
                         f'what the handler writes does not read as JSON here: {why}.',
                         ['check the literals around this line: keys as "\\"key\\":", '
                          'objects and arrays opened and closed in the same function or helper'])

    def peek(self):
        return self.toks[self.i] if self.i < len(self.toks) else (None, None, None)

    def take(self):
        t = self.peek()
        self.i += 1
        return t

    def values(self):
        out = []
        while self.i < len(self.toks):
            kind, _, p = self.peek()
            if kind == ',':
                self.i += 1
                continue
            v = self.value(self.route_terms, 0)
            if v is not None:
                out.append(v)
        return out

    def value(self, outer, base_cond):
        kind, val, p = self.take()
        if kind == '{':
            return self.obj(p)
        if kind == '[':
            items, first = None, True
            while True:
                k, _, q = self.peek()
                if k is None:
                    self.bad(p, 'an array is not closed')
                    break
                if k == ']':
                    self.i += 1
                    break
                if k == ',':
                    self.i += 1
                    continue
                v = self.value(outer, base_cond)
                if first and v is not None:
                    items, first = v[0], False
            return ({'type': 'array', 'items': items or {}}, p)
        if kind == 'str':
            text = all(isinstance(x, str) for x in val)
            if text:
                return ({'type': 'string', 'const': ''.join(val)}, p)
            s = {'type': 'string'}
            if any(not isinstance(x, str) and x.schema.get('x-hole') == 'b64' for x in val):
                s['contentEncoding'] = 'base64'
            return (s, p)
        if kind == 'num':
            return ({'type': 'integer', 'const': int(val)}, p)
        if kind == 'word':
            if val in ('true', 'false'):
                return ({'type': 'boolean', 'const': val == 'true'}, p)
            return ({'type': 'null'}, p)
        if kind == 'hole':
            return (hole_schema(val.schema), p)
        self.bad(p, f'a value was expected, found {kind or "the end"}')
        return None

    def obj(self, open_piece):
        props, required, branches = {}, [], []
        base_terms, base_cond = open_piece.terms, open_piece.cond
        while True:
            kind, val, p = self.peek()
            if kind is None:
                self.bad(open_piece, 'an object is not closed')
                break
            if kind == '}':
                self.i += 1
                break
            if kind == ',':
                self.i += 1
                continue
            if kind == 'members':
                self.i += 1
                branches = val.branches
                continue
            if kind != 'str' or not all(isinstance(x, str) for x in val):
                self.bad(p, 'a key was expected')
                self.i += 1
                continue
            self.i += 1
            key = ''.join(val)
            if self.peek()[0] != ':':
                self.bad(p, f'no ":" after "{key}"')
                continue
            self.i += 1
            v = self.value(base_terms, base_cond)
            if v is None:
                continue
            schema = dict(v[0])
            extra = simplify(p.terms - base_terms)
            if extra:
                schema['x-kconfig'] = extra
            if key not in props:
                props[key] = schema
                if p.cond == base_cond:
                    required.append(key)
        out = {'type': 'object', 'properties': props}
        if required:
            out['required'] = required
        if open_piece.owner:
            out['x-owner'] = open_piece.owner
        if branches:
            alts = []
            for title, bterms, pieces in branches:
                sub = Shape([Piece('lit', '{', terms=bterms, cond=open_piece.cond, where=open_piece.where)]
                            + pieces + [Piece('lit', '}', terms=bterms, where=open_piece.where)],
                            bterms, self.reader, self.route)
                vals = sub.values()
                b = vals[0][0] if vals else {'type': 'object'}
                b = {'title': title, **b}
                b = merge_props(out, b)
                extra = simplify(bterms - base_terms)
                if extra:
                    b['x-kconfig-branch'] = extra
                alts.append(b)
            return ({'oneOf': alts}, open_piece)
        return (out, open_piece)


def merge_props(common, branch):
    if not common.get('properties'):
        return branch
    b = copy.deepcopy(branch)
    b.setdefault('properties', {})
    b['properties'] = {**common['properties'], **b['properties']}
    req = list(common.get('required', [])) + [r for r in b.get('required', []) if r not in common.get('required', [])]
    if req:
        b['required'] = req
    return b


def hole_schema(s):
    kind = s.get('x-hole')
    if kind is None:
        return s
    return {'integer': {'type': 'integer'}, 'uint': {'type': 'integer', 'minimum': 0},
            'qstring': {'type': 'string'}, 'raw': {}, 'b64': {'type': 'string', 'contentEncoding': 'base64'}
            }[kind]


# --- the document -------------------------------------------------------------------------

ERROR_SCHEMA = {'type': 'object', 'required': ['error'], 'properties': {'error': {'type': 'string'}}}
ERROR_RESPONSES = {
    400: ('BadRequest', 'A parameter is out of range or malformed, or the request cannot be done now'),
    404: ('NotFound', 'A name (page, button, track, engine) names nothing, or no such route'),
}


def ordinal(n):
    return f'{n}' + ('th' if 10 <= n % 100 <= 20 else {1: 'st', 2: 'nd', 3: 'rd'}.get(n % 10, 'th'))


def route_template_params(path):
    return re.findall(r'\{(\w+)\}', path)


def operation_id(path):
    parts = [p for p in path.strip('/').replace('.', '/').split('/') if p and p != 'api']
    words = []
    for p in parts:
        m = re.fullmatch(r'\{(\w+)\}', p)
        words.append(m.group(1) if m else p)
    words = [w for k, w in enumerate(words) if k == 0 or w != words[k - 1]]
    return words[0] + ''.join(w[:1].upper() + w[1:] for w in words[1:]) if words else 'root'


def read_sources(paths=None):
    return [(str(p), Path(p).read_text()) for p in (paths or SOURCES)]


def generate(sources=None, overlay=None, config=None):
    """The document for every build: guards as x-kconfig. Raises GenError."""
    config = config or Config()
    reader = Reader(sources if sources is not None else read_sources(), config)
    problems = reader.problems

    for f in reader.files:
        for t in f.toks:
            if t.kind == 'id' and t.v == 'rpc_path_match':
                problems.append(Problem(
                    f'{rel(f.name)}:{t.line}', 'rpc_path_match(...)',
                    'routes are matched in one place, API_ROUTES[] in api_ctl.c; a path matched '
                    'anywhere else is not in the document.',
                    ['add the route to API_ROUTES[] in firmware/src/rpc/api_ctl.c:\n'
                     '    { "/api/your/path/{param}", api_your_handler },\n'
                     'and check {param} in the handler with api_arg_*(a[0], "param", ...)']))

    routes = []
    for f in reader.files:
        t = f.table(ROUTES_TABLE)
        if t is None:
            continue
        for grp in [g for g in split_top(t) if g]:
            inner = [y for y in grp if y.v not in ('{', '}')]
            parts = split_top(inner)
            if len(parts) == 2 and len(parts[0]) == 1 and parts[0][0].kind == 'str' and len(parts[1]) == 1:
                routes.append((parts[0][0].v, parts[1][0].v, f, parts[0][0].line))
    if not routes:
        problems.append(Problem(rel(SOURCES[0]), f'{ROUTES_TABLE}[]',
                                f'no {ROUTES_TABLE}[] = {{ {{ "path", handler }}, ... }} table found.',
                                [f'keep the routes in {ROUTES_TABLE}[] in firmware/src/rpc/api_ctl.c']))
        raise GenError(problems)

    paths = {}
    for path, fn, f, line in routes:
        where = f'{rel(f.name)}:{line}'
        if path in paths:
            problems.append(Problem(where, f'{{ "{path}", {fn} }}', f'{path} is in {ROUTES_TABLE}[] twice.',
                                    ['remove one of the entries']))
            continue
        res = reader.walk_route(path, fn, where)
        if res is None:
            continue
        pieces, params, codes = res
        route_terms = f.terms.get(line, frozenset())
        tparams = route_template_params(path)
        parameters = []
        for k, name in enumerate(tparams):
            found = [p for p in params if p[0] == k]
            if not found:
                problems.append(Problem(
                    where, f'{{ "{path}", {fn} }}',
                    f'{{{name}}} is the {ordinal(k + 1)} parameter of {path}, but {fn}() (and the helpers it '
                    f'calls) never checks a[{k}], so the document cannot say what it takes.',
                    [f'take it in {fn}(), for any text:\n'
                     f'    const char *{name} = api_arg_str(a[{k}], "{name}");',
                     f'or check its range or names, and return on a bad value:\n'
                     f'    if (!api_arg_int(a[{k}], "{name}", 0, 100, &v)) return;\n'
                     f'(also api_arg_enum, api_arg_int_or, api_arg_hex32, api_arg_hexbytes, '
                     f'api_arg_notes: see api_ctl.c)']))
                continue
            idx, pname, schema, pwhere = found[0]
            if pname != name:
                problems.append(Problem(
                    pwhere, f'a[{k}] checked as "{pname}"',
                    f'{path} calls its {ordinal(k + 1)} parameter {{{name}}}, the check calls it "{pname}".',
                    [f'use one name: "{name}" in the api_arg_*() call, or {{{pname}}} in the route']))
            parameters.append({'name': name, 'in': 'path', 'required': True, 'schema': schema or {}})
        shape = Shape(pieces, route_terms, reader, where)
        vals = shape.values()
        if not vals:
            problems.append(Problem(where, f'{fn}()', f'{fn}() writes no JSON reply.',
                                    ['write the reply with rpc_http_json_ok(tx) ... rpc_http_json_end(tx), '
                                     'or api_stream_begin() ... api_stream_end()']))
            continue
        alts = []
        for schema, p in vals:
            extra = simplify(p.terms - route_terms)
            s = dict(schema)
            if extra:
                s['x-kconfig'] = extra
            alts.append(s)
        body = alts[0] if len(alts) == 1 else {'oneOf': alts}
        op = {'operationId': operation_id(path),
              'responses': {'200': {'description': 'OK',
                                    'content': {'application/json': {'schema': body}}}}}
        if parameters:
            op['parameters'] = parameters
        for code in sorted(codes):
            if code in ERROR_RESPONSES:
                op['responses'][str(code)] = {'$ref': f'#/components/responses/{ERROR_RESPONSES[code][0]}'}
        item = {'get': op}
        if route_terms:
            item['x-kconfig'] = simplify(route_terms)
        paths[path] = item

    # JSON writes that no route reaches
    for f in reader.files:
        if Path(f.name).name not in REST_ONLY:
            continue
        for fname, (body, line) in f.functions.items():
            if fname in NOT_WALKED or fname in config.ignore:
                continue
            for k, t in enumerate(body):
                if (t.kind == 'id' and t.v in WRITERS and k + 1 < len(body) and body[k + 1].v == '('
                        and (f.name, t.line) not in reader.visited):
                    problems.append(Problem(
                        f'{rel(f.name)}:{t.line}', f'{t.v}(...)',
                        f'it is in {fname}(), which no route reaches (no API_ROUTES[] handler calls '
                        f'it, directly or through a helper), so its JSON is in no reply.',
                        [f'call {fname}() from the handler of the route that returns it',
                         f'if {fname}() is a handler, add its route to API_ROUTES[] in api_ctl.c:\n'
                         f'    {{ "/api/...", {fname} }},',
                         f'if it writes JSON for something other than the REST API, list it in '
                         f'Config.ignore in tools/codegen/gen_openapi.py:\n'
                         f"    ignore = {{..., '{fname}'}}"]))
                    break

    if problems:
        raise GenError(problems)

    doc = {'openapi': '3.1.0', 'info': {'title': 'modd firmware REST API', 'version': 'dev'},
           'paths': paths,
           'components': {'schemas': {'Error': ERROR_SCHEMA},
                          'responses': {name: {'description': desc, 'content': {'application/json': {
                              'schema': {'$ref': '#/components/schemas/Error'}}}}
                                        for name, desc in ERROR_RESPONSES.values()}}}
    titled_components(doc)
    apply_overlay(doc, overlay if overlay is not None else load_overlay(), problems)
    if problems:
        raise GenError(problems)
    shared_replies(doc)
    return strip_key(doc, 'x-owner')


def component_name(fn):
    words = [w for w in fn.split('_') if w and w not in ('api', 'json')]
    return ''.join(w[:1].upper() + w[1:] for w in words) or fn


def shared_replies(doc):
    """A reply schema several routes share (same writer, same shape) -> one component."""
    groups = {}
    for path, item in doc['paths'].items():
        content = item['get']['responses']['200']['content']['application/json']
        s = content['schema']
        owner = s.get('x-owner') if isinstance(s, dict) else None
        if owner:
            key = (owner, json.dumps(strip_key(s, 'x-owner'), sort_keys=True))
            groups.setdefault(key, []).append(content)
    schemas = doc['components']['schemas']
    for (owner, _), contents in groups.items():
        if len(contents) < 2:
            continue
        name = component_name(owner)
        if name in schemas:
            name += 'Reply'
        schemas[name] = strip_key(contents[0]['schema'], 'x-owner')
        for c in contents:
            c['schema'] = {'$ref': f'#/components/schemas/{name}'}


def strip_key(node, key):
    if isinstance(node, dict):
        return {k: strip_key(v, key) for k, v in node.items() if k != key}
    if isinstance(node, list):
        return [strip_key(v, key) for v in node]
    return node


def titled_components(doc):
    """Page data (one schema per page name) -> components/schemas/<NAME>, referenced."""
    schemas = doc['components']['schemas']

    def visit(node):
        if isinstance(node, dict):
            for k, v in list(node.items()):
                node[k] = visit(v)
            if 'title' in node and node.get('type') == 'object':
                title = node.pop('title')
                marker = node.pop('x-kconfig-branch', None)
                comp = dict(node)
                schemas.setdefault(title, {'title': title, **comp})
                ref = {'$ref': f'#/components/schemas/{title}'}
                if marker:
                    ref['x-kconfig'] = marker
                    schemas[title].setdefault('x-kconfig', marker)
                return ref
            return node
        if isinstance(node, list):
            return [visit(v) for v in node]
        return node

    for item in doc['paths'].values():
        visit(item)


# --- the overlay --------------------------------------------------------------------------

def load_overlay(path=OVERLAY):
    text = Path(path).read_text()
    return {'data': yaml.safe_load(text) or {}, 'lines': yaml_lines(text), 'name': rel(path)}


def yaml_lines(text):
    """(key, key, ...) -> line of that key in the YAML text."""
    out = {}

    def walk(node, path):
        if isinstance(node, yaml.MappingNode):
            for k, v in node.value:
                p = path + (k.value,)
                out[p] = k.start_mark.line + 1
                walk(v, p)

    walk(yaml.compose(text), ())
    return out


def find_field(schema, dotted, doc):
    """The property schemas a dotted path (a.b[].c) names, through $ref and oneOf."""
    found = []

    def deref(s):
        while isinstance(s, dict) and '$ref' in s:
            node = doc
            for part in s['$ref'].lstrip('#/').split('/'):
                node = node[part]
            s = node
        return s

    def walk(s, parts):
        s = deref(s)
        if not parts:
            found.append(s)
            return
        if 'oneOf' in s:
            for alt in s['oneOf']:
                walk(alt, parts)
            return
        part = parts[0]
        arr = part.endswith('[]')
        key = part[:-2] if arr else part
        prop = s.get('properties', {}).get(key)
        if prop is None:
            return
        if arr:
            prop = deref(prop)
            if prop.get('type') != 'array':
                return
            walk(prop['items'], parts[1:])
        else:
            walk(prop, parts[1:])

    walk(schema, dotted.split('.'))
    return found


def apply_overlay(doc, overlay, problems):
    data, lines, name = overlay['data'], overlay['lines'], overlay['name']

    def stale(keys, what, why, choices):
        line = lines.get(tuple(keys), '?')
        close = difflib.get_close_matches(keys[-1], choices, n=3)
        fixes = [f'delete "{keys[-1]}" (line {line}) and what is under it']
        if close:
            fixes.insert(0, f'it may have been renamed: rename the key to one of {", ".join(close)}')
        problems.append(Problem(f'{name}:{line}', ': '.join(keys), why, fixes))

    for k, v in (data.get('document') or {}).items():
        if k in ('paths', 'components', 'openapi'):
            continue
        doc[k] = copy.deepcopy(v) if k != 'info' else {**doc['info'], **v}

    for path, spec in (data.get('paths') or {}).items():
        if path not in doc['paths']:
            stale(['paths', path], 'path',
                  f'the overlay describes {path}, but no route in API_ROUTES[] has that path.',
                  list(doc['paths']))
            continue
        op = doc['paths'][path]['get']
        for k in ('summary', 'description', 'tags', 'operationId'):
            if k in (spec or {}):
                op[k] = spec[k]
        params = {p['name']: p for p in op.get('parameters', [])}
        for pname, pv in ((spec or {}).get('parameters') or {}).items():
            if pname not in params:
                stale(['paths', path, 'parameters', pname], 'parameter',
                      f'{path} has no parameter {{{pname}}}.', list(params))
                continue
            set_doc(params[pname], pv)
        resp = op['responses']['200']['content']['application/json']['schema']
        for dotted, fv in ((spec or {}).get('fields') or {}).items():
            hits = find_field(resp, dotted, doc)
            if not hits:
                stale(['paths', path, 'fields', dotted], 'field',
                      f'the reply of {path} has no field {dotted}.', field_names(resp, doc))
                continue
            for h in hits:
                set_doc(h, fv)
        if 'schema' in (spec or {}):
            set_doc(resp, spec['schema'])
        if 'response' in (spec or {}):
            set_doc(op['responses']['200'], {'description': spec['response']})

    schemas = doc['components']['schemas']
    for title, fields_ in (data.get('pages') or {}).items():
        if title not in schemas:
            stale(['pages', title], 'page',
                  f'no page writes data called {title} (the names in api_page_json_table[]).',
                  [k for k in schemas if k != 'Error'])
            continue
        for dotted, fv in (fields_ or {}).items():
            hits = find_field(schemas[title], dotted, doc)
            if not hits:
                stale(['pages', title, dotted], 'field', f'the data of page {title} has no field {dotted}.',
                      field_names(schemas[title], doc))
                continue
            for h in hits:
                set_doc(h, fv)


def field_names(schema, doc, prefix=''):
    out = []
    s = schema
    while isinstance(s, dict) and '$ref' in s:
        node = doc
        for part in s['$ref'].lstrip('#/').split('/'):
            node = node[part]
        s = node
    for alt in s.get('oneOf', [s]):
        if alt is not s:
            out += field_names(alt, doc, prefix)
            continue
        for k, v in s.get('properties', {}).items():
            out.append(prefix + k)
            if v.get('type') == 'array' and isinstance(v.get('items'), dict):
                out += field_names(v['items'], doc, prefix + k + '[].')
    return out


def set_doc(target, value):
    if isinstance(value, str):
        target['description'] = value
    elif isinstance(value, dict):
        target.update(copy.deepcopy(value))


# --- per-build filter ---------------------------------------------------------------------

def config_symbols(config_h):
    """CONFIG_X 1 -> {'X'}; other values -> {'X': value}."""
    on, values = set(), {}
    for name, val in re.findall(r'^#define CONFIG_(\w+) (.+)$', Path(config_h).read_text(), re.M):
        if val == '1':
            on.add(name)
        values[name] = val.strip().strip('"')
    return on, values


def _keep(node, on):
    return all(kconfig_true(e, on) for e in node.get('x-kconfig', []))


def _filter(node, on):
    if isinstance(node, dict):
        out = {}
        for k, v in node.items():
            if k == 'x-kconfig':
                continue
            if isinstance(v, dict) and not _keep(v, on):
                continue
            out[k] = _filter(v, on)
        return out
    if isinstance(node, list):
        return [_filter(v, on) for v in node if not (isinstance(v, dict) and not _keep(v, on))]
    return node


def _resolve(doc, ref):
    node = doc
    for part in ref.lstrip('#/').split('/'):
        if not isinstance(node, dict) or part not in node:
            return None
        node = node[part]
    return node


def _prune(doc, node):
    changed = False

    def dangling(v):
        return isinstance(v, dict) and '$ref' in v and _resolve(doc, v['$ref']) is None

    if isinstance(node, dict):
        out = {}
        for k, v in node.items():
            if dangling(v):
                changed = True
                continue
            v, c = _prune(doc, v)
            changed |= c
            if k in ('oneOf', 'anyOf', 'allOf') and isinstance(v, list) and not v:
                changed = True
                continue
            out[k] = v
        if isinstance(out.get('required'), list) and isinstance(out.get('properties'), dict):
            req = [r for r in out['required'] if r in out['properties']]
            if req != out['required']:
                changed = True
                out['required'] = req
            if not req:
                del out['required']
        if set(out) == {'oneOf'} and len(out['oneOf']) == 1:
            return out['oneOf'][0], True
        return out, changed
    if isinstance(node, list):
        out = []
        for v in node:
            if dangling(v):
                changed = True
                continue
            v, c = _prune(doc, v)
            changed |= c
            out.append(v)
        return out, changed
    return node, changed


def for_build(full, on, values, version):
    doc = _filter(copy.deepcopy(full), on)
    while True:
        doc, changed = _prune(doc, doc)
        if not changed:
            break
    drop_unused_components(doc)
    doc['info']['version'] = version
    if 'RPC_PORT' in values:
        for server in doc.get('servers', []):
            port = server.get('variables', {}).get('port')
            if port is not None:
                port['default'] = values['RPC_PORT']
    return doc


def drop_unused_components(doc):
    """Components no route reaches any more (their routes were filtered out)."""
    while True:
        text = json.dumps(doc['paths']) + json.dumps(doc['components'])
        dropped = False
        for kind in ('schemas', 'responses'):
            for name in list(doc['components'].get(kind, {})):
                ref = f'"#/components/{kind}/{name}"'
                rest = text.replace(json.dumps(doc['components'][kind][name]), '')
                if ref not in rest:
                    del doc['components'][kind][name]
                    dropped = True
        if not dropped:
            return


def c_header(text):
    lines = ['/* Generated by tools/codegen/gen_openapi.py from firmware/src/rpc (api_ctl.c,',
             ' * api_pages.c) and docs/api/openapi.overlay.yaml. Do not edit. */',
             '#pragma once', '',
             f'#define MODD_OPENAPI_JSON_LEN {len(text.encode())}u', '',
             'static const char modd_openapi_json[] =']
    for i in range(0, len(text), 96):
        chunk = text[i:i + 96].replace('\\', '\\\\').replace('"', '\\"')
        lines.append(f'    "{chunk}"')
    lines[-1] += ';'
    return '\n'.join(lines) + '\n'


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--config', help='build/<defconfig>/gen/config.h')
    ap.add_argument('--version', default='dev')
    ap.add_argument('--json', help='write the build\'s document here')
    ap.add_argument('--header', help='write it as a C string header here')
    ap.add_argument('--full', help='write the document for every build (guards kept) here')
    ap.add_argument('--check', action='store_true', help='only read and report problems')
    a = ap.parse_args(argv)

    try:
        full = generate()
    except GenError as e:
        print(f'{e}\n\ngen_openapi: {len(e.problems)} problem(s); the OpenAPI document was not written.',
              file=sys.stderr)
        return 1
    if a.full:
        Path(a.full).write_text(json.dumps(full, indent=2) + '\n')
    if a.check or not a.config:
        print(f'gen_openapi: {len(full["paths"])} routes, no problems')
        return 0
    on, values = config_symbols(a.config)
    doc = for_build(full, on, values, a.version)
    text = json.dumps(doc, separators=(',', ':'), ensure_ascii=True)
    if a.json:
        Path(a.json).write_text(text + '\n')
    if a.header:
        Path(a.header).write_text(c_header(text))
    print(f'gen_openapi: {len(doc["paths"])} paths, {len(text)} bytes'
          + (f' -> {a.json}' if a.json else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main())
