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
    html = """<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Yggdrasilcore C++ API</title>
<style>:root{--bg:#14101f;--panel:#1e1830;--card:#1c1530;--line:#3a2d63;--txt:#e8e2f5;--mut:#8f7fc9;--acc:#b79cff}
body{font-family:system-ui,sans-serif;background:var(--bg);color:var(--txt);margin:0;padding-bottom:40px}
#topbar{grid-column:1/-1;display:flex;gap:12px;align-items:center;padding:10px 16px;background:var(--panel);border-bottom:1px solid #352a55;z-index:10}
#hamburger{background:none;border:1px solid var(--line);border-radius:8px;color:var(--txt);font-size:16px;padding:6px 10px;cursor:pointer}
#logo{display:flex;gap:10px;align-items:center;color:var(--txt);text-decoration:none;font-weight:700;font-size:16px;white-space:nowrap}
#logo img{height:34px}
#swrap{position:relative;flex:1;margin-left:12px}
#q{width:100%;box-sizing:border-box;padding:8px 12px;border-radius:8px;border:1px solid #5b4a8a;background:#100c1c;color:#fff}
#res{position:absolute;top:110%;left:0;right:0;background:var(--panel);border:1px solid #5b4a8a;border-radius:8px;display:none;max-height:50vh;overflow:auto}
#res.open{display:block}
.sr-item{padding:8px 12px;cursor:pointer;font-size:13px}
.sr-item:hover{background:var(--line)}
.sr-item .c{color:var(--mut)}
.sr-badge{font-size:10px;font-weight:700;padding:1px 6px;border-radius:20px;flex:none}
.sr-badge.class{background:#6d28d9;color:#fff}
.sr-badge.method{background:#2d4a6e;color:#8fb8ec}
.sr-item .d{color:var(--mut);display:block;font-size:12px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
html,body{height:100%;overflow:hidden}
#app{display:grid;grid-template-rows:52px 1fr;grid-template-columns:480px 1fr;height:100vh;transition:grid-template-columns .25s ease}
@media (min-width:901px){
#app.sidebar-collapsed{grid-template-columns:0 1fr}
#app.sidebar-collapsed #sidebar{border-right-color:transparent}
#app.sidebar-collapsed #main{grid-column:2}
}
@media (max-width:900px){
#app{grid-template-columns:1fr}
#sidebar{position:fixed;top:52px;left:0;bottom:36px;width:min(480px,92vw);z-index:50;transform:translateX(-100%);transition:transform .2s}
#sidebar.open{transform:translateX(0)}
#sidebar-overlay{display:none;position:fixed;left:0;right:0;top:52px;bottom:0;background:#0006;z-index:49}
#sidebar-overlay.open{display:block}
}
#sidebar{grid-row:2;display:grid;grid-template-columns:220px 1fr;border-right:1px solid #352a55;min-height:0;overflow:hidden;background:var(--bg)}
#sidebar nav{overflow-y:auto;min-height:0}
#main{grid-row:2;overflow-y:auto;min-height:0}
#classcol{border-right:1px solid #352a55}
.shead{padding:12px 14px 6px;font-size:11px;font-weight:700;text-transform:uppercase;letter-spacing:1px;color:var(--mut)}
#classcol button{display:block;width:100%;text-align:left;background:none;border:none;color:#cfc4f5;padding:7px 14px;cursor:pointer;font-size:14px}
#classcol button.on{background:var(--line);color:#fff}
#methodcol button.mitem{display:flex;width:100%;text-align:left;background:none;border:none;color:#cfc4f5;padding:6px 14px;cursor:pointer;font-size:13px;align-items:baseline;gap:7px}
#methodcol button.mitem.on{background:var(--line);color:#fff}
#methodcol button.mitem.inherited{color:var(--mut)}
.own-dot,.inh-dot{flex:none;width:7px;height:7px;border-radius:50%;transform:translateY(-1px)}
.own-dot{background:var(--acc)}
.inh-dot{border:1.5px solid var(--mut);box-sizing:border-box}
#inhtoggle{margin:4px 14px 10px;background:#6d28d9;border:1px solid #6d28d9;border-radius:20px;color:#fff;padding:5px 14px;font-size:12px;cursor:pointer}
#inhtoggle[aria-pressed="false"]{background:none;border-color:var(--line);color:var(--mut)}
body.hideinh #methodcol button.mitem.inherited{display:none}
#main{padding:20px;max-width:960px}
.card{border:1px solid var(--line);border-radius:10px;margin:0 0 10px;background:var(--card)}
.chead{padding:12px 14px;cursor:pointer}
.chead:hover{background:#241b40;border-radius:10px}
.chead b{font-size:15px}
.star{color:var(--mut);font-size:12px;margin-left:6px}
.inh-note{font-size:12px;color:var(--mut);margin-left:8px}
.sig{font-family:monospace;color:var(--acc);font-size:13px;margin-top:4px;overflow-wrap:anywhere}
.cbody{display:none;padding:0 14px 12px}
.card.open .cbody{display:block}
.cbody p{margin:8px 0;line-height:1.5}
.cbody code{background:#100c1c;padding:1px 5px;border-radius:4px;font-size:12.5px}
.cbody small{color:var(--mut)}
.cbody a{color:var(--acc)}
#home img#homelogo{max-width:100%;height:auto}
#home .stats{display:flex;gap:12px;flex-wrap:wrap;margin:16px 0}
#home .stat{border:1px solid var(--line);border-radius:10px;padding:10px 18px;background:var(--card)}
#home .stat b{font-size:22px;display:block}
#home .stat span{font-size:12px;color:var(--mut)}
#home a{color:var(--acc)}
footer{position:fixed;left:0;right:0;bottom:0;background:var(--panel);border-top:1px solid #352a55;
 color:var(--mut);font-size:12px;padding:8px 16px;display:flex;gap:8px;justify-content:space-between;flex-wrap:wrap}</style>
</head><body>
<header id="topbar">
<button id="hamburger" aria-label="Toggle sidebar">☰</button>
<a id="logo" href="?"><span>Yggdrasilcore C++ API</span></a>
<div id="swrap"><input id="q" placeholder="Search classes and methods…" autocomplete="off" spellcheck="false"><div id="res"></div></div>
</header>
<div id="app"><aside id="sidebar">
<nav id="classcol"><div class="shead">Classes</div></nav>
<nav id="methodcol"></nav>
</aside><div id="sidebar-overlay"></div><main id="main"></main></div>
<footer><span>* Methods marked with &#9733; had their explanations generated by FOOT_MODEL.</span><span>© FOOT_YEAR Yggdrasilcore</span></footer>
<script>const D=DATA_PLACEHOLDER,H=HIER_PLACEHOLDER;
const main=document.getElementById("main"),classcol=document.getElementById("classcol"),
 methcol=document.getElementById("methodcol"),q=document.getElementById("q"),res=document.getElementById("res"),
 app=document.getElementById("app");
const params=new URLSearchParams(location.search);
let cur=params.get("class"),target=params.get("method");
const open=new Set();
if(cur&&target)open.add(cur+"::"+target);
function esc(s){return String(s).replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;").replace(/"/g,"&quot;");}
const allClasses=[...new Set(D.map(e=>e.class))].sort();
const cset=new Set(allClasses);
function fmt(s){let h=esc(s);
 h=h.replace(/`([^`]+)`/g,"<code>$1</code>");
 h=h.replace(/\\*\\*(.+?)\\*\\*/g,"<strong>$1</strong>");
 h=h.replace(/(^|\\W)\\*([^\\*\\n]+)\\*/g,"$1<em>$2</em>");
 h=h.replace(/\\[(\\w+)\\]/g,(m,c)=>cset.has(c)?'<a href="?class='+c+'">'+c+"</a>":m);
 return h.replace(/\\n/g,"<br>");}
function ancestors(cls){const seen=new Set(),stack=[...(H[cls]?H[cls].bases:[])];
 while(stack.length){const b=stack.pop();if(!b||seen.has(b))continue;seen.add(b);
  if(H[b])stack.push(...H[b].bases);}return seen;}
function methodsOf(cls){const anc=ancestors(cls),out=[];
 D.forEach((e,i)=>{if(e.class===cls)out.push({e:e,i:i,inh:null});
  else if(anc.has(e.class))out.push({e:e,i:i,inh:e.class});});
 out.sort((a,b)=>a.e.name.localeCompare(b.e.name));return out;}
function cardEl(m){const e=m.e,k=e.class+"::"+e.name,isOpen=open.has(k);
 const d=document.createElement("div");d.className="card"+(isOpen?" open":"");d.id="m"+m.i;
 const h=document.createElement("div");h.className="chead";
 const t=document.createElement("b");t.textContent=k;h.appendChild(t);
 if(e.ai){const s=document.createElement("span");s.className="star";s.textContent="\\u2605";h.appendChild(s);}
 if(m.inh){const n=document.createElement("span");n.className="inh-note";n.textContent="inherited from "+m.inh;h.appendChild(n);}
 const sg=document.createElement("div");sg.className="sig";sg.textContent=e.sig;h.appendChild(sg);
 h.onclick=()=>{const was=d.classList.toggle("open");
  if(was)open.add(k);else open.delete(k);};
 d.appendChild(h);
 const b=document.createElement("div");b.className="cbody";
 const p=document.createElement("p");p.innerHTML=fmt(e.desc||"(no description)");b.appendChild(p);
 const sm=document.createElement("small");sm.textContent=e.file+":"+e.line;b.appendChild(sm);
 d.appendChild(b);return d;}
function renderSidebar(){
 classcol.querySelectorAll("button").forEach(x=>x.remove());
 allClasses.forEach(c=>{const b=document.createElement("button");b.textContent=c;
  if(c===cur)b.classList.add("on");
  b.onclick=()=>{location.href="?class="+encodeURIComponent(c);};classcol.appendChild(b);});
 methcol.innerHTML="";
 if(!cur||!cset.has(cur)){
  const hd=document.createElement("div");hd.className="shead";hd.textContent="Classes";methcol.appendChild(hd);
  allClasses.forEach(c=>{const b=document.createElement("button");
   b.className="mitem";const nm=document.createElement("span");nm.textContent=c;b.appendChild(nm);
   b.onclick=()=>{location.href="?class="+encodeURIComponent(c);};methcol.appendChild(b);});
  return;}
 const hd=document.createElement("div");hd.className="shead";hd.textContent=cur+" Methods";methcol.appendChild(hd);
 const t=document.createElement("button");t.id="inhtoggle";t.textContent="Show inherited";
 t.setAttribute("aria-pressed",document.body.classList.contains("hideinh")?"false":"true");
 t.onclick=()=>{const h=document.body.classList.toggle("hideinh");t.setAttribute("aria-pressed",h?"false":"true");};
 methcol.appendChild(t);
 methodsOf(cur).forEach(m=>{const b=document.createElement("button");
  b.className="mitem"+(m.inh?" inherited":"");
  const dot=document.createElement("span");dot.className=m.inh?"inh-dot":"own-dot";b.appendChild(dot);
  const nm=document.createElement("span");nm.textContent=m.e.name;b.appendChild(nm);
  if((cur+"::"+m.e.name)===cur+"::"+target)b.classList.add("on");
  b.onclick=()=>{location.href="?class="+encodeURIComponent(cur)+"&method="+encodeURIComponent(m.e.name);};
  methcol.appendChild(b);});}
function renderHome(){main.innerHTML="";
 const w=document.createElement("div");w.id="home";
 const im=document.createElement("img");im.id="homelogo";im.src="logo.png";im.alt="Yggdrasil";
 im.onerror=()=>im.remove();w.appendChild(im);
 const h=document.createElement("h1");h.textContent="Yggdrasilcore C++ API";w.appendChild(h);
 const p=document.createElement("p");
 p.textContent="Yggdrasilcore is an AzerothCore-based open-source game server for the "
  + "WotLK 3.3.5a client. This reader documents its C++ core API: classes and methods parsed "
  + "from the source, with explanations generated by a hosted code model. Pick a class on the "
  + "left, or search above.";w.appendChild(p);
 const st=document.createElement("div");st.className="stats";
 const docd=D.filter(e=>e.desc).length;
 [["Methods",D.length],["Classes",allClasses.length],["Documented",docd]].forEach(s=>{
  const d=document.createElement("div");d.className="stat";
  const b=document.createElement("b");b.textContent=s[1];d.appendChild(b);
  const sp=document.createElement("span");sp.textContent=s[0];d.appendChild(sp);st.appendChild(d);});
 w.appendChild(st);
 const links=document.createElement("p");links.innerHTML="";
 [["Yggdrasil WoW","https://yggdrasilwow.com/"],
  ["Source code","https://github.com/YggdrasilWotLK/yggdrasilcore"],
  ["Lua API docs","https://yggdrasilwotlk.github.io/mod-yla/"]].forEach((l,i)=>{
  if(i)links.appendChild(document.createTextNode(" · "));
  const a=document.createElement("a");a.href=l[1];a.textContent=l[0];links.appendChild(a);});
 w.appendChild(links);main.appendChild(w);}
function renderClass(){main.innerHTML="";
 const h=document.createElement("h2");h.textContent=cur;main.appendChild(h);
 const bases=H[cur]?H[cur].bases:[];
 if(bases.length){const p=document.createElement("p");
  p.appendChild(document.createTextNode("Inherits: "));
  bases.forEach((b,i)=>{if(i)p.appendChild(document.createTextNode(", "));
   if(cset.has(b)){const a=document.createElement("a");a.href="?class="+encodeURIComponent(b);
    a.textContent=b;p.appendChild(a);}else p.appendChild(document.createTextNode(b));});
  main.appendChild(p);}
 const ms=methodsOf(cur),own=ms.filter(m=>!m.inh).length;
 const sub=document.createElement("p");sub.style.color="var(--mut)";
 sub.textContent=own+" own · "+(ms.length-own)+" inherited";main.appendChild(sub);
 ms.forEach(m=>main.appendChild(cardEl(m)));
 if(target){const idx=D.findIndex(e=>e.class===cur&&e.name===target);
  const el=idx>=0&&document.getElementById("m"+idx);
  if(el){el.classList.add("open");el.scrollIntoView({block:"center"});}}}
function render(){renderSidebar();
 if(!cur||!cset.has(cur))renderHome();else renderClass();}
const sidebar=document.getElementById("sidebar"),overlay=document.getElementById("sidebar-overlay");
const desktop=()=>window.matchMedia("(min-width: 901px)").matches;
function setCollapsed(c){app.classList.toggle("sidebar-collapsed",c);
 try{localStorage.setItem("ygg-cpp-sb",c?"1":"0");}catch(e){}}
document.getElementById("hamburger").onclick=()=>{
 if(desktop()){setCollapsed(!app.classList.contains("sidebar-collapsed"));}
 else{const o=!sidebar.classList.contains("open");
  sidebar.classList.toggle("open",o);overlay.classList.toggle("open",o);}};
overlay.onclick=()=>{sidebar.classList.remove("open");overlay.classList.remove("open");};
window.addEventListener("resize",()=>{
 if(!desktop()&&app.classList.contains("sidebar-collapsed"))setCollapsed(false);});
try{if(localStorage.getItem("ygg-cpp-sb")==="1"&&desktop())setCollapsed(true);}catch(e){}
q.addEventListener("input",()=>{const v=q.value.trim().toLowerCase();
 if(!v){res.classList.remove("open");res.innerHTML="";return;}
 const ch=allClasses.filter(c=>c.toLowerCase().includes(v)).slice(0,5).map(c=>({t:"class",c:c,n:c,d:""}));
 const mh=D.filter(e=>(e.class+"::"+e.name).toLowerCase().includes(v)||
  (e.desc&&e.desc.toLowerCase().includes(v))).slice(0,25).map(e=>({t:"method",c:e.class,n:e.name,d:e.desc}));
 const hits=ch.concat(mh);
 if(!hits.length){res.classList.remove("open");res.innerHTML="";return;}
 res.innerHTML="";hits.forEach(x=>{const d=document.createElement("div");d.className="sr-item";
  const bg=document.createElement("span");bg.className="sr-badge "+x.t;bg.textContent=x.t;d.appendChild(bg);
  const t=document.createElement("div");
  if(x.t==="method"){t.innerHTML='<span class="c"></span>';
   t.querySelector(".c").textContent=x.c+".";
   const nm=document.createElement("span");nm.textContent=x.n;t.appendChild(nm);}
  else t.textContent="Class: "+x.n;
  d.appendChild(t);
  if(x.d){const ds=document.createElement("span");ds.className="d";ds.textContent=(x.d||"").slice(0,120);d.appendChild(ds);}
  d.onclick=()=>{res.classList.remove("open");q.value="";
   location.href=x.t==="method"?"?class="+encodeURIComponent(x.c)+"&method="+encodeURIComponent(x.n)
    :"?class="+encodeURIComponent(x.n);};
  res.appendChild(d);});
 res.classList.add("open");});
document.addEventListener("click",e=>{if(!q.contains(e.target)&&!res.contains(e.target))res.classList.remove("open");});
q.addEventListener("keydown",e=>{if(e.key==="Escape"){res.classList.remove("open");q.blur();}});
render();</script></body></html>"""
    html = html.replace("DATA_PLACEHOLDER", json.dumps(entries))
    html = html.replace("HIER_PLACEHOLDER", json.dumps(classes))
    html = html.replace("FOOT_MODEL", model)
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
