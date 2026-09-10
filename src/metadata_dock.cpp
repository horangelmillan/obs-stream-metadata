/*
T-031: MVP dock. Qt Widgets + QNetworkAccessManager (async, no extra libs).
T-032: app credentials typed into the dock (BYO-app, ADR-008); tokens and
secrets in memory + DPAPI-encrypted store, never plaintext/printed/logged.
*/

#include "metadata_dock.h"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>
#include <util/bmem.h>

#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QFrame>
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
#include <QScrollArea>
#include <QSignalBlocker>
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

QString field(QLineEdit *edit)
{
	return edit ? edit->text().trimmed() : QString();
}

QJsonObject replyJson(QNetworkReply *reply)
{
	return QJsonDocument::fromJson(reply->readAll()).object();
}

} // namespace

MetadataDock::MetadataDock(QWidget *parent) : QWidget(parent)
{
	// Scrollable body: when docked into OBS the panel must scroll
	// instead of forcing the main window wide.
	QWidget *body = new QWidget(this);
	QVBoxLayout *top = new QVBoxLayout(body);

	// T-041: connection mode selector (ADR-012). One mode per plugin
	// installation; Independent keeps the existing direct/BYO-app flows,
	// Managed only prepares the context until T-048 wires the backend.
	QLabel *modeLabel = new QLabel(tr("Connection mode"), this);
	modeCombo_ = new QComboBox(this);
	modeCombo_->addItem(QString::fromLatin1(meta::connectionModeName(
		meta::ConnectionMode::Independent)));
	modeCombo_->addItem(QString::fromLatin1(meta::connectionModeName(
		meta::ConnectionMode::Managed)));
	connect(modeCombo_,
		static_cast<void (QComboBox::*)(int)>(
			&QComboBox::currentIndexChanged),
		this, &MetadataDock::onModeChanged);
	top->addWidget(modeLabel);
	top->addWidget(modeCombo_);

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

	QLabel *credTitle =
		new QLabel(tr("App credentials (register your own app per "
			      "platform)"),
			   this);
	credTitle_ = credTitle;
	top->addWidget(credTitle);
	twIdEdit_ = new QLineEdit(this);
	twIdEdit_->setPlaceholderText(tr("Twitch Client ID"));
	// T-047: distributed ID (public) prefilled when the build provides
	// one; the user can still override it (BYO/dev preserved).
	{
		const QString distributed =
			meta::twitchClientId(QString());
		if (!distributed.isEmpty())
			twIdEdit_->setText(distributed);
	}
	top->addWidget(twIdEdit_);
	ytIdEdit_ = new QLineEdit(this);
	ytIdEdit_->setPlaceholderText(tr("YouTube Client ID"));
	top->addWidget(ytIdEdit_);
	ytSecretEdit_ = new QLineEdit(this);
	ytSecretEdit_->setPlaceholderText(tr("YouTube Client Secret"));
	ytSecretEdit_->setEchoMode(QLineEdit::Password);
	top->addWidget(ytSecretEdit_);
	kkIdEdit_ = new QLineEdit(this);
	kkIdEdit_->setPlaceholderText(tr("Kick Client ID"));
	top->addWidget(kkIdEdit_);
	kkSecretEdit_ = new QLineEdit(this);
	kkSecretEdit_->setPlaceholderText(tr("Kick Client Secret"));
	kkSecretEdit_->setEchoMode(QLineEdit::Password);
	top->addWidget(kkSecretEdit_);
	QLabel *credNote = new QLabel(
		tr("Typed once: kept in memory and stored encrypted on this "
		   "PC (DPAPI). Never logged, never shared."),
		this);
	credNote->setWordWrap(true);
	credNote_ = credNote;
	top->addWidget(credNote);
	managedNote_ = new QLabel(
		tr("Managed mode: connections go through the managed "
		   "backend. No Client ID or Secret needed. Your "
		   "Independent credentials stay stored locally and are "
		   "never used for Managed."),
		this);
	managedNote_->setWordWrap(true);
	managedNote_->setVisible(false);
	top->addWidget(managedNote_);

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

	body->setLayout(top);
	QScrollArea *scroll = new QScrollArea(this);
	scroll->setWidget(body);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	QVBoxLayout *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->addWidget(scroll);
	setLayout(outer);

	net_ = new QNetworkAccessManager(this);
	connect(net_, &QNetworkAccessManager::finished, this,
		&MetadataDock::onReply);

	// T-032: encrypted account store (module config dir). Missing dir
	// or DPAPI failure -> memory-only fallback, never plaintext.
	const QString sp = storePath();
	if (!sp.isEmpty())
		store_ = new secure::Store(sp);
	else
		obs_log(LOG_WARNING, "account store unavailable, "
				     "sessions will not survive restart");
	loadStore();
	refreshModeUi();
	initManaged();
	obs_log(LOG_INFO, "dock ready (mode=%s)",
		meta::connectionModeName(mode_));
}

