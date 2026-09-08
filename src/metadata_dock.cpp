/*
T-031: MVP dock. Qt Widgets + QNetworkAccessManager (async, no extra libs).
Tokens/secrets: process memory only, never persisted, printed or logged.
*/

#include "metadata_dock.h"

#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QVBoxLayout>

namespace {

const char *kBrowserUa =
	"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
	"(KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36"; // F-022
const char *kTwScope = "channel:manage:broadcast";
const char *kYtScope = "https://www.googleapis.com/auth/youtube.force-ssl";
const char *kKkScope = "channel:write channel:read"; // read: identity label

QString env(const char *name)
{
	return QString::fromLocal8Bit(qgetenv(name)).trimmed();
}

QJsonObject replyJson(QNetworkReply *reply)
{
	return QJsonDocument::fromJson(reply->readAll()).object();
}

} // namespace

MetadataDock::MetadataDock(QWidget *parent) : QWidget(parent)
{
	QVBoxLayout *top = new QVBoxLayout(this);

	QLabel *platTitle = new QLabel(tr("Platforms"), this);
	top->addWidget(platTitle);

	auto addRow = [&](meta::Platform p, const QString &name) {
		QCheckBox *check = new QCheckBox(name, this);
		check->setChecked(true);
		QLabel *status = new QLabel(tr("Not connected"), this);
		QPushButton *connBtn = new QPushButton(tr("Connect"), this);
		QPushButton *disc =
			new QPushButton(tr("Disconnect"), this);
		top->addWidget(check);
		top->addWidget(status);
		top->addWidget(connBtn);
		top->addWidget(disc);
		if (p == meta::Platform::Twitch) {
			twCheck_ = check;
			twStatus_ = status;
			twConnect_ = connBtn;
			twDisconnect_ = disc;
			connect(connBtn, &QPushButton::clicked, this,
				&MetadataDock::onConnectTwitch);
			connect(disc, &QPushButton::clicked, this,
				&MetadataDock::onDisconnectTwitch);
		} else if (p == meta::Platform::YouTube) {
			ytCheck_ = check;
			ytStatus_ = status;
			ytConnect_ = connBtn;
			ytDisconnect_ = disc;
			connect(connBtn, &QPushButton::clicked, this,
				&MetadataDock::onConnectYouTube);
			connect(disc, &QPushButton::clicked, this,
				&MetadataDock::onDisconnectYouTube);
		} else {
			kkCheck_ = check;
			kkStatus_ = status;
			kkConnect_ = connBtn;
			kkDisconnect_ = disc;
			connect(connBtn, &QPushButton::clicked, this,
				&MetadataDock::onConnectKick);
			connect(disc, &QPushButton::clicked, this,
				&MetadataDock::onDisconnectKick);
		}
	};
	addRow(meta::Platform::Twitch, QStringLiteral("Twitch"));
	addRow(meta::Platform::YouTube, QStringLiteral("YouTube"));
	addRow(meta::Platform::Kick, QStringLiteral("Kick"));

	QLabel *credNote = new QLabel(
		tr("OAuth: register one app per platform; IDs/secrets come "
		   "from local env vars and live only in memory (never "
		   "stored or logged)."),
		this);
	credNote->setWordWrap(true);
	top->addWidget(credNote);

	QLabel *titleLabel = new QLabel(tr("Title"), this);
	titleEdit_ = new QLineEdit(this);
	titleLabel->setBuddy(titleEdit_);
	top->addWidget(titleLabel);
	top->addWidget(titleEdit_);

	QLabel *descLabel = new QLabel(tr("Description"), this);
	descEdit_ = new QPlainTextEdit(this);
	descEdit_->setPlaceholderText(
		tr("YouTube only — Twitch and Kick have no equivalent "
		   "stream description."));
	descLabel->setBuddy(descEdit_);
	top->addWidget(descLabel);
	top->addWidget(descEdit_);
	QLabel *descCaps = new QLabel(
		tr("YouTube: supported · Twitch: not available · Kick: not "
		   "available"),
		this);
	descCaps->setWordWrap(true);
	top->addWidget(descCaps);

	QLabel *bcLabel = new QLabel(tr("YouTube broadcast"), this);
	broadcastCombo_ = new QComboBox(this);
	bcLabel->setBuddy(broadcastCombo_);
	refreshButton_ = new QPushButton(tr("Refresh broadcasts"), this);
	connect(refreshButton_, &QPushButton::clicked, this,
		&MetadataDock::onRefreshBroadcasts);
	top->addWidget(bcLabel);
	top->addWidget(broadcastCombo_);
	top->addWidget(refreshButton_);

	devicePrompt_ = new QLabel(this);
	devicePrompt_->setWordWrap(true);
	devicePrompt_->setVisible(false);
	top->addWidget(devicePrompt_);

	applyButton_ = new QPushButton(tr("Apply changes"), this);
	connect(applyButton_, &QPushButton::clicked, this,
		&MetadataDock::onApply);
	top->addWidget(applyButton_);

	twResult_ = new QLabel(QStringLiteral("Twitch —"), this);
	ytResult_ = new QLabel(QStringLiteral("YouTube —"), this);
	kkResult_ = new QLabel(QStringLiteral("Kick —"), this);
	top->addWidget(twResult_);
	top->addWidget(ytResult_);
	top->addWidget(kkResult_);
	generalMsg_ = new QLabel(this);
	generalMsg_->setWordWrap(true);
	top->addWidget(generalMsg_);

	setLayout(top);

	net_ = new QNetworkAccessManager(this);
	connect(net_, &QNetworkAccessManager::finished, this,
		&MetadataDock::onReply);
}

