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
			reply->deleteLater();
			if (err == static_cast<int>(
					    QNetworkReply::OperationCanceledError) ||
			    reply->error() == QNetworkReply::TimeoutError) {
				cb(Result::NetworkError, {});
				return;
			}
			if (reply->error() != QNetworkReply::NoError) {
				cb(http == 429 ? Result::RateLimited :
						 Result::NetworkError,
				   {});
				return;
			}
			const QJsonObject o =
				QJsonDocument::fromJson(raw).object();
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
				reply->deleteLater();
				const bool failed =
					netErr != QNetworkReply::NoError;
				if (!failed && http == 401) {
					// Sesión revocada en servidor:
					// olvidarla para forzar re-autenticar.
					sessionToken_.clear();
				}
				Result res = Result::Ok;
				QJsonObject o;
				if (netErr == QNetworkReply::TimeoutError ||
				    netErr ==
					    QNetworkReply::OperationCanceledError) {
					res = Result::NetworkError;
				} else if (failed) {
					res = (http == 429)
						      ? Result::RateLimited
						      : Result::NetworkError;
				} else {
					o = QJsonDocument::fromJson(raw)
						    .object();
					if (http == 401)
						res = Result::Unauthorized;
					else if (http == 429)
						res = Result::RateLimited;
					else if (http < 200 || http >= 300)
						res = (http >= 500)
							      ? Result::ServerError
							      : Result::InvalidResponse;
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

} // namespace backend_auth
