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


def find_body(cls, method, header_path):
    """Grab up to 60 lines of Class::Method body from sibling .cpp files."""
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
                return "".join(chunk)
    return ""


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


def _post(url, payload, timeout=240, token=""):
    headers = {"Content-Type": "application/json"}
    if token:
        headers["Authorization"] = "Bearer " + token
    req = urllib.request.Request(
        url, data=json.dumps(payload).encode(), headers=headers
    )
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode())


def valid_desc(sig, what, effects):
    """Strict gate: returns cleaned desc or ''."""
    what = re.sub(r"\s+", " ", THINK_RE.sub("", what or "")).strip()
    effects = re.sub(r"\s+", " ", THINK_RE.sub("", effects or "")).strip()
    if not (25 <= len(what) <= 500):
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


GH_ENDPOINT = "https://models.github.ai/inference"


def resolve_model(endpoint, token, want):
    """Pick a model id. 'auto' prefers the biggest Qwen coder in the catalog."""
    if want != "auto":
        return want
    req = urllib.request.Request(
        endpoint.rstrip("/") + "/models",
        headers={"Authorization": "Bearer " + token} if token else {},
    )
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            ids = [m.get("id", "") for m in json.loads(r.read().decode()).get("data", [])]
    except Exception as e:
        print("WARN model catalog unavailable: %s" % e, file=sys.stderr)
        return "qwen2.5-coder-32b-instruct"
    qwens = sorted({i for i in ids if "qwen" in i.lower() and "coder" in i.lower()})
    big = [i for i in qwens if "32" in i] or qwens
    if big:
        return big[0]
    small = [i for i in ids if "mini" in i.lower()]
    return small[0] if small else (ids[0] if ids else "qwen2.5-coder-32b-instruct")


