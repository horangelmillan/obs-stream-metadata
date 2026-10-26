/*
obs-stream-metadata — T-032 secure account storage.

Windows: sensitive fields (tokens, client IDs/secrets) are encrypted
with DPAPI (CryptProtectData, CurrentUser scope, UI forbidden) and
stored as base64 blobs in a JSON file under the module config dir.
Non-sensitive labels (display name, broadcaster id) stay plaintext.
Plaintext persistence of secrets is prohibited (SECURITY.md, ADR-008).

Non-Windows: persistence is refused (save/load return false); the
dock falls back to memory-only tokens. The MVP targets Windows.

No OBS dependency: the caller resolves the file path (e.g. via
obs_module_config_path) and passes it in.
*/

#pragma once

#include "metadata.h"

#include <QDateTime>
#include <QString>

namespace secure {

// T-051: Managed connection snapshot. Plaintext identity labels ONLY
// (mirrors what backend /status returns); structurally incapable of
// holding secrets (no blob, no DPAPI involved).
struct ManagedSnapshot {
	bool connected = false;
	QString userId;
	QString display;
};

// [D 2026-10-26 usuario]: sesion de usuario Managed persistida
// ("si entraste antes, al abrir OBS vuelve directo al panel; solo pide
// login si vencio el tiempo o cerraste sesion"). SUSTITUYE spec §5
// memoria-solo. BLOB CIFRADO DPAPI como backendInstall (jamas en claro,
// incluida la etiqueta username); ligado a backendBaseUrl (fail-closed
// ante mismatch, igual que loadInstallation). Los snapshots Managed
// siguen en claro porque no son secretos; la sesion SI lo es. TTL lo
// manda el backend: ventana deslizante de 15 d renovada por actividad +
// tope absoluto de 30 d desde el login (contrato
// backend/tests/test_users_session_sliding.py); expirada = login de nuevo.
struct ManagedUserSession {
	bool loggedIn = false;
	QString username; // etiqueta "Conectado como" (dentro del blob)
	QString token;    // user_session opaco (jamas en claro)
	QDateTime expiry; // UTC (ventana deslizante, la renueva el backend)
	QDateTime loginAt; // UTC: origen del tope de 30 d (sesiones
			   // heredadas sin el pueden traerlo invalido:
			   // entonces el tope lo aplica el servidor)
};

struct Record {
	QString display;     // non-sensitive label ("Connected as")
	QString broadcaster; // non-sensitive Twitch user_id (may be empty)
	QString access;      // sensitive, DPAPI blob
	QString refresh;     // sensitive, DPAPI blob
	QString clientId;    // sensitive, DPAPI blob
	QString secret;      // sensitive, DPAPI blob (may be empty: Twitch)
	bool connected = false;
};

struct Data {
	Record twitch;
	Record youtube;
	Record kick;
	// T-071 FB-2: cuenta Independent Facebook BYO-app (DPAPI igual que
	// YT/Kick: App ID + tokens; secret opcional para long-lived).
	Record facebook;
	// T-044: backend installation identity. clientId = installation_id,
	// secret = installation_secret (both DPAPI blobs); access stays empty.
	// Never a provider credential; never shown in UI.
	Record backendInstall;
	// T-041: connection mode, canonical wire string ("independent" /
	// "managed", see meta::parseConnectionMode). Plaintext: not a secret,
	// only a context. Empty = legacy file without mode.
	QString connectionMode;
	// T-051: Managed snapshots (plaintext identity labels, see below).
	// FASE 2 adds Twitch Managed (same shape, key "managed_twitch").
	ManagedSnapshot managedYoutube;
	ManagedSnapshot managedKick;
	ManagedSnapshot managedTwitch;
	// FB-3 (T-072): Managed Facebook, misma forma (solo id+display).
	ManagedSnapshot managedFacebook;
	// T-073 FB-4: LiveVideo creado por el dock (POST /me/live_videos,
	// F-075 ID-centrico). Plaintext ID no-sensible (como userId/display):
	// sobrevive a Disconnect/restart; se limpia con DELETE.
	QString facebookLiveId;
	// [D 2026-10-26]: sesion de usuario Managed (ver ManagedUserSession).
	// Solo valida junto a backendBaseUrl (fail-closed); se limpia en
	// logout/401/expirada. No afecta a load(): es contexto, no proveedor.
	ManagedUserSession managedUser;
	// T-053: backend this installation belongs to (explicit environment
	// binding). Plaintext URL, not a secret; empty = legacy file without
	// binding (treated as mismatch → re-bootstrap, never cross-backend
	// reuse). Never a ConnectionMode; DEV/PROD live here, not in the mode.
	QString backendBaseUrl;
	bool anyConnected() const
	{
		return twitch.connected || youtube.connected ||
		       kick.connected || facebook.connected;
	}
	bool anyManaged() const
	{
		return managedYoutube.connected || managedKick.connected ||
		       managedTwitch.connected || managedFacebook.connected;
	}
};

class Store {
public:
	explicit Store(const QString &filePath);
	bool save(const Data &d); // false on any failure (nothing partial)
	bool load(Data &d);       // false = no usable file (fresh start)
	bool clear();             // true when no file remains

private:
	QString filePath_;
};

// T-053: installation↔backend binding. Fail-closed: only a non-empty
// stored URL exactly equal to the configured one matches. Legacy/empty
// stored URLs never match (caller re-bootstraps against the right
// backend instead of reusing a foreign installation secret).
bool installationUrlMatches(const QString &stored, const QString &current);

} // namespace secure
