# GIT — flujo Git + GitHub

- Remoto: `horangelmillan/obs-stream-metadata` (público). Base: `master`.
- Modelo: GitHub-flow. Ramas cortas desde `master`: `feat/<T-id>-slug`, `docs/<slug>`, `chore/<slug>`, `exp/<slug>`.
- Commits: Conventional Commits (`feat|fix|docs|chore|refactor|test`), atómicos, un cambio lógico por commit. Idioma: español (coherente con docs).
- PRs obligatorios desde Phase 0. Template `.github/pull_request_template.md`. Merge con squash salvo excepción. CI verde + 1 aprobación (usuario) antes de merge.
- Antes de editar: `git status --short --branch`, `git log --oneline -5`, leer archivo actual. No sobrescribir trabajo ajeno.
- PROHIBIDO sin autorización explícita: `reset --hard`, `clean -fd`, borrado masivo, rebase destructivo, force-push, commits con secretos.
- Rollback: `revert` antes que reescribir historia. Experimental en `exp/*`; parcial/WIP no se mergea a `master`.
