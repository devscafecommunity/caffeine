#!/usr/bin/env python3
"""Generate the scripting API reference (EN/PT) from src/caffeine/script/api/*.json.

    python3 scripts/generate-script-api-docs.py            # write caffeine-website pages
    python3 scripts/generate-script-api-docs.py --check    # validate the registry only

The registry is the single source of truth: tests/test_script.cpp fails when a binding is missing
from it or a documented name no longer exists. Every function needs `en`, `pt`, `signature` and an
`example`; this script refuses to generate otherwise.
"""
import html
import json
import sys
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
API = ROOT / "src/caffeine/script/api"
SITE = ROOT / "caffeine-website/content"
GROUP_ORDER = ["Core", "Input", "Physics", "Game", "World", "Rendering", "UI"]
T = {
    "en": dict(api="Scripting API reference", lede="Every function the engine exposes to scripts, grouped like a standard library. Lua names are shown as `caffeine.<namespace>.<function>`; the Dryad native is shown when one exists.",
               ns="Namespace", fn="Functions", lua="Lua", dryad="Dryad", params="Parameters", returns="Returns", none="none",
               name="Name", type="Type", desc="Description", example="Example", only_dryad="Dryad only", last="Last updated",
               how="How this page is built", howtext="Generated from the API registry in the engine repository; a test fails when a binding is undocumented. Do not edit by hand.",
               group="Group", total="functions across", nss="namespaces", back="All namespaces", missing="Lua: not available"),
    "pt": dict(api="Referência da API de scripting", lede="Todas as funções que o engine expõe aos scripts, agrupadas como uma biblioteca padrão. Os nomes Lua aparecem como `caffeine.<namespace>.<função>`; o native Dryad é mostrado quando existe.",
               ns="Namespace", fn="Funções", lua="Lua", dryad="Dryad", params="Parâmetros", returns="Devolve", none="nada",
               name="Nome", type="Tipo", desc="Descrição", example="Exemplo", only_dryad="Só Dryad", last="Última atualização",
               how="Como esta página é construída", howtext="Gerada a partir do registo da API no repositório do engine; um teste falha quando um binding não está documentado. Não editar à mão.",
               group="Grupo", total="funções em", nss="namespaces", back="Todos os namespaces", missing="Lua: indisponível"),
}


def load():
    data = []
    errors = []
    for f in sorted(API.glob("*.json")):
        d = json.loads(f.read_text(encoding="utf-8"))
        for fn in d["functions"]:
            for k in ("name", "signature", "en", "pt", "example"):
                if not fn.get(k):
                    errors.append(f"{f.name}: {fn.get('name')}: missing {k}")
            if not (fn.get("lua") or fn.get("dryad")):
                errors.append(f"{f.name}: {fn.get('name')}: neither lua nor dryad")
            for p in fn.get("params", []):
                if not (p.get("en") and p.get("pt")):
                    errors.append(f"{f.name}: {fn['name']}: param {p.get('name')} lacks en/pt")
        for k in ("title", "summary"):
            if not (d[k].get("en") and d[k].get("pt")):
                errors.append(f"{f.name}: {k} lacks en/pt")
        data.append(d)
    return data, errors


def esc(s):
    return html.escape(s, quote=False)


def md_inline(s):
    return s.replace("|", "\\|")


def ns_page(d, lang):
    t = T[lang]
    n_lua = sum(1 for f in d["functions"] if f.get("lua"))
    n_dry = sum(1 for f in d["functions"] if f.get("dryad"))
    out = [f"# {d['title'][lang]}", "",
           '<div class="doc-meta">',
           '  <span class="doc-badge doc-badge--reference">Reference</span>',
           f'  <span class="doc-badge">{esc(d["group"])}</span>',
           f'  <span>{t["last"]}: <time datetime="{date.today()}">{date.today()}</time></span>',
           f'  <span>Lua {n_lua} · Dryad {n_dry}</span>', "</div>", "",
           f'<p class="doc-lede">{esc(d["summary"][lang])}</p>', "",
           f'[{t["back"]}](index.md)', ""]
    if d.get("lua"):
        out += [f"Namespace: `{d['lua']}`" + (f" · Dryad prefix: `{d['dryad_prefix']}`" if d.get("dryad_prefix") else ""), ""]
    for f in d["functions"]:
        title = f["signature"].split("(")[0] if f.get("lua") else (f.get("dryad") or f["name"])
        out += [f"## {title}", ""]
        names = []
        if f.get("lua"):
            names.append(f"{t['lua']}: `{f['lua']}`")
        else:
            names.append(t["missing"])
        if f.get("dryad"):
            names.append(f"{t['dryad']}: `{f['dryad']}`")
        out += [" · ".join(names), "", "```", f["signature"], "```", "", f[lang], ""]
        params = f.get("params", [])
        if params:
            out += [f"<table>", f"<thead><tr><th>{t['params']}</th><th>{t['type']}</th><th>{t['desc']}</th></tr></thead>", "<tbody>"]
            for p in params:
                out.append(f"<tr><td><code>{esc(p['name'])}</code></td><td>{esc(p['type'])}</td><td>{esc(p[lang])}</td></tr>")
            out += ["</tbody>", "</table>", ""]
        r = f.get("returns")
        out += [f"**{t['returns']}:** " + (f"`{r['type']}`. {r[lang]}" if r else t["none"]), ""]
        out += [f"**{t['example']}**", "", "```" + ("lua" if f.get("lua") else "dryad"), f["example"], "```", ""]
    return "\n".join(out)


def index_page(data, lang):
    t = T[lang]
    total = sum(len(d["functions"]) for d in data)
    out = [f"# {t['api']}", "",
           '<div class="doc-meta">', '  <span class="doc-badge doc-badge--reference">Reference</span>',
           f'  <span>{t["last"]}: <time datetime="{date.today()}">{date.today()}</time></span>', "</div>", "",
           f'<p class="doc-lede">{esc(t["lede"].replace("`", ""))}</p>', "",
           f"{total} {t['total']} {len(data)} {t['nss']}.", ""]
    groups = {}
    for d in data:
        groups.setdefault(d["group"], []).append(d)
    for g in sorted(groups, key=lambda x: GROUP_ORDER.index(x) if x in GROUP_ORDER else 99):
        out += [f"## {g}", "", f"| {t['ns']} | {t['fn']} | {t['desc']} |", "|---|---|---|"]
        for d in sorted(groups[g], key=lambda x: x["namespace"]):
            out.append(f"| [{d['title'][lang]}]({d['namespace']}.md) | {len(d['functions'])} | {md_inline(d['summary'][lang])} |")
        out.append("")
    out += [f"## {t['how']}", "", t["howtext"], ""]
    return "\n".join(out)


def main():
    data, errors = load()
    if errors:
        print("\n".join(errors))
        return 1
    if "--check" in sys.argv:
        print(f"ok: {sum(len(d['functions']) for d in data)} functions, {len(data)} namespaces")
        return 0
    for lang in ("en", "pt"):
        out = SITE / lang / "docs/scripting/api"
        out.mkdir(parents=True, exist_ok=True)
        (out / "index.md").write_text(index_page(data, lang), encoding="utf-8")
        for d in data:
            (out / f"{d['namespace']}.md").write_text(ns_page(d, lang), encoding="utf-8")
    print(f"generated {len(data)} namespaces x 2 languages")
    return 0


sys.exit(main())