MetadataDock::~MetadataDock()
{
	delete store_;
	store_ = nullptr;
}

// --- connection mode (T-041, ADR-012) -------------------------------------

bool MetadataDock::isManaged() const
{
	return mode_ == meta::ConnectionMode::Managed;
}

meta::ConnectionMode MetadataDock::modeFromCombo() const
{
	if (modeCombo_ && modeCombo_->currentIndex() == 1)
		return meta::ConnectionMode::Managed;
	return meta::defaultConnectionMode();
}

void MetadataDock::onModeChanged(int)
{
	// Selecting a mode never moves or deletes credentials/tokens: it only
	// changes the context future Connect flows will use (T-048 wires
	// Managed). A pending Managed poll belongs to the old selection and
	// is abandoned (backend transaction expires on its own TTL).
	if (managedPollTimer_)
		managedPollTimer_->stop();
	mode_ = modeFromCombo();
	refreshModeUi();
	saveStore();
}

void MetadataDock::refreshModeUi()
{
	const bool managed = isManaged();
	if (modeCombo_) {
		const QSignalBlocker block(modeCombo_);
		modeCombo_->setCurrentIndex(managed ? 1 : 0);
	}
	credTitle_->setVisible(!managed);
	twIdEdit_->setVisible(!managed);
	ytIdEdit_->setVisible(!managed);
	ytSecretEdit_->setVisible(!managed);
	kkIdEdit_->setVisible(!managed);
	kkSecretEdit_->setVisible(!managed);
	credNote_->setVisible(!managed);
	managedNote_->setVisible(managed);
	repaintModeStatuses();
}

// --- managed wiring (T-048; Independent handlers untouched) --------------

namespace {

// DEV default only: local backend under test. Overridable (DEV-only) via
// STREAM_META_BACKEND_URL. No production URL is bundled in T-048.
const char *kManagedDefaultBaseUrl = "http://127.0.0.1:8080";

QString providerSlug(meta::Platform p)
{
	if (p == meta::Platform::YouTube)
		return QStringLiteral("youtube");
	if (p == meta::Platform::Kick)
		return QStringLiteral("kick");
	return QStringLiteral("twitch");
}

// Backend-backed providers in T-048 (YouTube T-045, Kick T-046). Twitch has
// no backend service: representable, not functional (§12 of the task).
bool managedSupported(meta::Platform p)
{
	return p == meta::Platform::YouTube || p == meta::Platform::Kick;
}

} // namespace

void MetadataDock::initManaged()
{
	managedBaseUrl_ = QString::fromLocal8Bit(
		qgetenv("STREAM_META_BACKEND_URL"));
	if (managedBaseUrl_.isEmpty())
		managedBaseUrl_ =
			QString::fromLatin1(kManagedDefaultBaseUrl);
	while (managedBaseUrl_.endsWith(QLatin1Char('/')))
		managedBaseUrl_.chop(1);
	managedAuth_ = new backend_auth::Client(net_, this);
	managedAuth_->setBaseUrl(managedBaseUrl_);
	managedAuth_->setStore(store_);
	managedPollTimer_ = new QTimer(this);
	managedPollTimer_->setSingleShot(false);
	connect(managedPollTimer_, &QTimer::timeout, this,
		&MetadataDock::onManagedPollTimeout);
}

