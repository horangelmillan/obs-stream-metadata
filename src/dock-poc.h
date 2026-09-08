/*
obs-stream-metadata — Stream Metadata dock PoC (T-020)
Interfaz C para el ciclo de vida del dock. La implementación Qt vive en
dock-poc.cpp; este header es consumible desde plugin-main.c (C99).
*/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Crea el contenido del dock y lo registra en OBS.
 * Devuelve true si OBS aceptó el dock. */
bool stream_metadata_dock_create(void);

/* Desregistra el dock de OBS. Nunca toca el QWidget: OBS es su dueño
 * (ver OBSStudioAPI::obs_frontend_add_dock_by_id, OBS 32.2.2). */
void stream_metadata_dock_destroy(void);

#ifdef __cplusplus
}
#endif
