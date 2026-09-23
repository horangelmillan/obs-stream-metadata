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
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
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
// T-071 FB-2: perfil primero (D9, F-072). Las pages_* son invalid scope
// en apps tipo Consumer: solo publish_video. Page queda diferido hasta
// decidir tipo de app (None/Business) o App Review. Jamas groups/email.
const char *kFbScope = "publish_video";
const char *kFbGraph = "https://graph.facebook.com/v26.0";
const quint16 kFbPort = 3001; // fijo como Kick: debe registrarse exacto
const char *kFbCbPath = "/fb-cb";

QString field(QLineEdit *edit)
{
	return edit ? edit->text().trimmed() : QString();
}

QJsonObject replyJson(QNetworkReply *reply)
{
	return QJsonDocument::fromJson(reply->readAll()).object();
}

// Forward: defined with the Managed wiring below; needed by the
// constructor's build-identity log (FASE 2.1-B).
bool managedSupported(meta::Platform p);

// FASE 1.1: platform glyphs as header icons (local Qt only: no downloads,
// no assets). Byte escapes keep them encoding-independent (the build sets
// no /utf-8 flag, so raw emoji literals would be codepage-dependent).
QString platformIcon(meta::Platform p)
{
	if (p == meta::Platform::Twitch)
		return QString::fromUtf8("\xF0\x9F\x8E\xAE"); // gamepad
	if (p == meta::Platform::YouTube)
		return QString::fromUtf8("\xE2\x96\xB6\xEF\xB8\x8F"); // play
	if (p == meta::Platform::Facebook)
		return QString::fromLatin1("f"); // facebook badge (ascii-safe)
	return QString::fromUtf8("\xF0\x9F\x9F\xA2"); // green circle
}