MetadataDock::MetadataDock::ManagedConn &MetadataDock::managedAccount(meta::Platform p)
{
	if (p == meta::Platform::YouTube)
		return mYt_;
	return mKk_;
}

void MetadataDock::repaintModeStatuses()
{
	// Labels always reflect the active mode's own state objects; the
	// other mode's accounts are never read here (§17: no mixing).
	if (isManaged()) {
		for (meta::Platform p :
		     {meta::Platform::YouTube, meta::Platform::Kick}) {
			const ManagedConn &m = managedAccount(p);
			if (m.connected && !m.display.isEmpty())
				setStatus(p, tr("Connected as %1").arg(m.display));
			else
				setStatus(p, tr("Not connected"));
		}
		return;
	}
	for (meta::Platform p :
	     {meta::Platform::Twitch, meta::Platform::YouTube,
	      meta::Platform::Kick}) {
		const Account &a = account(p);
		if (a.connected && !a.display.isEmpty())
			setStatus(p, tr("Connected as %1").arg(a.display));
		else
			setStatus(p, tr("Not connected"));
	}
}

void MetadataDock::onConnectManaged(meta::Platform p)
{
	if (!managedSupported(p)) {
		setResult(p, false,
			  tr("Managed Twitch is not available yet "
			     "(direct only)."));
		return;
	}
	startConnectBusy(p);
	managedAuth_->ensureSession([this, p](backend_auth::Result r) {
		if (r == backend_auth::Result::StorageError) {
			// No installation yet: bootstrap once, then retry.
			managedAuth_->bootstrap([this, p](backend_auth::Result b) {
				if (b != backend_auth::Result::Ok) {
					finishManagedError(
						p, managedAuthError(p, b, 0));
					return;
				}
				onConnectManaged(p);
			});
			return;
		}
		if (r != backend_auth::Result::Ok) {
			finishManagedError(p, managedAuthError(p, r, 0));
			return;
		}
		// T-051 reconstruction: if the backend still holds this
		// installation's connection (e.g. after OBS restart), reuse it
		// without a new browser flow. Anything else falls through.
		managedAuth_->apiGet(QStringLiteral("/connect/%1/status").arg(
					     providerSlug(p)),
				     [this, p](const backend_auth::Client::ApiReply &rep) {
					     if (rep.result ==
						     backend_auth::Result::Ok) {
						     const QJsonObject o = rep.body;
						     if (o.value(QStringLiteral("status"))
								     .toString() ==
							 QStringLiteral("connected")) {
							     const QJsonObject acc =
								     o.value(QStringLiteral(
									     "account"))
									     .toObject();
							     finishManagedConnected(
								     p, acc.value(QStringLiteral(
											"id"))
										.toString(),
								     acc.value(QStringLiteral(
											"displayName"))
										.toString());
							     return;
						     }
					     }
					     startManagedBrowserFlow(p);
				     });
	});
}

void MetadataDock::startManagedBrowserFlow(meta::Platform p)
{
	managedAuth_->apiPost(QStringLiteral("/connect/%1").arg(
					      providerSlug(p)),
			      QJsonObject(),
			      [this, p](const backend_auth::Client::ApiReply &rep) {
					      if (rep.result !=
						      backend_auth::Result::Ok) {
						      finishManagedError(
							      p, managedApiError(
									 p, rep));
						      return;
					      }
					      const QString url = rep.body
								  .value(QStringLiteral(
									  "authorization_url"))
								  .toString();
					      if (url.isEmpty()) {
						      finishManagedError(
							      p, tr("Backend returned no "
								    "authorization URL."));
						      return;
					      }
					      openBrowser(url);
					      setStatus(p, tr("Waiting for browser "
							      "authorization…"));
				      managedPollFor_ = p;
				      managedPollsLeft_ = 120; // 5 s x 10 min
				      managedPollTimer_->start(5000);
			      });
}

