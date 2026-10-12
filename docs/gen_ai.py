#!/usr/bin/env python3
"""C++ API doc generator: headers -> hosted-model explanations -> static Pages reader.

- Extracts public methods from headers under --scope dirs (default: Entities).
- Grounds each prompt with header signature + .cpp body.
- Caches by body hash in docs/ai-cache.json; only stale/missing entries hit the LLM.
- Builds a static reader (dist/) for GitHub Pages.

Providers (single OpenAI-compatible path):
- GitHub Models (default, hosted 32B-class):  GITHUB_TOKEN=... python3 docs/gen_ai.py
- Local Ollama:  python3 docs/gen_ai.py --endpoint http://localhost:11434/v1 --model qwen3:4b

Examples:
  python3 docs/gen_ai.py --dry-run --limit 50
  GITHUB_TOKEN=... python3 docs/gen_ai.py --limit 20
  python3 docs/gen_ai.py --diff-ref HEAD~1
"""

import argparse
import datetime
import difflib
import hashlib
import json
import os
import re
import subprocess
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_SCOPE = ["src/server/game/Entities"]

CLASS_RE = re.compile(r"^\s*(?:class|struct)\s+([A-Za-z_][\w\s:]*?)\s*(?=[{:;]|$)")
ACCESS_RE = re.compile(r"^\s*(public|protected|private)\s*:\s*$")
# method decl ending in ; with parens, e.g. "bool IsCivilian() const;" / "void SetCorpseDelay(uint32 delay) { ... }"
METHOD_RE = re.compile(
    r"^\s*(?:(?:virtual|static|explicit|inline|constexpr|friend)\s+)*"
    r"([\w:\<\>\*\&,\s]+?)\s+(\w+)\s*\(([^;{}]*)\)\s*(const)?\s*(override)?\s*(=\s*0|=\s*default|=\s*delete)?\s*[;{]"
)
SKIP_NAMES = {"if", "for", "while", "switch", "return", "sizeof"}

CLASS_DECL_RE = re.compile(
    r"^\s*(?:class|struct)\s+((?:\w+\s+)*\w+)\s*(?::\s*([^{;]*?))?\s*[{;]",
    re.MULTILINE,
)


def _clean_typename(tok):
    tok = re.sub(r"<.*", "", tok).strip()
    parts = tok.split()
    return parts[-1] if parts else ""


def scan_classes(headers):
    """Map class name -> {bases, file} for the inheritance sidebar."""
    out = {}
    for h in headers:
        try:
            with open(h, errors="ignore") as f:
                text = f.read()
        except OSError:
            continue
        for m in CLASS_DECL_RE.finditer(text):
            if m.group(0).rstrip().endswith(";"):
                continue  # forward declaration, no bases
            name = _clean_typename(m.group(1).split(":")[0])
            if not name or not name[0].isalpha() or name in SKIP_NAMES or name.isupper():
                continue
            bases = []
            if m.group(2):
                for part in m.group(2).split(","):
                    b = _clean_typename(re.sub(r"^(public|protected|private|virtual)\s+", "", part.strip()))
                    if b and b not in bases:
                        bases.append(b)
            if name not in out:
                out[name] = {"bases": bases, "file": os.path.relpath(h, ROOT)}
    return out


def git_diff_files(ref):
    try:
        out = subprocess.check_output(
            ["git", "diff", "--name-only", ref], cwd=ROOT, text=True
        )
        return set(out.split())
    except subprocess.CalledProcessError:
        return set()


def find_headers(scope_dirs):
    headers = []
    for d in scope_dirs:
        base = os.path.join(ROOT, d)
        for dp, _, fns in os.walk(base):
            for fn in fns:
                if fn.endswith(".h"):
                    headers.append(os.path.join(dp, fn))
    return sorted(headers)


def header_inline(header_path, lineno):
    """Body of a header-inline definition starting at/after decl line, else ''."""
    try:
        with open(header_path, errors="ignore") as f:
            lines = f.readlines()
    except OSError:
        return ""
    buf, depth, started = [], 0, False
    for ln in lines[lineno - 1 : lineno + 9]:
        if not started and "{" not in ln:
            if len(buf) >= 2:
                return ""  # pure declaration, definition lives elsewhere (or nowhere)
            buf.append(ln)
            continue
        for ch in ln:
            if ch == "{":
                depth += 1
                started = True
            elif ch == "}":
                depth -= 1
        buf.append(ln)
        if started and depth <= 0:
            break
    return "".join(buf) if started else ""


