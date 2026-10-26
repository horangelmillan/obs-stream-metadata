/*
 T-044: async backend auth client. See backend_auth.h for the contract.
*/

#include "backend_auth.h"

#include "secure_store.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageAuthenticationCode>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUuid>

namespace backend_auth {

namespace {
constexpr int kTimeoutMs = 15000;
constexpr int kExpiryMarginSecs = 30;

qint64 nowSecs()
{
	return QDateTime::currentDateTimeUtc().toSecsSinceEpoch();
}
} // namespace

QString hmacSignature(const QString &secret, const QString &installationId,
		      qint64 timestamp, const QString &nonce)
{
	const QByteArray msg =
		(installationId + QStringLiteral("|") + QString::number(timestamp) +
		 QStringLiteral("|") + nonce)
			.toUtf8();
	return QString::fromLatin1(
		QMessageAuthenticationCode::hash(msg, secret.toUtf8(),
						 QCryptographicHash::Sha256)
			.toHex());
}

QString makeNonce()
{
	return QUuid::createUuid().toString(QUuid::WithoutBraces).remove(
		QLatin1Char('-'));
}

Client::Client(QNetworkAccessManager *nam, QObject *parent)
	: QObject(parent), nam_(nam)
{
}

void Client::setBaseUrl(const QString &baseUrl)
{
	baseUrl_ = baseUrl;
	while (baseUrl_.endsWith(QLatin1Char('/')))
		baseUrl_.chop(1);
}

void Client::setStore(secure::Store *store)
{
	store_ = store;
}

bool Client::hasValidSession() const
{
	return !sessionToken_.isEmpty() &&
	       QDateTime::currentDateTimeUtc().secsTo(sessionExpiry_) >
		       kExpiryMarginSecs;
}

QJsonObject Client::signedBody(const QString &installationId,
			       const QString &secret) const
{
	QJsonObject o;
	o[QStringLiteral("installation_id")] = installationId;
	o[QStringLiteral("timestamp")] = static_cast<qint64>(nowSecs());
	o[QStringLiteral("nonce")] = makeNonce();
	o[QStringLiteral("signature")] = hmacSignature(
		secret, installationId, o[QStringLiteral("timestamp")].toInteger(),
		o[QStringLiteral("nonce")].toString());
	return o;
}

void Client::post(const QString &path, const QJsonObject &body,
		  std::function<void(Result, const QJsonObject &)> cb)
{
	if (baseUrl_.isEmpty() || !nam_) {
		cb(Result::InvalidResponse, {});
		return;
	}
	QNetworkRequest req(QUrl(baseUrl_ + path));
	req.setHeader(QNetworkRequest::ContentTypeHeader,
		      QVariant(QStringLiteral("application/json")));
	QNetworkReply *reply = nam_->post(
		req, QJsonDocument(body).toJson(QJsonDocument::Compact));
	QTimer *timer = new QTimer(reply);
	timer->setSingleShot(true);
	timer->setInterval(kTimeoutMs);
	connect(timer, &QTimer::timeout, reply, [reply]() { reply->abort(); });
	timer->start();
		connect(reply, &QNetworkReply::finished, this,
			[this, reply, timer, cb]() {
				timer->stop();
				timer->deleteLater();
				const int http = reply->attribute(
					QNetworkRequest::HttpStatusCodeAttribute)
						 .toInt();
			const QByteArray raw = reply->readAll();
			const int err = static_cast<int>(reply->error());
			const QJsonObject o =
				QJsonDocument::fromJson(raw).object();
			reply->deleteLater();
			// Qt also reports error() for answered HTTP error
			// statuses (e.g. 401 AuthenticationRequiredError):
			// only http==0 (or timeout/abort) means the
			// backend was never reached. Anything answered
			// maps by status so callers can recover (401 ->
			// fresh session) and the UI never reports
			// "network" for a reachable backend.
			if (err == static_cast<int>(
					    QNetworkReply::OperationCanceledError) ||
			    err == static_cast<int>(
					    QNetworkReply::TimeoutError) ||
			    (err != static_cast<int>(
					     QNetworkReply::NoError) &&
			     http == 0)) {
				cb(Result::NetworkError, {});
				return;
			}
			if (http == 401) {
				cb(Result::Unauthorized, o);
				return;
			}
			if (http == 429) {
				cb(Result::RateLimited, o);
				return;
			}
			if (http < 200 || http >= 300 || o.isEmpty()) {
				cb(http >= 500 ? Result::ServerError :
							Result::InvalidResponse,
				   o);
				return;
			}
			cb(Result::Ok, o);
		});
}

bool Client::persistInstallation(const QString &id, const QString &secret)
{
	if (!store_ || id.isEmpty() || secret.isEmpty())
		return false;
	secure::Data d;
	store_->load(d); // best-effort: keep provider records when present
	d.backendInstall.clientId = id;
	d.backendInstall.secret = secret;
	d.backendInstall.connected = true;
	// T-053: bind the installation to this backend. A later backend
	// switch is detected on load (mismatch → re-bootstrap there).
	d.backendBaseUrl = baseUrl_;
	return store_->save(d);
}

bool Client::loadInstallation(QString &id, QString &secret)
{
	if (!store_)
		return false;
	secure::Data d;
	store_->load(d); // false = no usable file; d.backendInstall stays empty
	if (!d.backendInstall.connected || d.backendInstall.clientId.isEmpty() ||
	    d.backendInstall.secret.isEmpty())
		return false;
	// T-053 fail-closed: never hand a foreign backend's secret to the
	// configured one. Mismatch (incl. legacy files without binding) reads
	// as "no installation" so the caller re-bootstraps (dock: StorageError
	// → bootstrap → retry, no cross-backend secret use, no fallback).
	if (!secure::installationUrlMatches(d.backendBaseUrl, baseUrl_))
		return false;
	id = d.backendInstall.clientId;
	secret = d.backendInstall.secret;
	return true;
}

void Client::bootstrap(std::function<void(Result)> cb)
{
	post(QStringLiteral("/auth/bootstrap"), QJsonObject(),
	     [this, cb](Result r, const QJsonObject &o) {
		     if (r != Result::Ok) {
			     cb(r);
			     return;
		     }
		     const QString id =
			     o.value(QStringLiteral("installation_id")).toString();
		     const QString secret = o.value(
					      QStringLiteral("installation_secret"))
					    .toString();
		     if (id.isEmpty() || secret.isEmpty() ||
			 !persistInstallation(id, secret)) {
			     cb(Result::StorageError);
			     return;
		     }
		     sessionToken_.clear();
		     cb(Result::Ok);
	     });
}

void Client::ensureSession(std::function<void(Result)> cb)
{
	if (hasValidSession()) {
		cb(Result::Ok);
		return;
	}
	QString id, secret;
	if (!loadInstallation(id, secret)) {
		cb(Result::StorageError);
		return;
	}
	// Prefer refresh when an (expired) token exists: rotation keeps the
	// window tight. Otherwise a fresh signed session request.
	if (!sessionToken_.isEmpty()) {
		QJsonObject body = signedBody(id, secret);
		body[QStringLiteral("session_token")] = sessionToken_;
		post(QStringLiteral("/auth/refresh"), body,
		     [this, cb](Result r, const QJsonObject &o) {
			     if (r == Result::Ok &&
				 storeSession(o)) {
				     cb(Result::Ok);
				     return;
			     }
			     if (r == Result::Unauthorized) {
				     // Refresh lost the race or was revoked:
				     // fall back to a fresh session.
				     freshSession(cb);
				     return;
			     }
			     cb(r);
		     });
		return;
	}
	freshSession(cb);
}

bool Client::storeSession(const QJsonObject &o)
{
	const QString token =
		o.value(QStringLiteral("session_token")).toString();
	const int expiresIn =
		o.value(QStringLiteral("expires_in")).toInt();
	if (token.isEmpty() || expiresIn <= 0)
		return false;
	sessionToken_ = token;
	sessionExpiry_ = QDateTime::currentDateTimeUtc().addSecs(expiresIn);
	return true;
}

void Client::freshSession(std::function<void(Result)> cb)
{
	QString id, secret;
	if (!loadInstallation(id, secret)) {
		cb(Result::StorageError);
		return;
	}
	post(QStringLiteral("/auth/session"), signedBody(id, secret),
	     [this, cb](Result r, const QJsonObject &o) {
		     if (r == Result::Ok && storeSession(o)) {
			     cb(Result::Ok);
			     return;
		     }
		     sessionToken_.clear();
		     cb(r == Result::Ok ? Result::InvalidResponse : r);
	     });
}

void Client::revokeSession(std::function<void(Result)> cb)
{
	if (sessionToken_.isEmpty()) {
		cb(Result::Ok);
		return;
	}
	QJsonObject body;
	body[QStringLiteral("session_token")] = sessionToken_;
	sessionToken_.clear();
	post(QStringLiteral("/auth/revoke"), body,
	     [cb](Result r, const QJsonObject &) { cb(r); });
}

void Client::revokeInstallation(std::function<void(Result)> cb)
{
	QString id, secret;
	if (!loadInstallation(id, secret)) {
		cb(Result::StorageError);
		return;
	}
	sessionToken_.clear();
	post(QStringLiteral("/auth/installation/revoke"), signedBody(id, secret),
	     [cb](Result r, const QJsonObject &) { cb(r); });
}

// --- Fase 4: cuenta de usuario Managed ([D 2026-10-26]: memoria +
// managedUser persistido cifrado; el 401 remoto borra ambos) ----------

bool Client::hasUserSession() const
{
	return !userSessionToken_.isEmpty() &&
	       QDateTime::currentDateTimeUtc().secsTo(userExpiry_) >
		       kExpiryMarginSecs;
}

void Client::clearUserSession()
{
	userSessionToken_.clear();
	loggedUsername_.clear();
	userExpiry_ = QDateTime();
	// [D 2026-10-26]: memoria y disco van juntos (logout/401/expirada
	// nunca dejan una sesion muerta que reaparezca al reiniciar).
	clearPersistedUserSession();
}

// [D 2026-10-26]: el registro managedUser sigue las reglas de
// backendInstall (DPAPI + binding a backendBaseUrl). Best-effort load
// para no perder providers/snapshots al persistir la sesion.
bool Client::persistUserSession()
{
	if (!store_ || baseUrl_.isEmpty() || userSessionToken_.isEmpty() ||
	    loggedUsername_.isEmpty() || !userExpiry_.isValid())
		return false;
	secure::Data d;
	store_->load(d); // best-effort: keep provider records when present
	d.managedUser.loggedIn = true;
	d.managedUser.username = loggedUsername_;
	d.managedUser.token = userSessionToken_;
	d.managedUser.expiry = userExpiry_;
	d.backendBaseUrl = baseUrl_;
	return store_->save(d);
}

void Client::clearPersistedUserSession()
{
	if (!store_)
		return;
	secure::Data d;
	store_->load(d); // best-effort
	if (!d.managedUser.loggedIn && d.managedUser.token.isEmpty())
		return; // nothing persisted: avoid a pointless rewrite
	d.managedUser = secure::ManagedUserSession{};
	store_->save(d); // best-effort: memory is already clean
}

bool Client::tryRestoreUserSession()
{
	// Ya en memoria y vigente: nada que restaurar.
	if (hasUserSession())
		return true;
	userSessionToken_.clear();
	loggedUsername_.clear();
	userExpiry_ = QDateTime();
	if (!store_ || baseUrl_.isEmpty())
		return false;
	secure::Data d;
	store_->load(d); // false = no usable file; managedUser stays empty
	if (!d.managedUser.loggedIn || d.managedUser.username.isEmpty() ||
	    d.managedUser.token.isEmpty() ||
	    !d.managedUser.expiry.isValid())
		return false;
	// Fail-closed ante mismatch (igual que loadInstallation): un secreto
	// de otro backend jamas se entrega al configurado. Legacy/sin binding
	// nunca coincide.
	if (!secure::installationUrlMatches(d.backendBaseUrl, baseUrl_)) {
		clearPersistedUserSession();
		return false;
	}
	// Expirada (con el mismo margen que hasUserSession): se ignora y se
	// limpia; el dock pide login de nuevo. NO hay red aqui: la revalida
	// contra el backend la hace el dock con me().
	if (QDateTime::currentDateTimeUtc().secsTo(d.managedUser.expiry) <=
	    kExpiryMarginSecs) {
		clearPersistedUserSession();
		return false;
	}
	userSessionToken_ = d.managedUser.token;
	loggedUsername_ = d.managedUser.username;
	userExpiry_ = d.managedUser.expiry;
	return true;
}

// POST público con bearer de instalación best-effort (adopción sin
// reset, spec §3): si hay sesión válida se adjunta, si no se sigue sin
// ella. Nunca X-User-Session (signup/login no tienen sesión todavía).
// Sin valores en logs: solo códigos.
void Client::postPublic(const QString &path, const QJsonObject &body,
			std::function<void(Result, const QJsonObject &)> cb)
{
	if (baseUrl_.isEmpty() || !nam_) {
		cb(Result::InvalidResponse, {});
		return;
	}
	QNetworkRequest req(QUrl(baseUrl_ + path));
	req.setHeader(QNetworkRequest::ContentTypeHeader,
		      QVariant(QStringLiteral("application/json")));
	if (hasValidSession())
		req.setRawHeader("Authorization",
				 ("Bearer " + sessionToken_).toLatin1());
	QNetworkReply *reply = nam_->post(
		req, QJsonDocument(body).toJson(QJsonDocument::Compact));
	QTimer *timer = new QTimer(reply);
	timer->setSingleShot(true);
	timer->setInterval(kTimeoutMs);
	connect(timer, &QTimer::timeout, reply, [reply]() { reply->abort(); });
	connect(reply, &QNetworkReply::finished, this,
		[this, reply, timer, cb]() {
			timer->stop();
			timer->deleteLater();
			const int http = reply->attribute(
				QNetworkRequest::HttpStatusCodeAttribute)
						 .toInt();
			const QByteArray raw = reply->readAll();
			const int err = static_cast<int>(reply->error());
			const QJsonObject o =
				QJsonDocument::fromJson(raw).object();
			reply->deleteLater();
			if (err == static_cast<int>(
					    QNetworkReply::OperationCanceledError) ||
			    err == static_cast<int>(
					    QNetworkReply::TimeoutError) ||
			    (err != static_cast<int>(QNetworkReply::NoError) &&
			     http == 0)) {
				cb(Result::NetworkError, {});
				return;
			}
			if (http == 401) {
				cb(Result::Unauthorized, o);
				return;
			}
			if (http == 429) {
				cb(Result::RateLimited, o);
				return;
			}
			if (http == 409) {
				cb(Result::InvalidResponse, o);
				return;
			}
			if (http < 200 || http >= 300 || o.isEmpty()) {
				cb(http >= 500 ? Result::ServerError :
						  Result::InvalidResponse,
				   o);
				return;
			}
			cb(Result::Ok, o);
		});
}

void Client::signup(const QString &username, const QString &password,
		    std::function<void(Result, const UserInfo &)> cb)
{
	const QString user = username.trimmed();
	if (user.isEmpty() || password.isEmpty()) {
		cb(Result::InvalidResponse, {});
		return;
	}
	QJsonObject body;
	body[QStringLiteral("username")] = user;
	body[QStringLiteral("password")] = password;
	postPublic(QStringLiteral("/users/signup"), body,
		   [user, cb](Result r, const QJsonObject &o) {
			   if (r != Result::Ok) {
				   cb(r, {});
				   return;
			   }
			   UserInfo info;
			   info.userId = o.value(QStringLiteral("user_id"))
						 .toString();
			   info.username = o.value(QStringLiteral("username"))
						   .toString();
			   if (info.userId.isEmpty() || info.username.isEmpty()) {
				   cb(Result::InvalidResponse, {});
				   return;
			   }
			   cb(Result::Ok, info);
		   });
}

void Client::login(const QString &username, const QString &password,
		   std::function<void(Result)> cb)
{
	const QString user = username.trimmed();
	if (user.isEmpty() || password.isEmpty()) {
		cb(Result::InvalidResponse);
		return;
	}
	// La adopción necesita el bearer de instalación: asegurar sesión
	// primero (bootstrap/re-auth lo decide ensureSession).
	ensureSession([this, user, password, cb](Result r) {
		if (r != Result::Ok || !hasValidSession()) {
			cb(r);
			return;
		}
		QJsonObject body;
		body[QStringLiteral("username")] = user;
		body[QStringLiteral("password")] = password;
		postPublic(QStringLiteral("/users/login"), body,
			   [this, user, cb](Result lr, const QJsonObject &o) {
				   if (lr != Result::Ok) {
					   cb(lr);
					   return;
				   }
				   const QString token =
					   o.value(QStringLiteral("user_session"))
						   .toString();
				   const int expiresIn = o.value(QStringLiteral(
								 "expires_in"))
								 .toInt();
				   if (token.isEmpty() || expiresIn <= 0) {
					   cb(Result::InvalidResponse);
					   return;
				   }
				   userSessionToken_ = token;
				   loggedUsername_ = user;
				   userExpiry_ =
					   QDateTime::currentDateTimeUtc().addSecs(
						   expiresIn);
				   // [D 2026-10-26]: sesion OK -> persiste
				   // cifrada para volver directo al panel al
				   // reabrir. Sin persistencia no hay restore:
				   // fail-closed (memoria limpia + StorageError).
				   if (!persistUserSession()) {
					   clearUserSession();
					   cb(Result::StorageError);
					   return;
				   }
				   cb(Result::Ok);
			   });
	});
}

// GET/POST con X-User-Session (+ bearer best-effort). Sin sesión en
// memoria: Unauthorized sin red. Un 401 remoto olvida la sesión local.
void Client::userSend(const QString &verb, const QString &path,
		      const QJsonObject &body,
		      std::function<void(const ApiReply &)> cb)
{
	if (!hasUserSession()) {
		cb(ApiReply{Result::Unauthorized, 401, {}});
		return;
	}
	if (baseUrl_.isEmpty() || !nam_) {
		cb(ApiReply{Result::InvalidResponse, 0, {}});
		return;
	}
	QNetworkRequest req(QUrl(baseUrl_ + path));
	req.setHeader(QNetworkRequest::ContentTypeHeader,
		      QVariant(QStringLiteral("application/json")));
	if (hasValidSession())
		req.setRawHeader("Authorization",
				 ("Bearer " + sessionToken_).toLatin1());
	req.setRawHeader(meta::userSessionHeaderName().toLatin1(),
			 userSessionToken_.toLatin1());
	QNetworkReply *reply = (verb == QStringLiteral("POST"))
				       ? nam_->post(req,
						    QJsonDocument(body).toJson(
							    QJsonDocument::Compact))
				       : nam_->get(req);
	QTimer *timer = new QTimer(reply);
	timer->setSingleShot(true);
	timer->setInterval(kTimeoutMs);
	connect(timer, &QTimer::timeout, reply,
		[reply]() { reply->abort(); });
	timer->start();
	connect(reply, &QNetworkReply::finished, this,
		[this, reply, timer, cb]() {
			timer->stop();
			timer->deleteLater();
			const int http = reply->attribute(
				QNetworkRequest::HttpStatusCodeAttribute)
						 .toInt();
			const QByteArray raw = reply->readAll();
			const QNetworkReply::NetworkError netErr =
				reply->error();
			const QJsonObject o =
				QJsonDocument::fromJson(raw).object();
			reply->deleteLater();
			const bool transportFail =
				netErr == QNetworkReply::TimeoutError ||
				netErr ==
					QNetworkReply::OperationCanceledError ||
				(netErr != QNetworkReply::NoError && http == 0);
			if (http == 401)
				clearUserSession();
			Result res = Result::Ok;
			if (transportFail) {
				res = Result::NetworkError;
			} else if (http == 401) {
				res = Result::Unauthorized;
			} else if (http == 429) {
				res = Result::RateLimited;
			} else if (http < 200 || http >= 300) {
				res = (http >= 500) ? Result::ServerError :
						      Result::InvalidResponse;
			}
			cb(ApiReply{res, http, o});
		});
}

void Client::logoutUser(std::function<void(Result)> cb)
{
	if (!hasUserSession()) {
		// Sin memoria puede quedar registro (p. ej. restore pendiente
		// nunca cargado): el logout siempre limpia el disco.
		clearPersistedUserSession();
		cb(Result::Ok);
		return;
	}
	userSend(QStringLiteral("POST"), QStringLiteral("/users/logout"),
		 QJsonObject(),
		 [this, cb](const ApiReply &rep) {
			 clearUserSession();
			 cb(rep.result);
		 });
}

void Client::me(std::function<void(Result, const UserInfo &)> cb)
{
	if (!hasUserSession()) {
		cb(Result::Unauthorized, {});
		return;
	}
	userSend(QStringLiteral("GET"), QStringLiteral("/users/me"),
		 QJsonObject(),
		 [this, cb](const ApiReply &rep) {
			 if (rep.result != Result::Ok) {
				 if (rep.result == Result::Unauthorized)
					 clearUserSession();
				 cb(rep.result, {});
				 return;
			 }
			 UserInfo info;
			 info.userId =
				 rep.body.value(QStringLiteral("user_id")).toString();
			 info.username = rep.body.value(QStringLiteral("username"))
						 .toString();
			 if (info.userId.isEmpty() || info.username.isEmpty()) {
				 cb(Result::InvalidResponse, {});
				 return;
			 }
			 loggedUsername_ = info.username; // etiqueta canónica
			 cb(Result::Ok, info);
		 });
}

void Client::apiSendUser(const QString &verb, const QString &path,
			 const QJsonObject &body,
			 std::function<void(const ApiReply &)> cb)
{
	// Gate previo: sin user-session NO hay red (ni fallback a
	// Independent: el llamador Managed ya decidió el contexto).
	if (!hasUserSession()) {
		cb(ApiReply{Result::Unauthorized, 401, {}});
		return;
	}
	ensureSession([this, verb, path, body, cb](Result r) {
		if (r != Result::Ok || !hasValidSession()) {
			cb(ApiReply{r, 0, {}});
			return;
		}
		if (baseUrl_.isEmpty() || !nam_) {
			cb(ApiReply{Result::InvalidResponse, 0, {}});
			return;
		}
		QNetworkRequest req(QUrl(baseUrl_ + path));
		req.setHeader(QNetworkRequest::ContentTypeHeader,
			      QVariant(QStringLiteral("application/json")));
		req.setRawHeader("Authorization",
				 ("Bearer " + sessionToken_).toLatin1());
		req.setRawHeader(meta::userSessionHeaderName().toLatin1(),
				 userSessionToken_.toLatin1());
		QNetworkReply *reply =
			(verb == QStringLiteral("POST"))
				? nam_->post(req,
					     QJsonDocument(body).toJson(
						     QJsonDocument::Compact))
				: nam_->get(req);
		QTimer *timer = new QTimer(reply);
		timer->setSingleShot(true);
		timer->setInterval(kTimeoutMs);
		connect(timer, &QTimer::timeout, reply,
			[reply]() { reply->abort(); });
		timer->start();
		connect(reply, &QNetworkReply::finished, this,
			[this, reply, timer, cb]() {
				timer->stop();
				timer->deleteLater();
				const int http = reply->attribute(
					QNetworkRequest::HttpStatusCodeAttribute)
							 .toInt();
				const QByteArray raw = reply->readAll();
				const QNetworkReply::NetworkError netErr =
					reply->error();
				const QJsonObject o =
					QJsonDocument::fromJson(raw).object();
				reply->deleteLater();
				const bool transportFail =
					netErr == QNetworkReply::TimeoutError ||
					netErr == QNetworkReply::
							  OperationCanceledError ||
					(netErr != QNetworkReply::NoError &&
					 http == 0);
				if (http == 401) {
					// El backend normaliza (auth doble):
					// olvidar ambas y pedir re-login.
					sessionToken_.clear();
					clearUserSession();
				}
				Result res = Result::Ok;
				if (transportFail) {
					res = Result::NetworkError;
				} else if (http == 401) {
					res = Result::Unauthorized;
				} else if (http == 429) {
					res = Result::RateLimited;
				} else if (http < 200 || http >= 300) {
					res = (http >= 500) ?
						      Result::ServerError :
						      Result::InvalidResponse;
				}
				cb(ApiReply{res, http, o});
			});
	});
}

void Client::apiSend(const QString &verb, const QString &path,
		     const QJsonObject &body,
		     std::function<void(const ApiReply &)> cb)
{
	ensureSession([this, verb, path, body, cb](Result r) {
		if (r != Result::Ok || sessionToken_.isEmpty()) {
			cb(ApiReply{r, 0, {}});
			return;
		}
		if (baseUrl_.isEmpty() || !nam_) {
			cb(ApiReply{Result::InvalidResponse, 0, {}});
			return;
		}
		QNetworkRequest req(QUrl(baseUrl_ + path));
		req.setHeader(QNetworkRequest::ContentTypeHeader,
			      QVariant(QStringLiteral("application/json")));
		req.setRawHeader("Authorization",
				 ("Bearer " + sessionToken_).toLatin1());
		QNetworkReply *reply = (verb == QStringLiteral("POST"))
					       ? nam_->post(req,
							    QJsonDocument(body).toJson(
								    QJsonDocument::
									    Compact))
					       : nam_->get(req);
		QTimer *timer = new QTimer(reply);
		timer->setSingleShot(true);
		timer->setInterval(kTimeoutMs);
		connect(timer, &QTimer::timeout, reply,
			[reply]() { reply->abort(); });
		timer->start();
		connect(reply, &QNetworkReply::finished, this,
			[this, reply, timer, cb]() {
				timer->stop();
				timer->deleteLater();
				const int http = reply->attribute(
					QNetworkRequest::HttpStatusCodeAttribute)
							 .toInt();
				const QByteArray raw = reply->readAll();
			const QNetworkReply::NetworkError netErr =
				reply->error();
			const QJsonObject o =
				QJsonDocument::fromJson(raw).object();
			reply->deleteLater();
			// Same rule as post(): Qt flags error() for
			// answered HTTP statuses too; only http==0 (or
			// timeout/abort) is a transport failure. This
			// keeps 401 visible so the dock can mark
			// "Needs reconnection" instead of "network".
			const bool transportFail =
				netErr == QNetworkReply::TimeoutError ||
				netErr ==
					QNetworkReply::OperationCanceledError ||
				(netErr != QNetworkReply::NoError &&
				 http == 0);
			if (http == 401) {
				// Sesión revocada en servidor:
				// olvidarla para forzar re-autenticar.
				sessionToken_.clear();
			}
			Result res = Result::Ok;
			if (transportFail) {
				res = Result::NetworkError;
			} else if (http == 401) {
				res = Result::Unauthorized;
			} else if (http == 429) {
				res = Result::RateLimited;
			} else if (http < 200 || http >= 300) {
				res = (http >= 500) ? Result::ServerError :
						      Result::InvalidResponse;
			}
			cb(ApiReply{res, http, o});
			});
	});
}

void Client::apiGet(const QString &path,
		    std::function<void(const ApiReply &)> cb)
{
	apiSend(QStringLiteral("GET"), path, QJsonObject(), cb);
}

void Client::apiPost(const QString &path, const QJsonObject &body,
		     std::function<void(const ApiReply &)> cb)
{
	apiSend(QStringLiteral("POST"), path, body, cb);
}

void Client::apiGetUser(const QString &path,
			std::function<void(const ApiReply &)> cb)
{
	apiSendUser(QStringLiteral("GET"), path, QJsonObject(), cb);
}

void Client::apiPostUser(const QString &path, const QJsonObject &body,
			 std::function<void(const ApiReply &)> cb)
{
	apiSendUser(QStringLiteral("POST"), path, body, cb);
}

} // namespace backend_auth
