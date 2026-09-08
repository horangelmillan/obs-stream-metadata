# ADR-002 — GitHub-flow público con PRs desde Phase 0

- **Problema:** integrar trabajo de múltiples agentes con control y revisión.
- **Opciones:** (a) commits directos a master; (b) GitHub-flow con PR + CI + revisión.
- **Elección:** (b) repo público `horangelmillan/obs-stream-metadata`, PR obligatoria, squash-merge, CI verde + aprobación del usuario.
- **Por qué:** decisión del usuario (2026-09-08, repo público, acepta PRs); skill `git-workflow`/`github-actions` lo respaldan.
- **Evidencia:** `gh auth` ok (`horangelmillan`); scopes `repo, workflow` disponibles.
- **Consecuencias:** más disciplina por cambio; prohibidos force-push / reset --hard sin autorización; rollback vía `revert`.