// --- helpers ----------------------------------------------------------

void MetadataDock::setStatus(meta::Platform p, const QString &text)
{
	if (p == meta::Platform::Twitch)
		twStatus_->setText(text);
	else if (p == meta::Platform::YouTube)
		ytStatus_->setText(text);
	else
		kkStatus_->setText(text);
}

void MetadataDock::setResult(meta::Platform p, bool ok, const QString &text)
{
	QString line = QStringLiteral("%1 %2 %3")
			       .arg(meta::platformName(p),
				    ok ? QStringLiteral("✓")
				       : QStringLiteral("✗"),
				    text);
	if (p == meta::Platform::Twitch)
		twResult_->setText(line);
	else if (p == meta::Platform::YouTube)
		ytResult_->setText(line);
	else
		kkResult_->setText(line);
}

MetadataDock::Account &MetadataDock::account(meta::Platform p)
{
	if (p == meta::Platform::Twitch)
		return tw_;
	if (p == meta::Platform::YouTube)
		return yt_;
	return kk_;
}

void MetadataDock::openBrowser(const QString &url)
{
	QDesktopServices::openUrl(QUrl(url));
}

QString MetadataDock::randomUrlSafe(int chars)
{
	static const char *alpha = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
				   "abcdefghijklmnopqrstuvwxyz"
				   "0123456789-_";
	QString out;
	out.reserve(chars);
	for (int i = 0; i < chars; ++i)
		out += QChar::fromLatin1(alpha[QRandomGenerator::global()
						       ->bounded(64)]);
	return out;
}

