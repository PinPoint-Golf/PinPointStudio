import sys, markdown, re
src, out = sys.argv[1:3]
body = markdown.markdown(open(src, encoding="utf-8").read(), extensions=["tables", "fenced_code", "toc"])
# point .md links at their rendered .html siblings
body = re.sub(r'href="([^"#:]+)\.md(#[^"]*)?"', r'href="\1.html\2"', body)
css = """body{font:15px/1.55 -apple-system,Helvetica,Arial,sans-serif;max-width:1100px;margin:2rem auto;padding:0 1.2rem;color:#1d1d1f;background:#fff}
h1,h2,h3{line-height:1.25}h2{border-bottom:1px solid #ddd;padding-bottom:.25rem;margin-top:2.2rem}
table{border-collapse:collapse;width:100%;margin:1rem 0;font-size:13.5px}th,td{border:1px solid #d6d6d6;padding:5px 8px;vertical-align:top;text-align:left}
th{background:#f3f3f5}tr:nth-child(even) td{background:#fafafb}code{background:#f0f0f3;padding:1px 4px;border-radius:3px;font-size:90%}
blockquote{border-left:3px solid #ccc;margin:.6rem 0;padding:.1rem .9rem;color:#444;background:#fafafa}
@media(prefers-color-scheme:dark){body{background:#1c1c1e;color:#e5e5e7}th{background:#2a2a2d}tr:nth-child(even) td{background:#222225}th,td{border-color:#444}code{background:#2c2c2f}blockquote{background:#232325;color:#ccc;border-color:#555}h2{border-color:#444}a{color:#6cb6ff}}"""
open(out, "w", encoding="utf-8").write(f"<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>{src.split('/')[-1]}</title><style>{css}</style>{body}")
