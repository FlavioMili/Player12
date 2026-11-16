#!/usr/bin/env python3
"""Render maintained project guides into the static website checkout.

Run against main's source tree and a separate website worktree. This only
refreshes documentation and licence text; marketing HTML,
CSS, JavaScript and game videos stay owned by the website branch.
"""
from __future__ import annotations

import argparse
import html
from html.parser import HTMLParser
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import textwrap
import xml.etree.ElementTree as ET
from urllib.parse import quote, unquote, urlsplit

import markdown

REPOSITORY = "https://github.com/FlavioMili/FootballManagement"
GUIDES = [
    ("CONTRIBUTING.md", "docs/contributing.html", "Contribute", "First contribution", "Choose a small change, set up and open your first pull request."),
    ("CONTRIBUTORS.md", "docs/contributors.html", "Contribute", "Contributor credits", "Meet the contributors and add your name or handle."),
    ("docs/contributing/player-cameo.md", "docs/contributing/player-cameo.html", "Contribute", "Your player cameo", "Add a fictional player with your name at your favorite club."),
    ("AI_GUIDELINES.md", "docs/ai-guidelines.html", "Contribute", "AI guidelines", "Declare assistance and review the work you submit."),
    ("CODE_OF_CONDUCT.md", "docs/code-of-conduct.html", "Contribute", "Code of conduct", "A welcoming space for issues, contributions and reviews."),
    ("docs/user/installation.md", "docs/user/installation.html", "Play", "Install and run", "Dependencies, commands and setup for each desktop platform."),
    ("docs/user/getting-started.md", "docs/user/getting-started.html", "Play", "Your first career", "From choosing a club to getting through match day."),
    ("docs/user/controls.md", "docs/user/controls.html", "Play", "Controls", "Keyboard, mouse and gamepad shortcuts for the game."),
    ("docs/user/files-and-troubleshooting.md", "docs/user/files-and-troubleshooting.html", "Play", "Files and troubleshooting", "Find saves and logs, restore backups and report a problem."),
    ("docs/project/features.md", "docs/project/features.html", "Play", "Feature tour", "Explore the career, competitions and full game feature list."),
    ("docs/project/roadmap.md", "docs/project/roadmap.html", "Play", "Roadmap", "Where the project can grow next."),
    ("CHANGELOG.md", "docs/changelog.html", "Play", "Changelog", "Release details and known limitations."),
    ("docs/development/README.md", "docs/development/README.html", "Develop", "Developer guide", "Find the right implementation and follow the data flow."),
    ("docs/ARCHITECTURE.md", "docs/ARCHITECTURE.html", "Develop", "Architecture", "Core, controller, persistence, scheduling and GUI boundaries."),
    ("docs/development/match-engine.md", "docs/development/match-engine.html", "Develop", "Match engine", "Simulation steps, physical units, rules, replay and scheduling."),
    ("docs/development/player-behavior.md", "docs/development/player-behavior.html", "Develop", "Player behavior", "Roles, movement, passing decisions, defending and goalkeeping."),
    ("docs/development/extending-and-modding.md", "docs/development/extending-and-modding.html", "Develop", "Extending and modding", "Working JSON edits, C++ extension points and validation."),
    ("assets/user_made_data/README.md", "docs/data-format.html", "Develop", "World data format", "Club, league, player and name-pool fields."),
    ("docs/development/builds.md", "docs/development/builds.html", "Develop", "Builds", "Presets, compilers, sanitizers, lint and dependencies."),
    ("docs/development/testing.md", "docs/development/testing.html", "Develop", "Tests and tools", "Focused checks, balance experiments and benchmarks."),
    ("docs/development/release.md", "docs/development/release.html", "Develop", "Release and packaging", "Desktop packages, installation and validation."),
    ("docs/development/website.md", "docs/development/website.html", "Develop", "Website maintenance", "Render guides, preserve readable diagrams and verify both themes."),
    ("docs/development/design-notes.md", "docs/development/design-notes.html", "Develop", "Design history", "Verified rationale recovered from the Spec Kitty archive."),
]


def relative(target: Path, current: Path) -> str:
    return Path(os.path.relpath(target, current.parent)).as_posix()