QString MetadataDock::pkceChallenge(const QString &verifier)
{
	const QByteArray hash = QCryptographicHash::hash(
		verifier.toLatin1(), QCryptographicHash::Sha256);
	return QString::fromLatin1(hash.toBase64(
		QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

void MetadataDock::sendForm(const QUrl &url, const QString &body, Op op,
			    const QString &bearer, bool browserUa)
{
	QNetworkRequest req(url);
	req.setTransferTimeout(30000);
	req.setHeader(QNetworkRequest::ContentTypeHeader,
		      QStringLiteral("application/x-www-form-urlencoded"));
	if (browserUa)
		req.setHeader(QNetworkRequest::UserAgentHeader,
			      QString::fromLatin1(kBrowserUa));
	if (!bearer.isEmpty())
		req.setRawHeader("Authorization",
				 "Bearer " + bearer.toLatin1());
	QNetworkReply *reply = net_->post(req, body.toLatin1());
	reply->setProperty("op", static_cast<int>(op));
	pending_ = op;
}

void MetadataDock::sendJson(const QUrl &url, const QString &verb,
			    const QString &body, Op op,
			    const QString &bearer, const QString &clientId,
			    bool browserUa)
{
	QNetworkRequest req(url);
	req.setTransferTimeout(30000);
	req.setHeader(QNetworkRequest::ContentTypeHeader,
		      QStringLiteral("application/json"));
	if (browserUa)
		req.setHeader(QNetworkRequest::UserAgentHeader,
			      QString::fromLatin1(kBrowserUa));
	if (!bearer.isEmpty())
		req.setRawHeader("Authorization",
				 "Bearer " + bearer.toLatin1());
	if (!clientId.isEmpty())
		req.setRawHeader("Client-Id", clientId.toLatin1());
	QNetworkReply *reply =
		net_->sendCustomRequest(req, verb.toLatin1(), body.toUtf8());
	reply->setProperty("op", static_cast<int>(op));
	pending_ = op;
}

void MetadataDock::sendGet(const QUrl &url, Op op, const QString &bearer,
			   const QString &clientId, bool browserUa)
{
	QNetworkRequest req(url);
	req.setTransferTimeout(30000);
	if (browserUa)
		req.setHeader(QNetworkRequest::UserAgentHeader,
			      QString::fromLatin1(kBrowserUa));
	if (!bearer.isEmpty())
		req.setRawHeader("Authorization",
				 "Bearer " + bearer.toLatin1());
	if (!clientId.isEmpty())
		req.setRawHeader("Client-Id", clientId.toLatin1());
	QNetworkReply *reply = net_->get(req);
	reply->setProperty("op", static_cast<int>(op));
	pending_ = op;
}

void MetadataDock::startConnectBusy(meta::Platform p)
{
	setStatus(p, tr("Connecting…"));
	devicePrompt_->setVisible(false);
	generalMsg_->clear();
}

void MetadataDock::finishConnectOk(meta::Platform p, const QString &display)
{
	Account &a = account(p);
	a.connected = true;
	a.display = display;
	a.verifier.clear();
	a.state.clear();
	a.deviceCode.clear();
	if (callbackServer_) {
		callbackServer_->close();
		callbackServer_->deleteLater();
		callbackServer_ = nullptr;
	}
	devicePrompt_->setVisible(false);
	pending_ = Op::None;
	setStatus(p, tr("Connected as %1").arg(display));
	obs_log(LOG_INFO, "oauth connected: %s", meta::platformName(p));
}

void MetadataDock::finishConnectError(meta::Platform p, const QString &msg)
{
	account(p).clear();
	if (callbackServer_) {
		callbackServer_->close();
		callbackServer_->deleteLater();
		callbackServer_ = nullptr;
	}
	devicePrompt_->setVisible(false);
	pending_ = Op::None;
	setStatus(p, tr("Error: %1").arg(msg));
	obs_log(LOG_WARNING, "oauth failed: %s", meta::platformName(p));
}

void MetadataDock::connectHttpError(meta::Platform p, const char *step,
				    int http, bool netFail)
{
	obs_log(LOG_WARNING, "connect %s %s http %d",
		meta::platformName(p), step, http);
	if (netFail || http == 0)
		finishConnectError(p, tr("Could not reach %1 (network).")
					  .arg(meta::platformName(p)));
	else if (http == 400)
		finishConnectError(p, tr("%1 rejected the request (check "
					 "client ID / secret / redirect).")
					  .arg(meta::platformName(p)));
	else
		finishConnectError(p, meta::userMessage(
					  meta::classifyStatus(http), p));
}

// --- connect: Twitch device flow (no secret, F-015) --------------------

void MetadataDock::onConnectTwitch()
{
	Account &a = tw_;
	a.clear();
	const QString id = env("STREAM_META_TWITCH_CLIENT_ID");
	if (id.isEmpty()) {
		finishConnectError(meta::Platform::Twitch,
				   tr("Set STREAM_META_TWITCH_CLIENT_ID "
				      "locally first."));
		return;
	}
	a.clientId = id;
	startConnectBusy(meta::Platform::Twitch);
	QUrlQuery q;
	q.addQueryItem(QStringLiteral("client_id"), id);
	q.addQueryItem(QStringLiteral("scopes"),
		       QString::fromLatin1(kTwScope));
	sendForm(QUrl(QStringLiteral("https://id.twitch.tv/oauth2/device")),
		 q.toString(QUrl::FullyEncoded), Op::TwDevice);
}

void MetadataDock::onDisconnectTwitch()
{
	tw_.clear();
	setStatus(meta::Platform::Twitch, tr("Not connected"));
	setResult(meta::Platform::Twitch, true, tr("disconnected"));
}

void MetadataDock::onTwitchPollTimeout()
{
	if (pending_ != Op::TwPoll || twPollsLeft_ <= 0)
		return;
	Account &a = tw_;
	QUrlQuery q;
	q.addQueryItem(QStringLiteral("client_id"), a.clientId);
	q.addQueryItem(QStringLiteral("scopes"),
		       QString::fromLatin1(kTwScope));
	q.addQueryItem(QStringLiteral("device_code"), a.deviceCode);
	q.addQueryItem(QStringLiteral("grant_type"),
		       QStringLiteral(
			       "urn:ietf:params:oauth:grant-type:device_code"));
	sendForm(QUrl(QStringLiteral("https://id.twitch.tv/oauth2/token")),
		 q.toString(QUrl::FullyEncoded), Op::TwPoll);
}

// --- connect: PKCE callback server (YouTube loopback, Kick localhost) ---

bool MetadataDock::listenCallback(quint16 &port, bool kick)
{
	if (callbackServer_) {
		callbackServer_->close();
		callbackServer_->deleteLater();
	}
	callbackServer_ = new QTcpServer(this);
	connect(callbackServer_, &QTcpServer::newConnection, this,
		&MetadataDock::onCallbackConnection);
	QHostAddress host = kick ? QHostAddress::LocalHost
				 : QHostAddress(QStringLiteral("127.0.0.1"));
	// Kick requires the EXACT registered redirect (F-017), so the
	// documented fixed port is used. Loopback ports need no
	// pre-registration for Google installed apps: ephemeral is fine.
	if (!callbackServer_->listen(host, kick ? port : 0))
		return false;
	port = callbackServer_->serverPort();
	redirect_ = QStringLiteral("http://%1:%2%3")
			    .arg(kick ? QStringLiteral("localhost")
				      : QStringLiteral("127.0.0.1"))
			    .arg(port)
			    .arg(kick ? QStringLiteral("/cb")
				      : QStringLiteral("/"));
	return true;
}

void MetadataDock::onCallbackConnection()
{
	if (!callbackServer_ || pending_ == Op::None)
		return;
	QTcpSocket *sock = callbackServer_->nextPendingConnection();
	if (!sock)
		return;
	connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
		if (!callbackDone_)
			handleCallbackData(sock->readAll());
		const QByteArray body = "Authorized. You can close this tab.";
		sock->write("HTTP/1.1 200 OK\r\nContent-Type: "
			    "text/plain\r\nConnection: close\r\n\r\n" +
			    body);
		sock->disconnectFromHost();
	});
}

void MetadataDock::handleCallbackData(const QByteArray &request)
{
	const int eol = request.indexOf("\r\n");
	const QString line =
		QString::fromLatin1(request.left(eol < 0 ? 256 : eol));
	if (!line.startsWith(QStringLiteral("GET ")))
		return;
	const QString target = line.section(QLatin1Char(' '), 1, 1);
	const QString path = target.left(target.indexOf(QLatin1Char('?')));
	// Ignore anything but the callback path itself (e.g. /favicon.ico).
	const QString expected = callbackFor_ == meta::Platform::Kick
					 ? QStringLiteral("/cb")
					 : QStringLiteral("/");
	if (path != expected)
		return;
	const QUrl url(QStringLiteral("http://x") + target);
	const QUrlQuery q(url);
	if (q.queryItemValue(QStringLiteral("state")) !=
	    account(callbackFor_).state) {
		finishConnectError(callbackFor_,
				   tr("State mismatch (possible CSRF)."));
		return;
	}
	if (q.hasQueryItem(QStringLiteral("error"))) {
		finishConnectError(callbackFor_, tr("Authorization denied."));
		return;
	}
	const QString code = q.queryItemValue(QStringLiteral("code"));
	if (code.isEmpty()) {
		finishConnectError(callbackFor_,
				   tr("No authorization code received."));
		return;
	}
	callbackDone_ = true; // first callback wins; ignore later requests
	if (callbackFor_ == meta::Platform::YouTube)
		startYouTubeExchange(code);
	else
		startKickExchange(code);
}

void MetadataDock::onConnectYouTube()
{
	Account &a = yt_;
	a.clear();
	const QString id = env("STREAM_META_YOUTUBE_CLIENT_ID");
	const QString secret = env("STREAM_META_YOUTUBE_CLIENT_SECRET");
	if (id.isEmpty() || secret.isEmpty()) {
		finishConnectError(meta::Platform::YouTube,
				   tr("Set STREAM_META_YOUTUBE_CLIENT_ID and "
				      "STREAM_META_YOUTUBE_CLIENT_SECRET "
				      "locally first."));
		return;
	}
	quint16 port = 0;
	callbackFor_ = meta::Platform::YouTube;
	callbackDone_ = false;
	if (!listenCallback(port, false)) {
		finishConnectError(meta::Platform::YouTube,
				   tr("Could not open loopback callback."));
		return;
	}
	a.clientId = id;
	a.secret = secret;
	a.verifier = randomUrlSafe(64);
	a.state = QUuid::createUuid().toString(QUuid::WithoutBraces)
			  .remove(QLatin1Char('-'))
			  .left(32);
	startConnectBusy(meta::Platform::YouTube);
	QUrlQuery q;
	q.addQueryItem(QStringLiteral("response_type"),
		       QStringLiteral("code"));
	q.addQueryItem(QStringLiteral("client_id"), id);
	q.addQueryItem(QStringLiteral("redirect_uri"), redirect_);
	q.addQueryItem(QStringLiteral("scope"),
		       QString::fromLatin1(kYtScope));
	q.addQueryItem(QStringLiteral("state"), a.state);
	q.addQueryItem(QStringLiteral("code_challenge"),
		       pkceChallenge(a.verifier));
	q.addQueryItem(QStringLiteral("code_challenge_method"),
		       QStringLiteral("S256"));
	q.addQueryItem(QStringLiteral("access_type"),
		       QStringLiteral("offline"));
	q.addQueryItem(QStringLiteral("prompt"), QStringLiteral("consent"));
	QUrl url(QStringLiteral(
		"https://accounts.google.com/o/oauth2/v2/auth"));
	url.setQuery(q);
	pending_ = Op::YtExchange; // waiting for the browser callback
	openBrowser(url.toString(QUrl::FullyEncoded));
}

void MetadataDock::startYouTubeExchange(const QString &code)
{
	Account &a = yt_;
	QUrlQuery q;
	q.addQueryItem(QStringLiteral("grant_type"),
		       QStringLiteral("authorization_code"));
	q.addQueryItem(QStringLiteral("code"), code);
	q.addQueryItem(QStringLiteral("client_id"), a.clientId);
	q.addQueryItem(QStringLiteral("client_secret"), a.secret);
	q.addQueryItem(QStringLiteral("redirect_uri"), redirect_);
	q.addQueryItem(QStringLiteral("code_verifier"), a.verifier);
	sendForm(QUrl(QStringLiteral("https://oauth2.googleapis.com/token")),
		 q.toString(QUrl::FullyEncoded), Op::YtExchange);
}

void MetadataDock::onDisconnectYouTube()
{
	yt_.clear();
	broadcastCombo_->clear();
	setStatus(meta::Platform::YouTube, tr("Not connected"));
	setResult(meta::Platform::YouTube, true, tr("disconnected"));
}

void MetadataDock::onConnectKick()
{
	Account &a = kk_;
	a.clear();
	const QString id = env("STREAM_META_KICK_CLIENT_ID");
	const QString secret = env("STREAM_META_KICK_CLIENT_SECRET");
	if (id.isEmpty() || secret.isEmpty()) {
		finishConnectError(meta::Platform::Kick,
				   tr("Set STREAM_META_KICK_CLIENT_ID and "
				      "STREAM_META_KICK_CLIENT_SECRET "
				      "locally first."));
		return;
	}
	quint16 port = 3000; // must match the registered redirect (F-017)
	callbackFor_ = meta::Platform::Kick;
	callbackDone_ = false;
	if (!listenCallback(port, true)) {
		finishConnectError(meta::Platform::Kick,
				   tr("Port localhost:3000 is busy or blocked. "
				      "Free it: the redirect must match the "
				      "registered one."));
		return;
	}
	a.clientId = id;
	a.secret = secret;
	a.verifier = randomUrlSafe(64);
	a.state = QUuid::createUuid().toString(QUuid::WithoutBraces)
			  .remove(QLatin1Char('-'))
			  .left(32);
	startConnectBusy(meta::Platform::Kick);
	QUrlQuery q;
	q.addQueryItem(QStringLiteral("response_type"),
		       QStringLiteral("code"));
	q.addQueryItem(QStringLiteral("client_id"), id);
	q.addQueryItem(QStringLiteral("redirect_uri"), redirect_);
	q.addQueryItem(QStringLiteral("scope"),
		       QString::fromLatin1(kKkScope));
	q.addQueryItem(QStringLiteral("state"), a.state);
	q.addQueryItem(QStringLiteral("code_challenge"),
		       pkceChallenge(a.verifier));
	q.addQueryItem(QStringLiteral("code_challenge_method"),
		       QStringLiteral("S256"));
	QUrl url(QStringLiteral("https://id.kick.com/oauth/authorize"));
	url.setQuery(q);
	pending_ = Op::KkExchange; // waiting for the browser callback
	openBrowser(url.toString(QUrl::FullyEncoded));
}

void MetadataDock::startKickExchange(const QString &code)
{
	Account &a = kk_;
	QUrlQuery q;
	q.addQueryItem(QStringLiteral("grant_type"),
		       QStringLiteral("authorization_code"));
	q.addQueryItem(QStringLiteral("client_id"), a.clientId);
	q.addQueryItem(QStringLiteral("client_secret"), a.secret);
	q.addQueryItem(QStringLiteral("redirect_uri"), redirect_);
	q.addQueryItem(QStringLiteral("code"), code);
	q.addQueryItem(QStringLiteral("code_verifier"), a.verifier);
	sendForm(QUrl(QStringLiteral("https://id.kick.com/oauth/token")),
		 q.toString(QUrl::FullyEncoded), Op::KkExchange, QString(),
		 true);
}

void MetadataDock::onDisconnectKick()
{
	kk_.clear();
	setStatus(meta::Platform::Kick, tr("Not connected"));
	setResult(meta::Platform::Kick, true, tr("disconnected"));
}

void MetadataDock::onRefreshBroadcasts()
{
	if (!yt_.connected) {
		generalMsg_->setText(meta::userMessage(
			meta::Outcome::AuthRequired,
			meta::Platform::YouTube));
		return;
	}
	applyAfterList_ = false;
	QUrl url(QStringLiteral(
		"https://www.googleapis.com/youtube/v3/liveBroadcasts"));
	QUrlQuery q;
	q.addQueryItem(QStringLiteral("part"), QStringLiteral("snippet"));
	q.addQueryItem(QStringLiteral("mine"), QStringLiteral("true"));
	q.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("25"));
	url.setQuery(q);
	refreshButton_->setEnabled(false);
	sendGet(url, Op::YtList, yt_.access);
}

