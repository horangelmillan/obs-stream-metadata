/*
 obs-stream-metadata — T-044 plugin-side backend auth client (no UI).

 Speaks the ADR-010 contract: anonymous bootstrap → per-installation
 secret (DPAPI via secure::Store, backendInstall record) → HMAC-signed
 session/refresh → short-lived opaque bearer in memory only. The Managed
 user session ([D 2026-10-26]) additionally persists encrypted
 (managedUser record, same DPAPI + backend binding) for direct-to-panel
 reopen; it is wiped on logout/401/expiry.

 All networking is async (QNAM signals, same pattern as metadata_dock,
 F-025): never blocks the OBS UI thread. No values ever logged.
 Wiring to the dock UI belongs to T-048; this class has no widgets.
*/

#pragma once

#include "metadata.h"

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
	void setStore(secure::Store *store); // not owned; persists backendInstall + managedUser

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

	// Fase 4: cuenta de usuario Managed (spec §§4–5, regimen de sesion
	// [D 2026-10-26 usuario] que SUSTITUYE spec §5 memoria-solo: "si
	// entraste antes, al abrir OBS vuelve directo al panel; solo pide
	// login si vencio el tiempo o cerraste sesion"). La sesion de usuario
	// persiste CIFRADA (DPAPI, registro managedUser ligado a backendBaseUrl,
	// igual que backendInstall; jamas en claro); al reiniciar OBS el dock
	// la restaura con tryRestoreUserSession() y la revalida con me().
	// Ningun token/secreto llega a la UI ni a logs: solo etiquetas
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
	// [D 2026-10-26]: restaura la sesion persistida (DPAPI) a memoria.
	// Lee el store: si hay token no expirado Y ligado a este backendBaseUrl
	// (fail-closed ante mismatch, igual que loadInstallation), lo carga a
	// memoria y devuelve Restored. NO valida red: eso lo hace el dock con
	// me(). Expirada = Expired (limpia el registro); mismatch = Mismatch
	// (limpia el registro: el secreto ajeno jamas se entrega); sin store,
	// sin baseUrl o sin registro = NoSession. El dock traduce cada caso
	// a mensaje de persona (meta::userReloginMessage) + obs_log con
	// codigo; jamas se pide al usuario buscar logs.
	enum class RestoreResult { Restored, NoSession, Expired, Mismatch };
	RestoreResult tryRestoreUserSession();
	// Limpia memoria Y registro persistido (logout/401/expirada). Los
	// datos del motivo (loginAt, expiracion, ultimo codigo 401) se
	// conservan para userReloginReason(): describen la sesion que
	// termino; los borra el proximo login/restore.
	void clearUserSession();
	// Motivo del fin de la sesion de usuario para la UI (lenguaje de
	// persona via meta::userReloginMessage) y obs_log con codigo
	// (meta::userReloginReasonName, sin secretos). Se calcula con el
	// ultimo error.code del backend ("session_expired" /
	// "authentication_error"), el tope local de 30 d (loginAt) y la
	// expiracion local. Valido tras un restore fallido o un 401.
	meta::UserReloginReason userReloginReason() const;
	// Ultimo error.code del backend en un 401 de sesion de usuario
	// (puede ir vacio si no hubo 401). Solo codigo, jamas secreto.
	QString lastUserAuthCode() const { return lastUserAuthCode_; }
	// Origen del tope absoluto de 30 d (invalido si la sesion es
	// heredada sin el: entonces el tope lo aplica el servidor).
	QDateTime userLoginAt() const { return userLoginAt_; }
	// Lo que queda de ventana deslizante (negativo si ya expiro; sirve
	// para los dias "sin usarlo" del mensaje).
	qint64 userSecsToExpiry() const;

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
	// [D 2026-10-26]: el registro managedUser sigue las mismas reglas que
	// backendInstall (DPAPI + binding a backendBaseUrl, best-effort load).
	bool persistUserSession();
	void clearPersistedUserSession();
	bool storeSession(const QJsonObject &o);
	void freshSession(std::function<void(Result)> cb);

	QNetworkAccessManager *nam_;
	QString baseUrl_;
	secure::Store *store_ = nullptr;
	QString sessionToken_;
	QDateTime sessionExpiry_;
	// Fase 4 + [D 2026-10-26]: sesion de usuario en memoria con respaldo
	// persistido cifrado (managedUser en el store). TTL largo del regimen
	// 2026-10-26 (ventana deslizante 15 d + tope absoluto 30 d, contrato
	// backend/tests/test_users_session_sliding.py): userExpiry_ con margen
	// de seguridad corto (kUserSessionMarginSecs, 5 min) y userLoginAt_
	// como origen del tope. lastUserAuthCode_ guarda el ultimo error.code
	// del backend en un 401 ("session_expired"/"authentication_error",
	// solo codigo) para distinguir el motivo sin tecnicismos en la UI.
	QString userSessionToken_;
	QString loggedUsername_; // etiqueta no sensible ("Conectado como")
	QDateTime userExpiry_;
	QDateTime userLoginAt_;
	QString lastUserAuthCode_;
};

} // namespace backend_auth
