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

	// Fase 4: cuenta de usuario Managed (spec §§4–5). La sesión de
	// usuario vive SOLO en memoria (jamás en accounts.json/DPAPI; al
	// reiniciar OBS se revalida vía me() o se pide login). Ningún
	// token/secreto llega a la UI ni a logs: solo etiquetas
	// (username/display). Rutas backend Fase 2: POST /users/signup,
	// POST /users/login, POST /users/logout, GET /users/me.
	struct UserInfo {
		QString userId;
		QString username;
	};
	// Alta v1 (username + password, sin email). No abre sesión: tras
	// 201 hay que llamar a login(). Lleva bearer de instalación si lo
	// hay (adopción best-effort); nunca X-User-Session.
	void signup(const QString &username, const QString &password,
		    std::function<void(Result, const UserInfo &)> cb);
	// Emite sesión de usuario en memoria. Asegura antes la sesión de
	// instalación (adopción sin reset). 401 normalizado sin distinguir
	// usuario/contraseña.
	void login(const QString &username, const QString &password,
		   std::function<void(Result)> cb);
	// Revoca la sesión de usuario (200 idempotente) y la olvida.
	// Sin sesión: Ok sin red.
	void logoutUser(std::function<void(Result)> cb);
	// Revalida la sesión en memoria (GET /users/me). Sin sesión: no
	// hay red (Unauthorized). 401 remoto: olvida la sesión local.
	void me(std::function<void(Result, const UserInfo &)> cb);
	bool hasUserSession() const;
	// Etiqueta no sensible para la UI ("Conectado como").
	QString userLabel() const { return loggedUsername_; }
	void clearUserSession();

	// T-048: llamada autenticada contra el backend (asegura sesión
	// primero). Nunca expone el token: viaja solo como header Bearer.
	// Respuesta: resultado + HTTP + cuerpo (cuerpo sin secretos por
	// contrato; el llamador solo lee campos de UX/estado).
	struct ApiReply {
		Result result;
		int http = 0;
		QJsonObject body;
	};
	void apiGet(const QString &path,
		    std::function<void(const ApiReply &)> cb);
	void apiPost(const QString &path, const QJsonObject &body,
		     std::function<void(const ApiReply &)> cb);
	// Fase 4: llamadas Managed con auth doble (instalación + usuario).
	// Sin user-session en memoria: callback Unauthorized INMEDIATO,
	// sin red ni fallback a Independent. Con sesión: bearer de
	// instalación + header X-User-Session. Un 401 remoto olvida AMBAS
	// sesiones (el backend normaliza; re-login decide el dock).
	void apiGetUser(const QString &path,
			std::function<void(const ApiReply &)> cb);
	void apiPostUser(const QString &path, const QJsonObject &body,
			 std::function<void(const ApiReply &)> cb);

private:
	QJsonObject signedBody(const QString &installationId,
			       const QString &secret) const;
	void post(const QString &path, const QJsonObject &body,
		  std::function<void(Result, const QJsonObject &)> cb);
	void apiSend(const QString &verb, const QString &path,
		     const QJsonObject &body,
		     std::function<void(const ApiReply &)> cb);
	// Fase 4: POST público con bearer de instalación best-effort
	// (adopción); GET/POST con X-User-Session (+ bearer best-effort);
	// auth doble con gate previo sin red. Sin valores en logs.
	void postPublic(const QString &path, const QJsonObject &body,
			std::function<void(Result, const QJsonObject &)> cb);
	void userSend(const QString &verb, const QString &path,
		      const QJsonObject &body,
		      std::function<void(const ApiReply &)> cb);
	void apiSendUser(const QString &verb, const QString &path,
			 const QJsonObject &body,
			 std::function<void(const ApiReply &)> cb);
	bool persistInstallation(const QString &id, const QString &secret);
	bool loadInstallation(QString &id, QString &secret);
	bool storeSession(const QJsonObject &o);
	void freshSession(std::function<void(Result)> cb);

	QNetworkAccessManager *nam_;
	QString baseUrl_;
	secure::Store *store_ = nullptr;
	QString sessionToken_;
	QDateTime sessionExpiry_;
	// Fase 4: sesión de usuario, SOLO memoria (jamás Store/DPAPI).
	// userExpiry_ con el mismo margen que la sesión de instalación.
	QString userSessionToken_;
	QString loggedUsername_; // etiqueta no sensible ("Conectado como")
	QDateTime userExpiry_;
};

} // namespace backend_auth