def find_impl(cls, method, header_path):
    """(body, relpath, line1) of the Class::Method .cpp definition,
    or ('', '', 0) if none (e.g. header-inline)."""
    stem = os.path.splitext(header_path)[0]
    candidates = [stem + ".cpp"]
    # also try same directory siblings, e.g. Player.cpp vs Player.h
    d = os.path.dirname(header_path)
    try:
        for fn in os.listdir(d):
            if fn.endswith(".cpp"):
                p = os.path.join(d, fn)
                if p not in candidates:
                    candidates.append(p)
    except OSError:
        pass
    pat = re.compile(r"\b%s\s*::\s*%s\s*\(" % (re.escape(cls), re.escape(method)))
    for cpp in candidates:
        if not os.path.isfile(cpp):
            continue
        try:
            with open(cpp, errors="ignore") as f:
                lines = f.readlines()
        except OSError:
            continue
        for i, ln in enumerate(lines):
            if pat.search(ln):
                chunk = []
                for bl in lines[i : i + 40]:
                    if chunk and NEXT_DEF_RE.match(bl):
                        break
                    chunk.append(bl)
                return "".join(chunk), os.path.relpath(cpp, ROOT), i + 1
    return "", "", 0


def parse_header(path):
    """Yield dicts for public/protected method declarations.

    Tracks a stack of (class, access, depth) so nested structs/enums
    never misattribute outer-class methods to the inner type.
    """
    try:
        with open(path, errors="ignore") as f:
            lines = f.readlines()
    except OSError:
        return
    depth = 0
    stack = []  # [class_name, access, entry_depth]
    pending = None  # (name, is_struct) when '{' is on the next line
    for i, ln in enumerate(lines):
        stripped = ln.strip()
        # skip preprocessor and single-line comments to keep brace counting honest
        if stripped.startswith("#") or stripped.startswith("//"):
            continue
        m = CLASS_RE.match(ln)
        if m and ("class" in ln or "struct" in ln):
            # ignore forward declarations ("class Foo;") and Q_OBJECT-style noise
            if "{" not in ln and ";" in ln:
                continue
            # "class AC_GAME_API MotdMgr" / "class Foo : public Bar" -> real name is last token before ':' 
            decl = m.group(1).split(":")[0].split()
            name = decl[-1] if decl else ""
            if not name or not name[0].isalpha() or name in SKIP_NAMES:
                continue
            is_struct = ln.lstrip().startswith("struct")
            if "{" in ln:
                stack.append([name, "public" if is_struct else "private", depth + ln[: ln.index("{")].count("{")])
                depth += ln.count("{") - ln.count("}")
            else:
                pending = (name, is_struct)
            continue
        if pending is not None:
            if "{" in ln:
                name, is_struct = pending
                stack.append([name, "public" if is_struct else "private", depth])
            pending = None
        depth += ln.count("{") - ln.count("}")
        while stack and depth <= stack[-1][2]:
            stack.pop()
        if not stack:
            continue
        am = ACCESS_RE.match(ln)
        if am:
            stack[-1][1] = am.group(1)
            continue
        if stack[-1][1] == "private":
            continue
        # skip enum bodies and data members
        mm = METHOD_RE.match(ln)
        if mm:
            ret, name, args = mm.group(1).strip(), mm.group(2), mm.group(3).strip()
            if name in SKIP_NAMES or name.isupper() or not name[0].isalpha():
                continue
            if len(ret.split()) > 6 or "(" in ret or ":" in ret:
                continue
            if stack and name == stack[-1][0]:
                continue  # constructors add noise, no doc value
            const = " const" if mm.group(4) else ""
            sig = "%s %s(%s)%s" % (ret, name, re.sub(r"\s+", " ", args), const)
            yield {
                "class": stack[-1][0],
                "name": name,
                "sig": sig,
                "file": os.path.relpath(path, ROOT),
                "line": i + 1,
                "access": stack[-1][1],
            }


def body_hash(entry, body):
    h = hashlib.sha1()
    h.update((entry["sig"] + "\n" + body).encode("utf-8", "ignore"))
    return h.hexdigest()[:12]


THINK_RE = re.compile(r"<think>.*?</think>", re.DOTALL)
NEXT_DEF_RE = re.compile(r"^\s*(?:[\w:\<\>\*\&]+\s+)?\w+::\w+\s*\(")
SYSTEM_PROMPT = "You are a code documentation API. Reply with a single JSON object only."

USER_TMPL = (
    'Document this C++ method as JSON: {{"what": "<what the code does>", '
    '"effects": "<side effects, or \\"none\\">"}}.\n'
    "Signature:\n{sig}\nDefinition:\n{body}"
)

SCHEMA = {
    "type": "object",
    "properties": {
        "what": {"type": "string"},
        "effects": {"type": "string"},
    },
    "required": ["what", "effects"],
}

