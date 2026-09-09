/*
 obs-stream-metadata — T-044 plugin-side backend auth client (no UI).

 Speaks the ADR-010 contract: anonymous bootstrap → per-installation
 secret (DPAPI via secure::Store, backendInstall record) → HMAC-signed
 session/refresh → short-lived opaque bearer in memory only.

 All networking is async (QNAM signals, same pattern as metadata_dock,
 F-025): never blocks the OBS UI thread. No values ever logged.
 Wiring to the dock UI belongs to T-048; this class has no widgets.
*/

#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

#include <functional>

namespace secure {
class Store;
struct Data;
} // namespace secure

namespace backend_auth {

enum class Result {
	Ok,
	NetworkError, // timeout / unreachable / TLS failure
	Unauthorized, // 401: bad signature, revoked, expired (caller re-auths)
	RateLimited, // 429: back off, do not retry aggressively
	ServerError, // 5xx / unexpected
	InvalidResponse, // malformed backend reply
	StorageError, // DPAPI/file persistence failed
};

QString hmacSignature(const QString &secret, const QString &installationId,
		      qint64 timestamp, const QString &nonce);
QString makeNonce();

class Client : public QObject {
	Q_OBJECT
public:
	explicit Client(QNetworkAccessManager *nam, QObject *parent = nullptr);

	void setBaseUrl(const QString &baseUrl); // e.g. https://host (no trailing /)
	void setStore(secure::Store *store); // not owned; persists backendInstall

	// Bootstrap a fresh installation. Persists identity via store.
	// Callback receives Ok only when identity is safely stored.
	void bootstrap(std::function<void(Result)> cb);

	// Ensure a valid bearer session (memory only). Refreshes or
	// re-authenticates with the stored installation secret as needed.
	void ensureSession(std::function<void(Result)> cb);

	// Revoke current session (best-effort) and forget it.
	void revokeSession(std::function<void(Result)> cb);

	// Revoke the whole installation (all sessions). Keeps local identity
	// so a fresh bootstrap can re-provision; T-048 decides UX.
	void revokeInstallation(std::function<void(Result)> cb);

	QString sessionToken() const { return sessionToken_; }
	bool hasValidSession() const;

private:
	QJsonObject signedBody(const QString &installationId,
			       const QString &secret) const;
	void post(const QString &path, const QJsonObject &body,
		  std::function<void(Result, const QJsonObject &)> cb);
	bool persistInstallation(const QString &id, const QString &secret);
	bool loadInstallation(QString &id, QString &secret);
	bool storeSession(const QJsonObject &o);
	void freshSession(std::function<void(Result)> cb);

	QNetworkAccessManager *nam_;
	QString baseUrl_;
	secure::Store *store_ = nullptr;
	QString sessionToken_;
	QDateTime sessionExpiry_;
};

} // namespace backend_auth