void MetadataDock::onManagedPollTimeout()
{
	const meta::Platform p = managedPollFor_;
	if (--managedPollsLeft_ < 0) {
		managedPollTimer_->stop();
		finishManagedError(p, tr("Timed out waiting for authorization."));
		return;
	}
	managedAuth_->apiGet(QStringLiteral("/connect/%1/status").arg(
					     providerSlug(p)),
			     [this, p](const backend_auth::Client::ApiReply &rep) {
				     if (rep.result != backend_auth::Result::Ok) {
					     if (rep.result == backend_auth::Result::
								    Unauthorized) {
						     managedPollTimer_->stop();
						     finishManagedError(
							     p, tr("Session expired. "
								   "Press Connect again."));
					     }
					     // Other transient failures: keep
					     // polling until timeout (no loops
					     // beyond the bounded poll count).
					     return;
				     }
				     const QJsonObject o = rep.body;
				     if (o.value(QStringLiteral("status")).toString() ==
					 QStringLiteral("connected")) {
					     managedPollTimer_->stop();
					     const QJsonObject acc = o.value(
							     QStringLiteral(
								     "account"))
							     .toObject();
					     finishManagedConnected(
						     p, acc.value(QStringLiteral("id"))
								.toString(),
						     acc.value(QStringLiteral(
									    "displayName"))
								.toString());
				     }
				     // Else still disconnected: keep polling.
			     });
}

void MetadataDock::finishManagedConnected(meta::Platform p,
					  const QString &userId,
					  const QString &display)
{
	ManagedConn &m = managedAccount(p);
	m.connected = true;
	m.userId = userId;
	m.display = display;
	setStatus(p, tr("Connected as %1").arg(display));
	setResult(p, true, tr("connected"));
	obs_log(LOG_INFO, "managed connected: %s", meta::platformName(p));
	saveStore(); // persist the snapshot (identity labels only, T-051)
}

void MetadataDock::finishManagedError(meta::Platform p, const QString &msg)
{
	managedPollTimer_->stop();
	setStatus(p, tr("Error: %1").arg(msg));
	obs_log(LOG_WARNING, "managed connect failed: %s",
		meta::platformName(p));
}

QString MetadataDock::managedAuthError(meta::Platform p, backend_auth::Result r,
				       int)
{
	switch (r) {
	case backend_auth::Result::RateLimited:
		return meta::userMessage(meta::Outcome::RateLimited, p);
	case backend_auth::Result::NetworkError:
		return tr("Could not reach the managed backend (network).");
	case backend_auth::Result::StorageError:
		return tr("Local installation storage failed (DPAPI).");
	default:
		return tr("Backend authentication failed.");
	}
}

QString MetadataDock::managedApiError(meta::Platform p,
				      const backend_auth::Client::ApiReply &rep)
{
	// Reuse the common UX error system (§22); backend bodies carry only
	// safe fields, and only the HTTP code leaves this function.
	switch (rep.result) {
	case backend_auth::Result::Unauthorized:
		return tr("Session expired. Press Connect again.");
	case backend_auth::Result::RateLimited:
		return meta::userMessage(meta::Outcome::RateLimited, p);
	case backend_auth::Result::ServerError:
		return meta::userMessage(meta::Outcome::ServerRetry, p);
	case backend_auth::Result::NetworkError:
		return tr("Could not reach the managed backend (network).");
	default:
		return meta::userMessage(
			meta::classifyStatus(rep.http == 0 ? -1 : rep.http), p);
	}
}