ECHO_RE = re.compile(
    r"one sentence|second sentence|describe what|the task\b|the problem says|"
    r"we need to|we are to|we are given|note that the|no preamble|repetition of|"
    r"output only|two lines|in this context is the|but note:|"
    r"is a const method \(so it|do not repeat|list parameters|"
    r"side effects:\s*['\"]?\s*plus|add\s+['\"]?\s*side effects",
    re.IGNORECASE,
)


def _post(url, payload, timeout=300):
    req = urllib.request.Request(
        url, data=json.dumps(payload).encode(), headers={"Content-Type": "application/json"}
    )
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode())


def valid_desc(sig, what, effects):
    """Strict gate: returns cleaned desc or ''."""
    what = re.sub(r"\s+", " ", THINK_RE.sub("", what or "")).strip()
    effects = re.sub(r"\s+", " ", THINK_RE.sub("", effects or "")).strip()
    if not (12 <= len(what) <= 500):
        return ""
    if ECHO_RE.search(what):
        return ""
    if difflib.SequenceMatcher(None, what.lower(), sig.lower()).ratio() > 0.55:
        return ""  # signature paraphrase
    if effects.lower() == "none":
        return what
    if len(effects) > 300 or ECHO_RE.search(effects):
        return what
    return "%s Side effects: %s" % (what, effects)


def ollama_describe(base_url, model, sig, body):
    """Returns (desc, raw). Native Ollama chat: thinking off, JSON-schema
    contract, one retry, else gap."""
    url = base_url.rstrip("/") + "/api/chat"
    base = {
        "model": model,
        "stream": False,
        "think": False,
        "format": SCHEMA,
        "options": {"temperature": 0, "num_ctx": 2048, "num_predict": 300},
    }
    content = USER_TMPL.format(sig=sig, body=body[:2000])
    msgs = [
        {"role": "system", "content": SYSTEM_PROMPT},
        {"role": "user", "content": content},
    ]
    raw = ""
    for attempt in range(2):
        try:
            payload = dict(base, messages=list(msgs))
            if attempt == 1:
                payload["messages"].append(
                    {"role": "user", "content": "Too vague. Name the concrete actions, checks and calls."}
                )
            data = _post(url, payload)
            raw = (data.get("message", {}).get("content") or "").strip()
            try:
                obj = json.loads(raw)
            except ValueError:
                continue
            desc = valid_desc(sig, obj.get("what"), obj.get("effects"))
            if desc:
                return desc, raw
        except Exception as e:  # never fail the job
            print("WARN llm failed: %s" % e, file=sys.stderr)
            return "", raw
    return "", raw


SRC_BASE = "https://github.com/YggdrasilWotLK/yggdrasilcore/blob/master-shadows/"