// --- apply -------------------------------------------------------------

void MetadataDock::onApply()
{
	meta::Metadata m{titleEdit_->text(), descEdit_->toPlainText()};
	meta::Selection s{twCheck_->isChecked(), ytCheck_->isChecked(),
			  kkCheck_->isChecked()};
	const QString err = meta::validate(m, s);
	if (!err.isEmpty()) {
		generalMsg_->setText(err);
		return;
	}
	generalMsg_->clear();
	applyQueue_.clear();
	if (s.twitch)
		applyQueue_ << meta::Platform::Twitch;
	if (s.youtube)
		applyQueue_ << meta::Platform::YouTube;
	if (s.kick)
		applyQueue_ << meta::Platform::Kick;
	applyButton_->setEnabled(false);
	applyButton_->setText(tr("Applying…"));
	startApplyNext();
}

void MetadataDock::startApplyNext()
{
	using P = meta::Platform;
	if (applyQueue_.isEmpty()) {
		finishApply();
		return;
	}
	const P p = applyQueue_.takeFirst();
	retried_ = false;
	if (!account(p).connected) {
		finishPlatform(p, false,
			       meta::userMessage(meta::Outcome::AuthRequired,
						 p));
		startApplyNext();
		return;
	}
	setResult(p, true, tr("Updating…"));
	const QString title = titleEdit_->text();
	if (p == P::Twitch) {
		QUrl url(QStringLiteral(
			"https://api.twitch.tv/helix/channels"));
		QUrlQuery q;
		q.addQueryItem(QStringLiteral("broadcaster_id"),
			       tw_.broadcaster);
		url.setQuery(q);
		sendJson(url, QStringLiteral("PATCH"),
			 meta::twitchPayload(title), Op::UpTw, tw_.access,
			 tw_.clientId);
	} else if (p == P::YouTube) {
		const QString item =
			broadcastCombo_->currentData().toString();
		if (item.isEmpty()) {
			// Fetch the list first, then retry this platform.
			applyQueue_.prepend(p);
			applyAfterList_ = true;
			QUrl url(QStringLiteral(
				"https://www.googleapis.com/youtube/v3/"
				"liveBroadcasts"));
			QUrlQuery q;
			q.addQueryItem(QStringLiteral("part"),
				       QStringLiteral("snippet"));
			q.addQueryItem(QStringLiteral("mine"),
				       QStringLiteral("true"));
			q.addQueryItem(QStringLiteral("maxResults"),
				       QStringLiteral("25"));
			url.setQuery(q);
			sendGet(url, Op::YtList, yt_.access);
			return;
		}
		QUrl url(QStringLiteral(
			"https://www.googleapis.com/youtube/v3/"
			"liveBroadcasts"));
		QUrlQuery q;
		q.addQueryItem(QStringLiteral("part"),
			       QStringLiteral("snippet"));
		url.setQuery(q);
		sendJson(url, QStringLiteral("PUT"),
			 meta::youTubePayload(item, title,
					      descEdit_->toPlainText()),
			 Op::UpYt, yt_.access);
	} else {
		sendJson(QUrl(QStringLiteral(
				 "https://api.kick.com/public/v1/channels")),
			 QStringLiteral("PATCH"), meta::kickPayload(title),
			 Op::UpKk, kk_.access, QString(), true);
	}
}

