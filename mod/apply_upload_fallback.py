#!/usr/bin/env python3
"""Fallback for gaggimate_fw_upload_endpoint.patch.

Used by the build workflow only when `git apply` rejects the patch because the
surrounding upstream code moved (context drift). The code to insert is read
from the patch itself (its added '+' lines, hunk by hunk); only the *placement*
is re-derived from stable anchor lines. Every anchor must match exactly once,
otherwise the script aborts (the build then fails instead of shipping a
half-patched firmware).

Usage: apply_upload_fallback.py <upstream-repo-dir> <patch-file>
"""
import re
import sys

repo, patch_file = sys.argv[1], sys.argv[2]

patch = open(patch_file, encoding="utf-8").read()
per_file = {}
for chunk in re.split(r"^diff --git ", patch, flags=re.M)[1:]:
    name = re.match(r"a/(\S+)", chunk).group(1)
    hunks = re.split(r"^@@ .*$", chunk, flags=re.M)[1:]
    blocks = []
    for h in hunks:
        added = [ln[1:] for ln in h.split("\n") if ln.startswith("+") and not ln.startswith("+++")]
        blocks.append("\n".join(added) + "\n")
    per_file[name] = blocks

cpp_name = "src/display/plugins/WebUIPlugin.cpp"
h_name = "src/display/plugins/WebUIPlugin.h"
if len(per_file.get(cpp_name, [])) != 4 or len(per_file.get(h_name, [])) != 1:
    sys.exit("patch layout not as expected (cpp 4 hunks, h 1 hunk)")
inc, loop, call, tail = per_file[cpp_name]
members = per_file[h_name][0]


def insert_once(text, anchor, block, before):
    if text.count(anchor) != 1:
        sys.exit("anchor not found exactly once: %r (found %d)" % (anchor, text.count(anchor)))
    return text.replace(anchor, block + anchor if before else anchor + block, 1)


cpp_path = repo + "/" + cpp_name
cpp = open(cpp_path, encoding="utf-8").read()
cpp = insert_once(cpp, "#include <version.h>\n", inc, before=True)
cpp = insert_once(cpp, "void WebUIPlugin::loop() {\n", loop, before=False)
cpp = insert_once(cpp, '    server.on("/api/core-dump"', call, before=True)
if not cpp.endswith("\n"):
    cpp += "\n"
cpp += tail
open(cpp_path, "w", encoding="utf-8").write(cpp)

h_path = repo + "/" + h_name
h = open(h_path, encoding="utf-8").read()
h = insert_once(h, "    GitHubOTA *ota = nullptr;\n", members, before=True)
open(h_path, "w", encoding="utf-8").write(h)

print("fallback applied: upload endpoint inserted via anchors")
