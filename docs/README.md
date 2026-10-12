# C++ API docs

Static reader for GitHub Pages, generated from core headers with hosted-model explanations.

- Generator: `docs/gen_ai.py` (stdlib only)
- Cache: `docs/ai-cache.json`
- The `ai-docs` workflow runs on the default branch: reads code there, writes the cache here, deploys Pages.

```bash
python3 docs/gen_ai.py --dry-run --limit 50
GITHUB_TOKEN=... python3 docs/gen_ai.py --limit 20
```