void MetadataDock::finishPlatform(meta::Platform p, bool ok,
				  const QString &msg)
{
	setResult(p, ok, msg);
	obs_log(ok ? LOG_INFO : LOG_WARNING, "apply %s: %s",
		meta::platformName(p), ok ? "ok" : "failed");
}

void MetadataDock::finishApply()
{
	pending_ = Op::None;
	applyButton_->setEnabled(true);
	applyButton_->setText(tr("Apply changes"));
}

void MetadataDock::refreshWithToken(meta::Platform p, Op resumeOp)
{
	Account &a = account(p);
	QUrlQuery q;
	q.addQueryItem(QStringLiteral("grant_type"),
		       QStringLiteral("refresh_token"));
	q.addQueryItem(QStringLiteral("refresh_token"), a.refresh);
	if (p == meta::Platform::Twitch) {
		q.addQueryItem(QStringLiteral("client_id"), a.clientId);
		q.addQueryItem(QStringLiteral("scopes"),
			       QString::fromLatin1(kTwScope));
		sendForm(QUrl(QStringLiteral(
				 "https://id.twitch.tv/oauth2/token")),
			 q.toString(QUrl::FullyEncoded), resumeOp);
	} else if (p == meta::Platform::YouTube) {
		q.addQueryItem(QStringLiteral("client_id"), a.clientId);
		q.addQueryItem(QStringLiteral("client_secret"), a.secret);
		sendForm(QUrl(QStringLiteral(
				 "https://oauth2.googleapis.com/token")),
			 q.toString(QUrl::FullyEncoded), resumeOp);
	} else {
		q.addQueryItem(QStringLiteral("client_id"), a.clientId);
		q.addQueryItem(QStringLiteral("client_secret"), a.secret);
		sendForm(QUrl(QStringLiteral(
				 "https://id.kick.com/oauth/token")),
			 q.toString(QUrl::FullyEncoded), resumeOp, QString(),
			 true);
	}
}