def navigation(site: Path, current: Path, name: str) -> str:
    def url(path: str) -> str:
        return relative(site / path, current)
    initials = "12" if name == "Player12" else "".join(word[0] for word in name.split() if word[0].isalpha())[:2]
    return f'''<a class="skip-link" href="#main">Skip to content</a>
<header class="site-header"><nav class="container nav" aria-label="Main navigation">
<a class="brand" href="{url('index.html')}"><span class="brand-mark" aria-hidden="true">{html.escape(initials)}</span><span>{html.escape(name)}</span></a>
<button class="menu-toggle" type="button" aria-controls="navigation" aria-expanded="false">Menu</button>
<div class="nav-links" id="navigation"><a href="{url('index.html')}">The game</a><a href="{url('gallery.html')}">Gallery</a><a href="{url('docs/index.html')}" aria-current="page">Docs</a><a href="{url('contribute.html')}">Contribute</a><a class="nav-source" href="{REPOSITORY}">Source code ↗</a><button class="theme-toggle" type="button" aria-label="Switch to dark theme"><span aria-hidden="true">☾</span></button></div></nav></header>'''


def document(site: Path, path: Path, title: str, description: str, body: str, name: str) -> str:
    def url(target: str) -> str:
        return relative(site / target, path)
    return f'''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>{html.escape(title)} · {html.escape(name)}</title><meta name="description" content="{html.escape(description, quote=True)}"><meta name="theme-color" content="#eceef1"><link rel="icon" href="{url('favicon.svg')}" type="image/svg+xml"><link rel="stylesheet" href="{url('css/styles.css')}"><script src="{url('js/theme.js')}"></script><script src="{url('js/site.js')}" defer></script></head><body>{navigation(site, path, name)}<main id="main">{body}</main><footer class="site-footer"><div class="container footer-content"><p>{html.escape(name)} · Open source under GNU GPL v3</p><div class="footer-links"><a href="{url('docs/index.html')}">All guides</a><a href="{url('docs/contributing.html')}">Contribute</a><a href="{url('LICENSE')}">Licence</a></div></div></footer></body></html>'''


class RewriteLinks(HTMLParser):
    """Keep guide links local; send code references to their GitHub source."""
    def __init__(self, source: Path, root: Path, output: Path, site: Path, mapping: dict[Path, Path]):
        super().__init__(convert_charrefs=False)
        self.source, self.root, self.output, self.site, self.mapping = source, root, output, site, mapping
        self.parts: list[str] = []

    def target(self, value: str) -> str:
        parsed = urlsplit(value)
        if parsed.scheme or parsed.netloc or not parsed.path:
            return value
        source_target = (self.source.parent / unquote(parsed.path)).resolve()
        suffix = ("?" + parsed.query if parsed.query else "") + ("#" + parsed.fragment if parsed.fragment else "")
        if source_target in self.mapping:
            return relative(self.mapping[source_target], self.output) + suffix
        try:
            path = source_target.relative_to(self.root)
        except ValueError:
            return value
        if path.parts[:2] == ("docs", "images"):
            return relative(self.site / "media/game" / (path.stem + ".webp"), self.output) + suffix
        kind = "tree" if source_target.is_dir() else "blob"
        return f"{REPOSITORY}/{kind}/main/{quote(path.as_posix(), safe='/')}" + suffix

    def handle_starttag(self, tag, attrs):
        attrs = [(key, self.target(value) if key in ("href", "src") and value else value) for key, value in attrs]
        self.parts.append("<" + tag + "".join(" " + key + (f'="{html.escape(value, quote=True)}"' if value is not None else "") for key, value in attrs) + ">")
    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)
    def handle_endtag(self, tag):
        self.parts.append(f"</{tag}>")
    def handle_data(self, value):
        self.parts.append(value)
    def handle_entityref(self, value):
        self.parts.append(f"&{value};")
    def handle_charref(self, value):
        self.parts.append(f"&#{value};")
    def handle_comment(self, value):
        self.parts.append(f"<!--{value}-->")


def normalize_lists(text: str) -> str:
    """Allow a prose paragraph to introduce a list without merging its items.

    Python Markdown requires a blank before a list. Only repair top-level list
    boundaries; indentation and fenced examples keep their original meaning.
    """
    lines = []
    fence = None
    for line in text.splitlines():
        marker = re.match(r"^\s{0,3}(`{3,}|~{3,})", line)
        if marker:
            token = marker.group(1)
            if fence is None:
                fence = token
            elif token[0] == fence[0] and len(token) >= len(fence):
                fence = None
            lines.append(line)
            continue
        if fence is None and re.match(r"^(?:[-+*] |\d+[.)] )", line) and lines:
            previous = lines[-1]
            # Continuations/nested items must remain inside their current list.
            if previous.strip() and not re.match(r"^(?:\s|[-+*] |\d+[.)] )", previous):
                lines.append("")
        lines.append(line)
    return "\n".join(lines) + "\n"


