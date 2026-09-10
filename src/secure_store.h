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

#include <QString>

namespace secure {

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
	// T-044: backend installation identity. clientId = installation_id,
	// secret = installation_secret (both DPAPI blobs); access stays empty.
	// Never a provider credential; never shown in UI.
	Record backendInstall;
	// T-041: connection mode, canonical wire string ("independent" /
	// "managed", see meta::parseConnectionMode). Plaintext: not a secret,
	// only a context. Empty = legacy file without mode.
	QString connectionMode;
	bool anyConnected() const
	{
		return twitch.connected || youtube.connected ||
		       kick.connected;
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

} // namespace secure