// --- reply router ------------------------------------------------------

void MetadataDock::onReply(QNetworkReply *reply)
{
	const Op op = static_cast<Op>(reply->property("op").toInt());
	const int http =
		reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
			.toInt();
	const bool netFail =
		reply->error() != QNetworkReply::NoError && http == 0;
	reply->deleteLater();
	using P = meta::Platform;

	auto fail = [&](P p, const QString &msg) {
		finishConnectError(p, msg);
	};

	switch (op) {
	case Op::None:
		return;

	case Op::TwDevice: {
		if (netFail || http != 200) {
			connectHttpError(P::Twitch, "device", http, netFail);
			return;
		}
		const QJsonObject o = replyJson(reply);
		tw_.deviceCode = o.value(QStringLiteral("device_code"))
					 .toString();
		tw_.pollInterval =
			o.value(QStringLiteral("interval")).toInt(5);
		const int expires =
			o.value(QStringLiteral("expires_in")).toInt(1800);
		twPollsLeft_ = qMax(1, expires /
						   qMax(1, tw_.pollInterval));
		devicePrompt_->setText(
			tr("Open %1 and enter code: %2")
				.arg(o.value(QStringLiteral("verification_"
							     "uri"))
					     .toString(),
				     o.value(QStringLiteral("user_code"))
					     .toString()));
		devicePrompt_->setVisible(true);
		openBrowser(o.value(QStringLiteral("verification_uri"))
				    .toString());
		pending_ = Op::TwPoll;
		QTimer::singleShot(tw_.pollInterval * 1000, this,
				   &MetadataDock::onTwitchPollTimeout);
		return;
	}

	case Op::TwPoll: {
		const QJsonObject o = replyJson(reply);
		if (http == 200) {
			tw_.access = o.value(QStringLiteral("access_token"))
					     .toString();
			tw_.refresh =
				o.value(QStringLiteral("refresh_token"))
					.toString();
			devicePrompt_->setVisible(false);
			sendGet(QUrl(QStringLiteral(
					"https://id.twitch.tv/oauth2/validate")),
				Op::TwValidate, tw_.access);
			return;
		}
		const QString msg =
			o.value(QStringLiteral("message")).toString();
		if (msg.contains(QStringLiteral("authorization_pending")) &&
		    --twPollsLeft_ > 0) {
			pending_ = Op::TwPoll;
			QTimer::singleShot(tw_.pollInterval * 1000, this,
					   &MetadataDock::onTwitchPollTimeout);
			return;
		}
		if (msg.contains(QStringLiteral("slow_down"))) {
			tw_.pollInterval += 5;
			if (--twPollsLeft_ > 0) {
				pending_ = Op::TwPoll;
				QTimer::singleShot(
					tw_.pollInterval * 1000, this,
					&MetadataDock::onTwitchPollTimeout);
				return;
			}
		}
		fail(P::Twitch,
		     msg.contains(QStringLiteral("access_denied"))
			     ? tr("Authorization denied.")
			     : tr("Device flow expired. Try again."));
		return;
	}

	case Op::TwValidate: {
		if (netFail || http != 200) {
			fail(P::Twitch, tr("Token validation failed."));
			return;
		}
		const QJsonObject o = replyJson(reply);
		bool scoped = false;
		for (const auto &v :
		     o.value(QStringLiteral("scopes")).toArray())
			if (v.toString() ==
			    QString::fromLatin1(kTwScope))
				scoped = true;
		if (!scoped) {
			fail(P::Twitch,
			     tr("Missing channel:manage:broadcast scope."));
			return;
		}
		tw_.broadcaster =
			o.value(QStringLiteral("user_id")).toString();
		finishConnectOk(
			P::Twitch,
			o.value(QStringLiteral("login")).toString());
		return;
	}

	case Op::YtExchange: {
		if (netFail || http != 200) {
			connectHttpError(P::YouTube, "exchange", http,
					 netFail);
			return;
		}
		const QJsonObject o = replyJson(reply);
		yt_.access = o.value(QStringLiteral("access_token"))
				     .toString();
		yt_.refresh = o.value(QStringLiteral("refresh_token"))
				      .toString();
		if (yt_.access.isEmpty()) {
			fail(P::YouTube, tr("No access token returned."));
			return;
		}
		QUrl url(QStringLiteral(
			"https://www.googleapis.com/youtube/v3/channels"));
		QUrlQuery q;
		q.addQueryItem(QStringLiteral("part"),
			       QStringLiteral("snippet"));
		q.addQueryItem(QStringLiteral("mine"),
			       QStringLiteral("true"));
		url.setQuery(q);
		sendGet(url, Op::YtChannels, yt_.access);
		return;
	}

	case Op::YtChannels: {
		QString label = tr("connected");
		if (!netFail && http == 200) {
			const QJsonArray items =
				replyJson(reply)
					.value(QStringLiteral("items"))
					.toArray();
			if (!items.isEmpty())
				label = items.first().toObject()
						.value(QStringLiteral("snippet"))
						.toObject()
						.value(QStringLiteral("title"))
						.toString(label);
		}
		finishConnectOk(P::YouTube, label);
		return;
	}

	case Op::KkExchange: {
		if (netFail || http != 200) {
			connectHttpError(P::Kick, "exchange", http, netFail);
			return;
		}
		const QJsonObject o = replyJson(reply);
		kk_.access = o.value(QStringLiteral("access_token"))
				     .toString();
		kk_.refresh = o.value(QStringLiteral("refresh_token"))
				      .toString();
		if (kk_.access.isEmpty()) {
			fail(P::Kick, tr("No access token returned."));
			return;
		}
		sendGet(QUrl(QStringLiteral(
					"https://api.kick.com/public/v1/channels")),
			Op::KkChannels, kk_.access, QString(), true);
		return;
	}

	case Op::KkChannels: {
		QString label = tr("connected");
		if (!netFail && http == 200) {
			const QJsonArray items =
				replyJson(reply)
					.value(QStringLiteral("data"))
					.toArray();
			if (!items.isEmpty()) {
				const QString slug =
					items.first().toObject()
						.value(QStringLiteral("slug"))
						.toString();
				if (!slug.isEmpty())
					label = slug;
			}
		}
		finishConnectOk(P::Kick, label);
		return;
	}

	case Op::YtList: {
		refreshButton_->setEnabled(true);
		if (netFail || http != 200) {
			const meta::Outcome oc = netFail
							 ? meta::Outcome::
								   NetworkError
							 : meta::classifyStatus(
								   http);
			if (applyAfterList_) {
				applyAfterList_ = false;
				const P p = applyQueue_.takeFirst();
				finishPlatform(p, false,
					       meta::userMessage(oc, p));
				startApplyNext();
			} else {
				generalMsg_->setText(meta::userMessage(
					oc, P::YouTube));
			}
			return;
		}
		broadcastCombo_->clear();
		const QJsonArray items = replyJson(reply)
						 .value(QStringLiteral("items"))
						 .toArray();
		for (const auto &v : items) {
			const QJsonObject it = v.toObject();
			const QJsonObject sn =
				it.value(QStringLiteral("snippet")).toObject();
			const QString title =
				sn.value(QStringLiteral("title")).toString();
			const QString id =
				it.value(QStringLiteral("id")).toString();
			broadcastCombo_->addItem(
				QStringLiteral("%1 (%2)").arg(title, id),
				QString::fromUtf8(QJsonDocument(it).toJson(
					QJsonDocument::Compact)));
		}
		if (applyAfterList_) {
			applyAfterList_ = false;
			if (broadcastCombo_->count() == 0 &&
			    !applyQueue_.isEmpty()) {
				const P p = applyQueue_.takeFirst();
				finishPlatform(p, false,
					       meta::userMessage(
						       meta::Outcome::NotFound,
						       p));
			}
			startApplyNext();
		} else {
			generalMsg_->setText(
				tr("Broadcasts loaded (%1).")
					.arg(broadcastCombo_->count()));
		}
		return;
	}

	case Op::UpTw:
	case Op::UpTwRetry: {
		const meta::Outcome oc = netFail
						 ? meta::Outcome::NetworkError
						 : meta::classifyStatus(http);
		if (oc == meta::Outcome::AuthRequired && !retried_ &&
		    !tw_.refresh.isEmpty() && op == Op::UpTw) {
			retried_ = true;
			refreshWithToken(P::Twitch, Op::TwRefresh);
			return;
		}
		const P p = P::Twitch;
		if (oc == meta::Outcome::Success) {
			finishPlatform(p, true,
				       meta::userMessage(oc, p));
		} else {
			if (oc == meta::Outcome::AuthRequired) {
				tw_.connected = false;
				setStatus(p, tr("Needs reconnection."));
			}
			finishPlatform(p, false,
				       meta::userMessage(oc, p));
		}
		startApplyNext();
		return;
	}

	case Op::TwRefresh: {
		if (!netFail && http == 200) {
			const QJsonObject o = replyJson(reply);
			tw_.access = o.value(QStringLiteral("access_token"))
					     .toString();
			tw_.refresh =
				o.value(QStringLiteral("refresh_token"))
					.toString();
			QUrl url(QStringLiteral(
				"https://api.twitch.tv/helix/channels"));
			QUrlQuery q;
			q.addQueryItem(QStringLiteral("broadcaster_id"),
				       tw_.broadcaster);
			url.setQuery(q);
			sendJson(url, QStringLiteral("PATCH"),
				 meta::twitchPayload(titleEdit_->text()),
				 Op::UpTwRetry, tw_.access, tw_.clientId);
		} else {
			tw_.connected = false;
			setStatus(P::Twitch, tr("Needs reconnection."));
			finishPlatform(P::Twitch, false,
				       meta::userMessage(
					       meta::Outcome::AuthRequired,
					       P::Twitch));
			startApplyNext();
		}
		return;
	}

	case Op::UpYt:
	case Op::UpYtRetry: {
		const meta::Outcome oc = netFail
						 ? meta::Outcome::NetworkError
						 : meta::classifyStatus(http);
		if (oc == meta::Outcome::AuthRequired && !retried_ &&
		    !yt_.refresh.isEmpty() && op == Op::UpYt) {
			retried_ = true;
			refreshWithToken(P::YouTube, Op::YtRefresh);
			return;
		}
		if (oc == meta::Outcome::AuthRequired) {
			yt_.connected = false;
			setStatus(P::YouTube, tr("Needs reconnection."));
		}
		finishPlatform(P::YouTube, oc == meta::Outcome::Success,
			       meta::userMessage(oc, P::YouTube));
		startApplyNext();
		return;
	}

	case Op::YtRefresh: {
		if (!netFail && http == 200) {
			const QJsonObject o = replyJson(reply);
			yt_.access = o.value(QStringLiteral("access_token"))
					     .toString();
			const QString item =
				broadcastCombo_->currentData().toString();
			QUrl url(QStringLiteral(
				"https://www.googleapis.com/youtube/v3/"
				"liveBroadcasts"));
			QUrlQuery q;
			q.addQueryItem(QStringLiteral("part"),
				       QStringLiteral("snippet"));
			url.setQuery(q);
			sendJson(url, QStringLiteral("PUT"),
				 meta::youTubePayload(item,
						      titleEdit_->text(),
						      descEdit_->toPlainText()),
				 Op::UpYtRetry, yt_.access);
		} else {
			yt_.connected = false;
			setStatus(P::YouTube, tr("Needs reconnection."));
			finishPlatform(P::YouTube, false,
				       meta::userMessage(
					       meta::Outcome::AuthRequired,
					       P::YouTube));
			startApplyNext();
		}
		return;
	}

	case Op::UpKk:
	case Op::UpKkRetry: {
		const meta::Outcome oc = netFail
						 ? meta::Outcome::NetworkError
						 : meta::classifyStatus(http);
		if (oc == meta::Outcome::AuthRequired && !retried_ &&
		    !kk_.refresh.isEmpty() && op == Op::UpKk) {
			retried_ = true;
			refreshWithToken(P::Kick, Op::KkRefresh);
			return;
		}
		if (oc == meta::Outcome::AuthRequired) {
			kk_.connected = false;
			setStatus(P::Kick, tr("Needs reconnection."));
		}
		finishPlatform(P::Kick, oc == meta::Outcome::Success,
			       meta::userMessage(oc, P::Kick));
		startApplyNext();
		return;
	}

	case Op::KkRefresh: {
		if (!netFail && http == 200) {
			const QJsonObject o = replyJson(reply);
			kk_.access = o.value(QStringLiteral("access_token"))
					     .toString();
			kk_.refresh =
				o.value(QStringLiteral("refresh_token"))
					.toString();
			sendJson(QUrl(QStringLiteral(
					 "https://api.kick.com/public/v1/"
					 "channels")),
				 QStringLiteral("PATCH"),
				 meta::kickPayload(titleEdit_->text()),
				 Op::UpKkRetry, kk_.access, QString(), true);
		} else {
			kk_.connected = false;
			setStatus(P::Kick, tr("Needs reconnection."));
			finishPlatform(P::Kick, false,
				       meta::userMessage(
					       meta::Outcome::AuthRequired,
					       P::Kick));
			startApplyNext();
		}
		return;
	}
	}
}