void MetadataDock::onDisconnectManaged(meta::Platform p)
{
	if (!managedSupported(p)) {
		setResult(p, false,
			  tr("Managed Twitch is not available yet "
			     "(direct only)."));
		return;
	}
	// Clear local Managed state first (mirrors Independent semantics),
	// then best-effort remote disconnect; a network failure must not
	// resurrect the local state.
	ManagedConn &m = managedAccount(p);
	m.connected = false;
	m.userId.clear();
	m.display.clear();
	setStatus(p, tr("Not connected"));
	setResult(p, true, tr("disconnected"));
	saveStore(); // drop the snapshot; Independent records untouched
	managedAuth_->ensureSession([this, p](backend_auth::Result r) {
		if (r != backend_auth::Result::Ok)
			return; // local state already cleared
		managedAuth_->apiPost(QStringLiteral("/connect/%1/disconnect").arg(
						      providerSlug(p)),
				      QJsonObject(),
				      [](const backend_auth::Client::ApiReply &) {});
	});
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
	setResult(p, true, tr("connected"));
	obs_log(LOG_INFO, "oauth connected: %s", meta::platformName(p));
	saveStore(); // tokens survive restart (DPAPI, never plaintext)
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

// --- secure store (T-032) ------------------------------------------------

QString MetadataDock::storePath()
{
	char *raw = obs_module_config_path("accounts.json");
	if (!raw)
		return QString();
	const QString path = QString::fromUtf8(raw);
	bfree(raw);
	return path;
}

void MetadataDock::saveStore()
{
	if (!store_)
		return;
	secure::Data d;
	// Preserve anything this dock does not own (T-044 backendInstall):
	// load first, then overwrite only provider records + mode.
	store_->load(d);
	auto fill = [](const Account &a, secure::Record &r) {
		r.connected = a.connected && !a.access.isEmpty();
		if (!r.connected)
			return;
		r.display = a.display;
		r.broadcaster = a.broadcaster;
		r.access = a.access;
		r.refresh = a.refresh;
		r.clientId = a.clientId;
		r.secret = a.secret;
	};
	fill(tw_, d.twitch);
	fill(yt_, d.youtube);
	fill(kk_, d.kick);
	// T-051: Managed snapshots (identity labels only; the structs
	// cannot hold secrets by construction). Omitted when disconnected.
	auto fillManaged = [](const ManagedConn &m,
			      secure::ManagedSnapshot &s) {
		s.connected = m.connected && !m.display.isEmpty();
		if (!s.connected) {
			s.userId.clear();
			s.display.clear();
			return;
		}
		s.userId = m.userId;
		s.display = m.display;
	};
	fillManaged(mYt_, d.managedYoutube);
	fillManaged(mKk_, d.managedKick);
	// T-041: the mode is always persisted explicitly (idempotent
	// migration: first save after upgrade writes it). Legacy clear
	// behavior stays unless Managed was explicitly selected or a
	// Managed snapshot exists.
	d.connectionMode = QString::fromLatin1(meta::connectionModeName(mode_))
				   .toLower();
	if (!d.anyConnected() && !d.anyManaged() && !isManaged()) {
		store_->clear();
		return;
	}
	if (!store_->save(d))
		obs_log(LOG_WARNING, "account store save failed");
}

void MetadataDock::loadStore()
{
	if (!store_)
		return;
	secure::Data d;
	store_->load(d); // return ignored on purpose: the mode restores
	// even when zero providers are connected (T-041).
	{
		const std::optional<meta::ConnectionMode> m =
			meta::parseConnectionMode(d.connectionMode);
		// Missing/invalid (pre-T-041 files) -> Independent, never
		// Managed: no silent upgrade into the service-operated mode.
		mode_ = m.value_or(meta::defaultConnectionMode());
	}
	auto restore = [](Account &a, const secure::Record &r) {
		a.clear();
		if (!r.connected)
			return;
		a.connected = true;
		a.display = r.display;
		a.broadcaster = r.broadcaster;
		a.access = r.access;
		a.refresh = r.refresh;
		a.clientId = r.clientId;
		a.secret = r.secret;
	};
	restore(tw_, d.twitch);
	restore(yt_, d.youtube);
	restore(kk_, d.kick);
	// T-051: Managed snapshots restore memory-only state (identity
	// labels, no secrets). Revalidation happens on user action via
	// /status (onConnectManaged); nothing is auto-fetched at startup.
	auto restoreManaged = [](ManagedConn &m,
				 const secure::ManagedSnapshot &s) {
		m.connected = false;
		m.userId.clear();
		m.display.clear();
		if (!s.connected)
			return;
		m.connected = true;
		m.userId = s.userId;
		m.display = s.display;
	};
	restoreManaged(mYt_, d.managedYoutube);
	restoreManaged(mKk_, d.managedKick);
	if (tw_.connected)
		twIdEdit_->setText(tw_.clientId);
	if (yt_.connected) {
		ytIdEdit_->setText(yt_.clientId);
		ytSecretEdit_->setText(yt_.secret);
	}
	if (kk_.connected) {
		kkIdEdit_->setText(kk_.clientId);
		kkSecretEdit_->setText(kk_.secret);
	}
	for (meta::Platform p :
	     {meta::Platform::Twitch, meta::Platform::YouTube,
	      meta::Platform::Kick}) {
		if (!account(p).connected)
			continue;
		setStatus(p, tr("Connected as %1").arg(account(p).display));
		setResult(p, true, tr("connected"));
		obs_log(LOG_INFO, "session restored: %s",
			meta::platformName(p));
	}
}

// --- revoke on Disconnect (T-032, best effort) ---------------------------

void MetadataDock::startRevoke(meta::Platform p, const Account &snapshot)
{
	revokeFor_ = p;
	revokeClientId_ = snapshot.clientId;
	revokeQueue_.clear();
	if (!snapshot.access.isEmpty())
		revokeQueue_ << snapshot.access;
	if (!snapshot.refresh.isEmpty() &&
	    snapshot.refresh != snapshot.access)
		revokeQueue_ << snapshot.refresh;
	sendNextRevoke();
}

void MetadataDock::sendNextRevoke()
{
	if (revokeQueue_.isEmpty()) {
		pending_ = Op::None;
		return;
	}
	const QString tok = revokeQueue_.takeFirst();
	const meta::RevokeEndpoint ep = meta::revokeEndpoint(revokeFor_);
	Op op = Op::RevTw;
	if (revokeFor_ == meta::Platform::YouTube)
		op = Op::RevYt;
	else if (revokeFor_ == meta::Platform::Kick)
		op = Op::RevKk;
	QUrl url(QString::fromLatin1(ep.url));
	QUrlQuery q;
	if (ep.tokenField[0] == QLatin1Char('\0')) {
		// Twitch: client_id + token in query, empty form body.
		q.addQueryItem(QStringLiteral("client_id"), revokeClientId_);
		q.addQueryItem(QStringLiteral("token"), tok);
		url.setQuery(q);
		sendForm(url, QString(), op);
	} else {
		q.addQueryItem(QString::fromLatin1(ep.tokenField), tok);
		sendForm(url, q.toString(QUrl::FullyEncoded), op, QString(),
			 ep.browserUa);
	}
}

void MetadataDock::wipeLocal(meta::Platform p)
{
	account(p).clear();
	if (p == meta::Platform::YouTube)
		broadcastCombo_->clear();
	setStatus(p, tr("Not connected"));
	setResult(p, true, tr("disconnected"));
	saveStore(); // drop the record; survivors stay encrypted
}

// --- connect: Twitch device flow (no secret, F-015) --------------------

void MetadataDock::onConnectTwitch()
{
	// T-041: Managed has no flow yet (T-048). Explicit pending state:
	// no network, no simulation, no fallback into Independent.
	if (isManaged()) {
		setResult(meta::Platform::Twitch, false,
			  tr("Managed connections arrive with T-048."));
		return;
	}
	Account &a = tw_;
	a.clear();
	// T-047: explicit field wins (BYO/dev); otherwise the distributed
	// build-time ID (public, DCF needs no secret).
	const QString id = meta::twitchClientId(field(twIdEdit_));
	if (id.isEmpty()) {
		finishConnectError(meta::Platform::Twitch,
				   tr("Enter your Twitch Client ID first."));
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
	const Account snap = tw_; // revoke needs the wiped tokens
	wipeLocal(meta::Platform::Twitch);
	startRevoke(meta::Platform::Twitch, snap);
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
	if (isManaged()) {
		onConnectManaged(meta::Platform::YouTube);
		return;
	}
	Account &a = yt_;
	a.clear();
	const QString id = field(ytIdEdit_);
	const QString secret = field(ytSecretEdit_);
	if (id.isEmpty() || secret.isEmpty()) {
		finishConnectError(meta::Platform::YouTube,
				   tr("Enter your YouTube Client ID and "
				      "Client Secret first."));
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
	if (isManaged()) {
		onDisconnectManaged(meta::Platform::YouTube);
		return;
	}
	const Account snap = yt_;
	wipeLocal(meta::Platform::YouTube);
	startRevoke(meta::Platform::YouTube, snap);
}

void MetadataDock::onConnectKick()
{
	if (isManaged()) {
		onConnectManaged(meta::Platform::Kick);
		return;
	}
	Account &a = kk_;
	a.clear();
	const QString id = field(kkIdEdit_);
	const QString secret = field(kkSecretEdit_);
	if (id.isEmpty() || secret.isEmpty()) {
		finishConnectError(meta::Platform::Kick,
				   tr("Enter your Kick Client ID and "
				      "Client Secret first."));
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
	if (isManaged()) {
		onDisconnectManaged(meta::Platform::Kick);
		return;
	}
	const Account snap = kk_;
	wipeLocal(meta::Platform::Kick);
	startRevoke(meta::Platform::Kick, snap);
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
	if (!backoffResume_) {
		// Fresh platform: one refresh retry + full backoff budget.
		// A backoff resend keeps both (bounded, no loops).
		retried_ = false;
		backoffCount_ = 0;
	}
	backoffResume_ = false;
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

bool MetadataDock::scheduleBackoff(meta::Platform p, meta::Outcome oc,
				   int http)
{
	if (oc != meta::Outcome::RateLimited &&
	    oc != meta::Outcome::ServerRetry)
		return false;
	if (backoffCount_ >= meta::kBackoffMaxRetries)
		return false;
	++backoffCount_;
	backoffResume_ = true; // keep the refresh-once budget
	applyQueue_.prepend(p);
	setResult(p, true, tr("Retrying…"));
	obs_log(LOG_WARNING, "apply %s http %d backing off (%d/%d)",
		meta::platformName(p), http, backoffCount_,
		meta::kBackoffMaxRetries);
	QTimer::singleShot(meta::backoffDelayMs(backoffCount_), this,
			   &MetadataDock::onBackoffTimeout);
	return true;
}

void MetadataDock::onBackoffTimeout()
{
	// Bounded resend of the same platform update (§28: limited
	// backoff, never a retry loop). startApplyNext keeps retried_.
	startApplyNext();
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
		--twPollsLeft_; // one attempt consumed by this response (T-047)
		switch (meta::classifyTwPoll(
			http, netFail,
			o.value(QStringLiteral("message")).toString(),
			twPollsLeft_)) {
		case meta::TwPollAction::Consume: {
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
		case meta::TwPollAction::KeepPolling:
			pending_ = Op::TwPoll;
			QTimer::singleShot(tw_.pollInterval * 1000, this,
					   &MetadataDock::onTwitchPollTimeout);
			return;
		case meta::TwPollAction::SlowDown:
			tw_.pollInterval += 5;
			pending_ = Op::TwPoll;
			QTimer::singleShot(
				tw_.pollInterval * 1000, this,
				&MetadataDock::onTwitchPollTimeout);
			return;
		case meta::TwPollAction::FailDenied:
			fail(P::Twitch, tr("Authorization denied."));
			return;
		case meta::TwPollAction::FailExpired:
		default:
			fail(P::Twitch, tr("Device flow expired. Try again."));
			return;
		}
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
		const P p = P::Twitch;
		const meta::Outcome oc = netFail
						 ? meta::Outcome::NetworkError
						 : meta::classifyStatus(http);
		if (oc == meta::Outcome::AuthRequired && !retried_ &&
		    !tw_.refresh.isEmpty() && op == Op::UpTw) {
			retried_ = true;
			refreshWithToken(P::Twitch, Op::TwRefresh);
			return;
		}
		if (scheduleBackoff(p, oc, http))
			return;
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
			saveStore(); // rotated tokens persist encrypted
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
		if (scheduleBackoff(P::YouTube, oc, http))
			return;
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
			saveStore(); // rotated tokens persist encrypted
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
		if (scheduleBackoff(P::Kick, oc, http))
			return;
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
			saveStore(); // rotated tokens persist encrypted
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

	case Op::RevTw:
	case Op::RevYt:
	case Op::RevKk: {
		// Best-effort revoke: local state is already wiped; the
		// code is the only thing worth logging. Chain the next
		// token (refresh) if any.
		obs_log(LOG_INFO, "revoke %s http %d",
			meta::platformName(revokeFor_), http);
		sendNextRevoke();
		return;
	}
	}
}
