# 005 — Nuevas plataformas: Facebook, TikTok, X (Twitter) + streaming

## Contexto

2026-09-19: con Twitch/YouTube/Kick cerrados (metadata), el operador fija
como siguientes objetivos Facebook, TikTok y, si es posible, X (Twitter),
añadiendo además capacidad de streaming. Idea del operador.

## Problema observado

Cada plataforma tiene OAuth, permisos y concepto de metadata distintos
(la lección de F-C2/YouTube-broadcast vale por tres). Y el "streaming"
choca con el alcance vigente: multistream es no-objetivo explícito
(`AGENTS.md` §51) y el plugin es independiente de Aitum por diseño.

## Propuesta

- **Una plataforma cada vez**, en este orden: Facebook → TikTok → X.
  Cada una se lleva hasta **lista para distribuir comercialmente**
  antes de empezar la siguiente (misma regla 1-objetivo-1-evidencia
  de FASES-COMERCIAL).
- Por plataforma, fase previa de investigación (sin código): flujo
  OAuth para app distribuida, endpoint real de título/descripción,
  límites, scopes mínimos, revocación y verificación — igual que se
  hizo con Twitch/YouTube/Kick.
- El alcance "streaming" requiere decisión previa: ¿metadata también
  para las nuevas, o se abre multistream (cambio de alcance mayor,
  con implicaciones en Aitum-independencia, RTMP/keys y backend)?

## Incógnitas conocidas (a verificar en su fase, no ahora)

- Facebook: estado del Live API para apps de terceros.
- TikTok: acceso a su API (proceso de aprobación) y operaciones de
  metadata disponibles.
- X: tiers de pago de la API y su coste para un indie.

## Estado

`aceptada→BACKLOG` parcial 2026-09-20: solo Facebook (T-069…T-073, plan en
`docs/superpowers/plans/2026-09-20-facebook-metadata-livestatus.md`);
TikTok/X siguen en `idea`, fuera de este ciclo. Alcance: metadata +
estado on/off investigado (sin multistream/RTMP, §51).