def build_reader(entries, classes, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "ai.json"), "w") as f:
        json.dump(entries, f, indent=1)
    with open(os.path.join(out_dir, "hierarchy.json"), "w") as f:
        json.dump(classes, f, indent=1)
    index = [
        {"type": "class", "name": c, "desc": "%d methods" % sum(1 for e in entries if e["class"] == c)}
        for c in sorted({e["class"] for e in entries})
    ] + [
        {
            "type": "method",
            "class": e["class"],
            "name": e["name"],
            "desc": (e.get("desc") or "")[:220],
            "file": e["file"],
        }
        for e in entries
    ]
    with open(os.path.join(out_dir, "search_index.json"), "w") as f:
        json.dump(index, f)
    models = [e.get("model") for e in entries if e.get("model")]
    model = max(set(models), key=models.count) if models else "AI model"
    logo_src = os.path.join(os.path.dirname(os.path.abspath(__file__)), "logo.png")
    if os.path.isfile(logo_src):
        with open(logo_src, "rb") as fsrc, open(os.path.join(out_dir, "logo.png"), "wb") as fdst:
            fdst.write(fsrc.read())
    for _fav in ("favicon.png", "favicon.ico"):
        fav_src = os.path.join(os.path.dirname(os.path.abspath(__file__)), _fav)
        if os.path.isfile(fav_src):
            with open(fav_src, "rb") as fsrc, open(os.path.join(out_dir, _fav), "wb") as fdst:
                fdst.write(fsrc.read())
    tpl_dir = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(tpl_dir, "reader_template.html")) as f:
        html = f.read()
    with open(os.path.join(tpl_dir, "vendor", "yla-style.css")) as f:
        html = html.replace("YLA_CSS_PLACEHOLDER", f.read())
    html = html.replace("DATA_PLACEHOLDER", json.dumps(entries))
    html = html.replace("HIER_PLACEHOLDER", json.dumps(classes))
    html = html.replace("FOOT_MODEL", model)
    html = html.replace("SRC_BASE_PLACEHOLDER", SRC_BASE)
    html = html.replace("FOOT_YEAR", str(datetime.date.today().year))
    with open(os.path.join(out_dir, "index.html"), "w") as f:
        f.write(html)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scope", nargs="*", default=DEFAULT_SCOPE)
    ap.add_argument("--root", default=None,
                    help="Source tree to parse (default: repo containing this script)")
    ap.add_argument("--cache", default=None)
    ap.add_argument("--out", default=None)
    ap.add_argument("--endpoint", default="http://localhost:11434",
                    help="Ollama base URL")
    ap.add_argument("--model", default="qwen3.5:4b")
    ap.add_argument("--diff-ref", default="")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    global ROOT
    if a.root:
        ROOT = os.path.abspath(a.root)
    if not a.cache:
        a.cache = os.path.join(ROOT, "docs", "ai-cache.json")
    if not a.out:
        a.out = os.path.join(ROOT, "docs", "dist")

    model = a.model
    print("model: %s @ %s" % (model, a.endpoint), flush=True)

    headers = find_headers(a.scope)
    classes = scan_classes(headers)
    diff_files = git_diff_files(a.diff_ref) if a.diff_ref else set()

    try:
        with open(a.cache) as f:
            cache = json.load(f)
    except (OSError, ValueError):
        cache = {}

    entries, done, skipped, failed, visited = [], 0, 0, 0, set()
    mtag = re.sub(r"\W+", "-", model)
    for h in headers:
        rel = os.path.relpath(h, ROOT)
        if diff_files and rel not in diff_files:
            continue
        for e in parse_header(h):
            body, impl_file, impl_line = find_impl(e["class"], e["name"], h)
            e["impl"] = {"file": impl_file, "line": impl_line} if impl_file else None
            if not body.strip():
                # header-inline definition (e.g. one-line getters) as grounding
                body = header_inline(h, e["line"])
            hsh = body_hash(e, body)
            sigh = hashlib.sha1(e["sig"].encode()).hexdigest()[:8]
            key = "%s::%s@%s:%s" % (e["class"], e["name"], sigh, mtag)
            visited.add(key)
            hit = cache.get(key)
            if hit and hit.get("hash") == hsh and hit.get("entry", {}).get("model") == model:
                entries.append(hit["entry"])  # empty desc is also a stored outcome
                skipped += 1
                continue
            if a.limit and done >= a.limit:
                continue
            if a.dry_run:
                desc, raw = "PLACEHOLDER (dry-run): %s" % e["sig"], ""
            elif not body.strip():
                desc, raw = "", ""  # no definition found: honest gap, not signature paraphrase
            else:
                desc, raw = ollama_describe(a.endpoint, model, e["sig"], body)
                if not desc and not raw:
                    failed += 1
            entry = dict(e, hash=hsh, desc=desc, ai=not a.dry_run, model=None if a.dry_run else model)
            if raw or desc or a.dry_run or not body.strip():
                # cache verdicts; transport failures (no raw) stay uncached for retry
                cache[key] = {"hash": hsh, "desc": desc, "raw": raw, "entry": entry}
            entries.append(entry)
            done += 1
            if done % 25 == 0:  # crash-safe: flush cache + reader incrementally
                with open(a.cache, "w") as f:
                    json.dump(cache, f, indent=1)
                build_reader(entries, classes, a.out)
                print("progress: new=%d cached=%d failed=%d" % (done, skipped, failed), flush=True)

    # keep cached entries for files outside diff scope so reader stays complete
    if diff_files:
        def _key(e):
            sigh = hashlib.sha1(e["sig"].encode()).hexdigest()[:8]
            return "%s::%s@%s:%s" % (e["class"], e["name"], sigh, mtag)

        seen = {_key(e) for e in entries}
        for key, hit in cache.items():
            if key not in seen and hit.get("entry"):
                entries.append(hit["entry"])
    entries.sort(key=lambda e: (e["class"], e["name"]))

    if not diff_files and not a.dry_run and (not a.limit or done < a.limit):
        # full pass completed: drop keys for deleted methods
        for key in list(cache.keys()):
            if key not in visited:
                del cache[key]

    os.makedirs(os.path.dirname(a.cache), exist_ok=True)
    with open(a.cache, "w") as f:
        json.dump(cache, f, indent=1)
    build_reader(entries, classes, a.out)
    print("headers=%d entries=%d new=%d cached=%d failed=%d" % (len(headers), len(entries), done, skipped, failed))


if __name__ == "__main__":
    main()