QString cardTitle(meta::Platform p, bool open)
{
	// ASCII chevron: guaranteed to render in any font (a previous
	// Unicode close glyph showed as an empty square in OBS).
	return QStringLiteral("%1 %2 %3")
		.arg(platformIcon(p),
		     QString::fromLatin1(meta::platformName(p)),
		     QString::fromLatin1(open ? "v" : ">"));
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

	// UX FASE 1 (Modelo B): one card per platform. The functional
	// widgets (check/status/buttons) are the same objects as before,
	// only reparented: header row (open/close + hover Apply check) on
	// top, detail below (hidden unless expanded). Single-open via
	// expanded_ (see toggleCard/updateCardVisibility).
	QVBoxLayout *twDetailLayout = nullptr;
	QVBoxLayout *ytDetailLayout = nullptr;
	QVBoxLayout *kkDetailLayout = nullptr;
	QVBoxLayout *fbDetailLayout = nullptr;
	auto addCard = [&](meta::Platform p, const QString &name) {
		QFrame *card = new QFrame(this);
		card->setFrameShape(QFrame::StyledPanel);
		QVBoxLayout *cardLayout = new QVBoxLayout(card);
		cardLayout->setContentsMargins(8, 8, 8, 8);
		cardLayout->setSpacing(4);
		QHBoxLayout *headerRow = new QHBoxLayout();
		headerRow->setContentsMargins(0, 0, 0, 0);
		QPushButton *header = new QPushButton(card);
		// Text set by updateCardVisibility() (icon + name + chevron).
		header->setToolTip(tr("Open %1 settings").arg(name));
		header->setAccessibleName(tr("%1 settings").arg(name));
		// The Apply-selection checkbox itself (same semantics as
		// before: onApply() reads isChecked()). Text cleared: the
		// header already names the platform. Visible on hover or
		// when the card is expanded; the selection persists.
		QCheckBox *check = new QCheckBox(card);
		check->setChecked(true);
		check->setToolTip(tr("Include %1 in Apply").arg(name));
		check->setAccessibleName(
			tr("Include %1 in Apply").arg(name));
		check->setVisible(false);
		// FASE 1.2: no separate close button. The header is the only
		// expand/collapse control; the check stays independent so a
		// header click never changes isChecked() and a check click
		// never expands/collapses the card.
		headerRow->addWidget(header, 1);
		headerRow->addWidget(check);
		cardLayout->addLayout(headerRow);
		QWidget *detail = new QWidget(card);
		QVBoxLayout *detailLayout = new QVBoxLayout(detail);
		detailLayout->setContentsMargins(0, 4, 0, 0);
		detail->setVisible(false);
		cardLayout->addWidget(detail);
		card->installEventFilter(this);
		top->addWidget(card);
		connect(header, &QPushButton::clicked, this,
			[this, p]() { toggleCard(p); });
		QLabel *status = new QLabel(tr("Not connected"), detail);
		QPushButton *connBtn =
			new QPushButton(tr("Connect"), detail);
		QPushButton *disc =
			new QPushButton(tr("Disconnect"), detail);
		detailLayout->addWidget(status);
		detailLayout->addWidget(connBtn);
		detailLayout->addWidget(disc);
		if (p == meta::Platform::Twitch) {
			twCard_ = card;
			twHeader_ = header;
			twDetail_ = detail;
			twDetailLayout = detailLayout;
			twCheck_ = check;
			twStatus_ = status;
			twConnect_ = connBtn;
			twDisconnect_ = disc;
			connect(connBtn, &QPushButton::clicked, this,
				&MetadataDock::onConnectTwitch);
			connect(disc, &QPushButton::clicked, this,
				&MetadataDock::onDisconnectTwitch);
		} else if (p == meta::Platform::YouTube) {
			ytCard_ = card;
			ytHeader_ = header;
			ytDetail_ = detail;
			ytDetailLayout = detailLayout;
			ytCheck_ = check;
			ytStatus_ = status;
			ytConnect_ = connBtn;
			ytDisconnect_ = disc;
			connect(connBtn, &QPushButton::clicked, this,
				&MetadataDock::onConnectYouTube);
			connect(disc, &QPushButton::clicked, this,
				&MetadataDock::onDisconnectYouTube);
		} else if (p == meta::Platform::Facebook) {
			fbCard_ = card;
			fbHeader_ = header;
			fbDetail_ = detail;
			fbDetailLayout = detailLayout;
			fbCheck_ = check;
			fbStatus_ = status;
			fbConnect_ = connBtn;
			fbDisconnect_ = disc;
			connect(connBtn, &QPushButton::clicked, this,
				&MetadataDock::onConnectFacebook);
			connect(disc, &QPushButton::clicked, this,
				&MetadataDock::onDisconnectFacebook);
		} else {
			kkCard_ = card;
			kkHeader_ = header;
			kkDetail_ = detail;
			kkDetailLayout = detailLayout;
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
	addCard(meta::Platform::Twitch, QStringLiteral("Twitch"));
	addCard(meta::Platform::YouTube, QStringLiteral("YouTube"));
	addCard(meta::Platform::Kick, QStringLiteral("Kick"));
	addCard(meta::Platform::Facebook, QStringLiteral("Facebook"));

	QLabel *credTitle =
		new QLabel(tr("App credentials (register your own app per "
			      "platform)"),
			   this);
	credTitle_ = credTitle;
	top->addWidget(credTitle);
	// Credential fields live inside their platform card detail (same
	// objects, same refreshModeUi() visibility rules as before).
	twIdEdit_ = new QLineEdit(twDetail_);
	twIdEdit_->setPlaceholderText(tr("Twitch Client ID"));
	// T-047: distributed ID (public) prefilled when the build provides
	// one; the user can still override it (BYO/dev preserved).
	{
		const QString distributed =
			meta::twitchClientId(QString());
		if (!distributed.isEmpty())
			twIdEdit_->setText(distributed);
	}
	twDetailLayout->addWidget(twIdEdit_);
	ytIdEdit_ = new QLineEdit(ytDetail_);
	ytIdEdit_->setPlaceholderText(tr("YouTube Client ID"));
	ytDetailLayout->addWidget(ytIdEdit_);
	ytSecretEdit_ = new QLineEdit(ytDetail_);
	ytSecretEdit_->setPlaceholderText(tr("YouTube Client Secret"));
	ytSecretEdit_->setEchoMode(QLineEdit::Password);
	ytDetailLayout->addWidget(ytSecretEdit_);
	kkIdEdit_ = new QLineEdit(kkDetail_);
	kkIdEdit_->setPlaceholderText(tr("Kick Client ID"));
	kkDetailLayout->addWidget(kkIdEdit_);
	kkSecretEdit_ = new QLineEdit(kkDetail_);
	kkSecretEdit_->setPlaceholderText(tr("Kick Client Secret"));
	kkSecretEdit_->setEchoMode(QLineEdit::Password);
	kkDetailLayout->addWidget(kkSecretEdit_);
	// T-071 FB-2: BYO-app. Solo el App ID es obligatorio (PKCE sin
	// secret, F-065); el App Secret es opcional y solo se usa para el
	// canje a long-lived (DPAPI, jamas en la URL ni en logs).
	fbIdEdit_ = new QLineEdit(fbDetail_);
	fbIdEdit_->setPlaceholderText(tr("Facebook App ID"));
	fbDetailLayout->addWidget(fbIdEdit_);
	fbSecretEdit_ = new QLineEdit(fbDetail_);
	fbSecretEdit_->setPlaceholderText(
		tr("Facebook App Secret (optional, for long-lived token)"));
	fbSecretEdit_->setEchoMode(QLineEdit::Password);
	fbDetailLayout->addWidget(fbSecretEdit_);
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
	titleLabel_ = titleLabel;
	top->addWidget(titleLabel);
	top->addWidget(titleEdit_);

	QLabel *descLabel = new QLabel(tr("Description"), this);
	descEdit_ = new QPlainTextEdit(this);
	descEdit_->setPlaceholderText(
		tr("YouTube and Facebook only — Twitch and Kick have no "
		   "equivalent stream description."));
	descLabel->setBuddy(descEdit_);
	descLabel_ = descLabel;
	top->addWidget(descLabel);
	top->addWidget(descEdit_);
	QLabel *descCaps = new QLabel(
		tr("YouTube: supported · Facebook: supported · Twitch: not "
		   "available · Kick: not available"),
		this);
	descCaps->setWordWrap(true);
	descCaps_ = descCaps;
	top->addWidget(descCaps);

	// YouTube broadcast controls live inside the YouTube card detail
	// (same objects: currentData storage and clear() behavior unchanged).
	QLabel *bcLabel = new QLabel(tr("YouTube broadcast"), ytDetail_);
	bcLabel_ = bcLabel;
	broadcastCombo_ = new QComboBox(ytDetail_);
	bcLabel->setBuddy(broadcastCombo_);
	refreshButton_ = new QPushButton(tr("Refresh broadcasts"), ytDetail_);
	connect(refreshButton_, &QPushButton::clicked, this,
		&MetadataDock::onRefreshBroadcasts);
	ytDetailLayout->addWidget(bcLabel);
	ytDetailLayout->addWidget(broadcastCombo_);
	ytDetailLayout->addWidget(refreshButton_);

	// T-071 FB-2: selector Page/perfil + live videos propios (D3/D9).
	// Patron broadcast persistente (F-067): jamas el preview web (H1).
	QLabel *fbTargetLabel = new QLabel(tr("Facebook target"), fbDetail_);
	fbTargetLabel_ = fbTargetLabel;
	fbTargetCombo_ = new QComboBox(fbDetail_);
	fbTargetLabel->setBuddy(fbTargetCombo_);
	connect(fbTargetCombo_,
		static_cast<void (QComboBox::*)(int)>(
			&QComboBox::currentIndexChanged),
		this, &MetadataDock::onFacebookTargetChanged);
	QLabel *fbLiveLabel = new QLabel(tr("Facebook live video"), fbDetail_);
	fbLiveLabel_ = fbLiveLabel;
	fbLiveCombo_ = new QComboBox(fbDetail_);
	// F-074: /me/live_videos puede devolver vacio aunque el objeto
	// exista (legible directo por ID). Editable: pegar el ID vale.
	fbLiveCombo_->setEditable(true);
	fbLiveCombo_->setPlaceholderText(tr("Select or paste live-video ID"));
	fbLiveLabel->setBuddy(fbLiveCombo_);
	fbRefreshButton_ =
		new QPushButton(tr("Refresh Facebook videos"), fbDetail_);
	connect(fbRefreshButton_, &QPushButton::clicked, this,
		&MetadataDock::onRefreshFacebook);
	fbDetailLayout->addWidget(fbTargetLabel);
	fbDetailLayout->addWidget(fbTargetCombo_);
	fbDetailLayout->addWidget(fbLiveLabel);
	fbDetailLayout->addWidget(fbLiveCombo_);
	fbDetailLayout->addWidget(fbRefreshButton_);
	// T-073 FB-4: indicador + ciclo de vida (D6R/D7/F-075). Sin
	// polling auto: todo a peticion (Refresh/Apply/botones).
	fbStateLabel_ = new QLabel(tr("Status: —"), fbDetail_);
	fbStateLabel_->setWordWrap(true);
	fbDetailLayout->addWidget(fbStateLabel_);
	fbStatusButton_ =
		new QPushButton(tr("Check live status"), fbDetail_);
	connect(fbStatusButton_, &QPushButton::clicked, this,
		&MetadataDock::onFbStatus);
	fbCreateButton_ =
		new QPushButton(tr("Create live video (dock)"), fbDetail_);
	fbCreateButton_->setToolTip(
		tr("POST /me/live_videos: creates a preview object owned "
		   "by the dock (F-075). No RTMP/keys touched."));
	connect(fbCreateButton_, &QPushButton::clicked, this,
		&MetadataDock::onFbCreate);
	fbGoLiveButton_ = new QPushButton(tr("Go live (ON)"), fbDetail_);
	fbGoLiveButton_->setToolTip(
		tr("Publishes the existing/dock-created video (LIVE_NOW). "
		   "Requires explicit confirmation."));
	connect(fbGoLiveButton_, &QPushButton::clicked, this,
		&MetadataDock::onFbGoLive);
	fbEndButton_ = new QPushButton(tr("End live (OFF)"), fbDetail_);
	connect(fbEndButton_, &QPushButton::clicked, this,
		&MetadataDock::onFbEndLive);
	fbDeleteButton_ =
		new QPushButton(tr("Delete live video"), fbDetail_);
	fbDeleteButton_->setToolTip(
		tr("DELETEs the dock object (cleanup F-075)."));
	connect(fbDeleteButton_, &QPushButton::clicked, this,
		&MetadataDock::onFbDelete);
	fbDetailLayout->addWidget(fbStatusButton_);
	fbDetailLayout->addWidget(fbCreateButton_);
	fbDetailLayout->addWidget(fbGoLiveButton_);
	fbDetailLayout->addWidget(fbEndButton_);
	fbDetailLayout->addWidget(fbDeleteButton_);

	// Twitch device-flow prompt lives inside the Twitch card detail
	// (same show/hide call sites as before).
	devicePrompt_ = new QLabel(twDetail_);
	devicePrompt_->setWordWrap(true);
	devicePrompt_->setVisible(false);
	twDetailLayout->addWidget(devicePrompt_);
	// FASE 1.1 note, retired in FASE 2 (Twitch connects in Managed;
	// kept hidden — see refreshContentVisibility).
	twManagedNote_ = new QLabel(
		tr("Twitch is not available in Managed mode (direct only). "
		   "Switch to Independent to connect it."),
		twDetail_);
	twManagedNote_->setWordWrap(true);
	twManagedNote_->setVisible(false);
	twDetailLayout->addWidget(twManagedNote_);

	applyButton_ = new QPushButton(tr("Apply changes"), this);
	// FASE 1.1: Apply is the primary action: positive-action green,
	// readable on the dark OBS theme, neutral gray when disabled.
	applyButton_->setStyleSheet(QStringLiteral(
		"QPushButton { background-color: #2da44e; color: white; "
		"border-radius: 6px; padding: 8px; font-weight: bold; } "
		"QPushButton:hover { background-color: #36b558; } "
		"QPushButton:disabled { background-color: #3a3a3a; "
		"color: #8a8a8a; }"));
	connect(applyButton_, &QPushButton::clicked, this,
		&MetadataDock::onApply);
	top->addWidget(applyButton_);

	// F-C2: borrado total Managed. Visible solo en Managed (ver
	// refreshModeUi); idempotente en el backend, seguro con cero
	// conexiones. Independent intacto (su Disconnect por proveedor
	// sigue siendo la vía para datos locales DPAPI).
	eraseButton_ = new QPushButton(
		tr("Borrar mis datos (Managed)"), this);
	eraseButton_->setToolTip(
		tr("Deletes all Managed connections, tokens, sessions and "
		   "the installation itself from the backend. Independent "
		   "accounts are untouched."));
	connect(eraseButton_, &QPushButton::clicked, this,
		&MetadataDock::onEraseManagedData);
	top->addWidget(eraseButton_);

	// Per-platform results live inside their card detail: no global
	// RESULTS section. setStatus()/setResult() call sites unchanged.
	twResult_ = new QLabel(QStringLiteral("Twitch —"), twDetail_);
	ytResult_ = new QLabel(QStringLiteral("YouTube —"), ytDetail_);
	kkResult_ = new QLabel(QStringLiteral("Kick —"), kkDetail_);
	fbResult_ = new QLabel(QStringLiteral("Facebook —"), fbDetail_);
	twResult_->setWordWrap(true);
	ytResult_->setWordWrap(true);
	kkResult_->setWordWrap(true);
	fbResult_->setWordWrap(true);
	twDetailLayout->addWidget(twResult_);
	ytDetailLayout->addWidget(ytResult_);
	kkDetailLayout->addWidget(kkResult_);
	fbDetailLayout->addWidget(fbResult_);
	// generalMsg_ stays global on purpose: validation messages and
	// broadcast counts have no single owning platform, and the UX
	// definitions require no invented mapping.
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
	// FASE 2.1-B: non-invasive build identity (public endpoint + feature
	// flags only, never secrets/tokens): lets any OBS log prove which
	// backend and provider×mode matrix the loaded DLL was built with.
	obs_log(LOG_INFO, "dock ready (mode=%s, managed-backend=%s, "
			  "twitch-managed=%s)",
		meta::connectionModeName(mode_),
		qPrintable(managedBaseUrl_),
		managedSupported(meta::Platform::Twitch) ? "yes" : "no");
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
	// changes the context future Connect flows will use. A pending
	// connect flow belongs to the old context and is cancelled safely
	// (stored accounts untouched); the backend transaction behind a
	// Managed poll expires on its own TTL.
	cancelPendingForModeSwitch();
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
	fbIdEdit_->setVisible(!managed);
	fbSecretEdit_->setVisible(!managed);
	credNote_->setVisible(!managed);
	managedNote_->setVisible(managed);
	if (eraseButton_)
		eraseButton_->setVisible(managed);
	repaintModeStatuses();
	updateAllCardStyles();
	refreshContentVisibility();
}

// --- platform cards (UX FASE 1, Modelo B; visibility/style only) --------

QFrame *MetadataDock::cardFor(meta::Platform p) const
{
	if (p == meta::Platform::Twitch)
		return twCard_;
	if (p == meta::Platform::YouTube)
		return ytCard_;
	if (p == meta::Platform::Facebook)
		return fbCard_;
	return kkCard_;
}

QWidget *MetadataDock::detailFor(meta::Platform p) const
{
	if (p == meta::Platform::Twitch)
		return twDetail_;
	if (p == meta::Platform::YouTube)
		return ytDetail_;
	if (p == meta::Platform::Facebook)
		return fbDetail_;
	return kkDetail_;
}

QPushButton *MetadataDock::headerFor(meta::Platform p) const
{
	if (p == meta::Platform::Twitch)
		return twHeader_;
	if (p == meta::Platform::YouTube)
		return ytHeader_;
	if (p == meta::Platform::Facebook)
		return fbHeader_;
	return kkHeader_;
}

QCheckBox *MetadataDock::checkFor(meta::Platform p) const
{
	if (p == meta::Platform::Twitch)
		return twCheck_;
	if (p == meta::Platform::YouTube)
		return ytCheck_;
	if (p == meta::Platform::Facebook)
		return fbCheck_;
	return kkCheck_;
}

QLabel *MetadataDock::statusFor(meta::Platform p) const
{
	if (p == meta::Platform::Twitch)
		return twStatus_;
	if (p == meta::Platform::YouTube)
		return ytStatus_;
	if (p == meta::Platform::Facebook)
		return fbStatus_;
	return kkStatus_;
}

QLabel *MetadataDock::resultFor(meta::Platform p) const
{
	if (p == meta::Platform::Twitch)
		return twResult_;
	if (p == meta::Platform::YouTube)
		return ytResult_;
	if (p == meta::Platform::Facebook)
		return fbResult_;
	return kkResult_;
}

void MetadataDock::toggleCard(meta::Platform p)
{
	// Single-open: clicking the open card closes it, clicking another
	// one switches. Never touches connection, credentials, broadcast
	// or Apply selection state.
	if (expanded_.has_value() && *expanded_ == p)
		expanded_.reset();
	else
		expanded_ = p;
	updateCardVisibility();
}

void MetadataDock::updateCardVisibility()
{
	using P = meta::Platform;
	for (P p : {P::Twitch, P::YouTube, P::Kick, P::Facebook}) {
		const bool open =
			expanded_.has_value() && *expanded_ == p;
		if (detailFor(p))
			detailFor(p)->setVisible(open);
		if (headerFor(p)) {
			headerFor(p)->setText(cardTitle(p, open));
			headerFor(p)->setToolTip(
				open ? tr("Close %1 settings")
					       .arg(meta::platformName(p))
				     : tr("Open %1 settings")
					       .arg(meta::platformName(p)));
		}
		// The Apply check is visible when open AND the platform is
		// usable; when closed it only appears on hover (eventFilter).
		// Either way isChecked() is preserved.
		if (checkFor(p))
			checkFor(p)->setVisible(open && platformUsable(p));
	}
}

// FASE 1.1 progressive disclosure. Usable == connected in the active
// mode, derived from the existing state objects only. YouTube needs no
// special case here: a missing broadcast is already handled inside
// Apply (auto-list) and reported per-platform, so no new availability
// concept is introduced.
bool MetadataDock::platformUsable(meta::Platform p)
{
	if (isManaged())
		return managedAccount(p).connected;
	return account(p).connected;
}

bool MetadataDock::anyUsable()
{
	using P = meta::Platform;
	return platformUsable(P::Twitch) || platformUsable(P::YouTube) ||
	       platformUsable(P::Kick) || platformUsable(P::Facebook);
}

void MetadataDock::refreshContentVisibility()
{
	// Nothing usable: Content + Apply have no target yet, hide them.
	const bool any = anyUsable();
	if (titleLabel_)
		titleLabel_->setVisible(any);
	titleEdit_->setVisible(any);
	if (descLabel_)
		descLabel_->setVisible(any);
	descEdit_->setVisible(any);
	if (descCaps_)
		descCaps_->setVisible(any);
	applyButton_->setVisible(any);
	// Broadcast depends on the YouTube connection, not on the card
	// being open: hide it while YouTube is not connected.
	const bool yt = platformUsable(meta::Platform::YouTube);
	if (bcLabel_)
		bcLabel_->setVisible(yt);
	broadcastCombo_->setVisible(yt);
	refreshButton_->setVisible(yt);
	// T-071 FB-2: Facebook target/videos follow the same rule.
	const bool fb = platformUsable(meta::Platform::Facebook);
	if (fbTargetLabel_)
		fbTargetLabel_->setVisible(fb);
	if (fbTargetCombo_)
		fbTargetCombo_->setVisible(fb);
	if (fbLiveLabel_)
		fbLiveLabel_->setVisible(fb);
	if (fbLiveCombo_)
		fbLiveCombo_->setVisible(fb);
	if (fbRefreshButton_)
		fbRefreshButton_->setVisible(fb);
	// T-073 FB-4 (F-086): indicador + ciclo de vida solo con sesion.
	// Sin conexion no hay ID que leer/crear/publicar/terminar/borrar.
	if (fbStateLabel_)
		fbStateLabel_->setVisible(fb);
	if (fbStatusButton_)
		fbStatusButton_->setVisible(fb);
	if (fbCreateButton_)
		fbCreateButton_->setVisible(fb);
	if (fbGoLiveButton_)
		fbGoLiveButton_->setVisible(fb);
	if (fbEndButton_)
		fbEndButton_->setVisible(fb);
	if (fbDeleteButton_)
		fbDeleteButton_->setVisible(fb);
	// T-073 FB-4 (F-086): Connect solo sin sesion, Disconnect solo con
	// sesion, en las cuatro tarjetas y en ambos modos (usable == connected
	// en el modo activo). Evita flujos con conflicto (p. ej. Disconnect
	// o ciclo de vida sin conexion).
	const bool tw = platformUsable(meta::Platform::Twitch);
	const bool kk = platformUsable(meta::Platform::Kick);
	twConnect_->setVisible(!tw);
	twDisconnect_->setVisible(tw);
	ytConnect_->setVisible(!yt);
	ytDisconnect_->setVisible(yt);
	kkConnect_->setVisible(!kk);
	kkDisconnect_->setVisible(kk);
	fbConnect_->setVisible(!fb);
	fbDisconnect_->setVisible(fb);
	// FASE 2: Twitch connects/disconnects in Managed like the other
	// platforms (own ManagedConn state, no shared Independent account).
	// The legacy "not available" note stays hidden.
	if (twManagedNote_)
		twManagedNote_->setVisible(false);
	updateCardVisibility();
}

void MetadataDock::updateCardStyle(meta::Platform p)
{
	QFrame *card = cardFor(p);
	const QLabel *status = statusFor(p);
	const QLabel *result = resultFor(p);
	if (!card || !status || !result)
		return;
	// Same sources the labels already show; no new state system.
	bool connected = false;
	if (isManaged())
		connected = managedAccount(p).connected;
	else
		connected = account(p).connected;
	const QString st = status->text();
	const QString rs = result->text();
	const bool failed =
		st.startsWith(tr("Error")) ||
		st == tr("Needs reconnection.") ||
		rs.contains(QStringLiteral(" ✗ "));
	const char *brand = "#8a8a8a";
	const char *tint = "transparent";
	if (p == meta::Platform::Twitch) {
		brand = "#9146FF";
		tint = "rgba(145, 70, 255, 36)";
	} else if (p == meta::Platform::YouTube) {
		brand = "#FF0000";
		tint = "rgba(255, 0, 0, 28)";
	} else if (p == meta::Platform::Facebook) {
		brand = "#1877F2";
		tint = "rgba(24, 119, 242, 30)";
	} else {
		brand = "#35c759";
		tint = "rgba(53, 199, 89, 30)";
	}
	QString border = QStringLiteral("#5a5a5a");
	QString bg = QStringLiteral("transparent");
	if (failed) {
		border = QStringLiteral("#e5484d");
		bg = QStringLiteral("rgba(229, 72, 77, 30)");
	} else if (connected) {
		border = QString::fromLatin1(brand);
		bg = QString::fromLatin1(tint);
	}
	card->setStyleSheet(
		QStringLiteral("QFrame { border: 2px solid %1; "
			       "border-radius: 10px; background: %2; }")
			.arg(border, bg));
}

void MetadataDock::updateAllCardStyles()
{
	using P = meta::Platform;
	for (P p : {P::Twitch, P::YouTube, P::Kick, P::Facebook})
		updateCardStyle(p);
}

bool MetadataDock::eventFilter(QObject *watched, QEvent *event)
{
	// Hover Apply check on closed cards. Never connects/disconnects:
	// it only shows the existing selection checkbox.
	meta::Platform p = meta::Platform::Twitch;
	bool isCard = false;
	if (watched == twCard_) {
		p = meta::Platform::Twitch;
		isCard = true;
	} else if (watched == ytCard_) {
		p = meta::Platform::YouTube;
		isCard = true;
	} else if (watched == fbCard_) {
		p = meta::Platform::Facebook;
		isCard = true;
	} else if (watched == kkCard_) {
		p = meta::Platform::Kick;
		isCard = true;
	}
	if (isCard && event) {
		const bool open =
			expanded_.has_value() && *expanded_ == p;
		if (event->type() == QEvent::Enter) {
			// Hover check only when the platform can take part
			// in Apply; never connects/disconnects anything.
			if (platformUsable(p) && checkFor(p))
				checkFor(p)->setVisible(true);
		} else if (event->type() == QEvent::Leave) {
			if (!open && checkFor(p))
				checkFor(p)->setVisible(false);
		}
	}
	return QWidget::eventFilter(watched, event);
}

// FASE 1.1: cancel in-flight Independent connect flows on mode switch so
// a late reply/callback cannot connect in the previous context. Only
// connect-phase ops are invalidated (Apply/refresh traffic keeps its own
// bounded lifecycle). Stored accounts are never wiped, nothing revoked.
void MetadataDock::cancelPendingForModeSwitch()
{
	if (managedPollTimer_)
		managedPollTimer_->stop();
	twPollsLeft_ = 0; // onTwitchPollTimeout re-checks pending_/count
	if (callbackServer_) {
		// Late browser callbacks find no listener and are ignored.
		callbackServer_->close();
		callbackServer_->deleteLater();
		callbackServer_ = nullptr;
	}
	callbackDone_ = false;
	switch (pending_) {
	case Op::TwDevice:
	case Op::TwPoll:
	case Op::TwValidate:
	case Op::YtExchange:
	case Op::YtChannels:
	case Op::KkExchange:
	case Op::KkChannels:
	case Op::FbExchange:
	case Op::FbLongLived:
	case Op::FbIdentity:
	case Op::FbTargets:
	case Op::FbList:
	case Op::FbRead:
	case Op::FbStatus:
	case Op::FbCreate:
	case Op::FbGoLive:
	case Op::FbEnd:
	case Op::FbDelete:
		pending_ = Op::None;
		devicePrompt_->setVisible(false);
		// Repaint from the real state: a finished connection was
		// never touched; a fresh attempt had already cleared its own
		// record when it started, so this only neutralizes the UI.
		repaintModeStatuses();
		break;
	default:
		break;
	}
}

// --- managed wiring (T-048; Independent handlers untouched) --------------

namespace {

// Build-time default (FASE 1): dev builds bundle the local backend;
// distribuible builds pass -DSTREAM_META_BACKEND_URL=<prod https URL>.
// Precedence at runtime: STREAM_META_BACKEND_URL env (DEV-only) wins,
// otherwise this bundled default. The URL is a public endpoint, never a
// secret; installation secrets stay DPAPI-bound per backend (T-053).
#ifdef STREAM_META_BACKEND_URL_DEFAULT
const char *kManagedDefaultBaseUrl = STREAM_META_BACKEND_URL_DEFAULT;
#else
const char *kManagedDefaultBaseUrl = "http://127.0.0.1:8080";
#endif

QString providerSlug(meta::Platform p)
{
	if (p == meta::Platform::YouTube)
		return QStringLiteral("youtube");
	if (p == meta::Platform::Kick)
		return QStringLiteral("kick");
	if (p == meta::Platform::Facebook)
		return QStringLiteral("facebook");
	return QStringLiteral("twitch");
}

// Backend-backed providers: YouTube (T-045), Kick (T-046), Twitch FASE 2
// (auth-code via backend; Independent DCF untouched) y Facebook FB-3
// (T-072; Independent PKCE intacto).
bool managedSupported(meta::Platform p)
{
	return p == meta::Platform::YouTube || p == meta::Platform::Kick ||
	       p == meta::Platform::Twitch || p == meta::Platform::Facebook;
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
	if (p == meta::Platform::Kick)
		return mKk_;
	if (p == meta::Platform::Facebook)
		return mFb_; // FB-3 lo cablea; aqui siempre desconectado
	return mTw_;
}

void MetadataDock::repaintModeStatuses()
{
	// Labels always reflect the active mode's own state objects; the
	// other mode's accounts are never read here (§17: no mixing).
	if (isManaged()) {
		for (meta::Platform p :
		     {meta::Platform::Twitch, meta::Platform::YouTube,
		      meta::Platform::Kick, meta::Platform::Facebook}) {
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
	      meta::Platform::Kick, meta::Platform::Facebook}) {
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
			  tr("Managed is not available for this platform."));
		return;
	}
	startConnectBusy(p);
	managedAuth_->ensureSession([this, p](backend_auth::Result r) {
		if (!isManaged())
			return; // user switched mode: previous context
		if (r == backend_auth::Result::StorageError) {
			// No installation yet: bootstrap once, then retry.
			managedAuth_->bootstrap([this, p](backend_auth::Result b) {
				if (!isManaged())
					return; // user switched mode
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
					     if (!isManaged())
						     return; // user switched mode
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
					      if (!isManaged())
						      return; // user switched mode
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
	if (!isManaged()) {
		// Cancelled by a mode switch (see onModeChanged).
		if (managedPollTimer_)
			managedPollTimer_->stop();
		return;
	}
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
			  tr("Managed is not available for this platform."));
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

// --- F-C2: borrado total Managed ------------------------------------------
// POST /privacy/erase (backend borra connections + tokens + sessions +
// installation; revoke remoto best-effort). Aquí solo se limpian
// snapshots Managed + backendInstall local; Independent intacto.
void MetadataDock::onEraseManagedData()
{
	using P = meta::Platform;
	if (!isManaged() || !eraseButton_)
		return;
	eraseButton_->setEnabled(false);
	managedAuth_->ensureSession([this](backend_auth::Result r) {
		if (r != backend_auth::Result::Ok) {
			eraseButton_->setEnabled(true);
			generalMsg_->setText(
				tr("Data erasure failed: backend unreachable."));
			return;
		}
		managedAuth_->apiPost(
			QStringLiteral("/privacy/erase"), QJsonObject(),
			[this](const backend_auth::Client::ApiReply &rep) {
				eraseButton_->setEnabled(true);
				if (rep.result !=
				    backend_auth::Result::Ok) {
					obs_log(LOG_WARNING,
						"privacy erase failed: "
						"result=%d http=%d",
						static_cast<int>(rep.result),
						rep.http);
					generalMsg_->setText(
						tr("Data erasure failed "
						   "(retry or Disconnect "
						   "each account)."));
					return;
				}
				using P = meta::Platform;
				for (P p :
				     {P::Twitch, P::YouTube, P::Kick,
				      P::Facebook}) {
					ManagedConn &m = managedAccount(p);
					m.connected = false;
					m.userId.clear();
					m.display.clear();
					setStatus(p, tr("Not connected"));
					setResult(p, true,
						  tr("data erased"));
				}
				broadcastCombo_->clear();
				// La fila installation ya no existe en el
				// backend: el secreto local queda huérfano y
				// debe caer (próximo connect = bootstrap
				// nuevo). Independent intacto por
				// construcción (saveStore preserva sus
				// records).
				if (store_) {
					secure::Data d;
					store_->load(d);
					d.backendInstall =
						secure::Record{};
					store_->save(d);
				}
				saveStore(); // drop snapshots
				refreshContentVisibility();
				// El bearer en memoria murió con sus
				// sessions: olvidarlo sin revocar (el
				// servidor ya lo borró).
				managedAuth_->revokeSession(
					[](backend_auth::Result) {});
				generalMsg_->setText(
					tr("All Managed data erased. "
					   "Reconnect to use Managed again."));
			});
	});
}

// --- managed YouTube apply (FASE 2.1-C) ----------------------------------
// Tokens stay server-side: the plugin sends only the session bearer (via
// managedAuth_) plus {broadcast_id,title,description}. Item data in the
// combo are plain broadcast IDs (Independent stores full JSON there;
// branches are mode-split, never mixed).

void MetadataDock::fetchManagedBroadcasts()
{
	using P = meta::Platform;
	refreshButton_->setEnabled(false);
	managedAuth_->apiGet(QStringLiteral("/metadata/youtube/broadcasts"),
			     [this](const backend_auth::Client::ApiReply &rep) {
				     using P = meta::Platform;
				     // FASE 2.1-C.1: diagnóstico redactado (enums/códigos;
				     // nunca cuerpos, tokens ni bearer).
				     obs_log(LOG_INFO,
					     "managed broadcasts reply: result=%d "
					     "http=%d",
					     static_cast<int>(rep.result),
					     rep.http);
				     refreshButton_->setEnabled(true);
				     if (!isManaged())
					     return; // user switched mode
				     if (rep.result !=
					 backend_auth::Result::Ok) {
					     const meta::Outcome oc =
						     rep.result ==
							     backend_auth::Result::
								     NetworkError
							     ? meta::Outcome::
								       NetworkError
							     : meta::classifyStatus(
								       rep.http == 0
									   ? -1
									   : rep.http);
					     if (applyAfterList_) {
						     applyAfterList_ = false;
						     const P p = applyQueue_
								     .takeFirst();
						     finishPlatform(
							     p, false,
							     meta::userMessage(
								     oc, p));
						     startApplyNext();
					     } else {
						     generalMsg_->setText(
							     meta::userMessage(
								     oc, P::YouTube));
					     }
					     return;
				     }
				     broadcastCombo_->clear();
				     const QJsonArray items =
					     rep.body
						     .value(QStringLiteral(
							     "resources"))
						     .toArray();
				     for (const auto &v : items) {
					     const QJsonObject it =
						     v.toObject();
					     const QString id = it.value(
								QStringLiteral(
									"id"))
								.toString();
					     const QString title =
						     it.value(QStringLiteral(
								     "title"))
								.toString();
					     if (id.isEmpty())
						     continue;
					     broadcastCombo_->addItem(
						     QStringLiteral("%1 (%2)")
							     .arg(title, id),
						     id);
				     }
				     if (applyAfterList_) {
					     applyAfterList_ = false;
					     if (broadcastCombo_->count() ==
						     0 &&
						 !applyQueue_.isEmpty()) {
						     const P p = applyQueue_
								     .takeFirst();
						     finishPlatform(
							     p, false,
							     meta::userMessage(
								     meta::Outcome::
									     NotFound,
								     p));
					     }
					     startApplyNext();
				     } else {
					     generalMsg_->setText(
						     tr("Broadcasts loaded (%1).")
							     .arg(broadcastCombo_
								      ->count()));
				     }
			     });
}

void MetadataDock::startManagedYouTubeApply()
{
	using P = meta::Platform;
	const P p = P::YouTube;
	if (!managedAccount(p).connected) {
		finishPlatform(p, false,
			       meta::userMessage(meta::Outcome::AuthRequired,
						 p));
		startApplyNext();
		return;
	}
	const QString broadcastId =
		broadcastCombo_->currentData().toString();
	if (broadcastId.isEmpty()) {
		// Same prepend/retry shape as the Independent list-first path.
		applyQueue_.prepend(p);
		applyAfterList_ = true;
		fetchManagedBroadcasts();
		return;
	}
	setResult(p, true, tr("Updating…"));
	QJsonObject body;
	body[QStringLiteral("broadcast_id")] = broadcastId;
	body[QStringLiteral("title")] = titleEdit_->text();
	body[QStringLiteral("description")] = descEdit_->toPlainText();
	managedAuth_->apiPost(QStringLiteral("/metadata/youtube"), body,
			      [this, p](const backend_auth::Client::ApiReply &rep) {
				      // FASE 2.1-C.1: diagnóstico redactado (enums/códigos;
				      // nunca cuerpos, tokens ni bearer).
				      obs_log(LOG_INFO,
					      "managed apply reply: result=%d "
					      "http=%d",
					      static_cast<int>(rep.result),
					      rep.http);
				      if (!isManaged())
					      return; // user switched mode
				      if (rep.result ==
					  backend_auth::Result::Ok) {
					      finishPlatform(
						      p, true,
						      meta::userMessage(
							      meta::Outcome::Success,
							      p));
					      obs_log(LOG_INFO,
						      "apply %s: ok (managed)",
						      meta::platformName(p));
					      startApplyNext();
					      return;
				      }
				      const meta::Outcome oc =
					      rep.result ==
						      backend_auth::Result::
							      NetworkError
					      ? meta::Outcome::NetworkError
					      : meta::classifyStatus(
						      rep.http == 0 ? -1
								    : rep.http);
				      if (scheduleBackoff(p, oc, rep.http))
					      return;
				      if (oc == meta::Outcome::AuthRequired) {
					      ManagedConn &m =
						      managedAccount(p);
					      m.connected = false;
					      m.userId.clear();
					      m.display.clear();
					      setStatus(p,
							tr("Needs reconnection."));
					      saveStore(); // drop the stale snapshot
				      }
				      finishPlatform(
					      p, false,
					      meta::userMessage(oc, p));
				      startApplyNext();
			      });
}

// --- managed Twitch apply ------------------------------------------------
// Token server-side: el plugin envía solo sesión bearer + {title,
// broadcaster_id} (la propia identidad Managed, visible en la UI; Twitch
// la valida contra el token). Sin descripción: Twitch no tiene equivalente.

void MetadataDock::startManagedTwitchApply()
{
	using P = meta::Platform;
	const P p = P::Twitch;
	const ManagedConn &m = managedAccount(p);
	if (!m.connected || m.userId.isEmpty()) {
		finishPlatform(p, false,
			       meta::userMessage(meta::Outcome::AuthRequired,
						 p));
		startApplyNext();
		return;
	}
	setResult(p, true, tr("Updating…"));
	QJsonObject body;
	body[QStringLiteral("title")] = titleEdit_->text();
	body[QStringLiteral("broadcaster_id")] = m.userId;
	managedAuth_->apiPost(QStringLiteral("/metadata/twitch"), body,
			      [this, p](const backend_auth::Client::ApiReply &rep) {
				      obs_log(LOG_INFO,
					      "managed apply reply: result=%d "
					      "http=%d",
					      static_cast<int>(rep.result),
					      rep.http);
				      if (!isManaged())
					      return; // user switched mode
				      if (rep.result ==
					  backend_auth::Result::Ok) {
					      finishPlatform(
						      p, true,
						      meta::userMessage(
							      meta::Outcome::Success,
							      p));
					      obs_log(LOG_INFO,
						      "apply %s: ok (managed)",
						      meta::platformName(p));
					      startApplyNext();
					      return;
				      }
				      const meta::Outcome oc =
					      rep.result ==
						      backend_auth::Result::
							      NetworkError
					      ? meta::Outcome::NetworkError
					      : meta::classifyStatus(
						      rep.http == 0 ? -1
								    : rep.http);
				      if (scheduleBackoff(p, oc, rep.http))
					      return;
				      if (oc == meta::Outcome::AuthRequired) {
					      ManagedConn &mm =
						      managedAccount(p);
					      mm.connected = false;
					      mm.userId.clear();
					      mm.display.clear();
					      setStatus(p,
							tr("Needs reconnection."));
					      saveStore(); // drop the stale snapshot
				      }
				      finishPlatform(
					      p, false,
					      meta::userMessage(oc, p));
				      startApplyNext();
			      });
}

// --- helpers ----------------------------------------------------------

void MetadataDock::setStatus(meta::Platform p, const QString &text)
{
	if (p == meta::Platform::Twitch)
		twStatus_->setText(text);
	else if (p == meta::Platform::YouTube)
		ytStatus_->setText(text);
	else if (p == meta::Platform::Facebook)
		fbStatus_->setText(text);
	else
		kkStatus_->setText(text);
	updateCardStyle(p); // card border/color follows the same state
	refreshContentVisibility(); // Content/Apply/checks follow usability
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
	else if (p == meta::Platform::Facebook)
		fbResult_->setText(line);
	else
		kkResult_->setText(line);
	updateCardStyle(p); // e.g. Apply failure shows a red card border
	refreshContentVisibility(); // e.g. 401 hides Content until reconnect
}

MetadataDock::Account &MetadataDock::account(meta::Platform p)
{
	if (p == meta::Platform::Twitch)
		return tw_;
	if (p == meta::Platform::YouTube)
		return yt_;
	if (p == meta::Platform::Facebook)
		return fb_;
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
	fill(fb_, d.facebook);
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
	fillManaged(mTw_, d.managedTwitch);
	fillManaged(mFb_, d.managedFacebook);
	// T-073 FB-4: LiveVideo ID del dock (plaintext, ID no-sensible).
	// Se preserva aunque no haya conexion (ID-centrico F-075): el
	// objeto Meta sobrevive a Disconnect/restart; solo DELETE lo limpia.
	// El combo manda cuando tiene ID; si esta vacio se conserva el
	// guardado (no se borra por un save incidental).
	{
		const QString comboId = currentFbLiveId();
		if (!comboId.isEmpty())
			d.facebookLiveId = comboId;
	}
	// T-041: the mode is always persisted explicitly (idempotent
	// migration: first save after upgrade writes it). Legacy clear
	// behavior stays unless Managed was explicitly selected or a
	// Managed snapshot exists.
	d.connectionMode = QString::fromLatin1(meta::connectionModeName(mode_))
				   .toLower();
	if (!d.anyConnected() && !d.anyManaged() && !isManaged() &&
	    d.facebookLiveId.isEmpty()) {
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
	restore(fb_, d.facebook);
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
	restoreManaged(mTw_, d.managedTwitch);
	restoreManaged(mFb_, d.managedFacebook);
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
	if (fb_.connected) {
		fbIdEdit_->setText(fb_.clientId);
		fbSecretEdit_->setText(fb_.secret);
	}
	// T-073 FB-4: restaurar el LiveVideo ID del dock (ID-centrico).
	if (!d.facebookLiveId.isEmpty() && fbLiveCombo_) {
		bool found = false;
		for (int i = 0; i < fbLiveCombo_->count(); ++i) {
			if (fbLiveCombo_->itemData(i).toString() ==
				    d.facebookLiveId ||
			    fbLiveCombo_->itemText(i).contains(
				    d.facebookLiveId)) {
				fbLiveCombo_->setCurrentIndex(i);
				found = true;
				break;
			}
		}
		if (!found) {
			fbLiveCombo_->addItem(d.facebookLiveId,
					      d.facebookLiveId);
			fbLiveCombo_->setCurrentIndex(
				fbLiveCombo_->count() - 1);
		}
	}
	for (meta::Platform p :
	     {meta::Platform::Twitch, meta::Platform::YouTube,
	      meta::Platform::Kick, meta::Platform::Facebook}) {
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
	else if (revokeFor_ == meta::Platform::Facebook)
		op = Op::RevFb;
	if (revokeFor_ == meta::Platform::Facebook) {
		// D10: DELETE /me/permissions con bearer (desautorizacion
		// total, invalida tokens). Best-effort: el borrado local ya
		// ocurrio; solo el codigo va al log.
		sendJson(QUrl(QString::fromLatin1(ep.url)),
			 QStringLiteral("DELETE"), QString(), op, tok);
		return;
	}
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
	if (p == meta::Platform::Facebook) {
		// T-073 FB-4: Disconnect no borra el LiveVideo ID del dock
		// (el objeto Meta sigue existiendo; ID-centrico F-075). Solo
		// se limpian credenciales/targets en memoria; el combo y el
		// ID persistido sobreviven para reconectar.
		fbTargetCombo_->clear();
		fbPageTokens_.clear();
		fbTarget_.clear();
		if (fbStateLabel_)
			fbStateLabel_->setText(tr("Status: —"));
	}
	setStatus(p, tr("Not connected"));
	setResult(p, true, tr("disconnected"));
	saveStore(); // drop the record; survivors stay encrypted
}

// --- connect: Twitch device flow (no secret, F-015) --------------------

void MetadataDock::onConnectTwitch()
{
	// FASE 2: Managed Twitch goes through the backend like YouTube/Kick;
	// Independent keeps the Device Flow below untouched.
	if (isManaged()) {
		onConnectManaged(meta::Platform::Twitch);
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
	// FASE 2: Managed Twitch disconnects via backend (local state is
	// cleared first inside onDisconnectManaged, mirroring YouTube/Kick).
	if (isManaged()) {
		onDisconnectManaged(meta::Platform::Twitch);
		return;
	}
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
	// T-071 FB-2: Facebook usa localhost fijo como Kick (Strict Mode +
	// localhost dev auto-permitido): puerto 3001 + path /fb-cb, debe
	// estar registrado exacto en el dashboard (D4).
	const bool fb = (callbackFor_ == meta::Platform::Facebook);
	QHostAddress host = (kick || fb) ? QHostAddress::LocalHost
				 : QHostAddress(QStringLiteral("127.0.0.1"));
	// Kick exige el redirect EXACTO registrado (F-017); Facebook igual
	// en modo desarrollo (D4). Loopback Google admite efimero.
	if (!callbackServer_->listen(host, (kick || fb) ? port : 0))
		return false;
	port = callbackServer_->serverPort();
	QString hostName = (kick || fb) ? QStringLiteral("localhost")
				       : QStringLiteral("127.0.0.1");
	QString cbPath = QStringLiteral("/");
	if (kick)
		cbPath = QStringLiteral("/cb");
	else if (fb)
		cbPath = QString::fromLatin1(kFbCbPath);
	redirect_ = QStringLiteral("http://%1:%2%3")
			    .arg(hostName)
			    .arg(port)
			    .arg(cbPath);
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
				 : callbackFor_ == meta::Platform::Facebook
					 ? QString::fromLatin1(kFbCbPath)
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
	else if (callbackFor_ == meta::Platform::Facebook)
		startFacebookExchange(code);
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

// --- connect: Facebook PKCE directo BYO-app (T-071 FB-2, D4) -------------
// App Nativa/Desktop: el secret NUNCA viaja en el code exchange (F-065:
// 400 "configured as a desktop app"); solo PKCE S256 + state. El secret
// BYO (DPAPI) solo se usa, si existe, para el canje a long-lived (D8).

void MetadataDock::onConnectFacebook()
{
	if (isManaged()) {
		// FB-3 (T-072): Connect Managed via backend (sin App ID
		// local, sin secretos en el plugin).
		onConnectManaged(meta::Platform::Facebook);
		return;
	}
	Account &a = fb_;
	a.clear();
	fbPageTokens_.clear();
	fbTarget_.clear();
	const QString id = field(fbIdEdit_);
	if (id.isEmpty()) {
		finishConnectError(meta::Platform::Facebook,
				   tr("Enter your Facebook App ID first. "
				      "Register http://localhost:3001/fb-cb "
				      "in the app dashboard."));
		return;
	}
	quint16 port = kFbPort; // debe coincidir con el redirect registrado
	callbackFor_ = meta::Platform::Facebook;
	callbackDone_ = false;
	if (!listenCallback(port, false)) {
		finishConnectError(meta::Platform::Facebook,
				   tr("Port localhost:3001 is busy or blocked. "
				      "Free it: the redirect must match the "
				      "registered one."));
		return;
	}
	a.clientId = id;
	a.secret = field(fbSecretEdit_); // opcional, solo long-lived (D8)
	a.verifier = randomUrlSafe(64);
	a.state = QUuid::createUuid().toString(QUuid::WithoutBraces)
			  .remove(QLatin1Char('-'))
			  .left(32);
	startConnectBusy(meta::Platform::Facebook);
	const QString url = meta::facebookAuthUrl(
		id, redirect_, a.state,
		QString::fromLatin1(kFbScope), pkceChallenge(a.verifier));
	pending_ = Op::FbExchange; // waiting for the browser callback
	openBrowser(url);
}

void MetadataDock::startFacebookExchange(const QString &code)
{
	Account &a = fb_;
	QUrlQuery q;
	q.addQueryItem(QStringLiteral("client_id"), a.clientId);
	q.addQueryItem(QStringLiteral("redirect_uri"), redirect_);
	q.addQueryItem(QStringLiteral("code"), code);
	// PKCE, sin client_secret en apps Nativa/Desktop (F-065).
	q.addQueryItem(QStringLiteral("code_verifier"), a.verifier);
	sendForm(QUrl(QString::fromLatin1(kFbGraph) +
		      QStringLiteral("/oauth/access_token")),
		 q.toString(QUrl::FullyEncoded), Op::FbExchange);
}

void MetadataDock::startFacebookLongLived()
{
	// D8: corto (horas) -> long-lived ~60d via fb_exchange_token.
	// Lleva app secret: solo si el usuario lo aporto (BYO DPAPI);
	// sin secret se conserva el corto y se sigue (best-effort).
	Account &a = fb_;
	if (a.secret.isEmpty() || a.access.isEmpty()) {
		fetchFacebookTargets();
		return;
	}
	QUrlQuery q;
	q.addQueryItem(QStringLiteral("grant_type"),
		       QStringLiteral("fb_exchange_token"));
	q.addQueryItem(QStringLiteral("client_id"), a.clientId);
	q.addQueryItem(QStringLiteral("client_secret"), a.secret);
	q.addQueryItem(QStringLiteral("fb_exchange_token"), a.access);
	sendForm(QUrl(QString::fromLatin1(kFbGraph) +
		      QStringLiteral("/oauth/access_token")),
		 q.toString(QUrl::FullyEncoded), Op::FbLongLived);
}

void MetadataDock::onDisconnectFacebook()
{
	if (isManaged()) {
		onDisconnectManaged(meta::Platform::Facebook);
		return;
	}
	const Account snap = fb_; // revoke necesita el token borrado
	wipeLocal(meta::Platform::Facebook);
	startRevoke(meta::Platform::Facebook, snap);
}

void MetadataDock::onRefreshBroadcasts()
{
	// FASE 2.1-C: in Managed the list comes from the backend (tokens
	// server-side); same combo model, plain broadcast IDs as item data.
	if (isManaged()) {
		if (!managedAccount(meta::Platform::YouTube).connected) {
			generalMsg_->setText(meta::userMessage(
				meta::Outcome::AuthRequired,
				meta::Platform::YouTube));
			return;
		}
		applyAfterList_ = false;
		fetchManagedBroadcasts();
		return;
	}
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

// --- facebook targets + live videos (T-071 FB-2, D3/D9) -------------------
// Perfil primero (E2E viable sin Page 100+); Pages via /me/accounts con
// page tokens en memoria (jamas persistidos). Objetos propios unicamente:
// jamas el preview web de herramientas externas (H1 refutada, F-065/066).

void MetadataDock::fetchFacebookTargets()
{
	if (!fb_.connected) {
		generalMsg_->setText(meta::userMessage(
			meta::Outcome::AuthRequired,
			meta::Platform::Facebook));
		return;
	}
	fbTargetCombo_->clear();
	fbLiveCombo_->clear();
	fbPageTokens_.clear();
	// Perfil siempre presente; las Pages llegan con /me/accounts.
	fbTargetCombo_->addItem(tr("Profile"), QStringLiteral("me"));
	fbTarget_ = QStringLiteral("me");
	QUrl url(QString::fromLatin1(kFbGraph) +
		 QStringLiteral("/me/accounts"));
	QUrlQuery q;
	q.addQueryItem(QStringLiteral("fields"),
		       QStringLiteral("id,name,access_token"));
	q.addQueryItem(QStringLiteral("limit"), QStringLiteral("25"));
	url.setQuery(q);
	fbRefreshButton_->setEnabled(false);
	sendGet(url, Op::FbTargets, fb_.access);
}

void MetadataDock::fetchFacebookVideos()
{
	if (!fb_.connected) {
		generalMsg_->setText(meta::userMessage(
			meta::Outcome::AuthRequired,
			meta::Platform::Facebook));
		return;
	}
	fbLiveCombo_->clear();
	const QString target =
		fbTarget_.isEmpty() ? QStringLiteral("me") : fbTarget_;
	QString token = fb_.access;
	if (target != QStringLiteral("me")) {
		if (!fbPageTokens_.contains(target)) {
			finishPlatform(meta::Platform::Facebook, false,
				       meta::userMessage(
					       meta::Outcome::AuthRequired,
					       meta::Platform::Facebook));
			startApplyNext();
			return;
		}
		token = fbPageTokens_.value(target);
	}
	QUrl url(QString::fromLatin1(kFbGraph) + QStringLiteral("/") +
		 target + QStringLiteral("/live_videos"));
	QUrlQuery q;
	q.addQueryItem(QStringLiteral("fields"),
		       QStringLiteral("id,title,description,status"));
	q.addQueryItem(QStringLiteral("limit"), QStringLiteral("25"));
	url.setQuery(q);
	fbRefreshButton_->setEnabled(false);
	sendGet(url, Op::FbList, token);
}

void MetadataDock::onRefreshFacebook()
{
	// FB-3: en Managed la lista viene del backend (tokens server-side),
	// mismo combo (vale item listado o ID pegado, F-074/F-075).
	if (isManaged()) {
		if (!managedAccount(meta::Platform::Facebook).connected) {
			generalMsg_->setText(meta::userMessage(
				meta::Outcome::AuthRequired,
				meta::Platform::Facebook));
			return;
		}
		applyAfterList_ = false;
		fetchManagedFacebookVideos();
		return;
	}
	if (!fb_.connected) {
		generalMsg_->setText(meta::userMessage(
			meta::Outcome::AuthRequired,
			meta::Platform::Facebook));
		return;
	}
	applyAfterList_ = false;
	fetchFacebookTargets();
}

void MetadataDock::onFacebookTargetChanged(int index)
{
	if (index < 0 || !fbTargetCombo_ || !fb_.connected)
		return;
	const QString target = fbTargetCombo_->itemData(index).toString();
	if (target.isEmpty() || target == fbTarget_)
		return;
	fbTarget_ = target;
	applyAfterList_ = false;
	fetchFacebookVideos();
}

// --- facebook lifecycle FB-4 (T-073, D6R/D7/F-075) ------------------------
// Indicador por `status` del LiveVideo; ON solo sobre ID existente o
// creado-por-dock + confirmacion explicita; OFF/DELETE confirman;
// CREATE persiste el ID (secure::Data.facebookLiveId) + limpieza DELETE.
// Cero RTMP/keys/Aitum (§51): solo estado del LiveVideo por API.

QString MetadataDock::currentFbLiveId() const
{
	if (!fbLiveCombo_)
		return QString();
	QString id = fbLiveCombo_->currentData().toString();
	if (id.isEmpty() && fbLiveCombo_->isEditable())
		id = fbLiveCombo_->currentText().trimmed();
	// El combo muestra "title (id)" o "title (status)": extraer el ID
	// entre parentesis cuando el texto pegado trae ese formato.
	if (id.contains(QLatin1Char('('))) {
		const int a = id.lastIndexOf(QLatin1Char('('));
		const int b = id.lastIndexOf(QLatin1Char(')'));
		if (a >= 0 && b > a)
			id = id.mid(a + 1, b - a - 1).trimmed();
	}
	return id;
}

void MetadataDock::setFbLiveId(const QString &id)
{
	const QString clean = id.trimmed();
	if (clean.isEmpty() || !fbLiveCombo_)
		return;
	// Evitar duplicados: reutilizar item existente o añadirlo.
	for (int i = 0; i < fbLiveCombo_->count(); ++i) {
		if (fbLiveCombo_->itemData(i).toString() == clean ||
		    fbLiveCombo_->itemText(i).contains(clean)) {
			fbLiveCombo_->setCurrentIndex(i);
			saveStore();
			return;
		}
	}
	fbLiveCombo_->addItem(clean, clean);
	fbLiveCombo_->setCurrentIndex(fbLiveCombo_->count() - 1);
	saveStore(); // persiste facebookLiveId (plaintext ID, sin secretos)
}

void MetadataDock::setFbState(const QString &status)
{
	if (!fbStateLabel_)
		return;
	const meta::FbLiveState st = meta::fbLiveState(status);
	const QString label = meta::fbLiveStateLabel(st);
	QString dot = QStringLiteral("○");
	if (st == meta::FbLiveState::Live)
		dot = QStringLiteral("● LIVE");
	else if (st == meta::FbLiveState::Preview)
		dot = QStringLiteral("○ Preview");
	else if (st == meta::FbLiveState::Ended)
		dot = QStringLiteral("■ Ended");
	fbStateLabel_->setText(tr("Status: %1 (%2)").arg(
		dot, status.isEmpty() ? QStringLiteral("—") : status));
}

void MetadataDock::onFbStatus()
{
	const QString liveId = currentFbLiveId();
	if (liveId.isEmpty()) {
		generalMsg_->setText(meta::userMessage(
			meta::Outcome::NotFound, meta::Platform::Facebook));
		return;
	}
	if (isManaged()) {
		if (!managedAccount(meta::Platform::Facebook).connected) {
			generalMsg_->setText(meta::userMessage(
				meta::Outcome::AuthRequired,
				meta::Platform::Facebook));
			return;
		}
		startManagedFbOp(QStringLiteral("status"), liveId);
		return;
	}
	if (!fb_.connected) {
		generalMsg_->setText(meta::userMessage(
			meta::Outcome::AuthRequired,
			meta::Platform::Facebook));
		return;
	}
	startFbStatusIndependent(liveId);
}

void MetadataDock::onFbCreate()
{
	if (isManaged()) {
		if (!managedAccount(meta::Platform::Facebook).connected) {
			generalMsg_->setText(meta::userMessage(
				meta::Outcome::AuthRequired,
				meta::Platform::Facebook));
			return;
		}
		startManagedFbOp(QStringLiteral("create"), QString());
		return;
	}
	if (!fb_.connected) {
		generalMsg_->setText(meta::userMessage(
			meta::Outcome::AuthRequired,
			meta::Platform::Facebook));
		return;
	}
	startFbCreateIndependent();
}

void MetadataDock::onFbGoLive()
{
	const QString liveId = currentFbLiveId();
	if (liveId.isEmpty()) {
		generalMsg_->setText(meta::userMessage(
			meta::Outcome::NotFound, meta::Platform::Facebook));
		return;
	}
	QMessageBox::StandardButton ok = QMessageBox::warning(
		this, tr("Go live on Facebook"),
		tr("This will PUBLISH the video %1 as LIVE_NOW, visible to "
		   "its audience. Continue?")
			.arg(liveId),
		QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
	if (ok != QMessageBox::Yes)
		return;
	if (isManaged()) {
		if (!managedAccount(meta::Platform::Facebook).connected) {
			generalMsg_->setText(meta::userMessage(
				meta::Outcome::AuthRequired,
				meta::Platform::Facebook));
			return;
		}
		startManagedFbOp(QStringLiteral("go_live"), liveId);
		return;
	}
	if (!fb_.connected) {
		generalMsg_->setText(meta::userMessage(
			meta::Outcome::AuthRequired,
			meta::Platform::Facebook));
		return;
	}
	startFbGoLiveIndependent(liveId);
}

void MetadataDock::onFbEndLive()
{
	const QString liveId = currentFbLiveId();
	if (liveId.isEmpty()) {
		generalMsg_->setText(meta::userMessage(
			meta::Outcome::NotFound, meta::Platform::Facebook));
		return;
	}
	QMessageBox::StandardButton ok = QMessageBox::question(
		this, tr("End Facebook live"),
		tr("End the live video %1 (VOD)?").arg(liveId),
		QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
	if (ok != QMessageBox::Yes)
		return;
	if (isManaged()) {
		if (!managedAccount(meta::Platform::Facebook).connected) {
			generalMsg_->setText(meta::userMessage(
				meta::Outcome::AuthRequired,
				meta::Platform::Facebook));
			return;
		}
		startManagedFbOp(QStringLiteral("end"), liveId);
		return;
	}
	if (!fb_.connected) {
		generalMsg_->setText(meta::userMessage(
			meta::Outcome::AuthRequired,
			meta::Platform::Facebook));
		return;
	}
	startFbEndIndependent(liveId);
}

void MetadataDock::onFbDelete()
{
	const QString liveId = currentFbLiveId();
	if (liveId.isEmpty()) {
		generalMsg_->setText(meta::userMessage(
			meta::Outcome::NotFound, meta::Platform::Facebook));
		return;
	}
	QMessageBox::StandardButton ok = QMessageBox::warning(
		this, tr("Delete Facebook live video"),
		tr("DELETE the live video %1? This cannot be undone.")
			.arg(liveId),
		QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
	if (ok != QMessageBox::Yes)
		return;
	if (isManaged()) {
		if (!managedAccount(meta::Platform::Facebook).connected) {
			generalMsg_->setText(meta::userMessage(
				meta::Outcome::AuthRequired,
				meta::Platform::Facebook));
			return;
		}
		startManagedFbOp(QStringLiteral("delete"), liveId);
		return;
	}
	if (!fb_.connected) {
		generalMsg_->setText(meta::userMessage(
			meta::Outcome::AuthRequired,
			meta::Platform::Facebook));
		return;
	}
	startFbDeleteIndependent(liveId);
}

void MetadataDock::startFbStatusIndependent(const QString &liveId)
{
	QUrl url(QString::fromLatin1(kFbGraph) + QStringLiteral("/") +
		 liveId);
	QUrlQuery q;
	q.addQueryItem(QStringLiteral("fields"),
		       QStringLiteral("id,title,description,status"));
	url.setQuery(q);
	setFbState(QString());
	generalMsg_->setText(tr("Checking Facebook status…"));
	sendGet(url, Op::FbStatus, fb_.access);
}

void MetadataDock::startFbCreateIndependent()
{
	const QString title = titleEdit_->text().trimmed();
	if (title.isEmpty() || title.size() > meta::kFacebookTitleMax) {
		generalMsg_->setText(meta::userMessage(
			meta::Outcome::BadRequest, meta::Platform::Facebook));
		return;
	}
	const QString target =
		fbTarget_.isEmpty() ? QStringLiteral("me") : fbTarget_;
	QString token = fb_.access;
	if (target != QStringLiteral("me"))
		token = fbPageTokens_.value(target, token);
	QUrl url(QString::fromLatin1(kFbGraph) + QStringLiteral("/") +
		 target + QStringLiteral("/live_videos"));
	QJsonObject body;
	body[QStringLiteral("title")] = title;
	const QString desc = descEdit_->toPlainText();
	if (!desc.isEmpty())
		body[QStringLiteral("description")] = desc;
	QJsonObject privacy;
	privacy[QStringLiteral("value")] = QStringLiteral("EVERYONE");
	body[QStringLiteral("privacy")] = privacy;
	generalMsg_->setText(tr("Creating Facebook live video…"));
	sendJson(url, QStringLiteral("POST"),
		 QString::fromUtf8(
			 QJsonDocument(body).toJson(QJsonDocument::Compact)),
		 Op::FbCreate, token);
}

void MetadataDock::startFbGoLiveIndependent(const QString &liveId)
{
	QUrl url(QString::fromLatin1(kFbGraph) + QStringLiteral("/") +
		 liveId);
	QJsonObject body;
	body[QStringLiteral("status")] = QStringLiteral("LIVE_NOW");
	generalMsg_->setText(tr("Going live on Facebook…"));
	sendJson(url, QStringLiteral("POST"),
		 QString::fromUtf8(
			 QJsonDocument(body).toJson(QJsonDocument::Compact)),
		 Op::FbGoLive, fb_.access);
}

void MetadataDock::startFbEndIndependent(const QString &liveId)
{
	QUrl url(QString::fromLatin1(kFbGraph) + QStringLiteral("/") +
		 liveId);
	QUrlQuery q;
	q.addQueryItem(QStringLiteral("end_live_video"),
		       QStringLiteral("true"));
	url.setQuery(q);
	generalMsg_->setText(tr("Ending Facebook live…"));
	sendJson(url, QStringLiteral("POST"), QString(), Op::FbEnd,
		 fb_.access);
}

void MetadataDock::startFbDeleteIndependent(const QString &liveId)
{
	QUrl url(QString::fromLatin1(kFbGraph) + QStringLiteral("/") +
		 liveId);
	generalMsg_->setText(tr("Deleting Facebook live video…"));
	sendJson(url, QStringLiteral("DELETE"), QString(), Op::FbDelete,
		 fb_.access);
}

void MetadataDock::startManagedFbOp(const QString &op, const QString &liveId)
{
	QJsonObject body;
	body[QStringLiteral("op")] = op;
	if (!liveId.isEmpty())
		body[QStringLiteral("live_video_id")] = liveId;
	if (op == QStringLiteral("create")) {
		body[QStringLiteral("title")] = titleEdit_->text();
		body[QStringLiteral("description")] =
			descEdit_->toPlainText();
		if (!fbTarget_.isEmpty())
			body[QStringLiteral("target")] = fbTarget_;
	}
	generalMsg_->setText(tr("Facebook %1…").arg(op));
	managedAuth_->apiPost(
		QStringLiteral("/metadata/facebook"), body,
		[this, op, liveId](const backend_auth::Client::ApiReply &rep) {
			obs_log(LOG_INFO,
				"managed fb %s reply: result=%d http=%d",
				qPrintable(op),
				static_cast<int>(rep.result), rep.http);
			if (!isManaged())
				return;
			if (rep.result !=
			    backend_auth::Result::Ok) {
				const meta::Outcome oc =
					rep.result ==
							backend_auth::Result::
								NetworkError
						? meta::Outcome::NetworkError
						: meta::classifyStatus(
							  rep.http == 0 ? -1
									: rep.http);
				generalMsg_->setText(meta::userMessage(
					oc, meta::Platform::Facebook));
				return;
			}
			const QJsonObject res =
				rep.body.value(QStringLiteral("result"))
					.toObject();
			const QString id =
				res.value(QStringLiteral("id")).toString();
			const QString st =
				res.value(QStringLiteral("status"))
					.toString();
			if (!id.isEmpty() &&
			    (op == QStringLiteral("create") ||
			     op == QStringLiteral("status")))
				setFbLiveId(id);
			if (!st.isEmpty())
				setFbState(st);
			// F-085: tras on/off, lectura encadenada (una vez,
			// sin polling) para dejar el indicador real sin
			// Check manual; el mensaje de la operacion se conserva.
			const QString doneId = !id.isEmpty()
						       ? id
						       : liveId;
			if ((op == QStringLiteral("go_live") ||
			     op == QStringLiteral("end")) &&
			    !doneId.isEmpty()) {
				if (op == QStringLiteral("go_live"))
					setFbState(QStringLiteral("LIVE"));
				else
					setFbState(QStringLiteral("VOD"));
				fbQuietStatus_ = true;
				QJsonObject sbody;
				sbody[QStringLiteral("op")] =
					QStringLiteral("status");
				sbody[QStringLiteral("live_video_id")] =
					doneId;
				managedAuth_->apiPost(
					QStringLiteral("/metadata/facebook"),
					sbody,
					[this, doneId](const backend_auth::Client::
						       ApiReply &srep) {
						if (!isManaged())
							return;
						if (srep.result !=
						    backend_auth::Result::
							    Ok) {
							fbQuietStatus_ = false;
							return;
						}
						const QJsonObject sres =
							srep.body
								.value(QStringLiteral(
									"result"))
								.toObject();
						setFbState(
							sres.value(QStringLiteral(
									"status"))
								.toString());
						fbQuietStatus_ = false;
					});
			}
			if (op == QStringLiteral("delete") && !id.isEmpty()) {
				// Limpieza F-075: retirar el ID del combo y
				// del store persistido.
				for (int i = fbLiveCombo_->count() - 1;
				     i >= 0; --i) {
					if (fbLiveCombo_->itemData(i)
						    .toString() == id ||
					    fbLiveCombo_->itemText(i)
						    .contains(id))
						fbLiveCombo_->removeItem(i);
				}
				if (store_) {
					secure::Data d;
					store_->load(d);
					if (d.facebookLiveId == id) {
						d.facebookLiveId.clear();
						store_->save(d);
					}
				}
				setFbState(QString());
			}
			generalMsg_->setText(
				tr("Facebook %1 OK.").arg(op));
		});
}

// --- apply -------------------------------------------------------------

void MetadataDock::onApply()
{
	meta::Metadata m{titleEdit_->text(), descEdit_->toPlainText()};
	meta::Selection s{twCheck_->isChecked(), ytCheck_->isChecked(),
			  kkCheck_->isChecked(), fbCheck_->isChecked()};
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
	if (s.facebook)
		applyQueue_ << meta::Platform::Facebook;
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
	// Managed Apply (backend token): YouTube (FASE 2.1-C), Twitch y
	// Kick (T-068), Facebook (FB-3). Solo Independent usa su cuenta
	// directa.
	const bool authorized =
		(isManaged() && (p == P::YouTube || p == P::Twitch ||
				 p == P::Kick || p == P::Facebook))
			? managedAccount(p).connected
			: account(p).connected;
	if (!authorized) {
		finishPlatform(p, false,
			       meta::userMessage(meta::Outcome::AuthRequired,
						 p));
		startApplyNext();
		return;
	}
	setResult(p, true, tr("Updating…"));
	const QString title = titleEdit_->text();
	if (p == P::Twitch) {
		// Apply Managed Twitch vía backend (token server-side);
		// Independent sigue directo abajo, intacto.
		if (isManaged()) {
			startManagedTwitchApply();
			return;
		}
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
		// FASE 2.1-C: Managed Apply goes through the backend with the
		// Managed token server-side; Independent path below untouched.
		if (isManaged()) {
			startManagedYouTubeApply();
			return;
		}
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
	} else if (p == P::Kick) {
		// T-068: Managed Apply va por el backend con el token
		// server-side; Independent directo abajo, intacto.
		if (isManaged()) {
			startManagedKickApply();
			return;
		}
		sendJson(QUrl(QStringLiteral(
				 "https://api.kick.com/public/v1/channels")),
			 QStringLiteral("PATCH"), meta::kickPayload(title),
			 Op::UpKk, kk_.access, QString(), true);
	} else if (p == P::Facebook) {
		// FB-3: Managed Apply va por el backend con el token
		// server-side; Independent directo abajo, intacto.
		if (isManaged()) {
			startManagedFacebookApply();
			return;
		}
		// F-074: vale el item listado o un ID pegado a mano.
		QString liveId = fbLiveCombo_->currentData().toString();
		if (liveId.isEmpty())
			liveId = fbLiveCombo_->currentText().trimmed();
		if (liveId.isEmpty()) {
			// Fetch targets+list first, then retry this platform.
			applyQueue_.prepend(p);
			applyAfterList_ = true;
			fetchFacebookTargets();
			return;
		}
		const QString target =
			fbTarget_.isEmpty() ? QStringLiteral("me")
					    : fbTarget_;
		QString token = fb_.access;
		if (target != QStringLiteral("me"))
			token = fbPageTokens_.value(target, token);
		QUrl url(QString::fromLatin1(kFbGraph) +
			 QStringLiteral("/") + liveId);
		// D1/D11: title+desc (+privacy EVERYONE validada en B').
		QJsonObject body;
		body[QStringLiteral("title")] = title;
		const QString desc = descEdit_->toPlainText();
		if (!desc.isEmpty())
			body[QStringLiteral("description")] = desc;
		QJsonObject privacy;
		privacy[QStringLiteral("value")] =
			QStringLiteral("EVERYONE");
		body[QStringLiteral("privacy")] = privacy;
		sendJson(url, QStringLiteral("POST"),
			 QString::fromUtf8(QJsonDocument(body).toJson(
				 QJsonDocument::Compact)),
			 Op::UpFb, token);
	}
}

// --- managed Kick apply (T-068) -------------------------------------------
// Token server-side: el plugin envía solo sesión bearer + {title}.
// Sin descripción: Kick no tiene equivalente (channel_description es del
// canal y jamás se escribe como descripción del stream).

void MetadataDock::startManagedKickApply()
{
	using P = meta::Platform;
	const P p = P::Kick;
	const ManagedConn &m = managedAccount(p);
	if (!m.connected || m.userId.isEmpty()) {
		finishPlatform(p, false,
			       meta::userMessage(meta::Outcome::AuthRequired,
						 p));
		startApplyNext();
		return;
	}
	setResult(p, true, tr("Updating…"));
	QJsonObject body;
	body[QStringLiteral("title")] = titleEdit_->text();
	managedAuth_->apiPost(QStringLiteral("/metadata/kick"), body,
			      [this, p](const backend_auth::Client::ApiReply &rep) {
				      obs_log(LOG_INFO,
					      "managed apply reply: result=%d "
					      "http=%d",
					      static_cast<int>(rep.result),
					      rep.http);
				      if (!isManaged())
					      return; // user switched mode
				      if (rep.result ==
					  backend_auth::Result::Ok) {
					      finishPlatform(
						      p, true,
						      meta::userMessage(
							      meta::Outcome::Success,
							      p));
					      obs_log(LOG_INFO,
						      "apply %s: ok (managed)",
						      meta::platformName(p));
					      startApplyNext();
					      return;
				      }
				      const meta::Outcome oc =
					      rep.result ==
						      backend_auth::Result::
							      NetworkError
					      ? meta::Outcome::NetworkError
					      : meta::classifyStatus(
						      rep.http == 0 ? -1
								    : rep.http);
				      if (scheduleBackoff(p, oc, rep.http))
					      return;
				      if (oc == meta::Outcome::AuthRequired) {
					      ManagedConn &mm =
						      managedAccount(p);
					      mm.connected = false;
					      mm.userId.clear();
					      mm.display.clear();
					      setStatus(p,
							tr("Needs reconnection."));
					      saveStore(); // drop the stale snapshot
				      }
				      finishPlatform(
					      p, false,
					      meta::userMessage(oc, p));
				      startApplyNext();
			      });
}

// --- managed Facebook videos + apply (FB-3, T-072) --------------------------
// Token server-side: el plugin envía solo sesión bearer + {live_video_id,
// title, description} (+ target si hay Page seleccionada; por defecto el
// perfil). Listado best-effort (F-074/F-075: puede venir vacío con objetos
// legibles por ID): el combo acepta pegar el ID a mano, sin inventar
// broadcasts. Título 1-254 + descripción SÍ (D2, como YouTube).

void MetadataDock::fetchManagedFacebookVideos()
{
	using P = meta::Platform;
	fbRefreshButton_->setEnabled(false);
	managedAuth_->apiGet(QStringLiteral("/metadata/facebook/broadcasts"),
			     [this](const backend_auth::Client::ApiReply &rep) {
				     using P = meta::Platform;
				     obs_log(LOG_INFO,
					     "managed fb videos reply: "
					     "result=%d http=%d",
					     static_cast<int>(rep.result),
					     rep.http);
				     fbRefreshButton_->setEnabled(true);
				     if (!isManaged())
					     return; // user switched mode
				     if (rep.result !=
					 backend_auth::Result::Ok) {
					     const meta::Outcome oc =
						     rep.result ==
							     backend_auth::Result::
								     NetworkError
						     ? meta::Outcome::
							       NetworkError
						     : meta::classifyStatus(
							       rep.http == 0
								       ? -1
								       : rep.http);
					     if (applyAfterList_) {
						     applyAfterList_ = false;
						     const P p = applyQueue_
								     .takeFirst();
						     finishPlatform(
							     p, false,
							     meta::userMessage(
								     oc, p));
						     startApplyNext();
					     } else {
						     generalMsg_->setText(
							     meta::userMessage(
								     oc, P::Facebook));
					     }
					     return;
				     }
				     fbLiveCombo_->clear();
				     const QJsonArray items =
					     rep.body
						     .value(QStringLiteral(
							 "resources"))
						     .toArray();
				     for (const auto &v : items) {
					     const QJsonObject it =
						     v.toObject();
					     const QString id = it.value(
								QStringLiteral(
									"id"))
								.toString();
					     const QString title =
						     it.value(QStringLiteral(
								"title"))
								.toString();
					     if (id.isEmpty())
						     continue;
					     fbLiveCombo_->addItem(
						     QStringLiteral("%1 (%2)")
							     .arg(title, id),
						     id);
				     }
				     if (applyAfterList_) {
					     applyAfterList_ = false;
					     if (fbLiveCombo_->count() ==
						     0 &&
						 !applyQueue_.isEmpty()) {
						     const P p = applyQueue_
								     .takeFirst();
						     finishPlatform(
							     p, false,
							     meta::userMessage(
								     meta::Outcome::
									     NotFound,
								     p));
					     }
					     startApplyNext();
				     } else {
					     generalMsg_->setText(
						     tr("Facebook videos loaded "
							"(%1).")
							     .arg(fbLiveCombo_
								      ->count()));
				     }
			     });
}

void MetadataDock::startManagedFacebookApply()
{
	using P = meta::Platform;
	const P p = P::Facebook;
	const ManagedConn &m = managedAccount(p);
	if (!m.connected || m.userId.isEmpty()) {
		finishPlatform(p, false,
			       meta::userMessage(meta::Outcome::AuthRequired,
						 p));
		startApplyNext();
		return;
	}
	// F-074: vale el item listado o un ID pegado a mano.
	QString liveId = fbLiveCombo_->currentData().toString();
	if (liveId.isEmpty())
		liveId = fbLiveCombo_->currentText().trimmed();
	if (liveId.isEmpty()) {
		// Fetch the list first, then retry this platform.
		applyQueue_.prepend(p);
		applyAfterList_ = true;
		fetchManagedFacebookVideos();
		return;
	}
	setResult(p, true, tr("Updating…"));
	QJsonObject body;
	body[QStringLiteral("live_video_id")] = liveId;
	body[QStringLiteral("title")] = titleEdit_->text();
	body[QStringLiteral("description")] = descEdit_->toPlainText();
	if (!fbTarget_.isEmpty())
		body[QStringLiteral("target")] = fbTarget_;
	managedAuth_->apiPost(QStringLiteral("/metadata/facebook"), body,
			      [this, p](const backend_auth::Client::ApiReply &rep) {
				      obs_log(LOG_INFO,
					      "managed apply reply: result=%d "
					      "http=%d",
					      static_cast<int>(rep.result),
					      rep.http);
				      if (!isManaged())
					      return; // user switched mode
				      if (rep.result ==
					  backend_auth::Result::Ok) {
					      finishPlatform(
						      p, true,
						      meta::userMessage(
							      meta::Outcome::Success,
							      p));
					      obs_log(LOG_INFO,
						      "apply %s: ok (managed)",
						      meta::platformName(p));
					      startApplyNext();
					      return;
				      }
				      const meta::Outcome oc =
					      rep.result ==
						      backend_auth::Result::
							      NetworkError
					      ? meta::Outcome::NetworkError
					      : meta::classifyStatus(
						      rep.http == 0 ? -1
								    : rep.http);
				      if (scheduleBackoff(p, oc, rep.http))
					      return;
				      if (oc == meta::Outcome::AuthRequired) {
					      ManagedConn &mm =
						      managedAccount(p);
					      mm.connected = false;
					      mm.userId.clear();
					      mm.display.clear();
					      setStatus(p,
							tr("Needs reconnection."));
					      saveStore(); // drop the stale snapshot
				      }
				      finishPlatform(
					      p, false,
					      meta::userMessage(oc, p));
				      startApplyNext();
			      });
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
	// T-071 FB-2: Facebook sin refresh_token clasico (D8): el 401/190
	// exige reconexion; este camino nunca se usa para FB.
	if (p == meta::Platform::Facebook) {
		fb_.connected = false;
		setStatus(p, tr("Needs reconnection."));
		finishPlatform(p, false,
			       meta::userMessage(meta::Outcome::AuthRequired,
						 p));
		startApplyNext();
		return;
	}
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

	// A mode switch cancels Independent connect-phase flows (see
	// cancelPendingForModeSwitch): a late reply must not complete a
	// connection in the previous context. Apply/refresh traffic keeps
	// its own lifecycle and is unaffected.
	switch (op) {
	case Op::TwDevice:
	case Op::TwPoll:
	case Op::TwValidate:
	case Op::YtExchange:
	case Op::YtChannels:
	case Op::KkExchange:
	case Op::KkChannels:
	case Op::FbExchange:
	case Op::FbLongLived:
	case Op::FbIdentity:
	case Op::FbTargets:
	case Op::FbStatus:
	case Op::FbCreate:
	case Op::FbGoLive:
	case Op::FbEnd:
	case Op::FbDelete:
		if (isManaged())
			return;
		break;
	default:
		break;
	}

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

	case Op::FbExchange: {
		// Code exchange PKCE sin secret (F-065). Con secret en app
		// Nativa/Desktop Graph responde 400: el usuario debe quitar
		// el tipo Business o usar PKCE (aqui ya sin secret).
		if (netFail || http != 200) {
			connectHttpError(P::Facebook, "exchange", http,
					 netFail);
			return;
		}
		const QJsonObject o = replyJson(reply);
		fb_.access = o.value(QStringLiteral("access_token"))
				     .toString();
		fb_.refresh.clear(); // FB sin refresh_token clasico (D8)
		if (fb_.access.isEmpty()) {
			fail(P::Facebook, tr("No access token returned."));
			return;
		}
		QUrl url(QString::fromLatin1(kFbGraph) +
			 QStringLiteral("/me"));
		QUrlQuery q;
		q.addQueryItem(QStringLiteral("fields"),
			       QStringLiteral("id,name"));
		url.setQuery(q);
		sendGet(url, Op::FbIdentity, fb_.access);
		return;
	}

	case Op::FbIdentity: {
		QString label = tr("connected");
		QString uid;
		if (!netFail && http == 200) {
			const QJsonObject o = replyJson(reply);
			uid = o.value(QStringLiteral("id")).toString();
			const QString name =
				o.value(QStringLiteral("name")).toString();
			if (!name.isEmpty())
				label = name;
		}
		if (uid.isEmpty()) {
			fail(P::Facebook, tr("Could not read profile."));
			return;
		}
		fb_.broadcaster = uid; // etiqueta no sensible (igual que TW)
		finishConnectOk(P::Facebook, label);
		startFacebookLongLived(); // best-effort; sin secret sigue igual
		return;
	}

	case Op::FbLongLived: {
		// Best-effort (D8): con secret BYO el corto se cambia por
		// ~60d; sin secret o con fallo se conserva el corto.
		// Solo el codigo va al log; el token jamas.
		if (!netFail && http == 200) {
			const QString lt = replyJson(reply)
						   .value(QStringLiteral(
							   "access_token"))
						   .toString();
			if (!lt.isEmpty()) {
				fb_.access = lt;
				saveStore();
				obs_log(LOG_INFO,
					"facebook long-lived exchanged");
			}
		} else {
			obs_log(LOG_WARNING,
				"facebook long-lived skipped http %d", http);
		}
		fetchFacebookTargets();
		return;
	}

	case Op::FbTargets: {
		fbRefreshButton_->setEnabled(true);
		if (netFail || http != 200) {
			// F-072: sin pages_show_list (/me/accounts 403) se
			// sigue solo con perfil (D9); el error real sale en
			// FbList si el token tampoco vale para /me.
			obs_log(LOG_WARNING,
				"facebook targets http %d, profile-only",
				http);
			// applyAfterList_ se mantiene: FbList continuara el
			// Apply con los videos del perfil.
			fetchFacebookVideos();
			return;
		}
		const QJsonArray items = replyJson(reply)
						 .value(QStringLiteral("data"))
						 .toArray();
		for (const auto &v : items) {
			const QJsonObject it = v.toObject();
			const QString pid =
				it.value(QStringLiteral("id")).toString();
			const QString pname =
				it.value(QStringLiteral("name")).toString();
			const QString ptoken = it.value(QStringLiteral(
								"access_token"))
						       .toString();
			if (pid.isEmpty() || ptoken.isEmpty())
				continue;
			fbPageTokens_.insert(pid, ptoken);
			fbTargetCombo_->addItem(
				tr("Page: %1").arg(
					pname.isEmpty() ? pid : pname),
				pid);
		}
		fetchFacebookVideos();
		return;
	}

	case Op::FbList: {
		fbRefreshButton_->setEnabled(true);
		auto fbCode = [&]() {
			const QJsonObject err = replyJson(reply)
							    .value(QStringLiteral(
								    "error"))
							    .toObject();
			int code = err.value(QStringLiteral("code"))
					   .toInt(0);
			const int sub = err.value(QStringLiteral(
							  "error_subcode"))
					      .toInt(0);
			if (code == 0)
				code = sub;
			else if (code == 100 && sub == 33)
				code = 33; // F-080: objeto web/ID desconocido
			return code;
		};
		if (netFail || http != 200) {
			const int fbc = fbCode();
			const meta::Outcome oc =
				meta::classifyFb(http, fbc);
			const QString spec =
				meta::fbEligibilityMessage(fbc);
			const QString msg = !spec.isEmpty()
						    ? spec
						    : meta::userMessage(
							      oc, P::Facebook);
			if (applyAfterList_) {
				applyAfterList_ = false;
				const P p = applyQueue_.takeFirst();
				finishPlatform(p, false, msg);
				startApplyNext();
			} else {
				generalMsg_->setText(msg);
			}
			return;
		}
		fbLiveCombo_->clear();
		const QJsonArray items = replyJson(reply)
						 .value(QStringLiteral("data"))
						 .toArray();
		for (const auto &v : items) {
			const QJsonObject it = v.toObject();
			const QString id =
				it.value(QStringLiteral("id")).toString();
			const QString title =
				it.value(QStringLiteral("title")).toString();
			const QString status =
				it.value(QStringLiteral("status")).toString();
			if (id.isEmpty())
				continue;
			fbLiveCombo_->addItem(
				QStringLiteral("%1 (%2)").arg(
					title.isEmpty() ? id : title, status),
				id);
		}
		if (applyAfterList_) {
			applyAfterList_ = false;
			if (fbLiveCombo_->count() == 0 &&
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
				tr("Facebook videos loaded (%1).")
					.arg(fbLiveCombo_->count()));
		}
		return;
	}

	case Op::FbRead: {
		// Read-back manual (E2E/sondas): informa, no decide Apply.
		if (netFail || http != 200)
			generalMsg_->setText(meta::userMessage(
				meta::classifyFb(http, 0), P::Facebook));
		else
			generalMsg_->setText(tr("Facebook video read OK."));
		return;
	}

	case Op::FbStatus: {
		// Indicador D7: solo lectura, sin decidir Apply.
		if (netFail || http != 200) {
			const QJsonObject err = replyJson(reply)
							    .value(QStringLiteral(
								    "error"))
							    .toObject();
			int code = err.value(QStringLiteral("code"))
					   .toInt(0);
			const int sub = err.value(QStringLiteral(
							  "error_subcode"))
					      .toInt(0);
			if (code == 0)
				code = sub;
			else if (code == 100 && sub == 33)
				code = 33;
			const QString spec =
				meta::fbEligibilityMessage(code);
			generalMsg_->setText(
				!spec.isEmpty()
					? spec
					: meta::userMessage(
						  meta::classifyFb(http, code),
						  P::Facebook));
			setFbState(QString());
			return;
		}
		const QJsonObject o = replyJson(reply);
		setFbState(o.value(QStringLiteral("status")).toString());
		if (!fbQuietStatus_)
			generalMsg_->setText(
				tr("Facebook status: %1")
					.arg(o.value(QStringLiteral("status"))
						     .toString()));
		fbQuietStatus_ = false;
		obs_log(LOG_INFO, "facebook status checked");
		return;
	}

	case Op::FbCreate: {
		if (netFail || http != 200) {
			const QJsonObject err = replyJson(reply)
							    .value(QStringLiteral(
								    "error"))
							    .toObject();
			int code = err.value(QStringLiteral("code"))
					   .toInt(0);
			const int sub = err.value(QStringLiteral(
							  "error_subcode"))
					      .toInt(0);
			if (code == 0)
				code = sub;
			else if (code == 100 && sub == 33)
				code = 33;
			const QString spec =
				meta::fbEligibilityMessage(code);
			generalMsg_->setText(
				!spec.isEmpty()
					? spec
					: meta::userMessage(
						  meta::classifyFb(http, code),
						  P::Facebook));
			return;
		}
		const QJsonObject o = replyJson(reply);
		const QString nid = o.value(QStringLiteral("id"))
					    .toString();
		if (nid.isEmpty()) {
			generalMsg_->setText(meta::userMessage(
				meta::Outcome::ServerRetry, P::Facebook));
			return;
		}
		setFbLiveId(nid);
		setFbState(QStringLiteral("UNPUBLISHED"));
		generalMsg_->setText(
			tr("Facebook live video created (%1).").arg(nid));
		obs_log(LOG_INFO, "facebook live video created");
		return;
	}

	case Op::FbGoLive:
	case Op::FbEnd:
	case Op::FbDelete: {
		const char *label = (op == Op::FbGoLive)
					    ? "facebook go-live"
					    : (op == Op::FbEnd)
						      ? "facebook end-live"
						      : "facebook delete";
		if (netFail || http != 200) {
			const QJsonObject err = replyJson(reply)
							    .value(QStringLiteral(
								    "error"))
							    .toObject();
			int code = err.value(QStringLiteral("code"))
					   .toInt(0);
			const int sub = err.value(QStringLiteral(
							  "error_subcode"))
					      .toInt(0);
			if (code == 0)
				code = sub;
			else if (code == 100 && sub == 33)
				code = 33;
			const QString spec =
				meta::fbEligibilityMessage(code);
			generalMsg_->setText(
				!spec.isEmpty()
					? spec
					: meta::userMessage(
						  meta::classifyFb(http, code),
						  P::Facebook));
			obs_log(LOG_WARNING, "%s http %d", label, http);
			return;
		}
		{
			const QString liveId = currentFbLiveId();
			if (op == Op::FbGoLive && !liveId.isEmpty()) {
				// F-085: refresco encadenado (una lectura,
				// sin polling): el indicador queda real sin
				// Check manual; el mensaje del ON se conserva.
				setFbState(QStringLiteral("LIVE"));
				fbQuietStatus_ = true;
				startFbStatusIndependent(liveId);
			} else if (op == Op::FbEnd && !liveId.isEmpty()) {
				setFbState(QStringLiteral("VOD"));
				fbQuietStatus_ = true;
				startFbStatusIndependent(liveId);
			}
		}
		if (op == Op::FbDelete) {
			const QString liveId = currentFbLiveId();
			for (int i = fbLiveCombo_->count() - 1; i >= 0;
			     --i) {
				if (fbLiveCombo_->itemData(i).toString() ==
					    liveId ||
				    fbLiveCombo_->itemText(i).contains(
					    liveId))
					fbLiveCombo_->removeItem(i);
			}
			if (store_) {
				secure::Data d;
				store_->load(d);
				if (d.facebookLiveId == liveId) {
					d.facebookLiveId.clear();
					store_->save(d);
				}
			}
			setFbState(QString());
			generalMsg_->setText(
				tr("Facebook live video deleted."));
		} else if (op == Op::FbGoLive) {
			setFbState(QStringLiteral("LIVE"));
			generalMsg_->setText(tr("Facebook is LIVE."));
		} else {
			setFbState(QStringLiteral("VOD"));
			generalMsg_->setText(tr("Facebook live ended."));
		}
		obs_log(LOG_INFO, "%s ok", label);
		return;
	}

	case Op::UpFb:
	case Op::UpFbRetry: {
		const P p = P::Facebook;
		const QJsonObject err = replyJson(reply)
						    .value(QStringLiteral(
							    "error"))
						    .toObject();
		int code =
			err.value(QStringLiteral("code")).toInt(0);
		const int sub =
			err.value(QStringLiteral("error_subcode"))
				.toInt(0);
		if (code == 0)
			code = sub;
		else if (code == 100 && sub == 33)
			code = 33; // F-080: objeto web/ID desconocido
		const meta::Outcome oc = netFail
						 ? meta::Outcome::NetworkError
						 : meta::classifyFb(http, code);
		// FB sin refresh_token clasico (D8): 401/190 exige
		// reconexion; jamas reintento de refresh ni loop.
		if (scheduleBackoff(p, oc, http))
			return;
		if (oc == meta::Outcome::AuthRequired) {
			fb_.connected = false;
			setStatus(p, tr("Needs reconnection."));
		}
		const QString spec =
			!netFail ? meta::fbEligibilityMessage(code)
				 : QString();
		finishPlatform(p, oc == meta::Outcome::Success,
			       !spec.isEmpty()
				       ? spec
				       : meta::userMessage(oc, p));
		startApplyNext();
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
	case Op::RevKk:
	case Op::RevFb: {
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
