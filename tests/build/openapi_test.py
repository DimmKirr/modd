#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""tools/codegen/gen_openapi.py: the OpenAPI document generated from the C.

Two parts:
  - the real sources (firmware/src/rpc) and overlay: the document has the routes,
    parameters, fields and guards the C says, and the per-build filter keeps the
    right ones;
  - one small C fixture per kind of problem: the generator stops, its message
    carries a fix, and applying that fix as written makes it pass.
"""
import json
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'codegen'))
import gen_openapi as g  # noqa: E402

FULL = g.generate()
ALL_ON = {'RPC', 'RPC_OPENAPI', 'RPC_INPUT', 'RPC_MEM', 'RPC_AUDIO_TAP', 'AUDIO', 'DEBUG_LOG_BUF',
          'DEBUG_FRAMEBUFFER', 'UI_SHELL_MODD', 'SYS_MENU', 'SYS_MENU_NETWORK', 'LWIP', 'WIFI', 'FX_HOST', 'ARENA_MELODEE'}
RELEASE = {'RPC', 'RPC_OPENAPI', 'AUDIO', 'UI_SHELL_MODD', 'SYS_MENU', 'SYS_MENU_NETWORK', 'LWIP',
           'DEBUG_LOG_BUF'}


def schema_200(doc, path):
    s = doc['paths'][path]['get']['responses']['200']['content']['application/json']['schema']
    while '$ref' in s:
        s = g._resolve(doc, s['$ref'])
    return s


def build(on, values=None):
    return g.for_build(FULL, set(on), values or {}, 'test')


def routes_in_c():
    text = (ROOT / 'firmware' / 'src' / 'rpc' / 'api_ctl.c').read_text()
    table = text[text.index('API_ROUTES[] = {'):]
    return re.findall(r'\{\s*"([^"]+)",\s*\w+\s*\}', table[:table.index('};')])


class TestRealSources(unittest.TestCase):

    def test_every_route_in_the_table(self):
        self.assertEqual(sorted(FULL['paths']), sorted(routes_in_c()))

    def test_guards_of_routes(self):
        p = FULL['paths']
        self.assertEqual(p['/api/input'].get('x-kconfig'), ['RPC_INPUT'])
        self.assertEqual(p['/api/track/{track}'].get('x-kconfig'), ['AUDIO', 'RPC_INPUT'])
        self.assertEqual(p['/api/mem/find/{hex}'].get('x-kconfig'), ['RPC_MEM'])
        self.assertEqual(p['/openapi.json'].get('x-kconfig'), ['RPC_OPENAPI'])
        self.assertNotIn('x-kconfig', p['/api/info'])

    def test_fields_and_their_guards(self):
        audio = schema_200(FULL, '/api/audio')['properties']
        self.assertEqual(audio['rms']['x-kconfig'], ['AUDIO', 'RPC_AUDIO_TAP'])
        self.assertEqual(audio['error']['x-kconfig'], ['!AUDIO'])
        self.assertEqual(audio['sample_rate']['const'], 44100)
        info = schema_200(FULL, '/api/info')
        self.assertEqual(info['properties']['firmware'], {'type': 'string', 'const': 'modd'})
        self.assertEqual(info['required'], ['firmware', 'version', 'uptime_ms', 'ip'])
        shot = schema_200(FULL, '/api/screenshot')['properties']
        self.assertEqual(shot['data']['contentEncoding'], 'base64')

    def test_parameters_from_the_checks(self):
        def params(path):
            return {p['name']: p['schema'] for p in FULL['paths'][path]['get']['parameters']}
        tap = params('/api/input/button/{button}/tap/{ms}')
        self.assertEqual(tap['ms'], {'type': 'integer', 'minimum': 10, 'maximum': 60000,
                                     'description': 'Milliseconds of firmware time until the release'}
                         if 'description' in tap['ms'] else {'type': 'integer', 'minimum': 10, 'maximum': 60000})
        self.assertIn('play', tap['button']['anyOf'][0]['enum'])
        self.assertEqual(tap['button']['anyOf'][1]['maximum'], 13)
        track = params('/api/track/{track}')['track']['anyOf']
        self.assertEqual(track[0]['maximum'], 3)                       # NTRK - 1, from the #defines
        self.assertEqual(track[1]['const'], 'sel')
        self.assertIn('{1,32}', params('/api/mem/find/{hex}')['hex']['pattern'])
        enc = params('/api/input/enc/{encoder}/{clicks}')
        self.assertEqual((enc['clicks']['minimum'], enc['clicks']['maximum']), (-1000, 1000))

    def test_optional_fields(self):
        wx = FULL['components']['schemas']['WX']
        self.assertEqual(wx['required'], ['track', 'engine', 'title', 'valid', 'fetching'])
        self.assertNotIn('sub', schema_200(FULL, '/modd/{page}/{sub}').get('required', []))
        menu = FULL['components']['schemas']['MENU']
        self.assertEqual(menu['required'], ['track', 'engine', 'title', 'tab'])

    def test_error_responses(self):
        r = FULL['paths']['/api/input/button/{button}/tap/{ms}']['get']['responses']
        self.assertEqual(sorted(r), ['200', '400', '404'])
        self.assertEqual(sorted(FULL['paths']['/api/info']['get']['responses']), ['200'])

    def test_overlay_applied(self):
        self.assertEqual(FULL['paths']['/api/info']['get']['operationId'], 'getInfo')
        self.assertIn('summary', FULL['paths']['/api/input']['get'])
        self.assertIn('Tenths', FULL['components']['schemas']['WX']['properties']['temp_c']['description'])

    def test_everything_on(self):
        doc = build(ALL_ON, {'RPC_PORT': '9300'})
        self.assertEqual(len(doc['paths']), len(FULL['paths']))
        self.assertEqual(doc['servers'][0]['variables']['port']['default'], '9300')
        self.assertNotIn('x-kconfig', json.dumps(doc))
        self.assertNotIn('error', schema_200(doc, '/api/audio')['properties'])

    def test_release_build(self):
        doc = build(RELEASE)
        for path in doc['paths']:
            self.assertFalse(path.startswith(('/api/input', '/api/track', '/api/mem', '/api/audio/capture',
                                              '/api/screenshot')), path)
        self.assertNotIn('rms', schema_200(doc, '/api/audio')['properties'])
        self.assertNotIn('InputOk', doc['components']['schemas'])
        text = json.dumps(doc)
        for ref in re.findall(r'"\$ref": "([^"]+)"', text):
            self.assertIsNotNone(g._resolve(doc, ref), ref)

    def test_header_is_the_json(self):
        text = json.dumps(build(ALL_ON), separators=(',', ':'), ensure_ascii=True)
        header = g.c_header(text)
        body = ''.join(re.findall(r'^    "(.*)";?$', header, re.M))
        self.assertEqual(body.replace('\\"', '"').replace('\\\\', '\\'), text)
        self.assertIn(f'MODD_OPENAPI_JSON_LEN {len(text)}u', header)
        self.assertNotIn('/*', header.split('*/', 1)[1])


# --- problems ----------------------------------------------------------------------------

SERVER = '''
static void rpc_http_json_ok(struct rpc_tx *tx) { rpc_tx_init(tx); rpc_tx_char(tx, '{'); }
static void rpc_http_json_end(struct rpc_tx *tx) { rpc_tx_char(tx, '}'); rpc_http_respond(tx, 200); }
'''

HANDLER = '''
static void api_info(const char *const *a)
{
    struct rpc_tx *tx = &rpc_tx_static;
    rpc_http_json_ok(tx);
    rpc_tx_key_uint(tx, "uptime_ms", fm1_ms);
    rpc_http_json_end(tx);
}
'''


def routes(*entries):
    rows = '\n'.join(entries)
    return ('static const struct { const char *path; void (*fn)(const char *const *a); } '
            f'API_ROUTES[] = {{\n{rows}\n}};\n')


EMPTY = {'data': {}, 'lines': {}, 'name': 'overlay.yaml'}


def overlay(text):
    import yaml
    return {'data': yaml.safe_load(text), 'lines': g.yaml_lines(text), 'name': 'overlay.yaml'}


def run(api_ctl, api_pages='', ov=EMPTY, config=None):
    sources = [('fx/api_ctl.c', api_ctl), ('fx/api_pages.c', api_pages), ('fx/rpc_server.c', SERVER)]
    return g.generate(sources, ov, config or g.Config())


class TestProblems(unittest.TestCase):
    """Each problem: the message names the place, the reason and a fix; the fix as written works."""

    def fails(self, *args, **kw):
        with self.assertRaises(g.GenError) as cm:
            run(*args, **kw)
        text = str(cm.exception)
        self.assertIn('Rules for handlers: docs/TESTING.md#writing-a-rest-handler', text)
        self.assertRegex(text, r'gen_openapi: \S+:\d+: ')
        return text

    def test_baseline_passes(self):
        doc = run(HANDLER + routes('{ "/api/info", api_info },'))
        self.assertEqual(list(doc['paths']), ['/api/info'])

    def test_unknown_macro(self):
        src = '#ifdef HAVE_THING\n' + HANDLER + '#endif\n' + routes('{ "/api/info", api_info },')
        msg = self.fails(src)
        self.assertIn("'HAVE_THING': 'SYMBOL_A || SYMBOL_B'", msg)
        cfg = g.Config()
        cfg.macros['HAVE_THING'] = 'AUDIO'                 # the fix, with the symbol filled in
        doc = run(src, config=cfg)
        self.assertEqual(doc['paths']['/api/info']['get']['responses']['200']['content']
                         ['application/json']['schema']['x-kconfig'], ['AUDIO'])

    def test_unchecked_parameter(self):
        src = HANDLER + routes('{ "/api/info/{name}", api_info },')
        msg = self.fails(src)
        fix = 'const char *name = api_arg_str(a[0], "name");'
        self.assertIn(fix, msg)
        fixed = src.replace('struct rpc_tx *tx = &rpc_tx_static;',
                            'struct rpc_tx *tx = &rpc_tx_static;\n    ' + fix)
        doc = run(fixed)
        self.assertEqual(doc['paths']['/api/info/{name}']['get']['parameters'][0]['name'], 'name')

    def test_parameter_name_mismatch(self):
        src = (HANDLER.replace('rpc_http_json_ok(tx);', 'api_arg_str(a[0], "nam");\n    rpc_http_json_ok(tx);')
               + routes('{ "/api/info/{name}", api_info },'))
        msg = self.fails(src)
        self.assertIn('use one name: "name" in the api_arg_*() call', msg)
        run(src.replace('"nam"', '"name"'))

    def test_non_literal_text(self):
        src = HANDLER.replace('rpc_tx_key_uint(tx, "uptime_ms", fm1_ms);',
                              'rpc_tx_raw(tx, name);') + routes('{ "/api/info", api_info },')
        msg = self.fails(src)
        self.assertIn('rpc_tx_key_str(tx, "key", value)', msg)
        doc = run(src.replace('rpc_tx_raw(tx, name);', 'rpc_tx_key_str(tx, "key", name);'))
        self.assertIn('key', json.dumps(doc))

    def test_non_literal_key(self):
        src = HANDLER.replace('"uptime_ms"', 'k') + routes('{ "/api/info", api_info },')
        msg = self.fails(src)
        self.assertIn('rpc_tx_key_uint(tx, "field_name", value)', msg)
        run(src.replace('rpc_tx_key_uint(tx, k,', 'rpc_tx_key_uint(tx, "field_name",'))

    def test_json_no_route_reaches(self):
        lat = ('static void api_lat_json(struct rpc_tx *tx)\n{\n'
               '    rpc_tx_key_uint(tx, "latency_us", lat);\n}\n')
        src = HANDLER + lat + routes('{ "/api/info", api_info },')
        msg = self.fails(src)
        self.assertIn('fx/api_ctl.c:', msg)
        self.assertIn('rpc_tx_key_uint(...)', msg)
        self.assertIn("ignore = {..., 'api_lat_json'}", msg)
        self.assertIn('call api_lat_json() from the handler of the route that returns it', msg)
        cfg = g.Config()
        cfg.ignore.add('api_lat_json')                    # fix 3
        run(src, config=cfg)
        run(src.replace('rpc_tx_key_uint(tx, "uptime_ms", fm1_ms);',     # fix 1
                        'rpc_tx_key_uint(tx, "uptime_ms", fm1_ms);\n    rpc_tx_char(tx, \',\');\n'
                        '    api_lat_json(tx);'))

    def test_old_style_route(self):
        src = HANDLER + routes('{ "/api/info", api_info },') + (
            'static int old(const char *path) { if (rpc_path_match(path, "/api/old")) return 1; return 0; }\n')
        msg = self.fails(src)
        self.assertIn('{ "/api/your/path/{param}", api_your_handler },', msg)
        run(src.replace('rpc_path_match(path, "/api/old")', '0'))

    def test_function_pointer(self):
        pages = ('static void page_a_json(struct rpc_tx *tx) { rpc_tx_key_uint(tx, "x", 1); }\n'
                 'static const struct { const char *name; void (*fn)(struct rpc_tx *); } tbl[] = {\n'
                 '    { "A", page_a_json },\n    { 0, 0 }\n};\n')
        src = HANDLER.replace('rpc_tx_key_uint(tx, "uptime_ms", fm1_ms);',
                              'fnp_t f = find();\n    rpc_tx_raw(tx, "\\"data\\":{");\n    f(tx);\n'
                              '    rpc_tx_char(tx, \'}\');') + routes('{ "/api/info", api_info },')
        msg = self.fails(src, pages)
        self.assertIn("dispatch = {..., 'f': 'your_table'}", msg)
        cfg = g.Config()
        cfg.dispatch['f'] = 'tbl'
        doc = run(src, pages, config=cfg)
        self.assertIn('A', doc['components']['schemas'])

    def test_bound_not_a_number(self):
        src = (HANDLER.replace('rpc_http_json_ok(tx);',
                               'int32_t v;\n    if (!api_arg_int(a[0], "n", 0, limit(), &v)) return;\n'
                               '    rpc_http_json_ok(tx);')
               + routes('{ "/api/info/{n}", api_info },'))
        msg = self.fails(src)
        self.assertIn('use a literal or a #define NAME <number> for the upper bound', msg)
        doc = run(src.replace('limit()', '99'))
        self.assertEqual(doc['paths']['/api/info/{n}']['get']['parameters'][0]['schema']['maximum'], 99)

    def test_missing_handler(self):
        src = HANDLER + routes('{ "/api/info", api_info },', '{ "/api/gone", api_gone },')
        msg = self.fails(src)
        self.assertIn('static void api_gone(const char *const *a) { ... }', msg)
        run(src.replace('{ "/api/gone", api_gone },', ''))

    def test_json_does_not_close(self):
        src = HANDLER.replace('rpc_http_json_end(tx);', 'rpc_http_respond(tx, 200);') + \
            routes('{ "/api/info", api_info },')
        msg = self.fails(src)
        self.assertIn('an object is not closed', msg)
        run(src.replace('rpc_http_respond(tx, 200);', 'rpc_http_json_end(tx);'))

    def test_stale_overlay(self):
        src = HANDLER + routes('{ "/api/info", api_info },')
        text = ('paths:\n  /api/infos:\n    summary: x\n  /api/info:\n    parameters:\n      nope: x\n'
                '    fields:\n      uptime: x\npages:\n  NOPE:\n    x: y\n')
        msg = self.fails(src, ov=overlay(text))
        self.assertIn('overlay.yaml:2: paths: /api/infos', msg)
        self.assertIn('rename the key to one of /api/info', msg)
        self.assertIn('delete "nope" (line 6)', msg)
        self.assertIn('rename the key to one of uptime_ms', msg)
        self.assertIn('delete "NOPE" (line 10)', msg)
        self.assertEqual(msg.count('gen_openapi: '), 4, 'every problem in one run')
        run(src, ov=overlay('paths:\n  /api/info:\n    fields:\n      uptime_ms: x\n'))


if __name__ == '__main__':
    unittest.main()