def diagram_parts(code: str) -> tuple[dict[str, str], list[tuple[str, str]]]:
    """Read our flowchart subset in one pass, including malformed input."""
    nodes = {}
    edges = []
    last_node = None
    source = None
    # Each token can match independently: an identifier never backtracks to
    # find a trailing bracket or arrow. Label scans also stop at the next '['.
    for token in re.finditer(r"\[([^\[\]]*)\]|(\w+)|(-->)", code):
        label, node, arrow = token.groups()
        if label is not None and last_node is not None:
            nodes[last_node] = label
        elif arrow:
            source = last_node
        elif node:
            last_node = node
            if source is not None:
                edges.append((source, node))
                source = None
    return nodes, edges


def theme_diagram(rendered: str, label: str) -> str:
    svg = ET.fromstring(rendered)
    ET.register_namespace("", "http://www.w3.org/2000/svg")
    width = float(svg.attrib["viewBox"].split()[2]) * 96 / 72
    svg.set("style", f"--diagram-width:{width:.1f}px")
    svg.set("role", "img")
    svg.set("aria-label", label)
    colors = {
        "#f6f7f9": "var(--surface, #f6f7f9)",
        "#21a663": "var(--accent, #087b43)",
        "#14171c": "var(--ink, #14171c)",
    }
    for element in svg.iter():
        for attr in ("fill", "stroke"):
            if element.get(attr) in colors:
                element.set(attr, colors[element.get(attr)])
    return ET.tostring(svg, encoding="unicode")


def diagrams(text: str) -> tuple[str, dict[str, str]]:
    replacements: dict[str, str] = {}
    def convert(match):
        code = match.group(1)
        nodes, edges = diagram_parts(code)
        if not nodes or not edges or not shutil.which("dot"):
            return match.group(0)
        rank = "LR" if "flowchart LR" in code else "TB"
        # Graphviz sizes are points; 12pt becomes 16 CSS pixels at the SVG's
        # natural 96dpi browser size. Keep that scale consistent across flows.
        dot = 'digraph G { graph [bgcolor="transparent", rankdir=' + rank + ', pad="0.2", nodesep="0.35", ranksep="0.5"]; node [shape=box, style="rounded,filled", fillcolor="#f6f7f9", color="#21a663", fontcolor="#14171c", fontname="sans-serif", fontsize=12, margin="0.2,0.14"]; edge [color="#21a663"];'
        dot += "".join(f'{key} [label={json.dumps(textwrap.fill(label, width=24, break_long_words=False, break_on_hyphens=False))}];' for key, label in nodes.items())
        dot += "".join(f"{a} -> {b};" for a, b in edges) + "}"
        rendered = subprocess.run(["dot", "-Tsvg"], input=dot, text=True, check=True, capture_output=True).stdout
        token = f"WEBSITEDIAGRAM{len(replacements)}"
        # Inline SVG inherits the page theme. Preserve its natural size inside
        # a scroll viewport so long flows never reduce labels to unreadable text.
        label = "Flow diagram: " + "; ".join(nodes.values())
        inline_svg = theme_diagram(rendered, label)
        replacements[token] = f'<figure class="diagram"><div class="diagram-viewport" tabindex="0" role="region" aria-label="{html.escape(label, quote=True)}">{inline_svg}</div><figcaption>Scroll horizontally to follow wider diagrams.</figcaption><details><summary>View diagram source</summary><pre><code>{html.escape(code)}</code></pre></details></figure>'
        return "\n\n" + token + "\n\n"
    return re.sub(r"```mermaid\n(.*?)\n```", convert, text, flags=re.S), replacements


def contained_path(root: Path, route: str) -> Path:
    """Reject absolute paths, traversal and symlinks out of a checkout."""
    path = (root / route).resolve()
    if Path(route).is_absolute() or not path.is_relative_to(root) or path == root:
        raise ValueError(f"Path escapes checkout: {route}")
    return path