def openai_describe(endpoint, token, model, sig, body):
    """Returns (desc, raw). JSON-schema contract, one retry, else gap."""
    url = endpoint.rstrip("/") + "/chat/completions"
    schema = dict(SCHEMA)
    base = {
        "model": model,
        "temperature": 0,
        "max_tokens": 300,
        "response_format": {
            "type": "json_schema",
            "json_schema": {"name": "doc", "strict": True, "schema": schema},
        },
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
            data = _post(url, payload, timeout=300, token=token)
            raw = (data.get("choices", [{}])[0].get("message", {}).get("content") or "").strip()
            try:
                obj = json.loads(raw)
            except ValueError:
                continue
            desc = valid_desc(sig, obj.get("what"), obj.get("effects"))
            if desc:
                return desc, raw
        except Exception as e:  # never fail the job; 429s just become gaps
            print("WARN llm failed: %s" % e, file=sys.stderr)
            return "", raw
    return "", raw


def build_reader(entries, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "ai.json"), "w") as f:
        json.dump(entries, f, indent=1)
    index = [
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
    html = """<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>YggdrasilCore C++ API</title>
<style>body{font-family:system-ui,sans-serif;background:#14101f;color:#e8e2f5;margin:0;padding-bottom:34px}
header{padding:12px 16px;background:#1e1830;position:sticky;top:0;display:flex;gap:12px;align-items:center;z-index:5}
#swrap{position:relative;flex:1;max-width:520px}
#q{width:100%;box-sizing:border-box;padding:8px 12px;border-radius:8px;border:1px solid #5b4a8a;background:#100c1c;color:#fff}
#res{position:absolute;top:110%;left:0;right:0;background:#1e1830;border:1px solid #5b4a8a;border-radius:8px;display:none;max-height:50vh;overflow:auto}
#res.open{display:block}
.sr-item{padding:8px 12px;cursor:pointer;font-size:13px}
.sr-item:hover{background:#3a2d63}
.sr-item .c{color:#8f7fc9}
.sr-item .d{color:#8f7fc9;display:block;font-size:12px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
main{display:grid;grid-template-columns:260px 1fr;min-height:100vh}
#cls{border-right:1px solid #352a55;overflow:auto;max-height:calc(100vh - 57px)}
#cls button{display:block;width:100%;text-align:left;background:none;border:none;color:#cfc4f5;padding:8px 14px;cursor:pointer}
#cls button.on{background:#3a2d63;color:#fff}
#list{padding:16px;max-width:900px}
.card{border:1px solid #3a2d63;border-radius:10px;margin:0 0 10px;background:#1c1530}
.chead{padding:12px 14px;cursor:pointer}
.chead:hover{background:#241b40;border-radius:10px}
.chead b{font-size:15px}
.star{color:#8f7fc9;font-size:12px;margin-left:6px}
.sig{font-family:monospace;color:#b79cff;font-size:13px;margin-top:4px}
.cbody{display:none;padding:0 14px 12px}
.card.open .cbody{display:block}
.cbody p{margin:8px 0;line-height:1.5}
.cbody code{background:#100c1c;padding:1px 5px;border-radius:4px;font-size:12.5px}
.cbody small{color:#8f7fc9}
.cbody a{color:#b79cff}
footer{position:fixed;left:0;right:0;bottom:0;background:#1e1830;border-top:1px solid #352a55;
 color:#8f7fc9;font-size:12px;padding:8px 16px}</style>
</head><body><header><b>C++ API</b><div id="swrap"><input id="q" placeholder="Search Class::method…" autocomplete="off" spellcheck="false"><div id="res"></div></div></header>
<main><nav id="cls"></nav><div id="list"></div></main>
<footer>* Methods marked with &#9733; had their explanations generated by FOOT_MODEL.</footer>
<script>const D=DATA_PLACEHOLDER;
const list=document.getElementById("list"),nav=document.getElementById("cls"),
 q=document.getElementById("q"),res=document.getElementById("res");
const classes=[...new Set(D.map(e=>e.class))].sort();
const cset=new Set(classes);
let cur=new URLSearchParams(location.search).get("class");
let target=new URLSearchParams(location.search).get("method");
const open=new Set();
if(cur&&target)open.add(cur+"::"+target);
function esc(s){return String(s).replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;").replace(/"/g,"&quot;");}
function fmt(s){let h=esc(s);
 h=h.replace(/`([^`]+)`/g,"<code>$1</code>");
 h=h.replace(/\\*\\*(.+?)\\*\\*/g,"<strong>$1</strong>");
 h=h.replace(/(^|\\W)\\*([^\\*\\n]+)\\*/g,"$1<em>$2</em>");
 h=h.replace(/\\[(\\w+)\\]/g,(m,c)=>cset.has(c)?'<a href="?class='+c+'">'+c+"</a>":m);
 return h.replace(/\\n/g,"<br>");}
function card(e,i){const id="m"+i,k=e.class+"::"+e.name,isOpen=open.has(k);
 const d=document.createElement("div");d.className="card"+(isOpen?" open":"");d.id=id;
 const h=document.createElement("div");h.className="chead";
 const t=document.createElement("b");t.textContent=k;h.appendChild(t);
 if(e.ai){const s=document.createElement("span");s.className="star";s.textContent="\\u2605";h.appendChild(s);}
 const sg=document.createElement("div");sg.className="sig";sg.textContent=e.sig;h.appendChild(sg);
 h.onclick=()=>{d.classList.toggle("open");
  if(d.classList.contains("open"))open.add(k);else open.delete(k);};
 d.appendChild(h);
 const b=document.createElement("div");b.className="cbody";
 const p=document.createElement("p");p.innerHTML=fmt(e.desc||"(no description)");b.appendChild(p);
 const sm=document.createElement("small");sm.textContent=e.file+":"+e.line;b.appendChild(sm);
 d.appendChild(b);return d;}
function render(){list.innerHTML="";
 D.forEach((e,i)=>{if(cur&&e.class!==cur)return;list.appendChild(card(e,i));});
 [...nav.children].forEach(b=>b.classList.toggle("on",b.textContent===cur));}
function go(cls,name){cur=cls;render();open.add(cls+"::"+name);
 const idx=D.findIndex(e=>e.class===cls&&e.name===name);
 const el=idx>=0&&document.getElementById("m"+idx);
 if(el){el.classList.add("open");el.scrollIntoView({block:"center"});}
 history.replaceState(null,"","?class="+encodeURIComponent(cls)+"&method="+encodeURIComponent(name));}
classes.forEach(c=>{const b=document.createElement("button");b.textContent=c;
 b.onclick=()=>{cur=c;target=null;history.replaceState(null,"","?class="+encodeURIComponent(c));render();};
 nav.appendChild(b);});
q.addEventListener("input",()=>{const v=q.value.trim().toLowerCase();
 if(!v){res.classList.remove("open");res.innerHTML="";return;}
 const hits=D.filter(e=>(e.class+"::"+e.name).toLowerCase().includes(v)||
  (e.desc&&e.desc.toLowerCase().includes(v))).slice(0,30);
 if(!hits.length){res.classList.remove("open");res.innerHTML="";return;}
 res.innerHTML="";hits.forEach(e=>{const d=document.createElement("div");d.className="sr-item";
  const t=document.createElement("div");t.innerHTML='<span class="c"></span>';
  t.querySelector(".c").textContent=e.class+".";
  const nm=document.createElement("span");nm.textContent=e.name;t.appendChild(nm);d.appendChild(t);
  const ds=document.createElement("span");ds.className="d";ds.textContent=(e.desc||"").slice(0,120);d.appendChild(ds);
  d.onclick=()=>{res.classList.remove("open");q.value="";go(e.class,e.name);};
  res.appendChild(d);});
 res.classList.add("open");});
document.addEventListener("click",e=>{if(!q.contains(e.target)&&!res.contains(e.target))res.classList.remove("open");});
q.addEventListener("keydown",e=>{if(e.key==="Escape"){res.classList.remove("open");q.blur();}});
render();</script></body></html>"""
    html = html.replace("DATA_PLACEHOLDER", json.dumps(entries))
    html = html.replace("FOOT_MODEL", model)
    with open(os.path.join(out_dir, "index.html"), "w") as f:
        f.write(html)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scope", nargs="*", default=DEFAULT_SCOPE)
    ap.add_argument("--root", default=None,
                    help="Source tree to parse (default: repo containing this script)")
    ap.add_argument("--cache", default=None)
    ap.add_argument("--out", default=None)
    ap.add_argument("--endpoint", default=GH_ENDPOINT,
                    help="OpenAI-compatible base URL (GitHub Models default, or http://localhost:11434/v1)")
    ap.add_argument("--token", default=os.environ.get("GITHUB_TOKEN", ""),
                    help="Bearer token (GITHUB_TOKEN in Actions)")
    ap.add_argument("--model", default="auto",
                    help="'auto' picks the biggest Qwen coder from the model catalog")
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

    model = resolve_model(a.endpoint, a.token, a.model)
    print("model: %s @ %s" % (model, a.endpoint), flush=True)

    headers = find_headers(a.scope)
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
            body = find_body(e["class"], e["name"], h)
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
                desc, raw = openai_describe(a.endpoint, a.token, model, e["sig"], body)
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
                build_reader(entries, a.out)
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
    build_reader(entries, a.out)
    print("headers=%d entries=%d new=%d cached=%d failed=%d" % (len(headers), len(entries), done, skipped, failed))


if __name__ == "__main__":
    main()
