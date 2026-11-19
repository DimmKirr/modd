# Smoke test: walk through pages and verify page data.
# Run: hurl --variable host=<ip>:9222 tests/ui/themes/modd/http/smoke.hurl
#
# Requires: firmware built with CONFIG_RPC + CONFIG_UI_MODD, network up.

# 1. Firmware is alive
GET http://{{host}}/api/info
HTTP 200
[Asserts]
jsonpath "$.firmware" == "modd"
jsonpath "$.version" isString
jsonpath "$.uptime_ms" >= 0

# 2. Navigate to home, verify page data
GET http://{{host}}/modd/home
HTTP 200
[Asserts]
jsonpath "$.ok" == true
jsonpath "$.page" == "HOME"
jsonpath "$.data.selected_track" >= 0
jsonpath "$.data.tracks" count == 4
jsonpath "$.data.tracks[0].engine" isString
jsonpath "$.data.tracks[0].preset" >= 0
jsonpath "$.data.palette" >= 0
jsonpath "$.data.palette_name" isString
jsonpath "$.data.bpm" >= 1

# 3. Open settings
GET http://{{host}}/modd/menu
HTTP 200
[Asserts]
jsonpath "$.ok" == true
jsonpath "$.page" == "MENU"
jsonpath "$.data.tab" isString

# 4. Switch to NET tab, verify network data
GET http://{{host}}/modd/menu/net
HTTP 200
[Asserts]
jsonpath "$.ok" == true
jsonpath "$.data.tab" == "NET"
jsonpath "$.data.link" isString
jsonpath "$.data.ip" >= 0
jsonpath "$.data.connected" isBoolean

# 5. Navigate to weather page
GET http://{{host}}/modd/wx
HTTP 200
[Asserts]
jsonpath "$.ok" == true
jsonpath "$.page" == "WX"
jsonpath "$.data.valid" isBoolean
jsonpath "$.data.fetching" isBoolean

# 6. Back to home to confirm round-trip navigation
GET http://{{host}}/modd/home
HTTP 200
[Asserts]
jsonpath "$.ok" == true
jsonpath "$.page" == "HOME"