def write_site_text(site: Path, route: str, content: str) -> None:
    """Revalidate every generated text destination at the filesystem sink."""
    output = contained_path(site, route)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(content)


def sidebar(site: Path, output: Path, destination: str) -> str:
    parts = []
    for section in ("Play", "Develop", "Contribute"):
        parts.append(f"<h2>{section}</h2>")
        for _, route, category, label, _ in GUIDES:
            if category == section:
                current = ' aria-current="page"' if route == destination else ''
                parts.append(f'<a href="{relative(contained_path(site, route), output)}"{current}>{html.escape(label)}</a>')
    return "".join(parts)


def guide_content(source: Path, root: Path, output: Path, site: Path, mapping: dict[Path, Path]) -> tuple[str, str]:
    text, replacements = diagrams(normalize_lists(source.read_text()))
    renderer = markdown.Markdown(extensions=["tables", "fenced_code", "toc", "sane_lists"], extension_configs={"toc": {"permalink": False}})
    rewrite = RewriteLinks(source, root, output, site, mapping)
    rewrite.feed(renderer.convert(text))
    content = "".join(rewrite.parts)
    for token, replacement in replacements.items():
        content = content.replace(f"<p>{token}</p>", replacement)
    content = content.replace("<table>", '<div class="table-wrap" tabindex="0" role="region" aria-label="Scrollable data table"><table>').replace("</table>", "</table></div>")
    return content, renderer.toc


def build(root: Path, site: Path) -> None:
    root, site = root.resolve(), site.resolve()
    if root.is_relative_to(site) or site.is_relative_to(root):
        raise ValueError("Use a separate website checkout, not the source tree")
    name = re.search(r"^# (.+)$", contained_path(root, "README.md").read_text(), re.M).group(1)
    mapping = {contained_path(root, source): contained_path(site, output) for source, output, *_ in GUIDES}
    index = contained_path(site, "docs/index.html")
    license_output = contained_path(site, "LICENSE")
    site.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(contained_path(root, "LICENSE"), license_output)
    for source_name, destination, group, title, description in GUIDES:
        source, output = contained_path(root, source_name), contained_path(site, destination)
        content, outline = guide_content(source, root, output, site, mapping)
        navigation_links = sidebar(site, output, destination)
        edit_url = f"{REPOSITORY}/edit/main/{quote(source_name, safe='/')}"
        body = f'<div class="container doc-layout"><aside class="doc-sidebar" aria-label="Documentation navigation">{navigation_links}</aside><article class="doc-article"><p class="doc-meta">{group} / {html.escape(title)} · <a href="{relative(index, output)}">All guides</a> · <a href="{edit_url}">Improve this page ↗</a></p><details class="doc-outline"><summary>On this page</summary>{outline}</details>{content}</article></div>'
        write_site_text(
            site, destination, document(site, output, title, description, body, name)
        )
    cards = "".join(f'<a class="doc-card" data-search="{html.escape((group + " " + title + " " + description).lower(), quote=True)}" href="{relative(site / destination, site / "docs/index.html")}"><h2>{html.escape(title)}</h2><p>{html.escape(description)}</p><span>{group} ↗</span></a>' for _, destination, group, title, description in GUIDES)
    api = '<p><a class="text-link" href="../api/index.html">Browse the generated C++ API reference ↗</a></p>' if (site / "api/index.html").is_file() else ""
    body = f'<div class="container"><header class="page-intro"><p class="eyebrow">Play. Understand. Make it yours.</p><h1>A guide for<br>your next step.</h1><p>Start a career, find your first contribution or follow a decision through the match engine. These guides come from the maintained project documentation.</p></header><section class="section" style="padding-top:0"><label for="docs-search">Find a guide</label><br><input class="search-field" id="docs-search" type="search" placeholder="Try match, player, build or contribute" autocomplete="off"><p class="search-count" id="search-count" aria-live="polite">{len(GUIDES)} guides</p><div class="doc-cards">{cards}</div>{api}</section></div>'
    write_site_text(
        site,
        "docs/index.html",
        document(site, index, "Documentation", "Player, developer and contributor guides for the game.", body, name),
    )
    print(f"Rendered {len(GUIDES)} guides and the searchable documentation index")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--site", type=Path, required=True)
    args = parser.parse_args()
    build(args.source.resolve(), args.site.resolve())
