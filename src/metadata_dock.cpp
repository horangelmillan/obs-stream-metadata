/*
T-031: MVP dock. Qt Widgets + QNetworkAccessManager (async, no extra libs).
T-032: app credentials typed into the dock (BYO-app, ADR-008); tokens and
secrets in memory + DPAPI-encrypted store, never plaintext/printed/logged.
*/

#include "metadata_dock.h"

#include "relay_manager.h"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>
#include <util/bmem.h>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFileSystemWatcher>
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
#include <QSettings>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>

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
	// f6: chrome global (fuera del QStackedWidget) para poder salir de
	// ENTRAR a Independent sin sesión.
	modeLabel_ = new QLabel(tr("Connection mode"), this);
	modeCombo_ = new QComboBox(this);
	modeCombo_->addItem(QString::fromLatin1(meta::connectionModeName(
		meta::ConnectionMode::Independent)));
	modeCombo_->addItem(QString::fromLatin1(meta::connectionModeName(
		meta::ConnectionMode::Managed)));
	connect(modeCombo_,
		static_cast<void (QComboBox::*)(int)>(
			&QComboBox::currentIndexChanged),
		this, &MetadataDock::onModeChanged);
	top->addWidget(modeLabel_);
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
		tr("Clave secreta (opcional)"));
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

	// Fase 4: cuenta de usuario Managed (v1: username+password, sin
	// email). Solo memoria (jamás Store/DPAPI); visible solo en
	// Managed (refreshModeUi). Sin login, las acciones Managed no
	// hacen red: piden login con el mensaje exacto de
	// meta::managedLoginRequiredMessage().
	// f8 REDISENO ENTRAR (2026-10-26 [D]): dos modos excluyentes con
	// QTabWidget (doc oficial Qt6
	// https://doc.qt.io/qt-6/qtabwidget.html : tab bar + page area,
	// solo la pagina actual visible; la exclusividad la garantiza el
	// propio widget). "Entrar" reusa los campos de login; "Crear
	// cuenta" tiene sus propios campos + aviso visible de la politica
	// (minimo 12, sin email, sin recuperacion). Los botones ya no
	// comparten campos.
	authTabs_ = new QTabWidget(this);
	loginTab_ = new QWidget(authTabs_);
	signupTab_ = new QWidget(authTabs_);
	QVBoxLayout *loginTabLayout = new QVBoxLayout(loginTab_);
	QVBoxLayout *signupTabLayout = new QVBoxLayout(signupTab_);
	userNameEdit_ = new QLineEdit(loginTab_);
	userNameEdit_->setPlaceholderText(tr("Managed username"));
	userPassEdit_ = new QLineEdit(loginTab_);
	userPassEdit_->setPlaceholderText(tr("Managed password"));
	userPassEdit_->setEchoMode(QLineEdit::Password);
	loginNameError_ = new QLabel(loginTab_);
	loginNameError_->setWordWrap(true);
	loginPassError_ = new QLabel(loginTab_);
	loginPassError_->setWordWrap(true);
	userLoginBtn_ = new QPushButton(tr("Log in (Managed)"), loginTab_);
	connect(userLoginBtn_, &QPushButton::clicked, this,
		&MetadataDock::onUserLogin);
	loginTabLayout->addWidget(userNameEdit_);
	loginTabLayout->addWidget(loginNameError_);
	loginTabLayout->addWidget(userPassEdit_);
	loginTabLayout->addWidget(loginPassError_);
	loginTabLayout->addWidget(userLoginBtn_);
	loginTabLayout->addStretch(1);
	signupNameEdit_ = new QLineEdit(signupTab_);
	signupNameEdit_->setPlaceholderText(tr("Choose a username"));
	signupPassEdit_ = new QLineEdit(signupTab_);
	signupPassEdit_->setPlaceholderText(tr("Choose a password"));
	signupPassEdit_->setEchoMode(QLineEdit::Password);
	signupNameError_ = new QLabel(signupTab_);
	signupNameError_->setWordWrap(true);
	signupPassError_ = new QLabel(signupTab_);
	signupPassError_->setWordWrap(true);
	signupPolicy_ = new QLabel(meta::managedPasswordPolicyNotice(),
				   signupTab_);
	signupPolicy_->setWordWrap(true);
	userSignupBtn_ =
		new QPushButton(tr("Create account (Managed)"), signupTab_);
	connect(userSignupBtn_, &QPushButton::clicked, this,
		&MetadataDock::onUserSignup);
	signupTabLayout->addWidget(signupNameEdit_);
	signupTabLayout->addWidget(signupNameError_);
	signupTabLayout->addWidget(signupPassEdit_);
	signupTabLayout->addWidget(signupPassError_);
	signupTabLayout->addWidget(signupPolicy_);
	signupTabLayout->addWidget(userSignupBtn_);
	signupTabLayout->addStretch(1);
	authTabs_->addTab(loginTab_, tr("Log in"));
	authTabs_->addTab(signupTab_, tr("Create account"));
	connect(authTabs_, &QTabWidget::currentChanged, this,
		&MetadataDock::onAuthTabChanged);
	userLogoutBtn_ = new QPushButton(tr("Log out (Managed)"), this);
	connect(userLogoutBtn_, &QPushButton::clicked, this,
		&MetadataDock::onUserLogout);
	userStatus_ = new QLabel(tr("Not logged in"), this);
	userStatus_->setWordWrap(true);
	userNotice_ =
		new QLabel(meta::userNoRecoveryNotice(), this);
	userNotice_->setWordWrap(true);
	top->addWidget(authTabs_);
	top->addWidget(userLogoutBtn_);
	top->addWidget(userStatus_);
	top->addWidget(userNotice_);

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
	// T-076: explicar para qué es el selector (solo título/descripción,
	// pero la API exige un video). Sin tecnicismos en la pregunta del
	// usuario: elegir a qué video se le cambia el título.
	bcLabel->setToolTip(
		tr("Video al que se le cambia el título y la descripción. "
		   "Se elige solo el activo o el próximo más cercano; cámbialo "
		   "si transmites a otro."));
	broadcastCombo_->setToolTip(
		tr("Video al que se le cambia el título y la descripción. "
		   "Se elige solo el activo o el próximo más cercano; cámbialo "
		   "si transmites a otro."));
	refreshButton_ = new QPushButton(tr("Refresh broadcasts"), ytDetail_);
	connect(refreshButton_, &QPushButton::clicked, this,
		&MetadataDock::onRefreshBroadcasts);
	ytDetailLayout->addWidget(bcLabel);
	ytDetailLayout->addWidget(broadcastCombo_);
	ytDetailLayout->addWidget(refreshButton_);

	// T-071 FB-2: selector Page/perfil + live videos propios (D3/D9).
	// Patron broadcast persistente (F-067): jamas el preview web (H1).
	QLabel *fbTargetLabel = new QLabel(tr("Página o perfil"), fbDetail_);
	fbTargetLabel_ = fbTargetLabel;
	fbTargetCombo_ = new QComboBox(fbDetail_);
	fbTargetLabel->setBuddy(fbTargetCombo_);
	connect(fbTargetCombo_,
		static_cast<void (QComboBox::*)(int)>(
			&QComboBox::currentIndexChanged),
		this, &MetadataDock::onFacebookTargetChanged);
	QLabel *fbLiveLabel = new QLabel(tr("Vídeo"), fbDetail_);
	fbLiveLabel_ = fbLiveLabel;
	fbLiveCombo_ = new QComboBox(fbDetail_);
	// F-074: /me/live_videos puede devolver vacio aunque el objeto
	// exista (legible directo por ID). Editable: pegar el ID vale.
	fbLiveCombo_->setEditable(true);
	fbLiveCombo_->setPlaceholderText(tr("Elige o pega el vídeo"));
	fbLiveLabel->setBuddy(fbLiveCombo_);
	fbRefreshButton_ =
		new QPushButton(tr("Actualizar lista"), fbDetail_);
	connect(fbRefreshButton_, &QPushButton::clicked, this,
		&MetadataDock::onRefreshFacebook);
	fbDetailLayout->addWidget(fbTargetLabel);
	fbDetailLayout->addWidget(fbTargetCombo_);
	fbDetailLayout->addWidget(fbLiveLabel);
	fbDetailLayout->addWidget(fbLiveCombo_);
	fbDetailLayout->addWidget(fbRefreshButton_);
	// T-073 FB-4: indicador + ciclo de vida (D6R/D7/F-075). Sin
	// polling auto: todo a peticion (Refresh/Apply/botones).
	fbStateLabel_ = new QLabel(tr("Estado: —"), fbDetail_);
	fbStateLabel_->setWordWrap(true);
	fbDetailLayout->addWidget(fbStateLabel_);
	fbVerifyLabel_ = new QLabel(tr("Comprobación: —"), fbDetail_);
	fbVerifyLabel_->setWordWrap(true);
	fbDetailLayout->addWidget(fbVerifyLabel_);
	fbStatusButton_ =
		new QPushButton(tr("Comprobar"), fbDetail_);
	connect(fbStatusButton_, &QPushButton::clicked, this,
		&MetadataDock::onFbStatus);
	fbAdoptButton_ =
		new QPushButton(tr("Usar el que está al aire"), fbDetail_);
	fbAdoptButton_->setToolTip(
		tr("Usa el directo que ya está al aire en tu perfil. No toca "
		   "tu programa de emisión."));
	connect(fbAdoptButton_, &QPushButton::clicked, this,
		&MetadataDock::onFbAdopt);
	fbCreateButton_ =
		new QPushButton(tr("Crear vídeo"), fbDetail_);
	fbCreateButton_->setToolTip(
		tr("Crea aquí el vídeo de trabajo. No toca tu programa "
		   "de emisión. Cada vídeo trae su propia dirección."));
	connect(fbCreateButton_, &QPushButton::clicked, this,
		&MetadataDock::onFbCreate);
	// T-078 Fase B (vista): opt-in de clave persistente, defecto OFF,
	// cableado a setFbPersistent(). Texto honesto F-092: ni con ENABLE
	// la URL es unica (cada LiveVideo trae su propia ingesta).
	fbPersistentCheck_ = new QCheckBox(
		tr("Pedir clave que no cambie (opcional, apagado)"),
		fbDetail_);
	fbPersistentCheck_->setChecked(false);
	fbPersistentCheck_->setToolTip(
		tr("Solo para este vídeo. Cada vídeo trae su propia "
		   "dirección; ni así se promete que sea siempre la misma. "
		   "Si falla, reintenta apagado."));
	fbPersistentCheck_->setAccessibleName(
		tr("Pedir clave que no cambie para el vídeo creado"));
	connect(fbPersistentCheck_, &QCheckBox::toggled, this,
		[this](bool on) { setFbPersistent(on); });
	setFbPersistent(false);
	fbGoLiveButton_ = new QPushButton(tr("Salir al aire"), fbDetail_);
	fbGoLiveButton_->setToolTip(
		tr("Sale al aire con el vídeo de trabajo. Te pide confirmar."));
	connect(fbGoLiveButton_, &QPushButton::clicked, this,
		&MetadataDock::onFbGoLive);
	fbEndButton_ = new QPushButton(tr("Terminar"), fbDetail_);
	fbEndButton_->setToolTip(
		tr("Termina el directo. El vídeo queda guardado salvo que lo "
		   "borres."));
	connect(fbEndButton_, &QPushButton::clicked, this,
		&MetadataDock::onFbEndLive);
	fbDeleteButton_ =
		new QPushButton(tr("Borrar vídeo"), fbDetail_);
	fbDeleteButton_->setToolTip(
		tr("Borra el vídeo de trabajo. También borra su grabación."));
	connect(fbDeleteButton_, &QPushButton::clicked, this,
		&MetadataDock::onFbDelete);
	fbDetailLayout->addWidget(fbStatusButton_);
	fbDetailLayout->addWidget(fbAdoptButton_);
	fbDetailLayout->addWidget(fbCreateButton_);
	fbDetailLayout->addWidget(fbPersistentCheck_);
	fbDetailLayout->addWidget(fbGoLiveButton_);
	fbDetailLayout->addWidget(fbEndButton_);
	fbDetailLayout->addWidget(fbDeleteButton_);
	// PoC ingestión: valor completo del create, oculto por defecto
	// (Password + oculto hasta haber valor). Solo sesión, sin
	// persistencia; el operador lo pega a mano en Aitum (§23/§51).
	fbIngestLabel_ = new QLabel(
		tr("Dirección del vídeo (solo esta sesión — pégala en tu programa a mano)"),
		fbDetail_);
	fbIngestLabel_->setWordWrap(true);
	fbIngestEdit_ = new QLineEdit(fbDetail_);
	fbIngestEdit_->setReadOnly(true);
	fbIngestEdit_->setEchoMode(QLineEdit::Password);
	fbIngestEdit_->setPlaceholderText(
		tr("Aparece al crear el vídeo"));
	fbIngestShowButton_ = new QPushButton(tr("Mostrar"), fbDetail_);
	connect(fbIngestShowButton_, &QPushButton::clicked, this,
		&MetadataDock::onFbIngestToggled);
	fbIngestCopyButton_ = new QPushButton(tr("Copiar"), fbDetail_);
	connect(fbIngestCopyButton_, &QPushButton::clicked, this,
		&MetadataDock::onFbIngestCopy);
	fbDetailLayout->addWidget(fbIngestLabel_);
	fbDetailLayout->addWidget(fbIngestEdit_);
	fbDetailLayout->addWidget(fbIngestShowButton_);
	fbDetailLayout->addWidget(fbIngestCopyButton_);
	fbIngestLabel_->setVisible(false);
	fbIngestEdit_->setVisible(false);
	fbIngestShowButton_->setVisible(false);
	fbIngestCopyButton_->setVisible(false);

	// T-078 Fase 4 (vista): seccion relay local MediaMTX (ADR-016)
	// dentro de la tarjeta Facebook. Solo vista: URL fija copiable,
	// Iniciar/Detener, indicador por estados y mensajes honestos del
	// gestor tal cual (sin reescribir logica, sin prometer
	// persistencia/latencia/CPU/recovery). Visible solo con sesion
	// FB (refreshContentVisibility); jamas pisa fbState_/fbVerify_.
	relaySectionLabel_ = new QLabel(tr("Repetidor local"),
					fbDetail_);
	relayFixedEdit_ = new QLineEdit(fbDetail_);
	relayFixedEdit_->setReadOnly(true);
	relayFixedEdit_->setText(relay::fixedDestination());
	relayFixedEdit_->setToolTip(
		tr("Dirección fija: apúntala una vez en tu programa, a mano. "
		   "El plugin no la cambia."));
	relayFixedEdit_->setAccessibleName(
		tr("Dirección fija del repetidor"));
	relayFixedCopy_ =
		new QPushButton(tr("Copiar dirección fija"), fbDetail_);
	relayFixedCopy_->setToolTip(
		tr("Copia la dirección fija"));
	connect(relayFixedCopy_, &QPushButton::clicked, this,
		&MetadataDock::onRelayFixedCopy);
	relayStateLabel_ = new QLabel(fbDetail_);
	relayStateLabel_->setWordWrap(true);
	relayStateLabel_->setAccessibleName(tr("Estado del repetidor"));
	relayToggleButton_ =
		new QPushButton(tr("Iniciar"), fbDetail_);
	relayToggleButton_->setToolTip(
		tr("Inicia o detiene el repetidor local. Al crear el vídeo se "
		   "dirige solo; detener no toca tu programa."));
	connect(relayToggleButton_, &QPushButton::clicked, this,
		&MetadataDock::onRelayToggle);
	relayMsgLabel_ = new QLabel(fbDetail_);
	relayMsgLabel_->setWordWrap(true);
	relayMsgLabel_->setAccessibleName(tr("Mensaje del repetidor"));
	relayVodLabel_ =
		new QLabel(relay::vodNoticeMessage(), fbDetail_);
	relayVodLabel_->setWordWrap(true);
	fbDetailLayout->addWidget(relaySectionLabel_);
	fbDetailLayout->addWidget(relayFixedEdit_);
	fbDetailLayout->addWidget(relayFixedCopy_);
	fbDetailLayout->addWidget(relayStateLabel_);
	fbDetailLayout->addWidget(relayToggleButton_);
	fbDetailLayout->addWidget(relayMsgLabel_);
	fbDetailLayout->addWidget(relayVodLabel_);
	updateRelayUi();
	// T-078 Fase B (vista simplificada): camino feliz arriba +
	// Avanzado colapsado (reordena widgets existentes, sin logica).
	buildFbSimplifiedView();

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

	// Fase 5b: borrado de CUENTA Managed (/users/erase en cascada).
	// Activo: exige login (gate Fase 4, sin red sin sesión), usa auth
	// doble (apiPostUser) y limpia sesión/snapshots locales tras el 200.
	// Operación distinta de POST /privacy/erase (F-C2, por instalación).
	eraseAccountButton_ =
		new QPushButton(tr("Borrar cuenta (Managed)"), this);
	eraseAccountButton_->setToolTip(
		tr("Deletes your Managed user account with ALL its "
		   "installations from the backend (/users/erase). Distinct "
		   "from data erasure (F-C2). Independent accounts are "
		   "untouched."));
	connect(eraseAccountButton_, &QPushButton::clicked, this,
		&MetadataDock::onEraseAccount);
	top->addWidget(eraseAccountButton_);

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
	// f6 ENTRAR-primero: dos pantallas excluyentes en Managed con
	// QStackedWidget (doc oficial Qt6
	// https://doc.qt.io/qt-6/qstackedwidget.html : addWidget +
	// setCurrentWidget; la exclusividad la garantiza el propio widget,
	// no flags manuales). ENTRAR (indice 0) solo el formulario de
	// cuenta; PANEL (indice 1) el dock completo con el scroll
	// existente. Independent queda fijado en PANEL (una sola pantalla,
	// sin cambios visibles). El selector de modo queda como chrome
	// global para poder salir de ENTRAR a Independent sin sesión.
	managedStack_ = new QStackedWidget(this);
	loginPage_ = new QWidget(managedStack_);
	QVBoxLayout *loginLayout = new QVBoxLayout(loginPage_);
	QWidget *enterWidgets[] = {
		authTabs_,
		userStatus_, userNotice_,
	};
	for (QWidget *w : enterWidgets) {
		top->removeWidget(w);
		loginLayout->addWidget(w);
	}
	loginLayout->addStretch(1);
	managedStack_->addWidget(loginPage_); // 0 = ENTRAR
	panelPage_ = new QWidget(managedStack_);
	QVBoxLayout *panelLayout = new QVBoxLayout(panelPage_);
	panelLayout->setContentsMargins(0, 0, 0, 0);
	panelSession_ = new QLabel(panelPage_);
	panelSession_->setWordWrap(true);
	panelSession_->setVisible(false); // solo Managed con sesión
	panelLayout->addWidget(panelSession_);
	top->removeWidget(userLogoutBtn_);
	panelLayout->addWidget(userLogoutBtn_);
	QScrollArea *scroll = new QScrollArea(panelPage_);
	scroll->setWidget(body);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	panelLayout->addWidget(scroll);
	managedStack_->addWidget(panelPage_); // 1 = PANEL
	QVBoxLayout *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	// Chrome global: selector de modo + nota Managed (salida de ENTRAR
	// sin sesión; el modo debe poder cambiarse sin estar logueado).
	top->removeWidget(modeLabel_);
	outer->addWidget(modeLabel_);
	top->removeWidget(modeCombo_);
	outer->addWidget(modeCombo_);
	top->removeWidget(managedNote_);
	outer->addWidget(managedNote_);
	outer->addWidget(managedStack_);
	setLayout(outer);

	net_ = new QNetworkAccessManager(this);
	connect(net_, &QNetworkAccessManager::finished, this,
		&MetadataDock::onReply);

	// T-078 Fase 3 (cableado): gestor async del relay local (ADR-016).
	// QProcess + Control API con senales, sin bloquear la UI; el
	// indicador viaja por estados (la vista redacta despues).
	relay_ = new RelayManager(this);
	connect(relay_, &RelayManager::stateChanged, this,
		&MetadataDock::onRelayStateChanged);
	connect(relay_, &RelayManager::operatorMessage, this,
		&MetadataDock::onRelayOperatorMessage);

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
	// Plan v3 Fase 1: arranque exacto SIN temporizadores de reintento.
	// Nada se auto-inicia aqui: el unico punto de arranque es
	// fbAutoStartFromConnected(), llamado SOLO desde
	// finishManagedConnected(p == Facebook) y desde la rama
	// "connected" del lambda de silentRevalidateManaged (post-GET
	// fresco). La escucha del repetidor (watcher + respaldo 3 s) se
	// arma aqui sin red (solo observa el dir de senales local).
	ensureFbSignalWatch();
}

MetadataDock::~MetadataDock()
{
	// T-078 Fase 3: parada limpia al cerrar (sin huérfanos). La
	// memoria de sesión se olvida sin persistir ni loguear.
	if (relay_)
		relay_->stop();
	// Plan v3 Fase 1: la escucha diferida y la cola refire mueren con
	// el dock (sin red en el destructor; solo se paran timers).
	fbCancelRefire();
	if (fbSigBackup_)
		fbSigBackup_->stop();
	if (fbSigFast_)
		fbSigFast_->stop();
	if (fbCooldownTick_)
		fbCooldownTick_->stop();
	fbIngest_.clear();
	fbRefire_.dest.clear();
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
	if (eraseAccountButton_)
		eraseAccountButton_->setVisible(managed);
	// Fase 4: formulario de cuenta (solo Managed; Independent no lo
	// ve ni lo necesita, ADR-008/012).
	// f8: las pestanas Entrar|Crear cuenta viajan juntas (authTabs_);
	// los campos/botones viven dentro de su pestana y jamas se
	// ocultan por separado.
	const bool showLogin = managed;
	if (authTabs_)
		authTabs_->setVisible(showLogin);
	if (userLogoutBtn_)
		userLogoutBtn_->setVisible(showLogin);
	if (userStatus_)
		userStatus_->setVisible(showLogin);
	if (userNotice_)
		userNotice_->setVisible(showLogin);
	refreshManagedLoginUi();
	repaintModeStatuses();
	updateAllCardStyles();
	refreshContentVisibility();
	refreshManagedScreen(); // f6: Managed sin sesión => ENTRAR
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
	if (fbVerifyLabel_)
		fbVerifyLabel_->setVisible(fb);
	if (fbStatusButton_)
		fbStatusButton_->setVisible(fb);
	if (fbAdoptButton_)
		fbAdoptButton_->setVisible(fb);
	if (fbCreateButton_)
		fbCreateButton_->setVisible(fb);
	// T-078 Fase B (vista): el opt-in vive junto al create (misma
	// visibilidad: solo con sesion).
	if (fbPersistentCheck_)
		fbPersistentCheck_->setVisible(fb);
	if (fbGoLiveButton_)
		fbGoLiveButton_->setVisible(fb);
	if (fbEndButton_)
		fbEndButton_->setVisible(fb);
	if (fbDeleteButton_)
		fbDeleteButton_->setVisible(fb);
	// PoC ingestión: solo con sesión Y valor en memoria (oculta por
	// defecto; jamas en disco ni logs).
	const bool showIngest = fb && !fbIngest_.isEmpty();
	if (fbIngestLabel_)
		fbIngestLabel_->setVisible(showIngest);
	if (fbIngestEdit_)
		fbIngestEdit_->setVisible(showIngest);
	if (fbIngestShowButton_)
		fbIngestShowButton_->setVisible(showIngest);
	if (fbIngestCopyButton_)
		fbIngestCopyButton_->setVisible(showIngest);
	// T-078 Fase 4 (vista): seccion relay solo con sesion FB (el
	// forward apunta a la ingesta del LiveVideo vigente). El mensaje
	// del gestor vive en su propia etiqueta (jamas pisa
	// fbState_/fbVerify_/generalMsg_ ni resultados por plataforma).
	if (relaySectionLabel_)
		relaySectionLabel_->setVisible(fb);
	if (relayFixedEdit_)
		relayFixedEdit_->setVisible(fb);
	if (relayFixedCopy_)
		relayFixedCopy_->setVisible(fb);
	if (relayStateLabel_)
		relayStateLabel_->setVisible(fb);
	if (relayToggleButton_)
		relayToggleButton_->setVisible(fb);
	if (relayVodLabel_)
		relayVodLabel_->setVisible(fb);
	if (relayMsgLabel_)
		relayMsgLabel_->setVisible(
			fb && !relayMsgLabel_->text().isEmpty());
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
	// T-078 Fase B: refinamiento camino-feliz/Avanzado (ver visibilidad
	// condicional en updateFbHappyPath; la base de sesion ya quedo
	// arriba y el contenedor colapsado manda sobre los hijos).
	updateFbHappyPath();
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
	// Bugfix visual 2026-10-26 [D]: el tono sale del predicado puro
	// meta::cardToneFor (unica fuente; testeado en selfcheck).
	// Neutral = gris (limpio/logout), Brand = color (conectada),
	// Error = rojo (solo tras accion fallida o ruptura).
	bool connected = false;
	if (isManaged())
		connected = managedAccount(p).connected;
	else
		connected = account(p).connected;
	const QString st = status->text();
	const QString rs = result->text();
	const meta::CardTone tone =
		meta::cardToneFor(connected, st, rs);
	const bool failed = (tone == meta::CardTone::Error);
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
	// PoC ingestión: el valor pertenece al contexto anterior; al
	// cambiar de modo se olvida (sesión, sin persistencia).
	clearFbIngest();
	// Fase A: el ciclo automatico (arranque/Apply/End) pertenece al
	// contexto anterior: se cierra sin red adicional. El presupuesto
	// de arranque no se reutiliza en el modo nuevo.
	fbAutoActive_ = false;
	fbAutoArmed_ = false;
	fbAutoDidCreate_ = false;
	fbAutoDidDelete_ = false;
	fbAutoDeleteId_.clear();
	fbAutoPrevId_.clear();
	fbAutoPrevStatus_.clear();
	fbAutoPrevTitle_.clear();
	fbAutoPrevDesc_.clear();
	fbApplyChain_ = false;
	fbApplyChainId_.clear();
	fbApplyChainMsg_.clear();
	fbEndAutoPending_ = false;
	fbQuietStatus_ = false;
	// Plan v3 Fase 1: la escucha diferida, la sesion GoLive propia y la
	// cola refire pertenecen al contexto anterior: se cancelan sin red.
	fbSignalCancelPending();
	clearFbGoLiveSession();
	fbCancelRefire();
	// Plan v3 (vista): el contador es solo-aritmética de sesión;
	// al cambiar de modo se detiene y se oculta sin afirmar nada.
	if (fbCooldownTick_)
		fbCooldownTick_->stop();
	if (fbCooldownLabel_)
		fbCooldownLabel_->setVisible(false);
	fbCooldownCache_.clear();
	// T-078 Fase 3: el forward apunta a la ingesta del contexto
	// anterior; parada limpia del relay en el cambio de modo.
	if (relay_)
		relay_->stop();
	// La verificacion tambien pertenece al contexto anterior.
	clearFbVerify();
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
	case Op::FbStatusRetry:
	case Op::FbCreate:
	case Op::FbGoLive:
	case Op::FbEnd:
	case Op::FbDelete:
	case Op::FbAdopt:
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

// --- relay local MediaMTX (T-078 Fase 3, ADR-016) --------------------------
// Indicador por estados (la vista redacta despues): aqui solo se guarda
// el estado, se registra su nombre en el log y se reemite. Los mensajes
// honestos del gestor (relay.h) se muestran tal cual, sin textos nuevos.
// La ingesta jamas se persiste ni se loguea; Update/Apply no tocan el
// forward (solo metadatos).

// --- relay local MediaMTX (T-078 Fase 3, ADR-016) --------------------------
// Fase 3 guarda el estado y lo reemite; Fase 4 (vista) pinta el
// indicador y muestra los mensajes honestos del gestor tal cual en
// la seccion relay (jamas fbState_/fbVerify_/generalMsg_). Sin
// prometer persistencia, latencia, CPU ni recovery.

void MetadataDock::onRelayStateChanged(relay::State s)
{
	relayState_ = s;
	obs_log(LOG_INFO, "relay indicator: %s", relay::stateName(s));
	updateRelayUi();
	emit relayStateChanged(s);
}

void MetadataDock::onRelayOperatorMessage(const QString &msg)
{
	// Mensaje honesto de relay.h (sin secretos ni ingestas por
	// construccion): solo a la etiqueta propia del relay. No pisa
	// verify/audiencia (fbVerify_) ni estado del LiveVideo
	// (fbState_) ni el aviso general ni resultados por plataforma.
	if (relayMsgLabel_) {
		relayMsgLabel_->setText(msg);
		refreshContentVisibility();
	}
}

void MetadataDock::onRelayToggle()
{
	if (!relay_)
		return;
	// Solo vista: arranca o para el proceso hijo (API existente del
	// gestor). El re-fijado del forward lo hace Fase 3 al crear el
	// LiveVideo; aqui no se toca ingesta ni forward directamente.
	if (relay::stateIsRunning(relayState_))
		relay_->stop();
	else
		relay_->ensureStarted();
}

void MetadataDock::onRelayFixedCopy()
{
	if (QClipboard *cb = QApplication::clipboard())
		cb->setText(relay::fixedDestination());
	// Confirmacion generica: el destino fijo es publico (no secreto),
	// pero no se repite su valor en el aviso general.
	if (relayMsgLabel_) {
		relayMsgLabel_->setText(
				tr("Dirección copiada — pégala una vez en tu programa."));
		refreshContentVisibility();
	}
}

void MetadataDock::updateRelayUi()
{
	if (relayFixedEdit_ &&
	    relayFixedEdit_->text() != relay::fixedDestination())
		relayFixedEdit_->setText(relay::fixedDestination());
	// Indicador por estados (vista redacta el texto corto; el detalle
	// honesto queda en relayMsgLabel_ via operatorMessage).
	QString label;
	QString bg;
	QString border;
	switch (relayState_) {
	case relay::State::Starting:
		label = tr("Arrancando…");
		bg = QStringLiteral("rgba(255, 193, 7, 30)");
		border = QStringLiteral("#ffc107");
		break;
	case relay::State::Running:
		label = tr("En marcha (aún sin dirigir)");
		bg = QStringLiteral("rgba(45, 158, 68, 30)");
		border = QStringLiteral("#2da44e");
		break;
	case relay::State::ForwardOk:
		label = tr("En marcha — dirigiendo bien");
		bg = QStringLiteral("rgba(24, 119, 242, 30)");
		border = QStringLiteral("#1877F2");
		break;
	case relay::State::ForwardFailed:
		label = tr("No se pudo dirigir — mira el mensaje");
		bg = QStringLiteral("rgba(229, 72, 77, 30)");
		border = QStringLiteral("#e5484d");
		break;
	case relay::State::BinaryMissing:
		label = tr("Falta el programa — reinstala");
		bg = QStringLiteral("rgba(229, 72, 77, 30)");
		border = QStringLiteral("#e5484d");
		break;
	case relay::State::PortBusy:
		label = tr("Paso ocupado — cierra el otro programa");
		bg = QStringLiteral("rgba(229, 72, 77, 30)");
		border = QStringLiteral("#e5484d");
		break;
	case relay::State::Stopped:
	default:
		label = tr("Parado");
		bg = QStringLiteral("transparent");
		border = QStringLiteral("#5a5a5a");
		break;
	}
	if (relayStateLabel_) {
		relayStateLabel_->setText(label);
		relayStateLabel_->setStyleSheet(
			QStringLiteral("QLabel { border: 1px solid %1; "
				       "border-radius: 6px; padding: 6px; "
				       "background: %2; }")
				.arg(border, bg));
	}
	if (relayToggleButton_) {
		relayToggleButton_->setText(
			relay::stateIsRunning(relayState_)
			? tr("Detener")
			: tr("Iniciar"));
	}
	updateFbHappyPath();
}

// --- T-078 Fase B: vista simplificada Facebook ---------------------------
// Camino feliz arriba (estado/verificacion/senal + Create/GoLive +
// relay toggle/indicador); Avanzado colapsado con Page/target, IDs,
// Refresh, Adopt, Check manual, ingesta, End/Delete, clave persistente
// y relay fijo. Solo reordena widgets existentes y decide visibilidad
// por estados Fase A (verify/relay/senal/fbLastStatus_); cero auth,
// cero red, cero secretos (requestRelaySignal ya existe y solo lee
// localhost a demanda, sin polling).

void MetadataDock::buildFbSimplifiedView()
{
	if (!fbDetail_ || !fbLiveCombo_)
		return;
	QVBoxLayout *detail =
		qobject_cast<QVBoxLayout *>(fbDetail_->layout());
	if (!detail)
		return;
	// 1. Linea compuesta global, literalmente encima de titulo/Aplicar.
	fbSummary_ = new QLabel(this);
	fbSummary_->setWordWrap(true);
	fbSummary_->setAccessibleName(tr("Estado de Facebook"));
	fbSummary_->setVisible(false);
	if (titleLabel_ && titleLabel_->parentWidget() &&
	    titleLabel_->parentWidget()->layout()) {
		if (QVBoxLayout *top = qobject_cast<QVBoxLayout *>(
			    titleLabel_->parentWidget()->layout())) {
			top->insertWidget(top->indexOf(titleLabel_),
					  fbSummary_);
		}
	}
	// 2. Senal del emisor (texto+color, nunca solo color) tras Verify.
	fbSignalLabel_ = new QLabel(fbDetail_);
	fbSignalLabel_->setWordWrap(true);
	fbSignalLabel_->setAccessibleName(tr("Señal"));
	fbSignalCheck_ = new QPushButton(tr("Comprobar señal"),
					 fbDetail_);
	fbSignalCheck_->setToolTip(
		tr("Mira una vez si llega señal. Solo con el repetidor en marcha."));
	fbSignalCheck_->setAccessibleName(tr("Comprobar señal"));
	connect(fbSignalCheck_, &QPushButton::clicked, this,
		&MetadataDock::onFbSignalCheck);
	detail->insertWidget(detail->indexOf(fbVerifyLabel_) + 1,
			     fbSignalLabel_);
	detail->insertWidget(detail->indexOf(fbSignalLabel_) + 1,
			     fbSignalCheck_);
	// 3. Contenedor Avanzado (colapsado por defecto).
	fbAdvancedToggle_ = new QPushButton(tr("Avanzado >"), fbDetail_);
	fbAdvancedToggle_->setToolTip(
		tr("Muestra los controles extra (lista, comprobar a mano, "
		   "dirección, terminar, borrar)"));
	fbAdvancedToggle_->setAccessibleName(
		tr("Muestra u oculta los controles extra de Facebook"));
	connect(fbAdvancedToggle_, &QPushButton::clicked, this,
		&MetadataDock::toggleFbAdvanced);
	fbAdvanced_ = new QWidget(fbDetail_);
	QVBoxLayout *adv = new QVBoxLayout(fbAdvanced_);
	adv->setContentsMargins(0, 4, 0, 0);
	// Mueve (sin recrear: conexiones intactas) lo no-camino-feliz.
	QWidget *moved[] = {
		fbTargetLabel_,	  fbTargetCombo_,	  fbLiveLabel_,
		fbLiveCombo_,	  fbRefreshButton_,	  fbAdoptButton_,
		fbStatusButton_,  fbPersistentCheck_, fbEndButton_,
		fbDeleteButton_,  fbIngestLabel_,	  fbIngestEdit_,
		fbIngestShowButton_, fbIngestCopyButton_,
		relayFixedEdit_,  relayFixedCopy_,
	};
	for (QWidget *w : moved) {
		if (!w)
			continue;
		detail->removeWidget(w);
		adv->addWidget(w);
	}
	detail->addWidget(fbAdvancedToggle_);
	detail->addWidget(fbAdvanced_);
	fbAdvanced_->setVisible(false);
	// El ID manda en Create/GoLive/End/Delete: re-evaluar al cambiar.
	connect(fbLiveCombo_,
		static_cast<void (QComboBox::*)(int)>(
			&QComboBox::currentIndexChanged),
		this, [this]() { updateFbHappyPath(); });
	connect(fbLiveCombo_, &QComboBox::editTextChanged, this,
		[this]() { updateFbHappyPath(); });
	// Plan v3 (vista): check global no-volver-a-mostrar del aviso de
	// audiencia en Avanzado, junto al de persistente (defecto OFF) +
	// detalle de riesgo F-093 en lenguaje de persona. Persistente
	// plaintext (QSettings); lo lee la logica via
	// fbAudienceNoticeHidden(). Sin secretos, sin red.
	fbAudienceHideCheck_ = new QCheckBox(
		tr("No volver a mostrar este aviso (solo este aviso)"),
		fbAdvanced_);
	fbAudienceHideCheck_->setChecked(false);
	fbAudienceHideCheck_->setToolTip(
		tr("Solo deja de mostrar el aviso de público no confirmado."));
	fbAudienceHideCheck_->setAccessibleName(
		tr("No volver a mostrar el aviso de público"));
	connect(fbAudienceHideCheck_, &QCheckBox::toggled, this,
		[this](bool on) { setFbAudienceNoticeHidden(on); });
	fbAudienceRisk_ = new QLabel(
		tr("Ojo: Facebook a veces muestra el directo como «Solo yo» "
		   "aunque lo publiques. Revisa en Facebook quién puede verlo."),
		fbAdvanced_);
	fbAudienceRisk_->setWordWrap(true);
	{
		const int pi = adv->indexOf(fbPersistentCheck_);
		if (pi >= 0) {
			adv->insertWidget(pi + 1, fbAudienceHideCheck_);
			adv->insertWidget(pi + 2, fbAudienceRisk_);
		} else {
			adv->addWidget(fbAudienceHideCheck_);
			adv->addWidget(fbAudienceRisk_);
		}
	}
	{
		const QSettings st(QStringLiteral("obs-stream-metadata"),
			   QStringLiteral("dock"));
		setFbAudienceNoticeHidden(st.value(
			QStringLiteral("facebook/audienceNoticeHidden"),
			false).toBool());
	}
	buildFbHeaderBadges();
	updateFbHappyPath();
}

void MetadataDock::updateFbHappyPath()
{
	const bool any = anyUsable();
	const bool fb = platformUsable(meta::Platform::Facebook);
	const QString liveId = fb ? currentFbLiveId() : QString();
	const bool hasId = !liveId.isEmpty();
	const meta::FbLiveState st = meta::fbLiveState(fbLastStatus_);
	const bool verified =
		fbVerify_.outcome == meta::FbVerify::Verified;
	const bool audPending = fbVerify_.outcome ==
				meta::FbVerify::AudienceUnverified;
	const bool verifying = fbQuietStatus_;
	const bool running = relay::stateIsRunning(relayState_);
	const bool relayErr = relay::stateIsError(relayState_);
	const bool fwdOk = (relayState_ == relay::State::ForwardOk);

	// 1. Resumen compuesto global (texto siempre; color solo refuerzo).
	// Trio operador 2026-10-26 [D]: "Listo" exige emisor (predicado
	// unico meta::fbEmitReadiness, compartido con el semaforo).
	// Verde solo con verify + forward + senal; sin emisor pero resto
	// ok: ambar "lista pero sin senal", jamas verde.
	QString ready;
	QString rBorder = QStringLiteral("#5a5a5a");
	QString rBg = QStringLiteral("transparent");
	const meta::FbEmitReadiness emitReady = meta::fbEmitReadiness(
		(verified || audPending), fwdOk,
		relaySignalHasEmitter());
	if (fb) {
		if (st == meta::FbLiveState::Live) {
			ready = tr("Estás al aire — transmitiendo ahora");
			rBorder = QStringLiteral("#e5484d");
			rBg = QStringLiteral("rgba(229, 72, 77, 30)");
		} else if (st == meta::FbLiveState::Ended) {
			ready = tr("Tu directo terminó. El vídeo queda guardado salvo "
			   "que lo borres.");
		} else if (verifying) {
			ready = tr("Preparo el de trabajo — no tienes que hacer nada");
			rBorder = QStringLiteral("#ffc107");
			rBg = QStringLiteral("rgba(255, 193, 7, 30)");
		} else if (!hasId) {
			ready = tr("Conectado. Aún no hay vídeo — pulsa Crear.");
		} else if (emitReady == meta::FbEmitReadiness::Ready) {
			ready = audPending
				? tr("Listo para emitir. No pude confirmar quién podrá verlo.")
				: tr("Listo para emitir.");
			rBorder = QStringLiteral("#2da44e");
			rBg = QStringLiteral("rgba(45, 158, 68, 30)");
		} else if (emitReady ==
			   meta::FbEmitReadiness::ReadyNoSignal) {
			ready = tr("Lista pero sin señal — apunta tu programa a la "
				   "dirección fija (cópiala en Avanzado).");
			rBorder = QStringLiteral("#ffc107");
			rBg = QStringLiteral("rgba(255, 193, 7, 30)");
		} else if (verified || audPending) {
			ready = tr("Tu vídeo está listo — pulsa Iniciar.");
			rBorder = QStringLiteral("#ffc107");
			rBg = QStringLiteral("rgba(255, 193, 7, 30)");
		} else {
			// Plan v3: solo 3 errores, cada uno con su único botón.
			// Error 1 (título): el único botón es Aplicar.
			if (fbVerify_.reason == QStringLiteral("mismatch")) {
				ready = tr("Revisa el título y pulsa Aplicar.");
			} else {
				// Error 3 (conexión): el único botón es Comprobar
				// (Avanzado). Sin motivos crudos en el resumen.
				ready = tr("Se cortó la conexión — abre Avanzado y "
				   "pulsa Comprobar.");
			}
			rBorder = QStringLiteral("#ffc107");
			rBg = QStringLiteral("rgba(255, 193, 7, 30)");
		}
	}
	// Error 2 (permiso/cuenta): sin sesión el único botón es Conectar
	// (visible por refreshContentVisibility); el resumen lo nombra.
	if (!fb && any)
		ready = tr("Facebook sin conexión — pulsa Conectar.");
	if (fbSummary_) {
		fbSummary_->setText(ready);
		fbSummary_->setStyleSheet(
			QStringLiteral("QLabel { border: 1px solid %1; "
				       "border-radius: 6px; padding: 6px; "
				       "background: %2; }")
				.arg(rBorder, rBg));
		fbSummary_->setVisible(fb || any);
	}
	// 2. Senal del emisor (texto+color, nunca solo color).
	QString sig;
	QString sBorder = QStringLiteral("#5a5a5a");
	QString sBg = QStringLiteral("transparent");
	if (fb) {
		if (!running &&
		    relayState_ == relay::State::Stopped) {
			sig = tr("Sin señal — el repetidor está parado");
		} else if (relayState_ == relay::State::Starting) {
			sig = tr("Arrancando — no tienes que hacer nada");
			sBorder = QStringLiteral("#ffc107");
			sBg = QStringLiteral("rgba(255, 193, 7, 30)");
		} else if (relayErr) {
			sig = tr("Problema con el repetidor — mira el mensaje "
			 "de abajo");
			sBorder = QStringLiteral("#e5484d");
			sBg = QStringLiteral("rgba(229, 72, 77, 30)");
		} else if (!relaySignal_.present) {
			sig = tr("Señal desconocida — pulsa Comprobar señal");
		} else if (relaySignalHasEmitter()) {
			sig = tr("Llega señal — todo bien");
			sBorder = QStringLiteral("#2da44e");
			sBg = QStringLiteral("rgba(45, 158, 68, 30)");
		} else {
			sig = tr("No llega señal — apunta tu programa a la "
			 "dirección fija (cópiala en Avanzado)");
			sBorder = QStringLiteral("#ffc107");
			sBg = QStringLiteral("rgba(255, 193, 7, 30)");
		}
	}
	if (fbSignalLabel_) {
		fbSignalLabel_->setText(sig);
		fbSignalLabel_->setStyleSheet(
			QStringLiteral("QLabel { border: 1px solid %1; "
				       "border-radius: 6px; padding: 6px; "
				       "background: %2; }")
				.arg(sBorder, sBg));
		fbSignalLabel_->setVisible(fb);
	}
	if (fbSignalCheck_)
		fbSignalCheck_->setVisible(fb && running);
	// 3. Visibilidad condicional: ningun boton que falle con
	// "primero haz X". GoLive solo si verify lo permite (el motivo
	// vive en Verify); Create solo sin video valido; End solo en
	// LIVE; Delete/Check con ID; Start/Stop segun estado (texto ya
	// conmuta en updateRelayUi); Advanced colapsado por defecto.
	if (fbGoLiveButton_)
		fbGoLiveButton_->setVisible(
			fb && hasId && (verified || audPending) &&
			st != meta::FbLiveState::Live);
	if (fbCreateButton_)
		fbCreateButton_->setVisible(
			fb && (!hasId || st == meta::FbLiveState::Ended));
	const bool adv = fb && fbAdvancedOpen_;
	if (fbStatusButton_)
		fbStatusButton_->setVisible(adv && hasId);
	if (fbEndButton_)
		fbEndButton_->setVisible(
			adv && hasId && st == meta::FbLiveState::Live);
	if (fbDeleteButton_)
		fbDeleteButton_->setVisible(adv && hasId);
	if (fbAdoptButton_)
		fbAdoptButton_->setVisible(adv);
	if (fbTargetLabel_)
		fbTargetLabel_->setVisible(adv);
	if (fbTargetCombo_)
		fbTargetCombo_->setVisible(adv);
	if (fbLiveLabel_)
		fbLiveLabel_->setVisible(adv);
	if (fbLiveCombo_)
		fbLiveCombo_->setVisible(adv);
	if (fbRefreshButton_)
		fbRefreshButton_->setVisible(adv);
	if (fbPersistentCheck_)
		fbPersistentCheck_->setVisible(adv);
	const bool showIngest = adv && !fbIngest_.isEmpty();
	if (fbIngestLabel_)
		fbIngestLabel_->setVisible(showIngest);
	if (fbIngestEdit_)
		fbIngestEdit_->setVisible(showIngest);
	if (fbIngestShowButton_)
		fbIngestShowButton_->setVisible(showIngest);
	if (fbIngestCopyButton_)
		fbIngestCopyButton_->setVisible(showIngest);
	if (relayFixedEdit_)
		relayFixedEdit_->setVisible(adv);
	if (relayFixedCopy_)
		relayFixedCopy_->setVisible(adv);
	if (fbAdvancedToggle_) {
		fbAdvancedToggle_->setVisible(fb);
		fbAdvancedToggle_->setText(
			fbAdvancedOpen_ ? tr("Avanzado v")
				: tr("Avanzado >"));
	}
	if (fbAdvanced_)
		fbAdvanced_->setVisible(adv);
	updateFbSemaphore();
	ensureCooldownTick();
}

void MetadataDock::toggleFbAdvanced()
{
	fbAdvancedOpen_ = !fbAdvancedOpen_;
	updateFbHappyPath();
}

// --- Plan v3 (vista): semaforo + contador + check audiencia ---------
// Semaforo en cabecera FB: icono sin texto (formas distintas por
// estado) + AccessibleName/tooltip con la frase + cache para no
// repintar. Contador ~mm:ss junto al semaforo: visible solo con
// restante>0, tick 1 s solo-aritmetica, se detiene en 0 sin afirmar
// nada. Check global no-volver-a-mostrar en Avanzado (defecto OFF,
// persistente plaintext en QSettings). Todo solo vista: jamas red,
// jamas secretos, jamas afirma estado del proveedor.

void MetadataDock::buildFbHeaderBadges()
{
	if (!fbCard_ || !fbHeader_ || fbSemaLabel_)
		return; // una sola vez
	QHBoxLayout *hr = nullptr;
	if (fbCard_->layout() && fbCard_->layout()->itemAt(0))
		hr = qobject_cast<QHBoxLayout *>(fbCard_->layout()->itemAt(0)->layout());
	if (!hr)
		return;
	fbSemaLabel_ = new QLabel(fbCard_);
	fbSemaLabel_->setTextFormat(Qt::PlainText);
	fbSemaLabel_->setAccessibleName(tr("Estado de Facebook"));
	fbCooldownLabel_ = new QLabel(fbCard_);
	fbCooldownLabel_->setTextFormat(Qt::PlainText);
	fbCooldownLabel_->setVisible(false);
	const int at = hr->indexOf(fbHeader_) + 1;
	if (at > 0) {
		hr->insertWidget(at, fbSemaLabel_);
		hr->insertWidget(at + 1, fbCooldownLabel_);
	} else {
		hr->addWidget(fbSemaLabel_);
		hr->addWidget(fbCooldownLabel_);
	}
	fbCooldownTick_ = new QTimer(this);
	fbCooldownTick_->setSingleShot(false);
	connect(fbCooldownTick_, &QTimer::timeout, this, &MetadataDock::tickFbCooldown);
	fbSemaCache_.clear();
	fbCooldownCache_.clear();
	updateFbSemaphore();
	ensureCooldownTick();
}

QString MetadataDock::fbPersonStateWord(const QString &raw) const
{
	switch (meta::fbLiveState(raw)) {
	case meta::FbLiveState::Live:
		return tr("Al aire");
	case meta::FbLiveState::Preview:
		return tr("Preparado (sin publicar)");
	case meta::FbLiveState::Ended:
		return tr("Terminado");
	default:
		break;
	}
	return tr("Desconocido");
}

void MetadataDock::updateFbSemaphore()
{
	if (!fbSemaLabel_)
		return;
	const bool fb = platformUsable(meta::Platform::Facebook);
	const meta::FbLiveState st = fb ? meta::fbLiveState(fbLastStatus_)
	                                   : meta::FbLiveState::Unknown;
	const bool waiting = fbQuietStatus_ || fbAutoActive_ || fbAutoArmed_ ||
	    fbEndAutoPending_ || relayState_ == relay::State::Starting;
	const bool relayErr = relay::stateIsError(relayState_);
	const bool hardBlocked = fb && !currentFbLiveId().isEmpty() &&
	    fbVerify_.outcome == meta::FbVerify::Blocked &&
	    fbVerify_.reason != QStringLiteral("write-accepted") &&
	    fbVerify_.reason != QStringLiteral("no-expected");
	// Trío operador 2026-10-26 [D]: mismo predicado único que el
	// resumen (meta::fbEmitReadiness). Verde solo con verify +
	// forward + emisor; sin emisor pero resto ok: ámbar intermedio.
	const bool verifiedOrAud =
		fbVerify_.outcome == meta::FbVerify::Verified ||
		fbVerify_.outcome == meta::FbVerify::AudienceUnverified;
	const meta::FbEmitReadiness emitReady = meta::fbEmitReadiness(
		verifiedOrAud, (relayState_ == relay::State::ForwardOk),
		relaySignalHasEmitter());
	const bool ready = (emitReady == meta::FbEmitReadiness::Ready);
	const bool readyNoSignal =
		(emitReady == meta::FbEmitReadiness::ReadyNoSignal);
	QString key;
	QString glyph;
	QString phrase;
	QString color;
	if (!fb) {
		key = QStringLiteral("idle-off");
		glyph = QString::fromUtf8("\xE2\x97\x8B");
		phrase = tr("Sin conexión");
		color = QStringLiteral("#8a8a8a");
	} else if (st == meta::FbLiveState::Live) {
		key = QStringLiteral("live");
		glyph = QString::fromUtf8("\xE2\x97\x8F");
		phrase = tr("Estás al aire — transmitiendo ahora");
		color = QStringLiteral("#e5484d");
	} else if (waiting) {
		key = QStringLiteral("wait");
		glyph = QString::fromUtf8("\xE2\x97\x8C");
		phrase = tr("Trabajando — no tienes que hacer nada");
		color = QStringLiteral("#ffc107");
	} else if (relayErr || hardBlocked) {
		key = QStringLiteral("prob");
		glyph = QString::fromUtf8("\xE2\x96\xB2");
		phrase = tr("Hay un problema — mira el resumen");
		color = QStringLiteral("#e5484d");
	} else if (ready) {
		key = QStringLiteral("ready");
		glyph = QString::fromUtf8("\xE2\x97\x8B");
		phrase = tr("Listo para emitir");
		color = QStringLiteral("#2da44e");
	} else if (readyNoSignal) {
		key = QStringLiteral("ready-nosignal");
		glyph = QString::fromUtf8("\xE2\x97\x90");
		phrase = tr("Lista pero sin señal — apunta tu programa a "
			    "la dirección fija");
		color = QStringLiteral("#ffc107");
	} else {
		key = QStringLiteral("idle");
		glyph = QString::fromUtf8("\xE2\x97\x8B");
		phrase = tr("Aún no listo");
		color = QStringLiteral("#8a8a8a");
	}
	if (key == fbSemaCache_)
		return; // cache: no repintar
	fbSemaCache_ = key;
	fbSemaLabel_->setText(glyph);
	fbSemaLabel_->setStyleSheet(
		QStringLiteral("QLabel { color: %1; font-size: 18px; }").arg(color));
	fbSemaLabel_->setAccessibleName(phrase);
	fbSemaLabel_->setToolTip(phrase);
}

void MetadataDock::ensureCooldownTick()
{
	if (!fbCooldownTick_ || !fbCooldownLabel_)
		return;
	const bool fb = platformUsable(meta::Platform::Facebook);
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	const qint64 lastOk = relay_ ? relay_->lastForwardOkMs() : 0;
	// Trío operador 2026-10-26 [D]: sin forward aceptado (lastOk==0)
	// jamás hay contador (cooldownRemainingMs ya da 0, pero el gate
	// explícito documenta la invariante).
	if (lastOk <= 0) {
		fbCooldownTick_->stop();
		if (fbCooldownLabel_->isVisible())
			fbCooldownLabel_->setVisible(false);
		fbCooldownCache_.clear();
		return;
	}
	const qint64 rem = relay::cooldownRemainingMs(lastOk, now);
	if (fb && rem > 0) {
		tickFbCooldown();
		if (!fbCooldownTick_->isActive())
			fbCooldownTick_->start(1000);
	} else {
		fbCooldownTick_->stop();
		if (fbCooldownLabel_->isVisible())
			fbCooldownLabel_->setVisible(false);
		fbCooldownCache_.clear();
	}
}

void MetadataDock::tickFbCooldown()
{
	if (!fbCooldownLabel_)
		return;
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	const qint64 lastOk = relay_ ? relay_->lastForwardOkMs() : 0;
	// Sin origen no hay contador: jamás mostrar ~m:ss con lastOk==0.
	if (lastOk <= 0) {
		if (fbCooldownTick_)
			fbCooldownTick_->stop();
		fbCooldownLabel_->setVisible(false);
		fbCooldownCache_.clear();
		return;
	}
	const qint64 rem = relay::cooldownRemainingMs(lastOk, now);
	if (rem <= 0) {
		// Se detiene en 0 sin afirmar nada: ni mensaje ni estado.
		if (fbCooldownTick_)
			fbCooldownTick_->stop();
		fbCooldownLabel_->setVisible(false);
		fbCooldownCache_.clear();
		return;
	}
	// Trío operador 2026-10-26 [D]: el contador dice su causa (tras
	// preparar el vídeo al abrir / tras el último reenvío aceptado).
	const int okCount = relay_ ? relay_->forwardOkCount() : 0;
	const relay::CooldownOrigin origin =
		relay::cooldownOrigin(lastOk, okCount);
	const QString t =
		relay::cooldownCounterText(rem, now - lastOk, origin);
	if (t.isEmpty()) {
		if (fbCooldownTick_)
			fbCooldownTick_->stop();
		fbCooldownLabel_->setVisible(false);
		fbCooldownCache_.clear();
		return;
	}
	if (t != fbCooldownCache_) {
		fbCooldownCache_ = t;
		fbCooldownLabel_->setText(t);
		const QString phrase =
			tr("Sigo solo %1 — no tienes que hacer nada").arg(t);
		fbCooldownLabel_->setAccessibleName(phrase);
		fbCooldownLabel_->setToolTip(phrase);
	}
	if (!fbCooldownLabel_->isVisible())
		fbCooldownLabel_->setVisible(true);
}

void MetadataDock::setFbAudienceNoticeHidden(bool on)
{
	fbAudienceHide_ = on;
	// Persistente plaintext (QSettings, sin secretos): la logica lo lee
	// via fbAudienceNoticeHidden(). Solo este aviso; defecto OFF.
	QSettings st(QStringLiteral("obs-stream-metadata"),
	     QStringLiteral("dock"));
	st.setValue(QStringLiteral("facebook/audienceNoticeHidden"), on);
	if (fbAudienceHideCheck_ &&
	    fbAudienceHideCheck_->isChecked() != on) {
		const QSignalBlocker block(fbAudienceHideCheck_);
		fbAudienceHideCheck_->setChecked(on);
	}
}

void MetadataDock::onFbSignalCheck()
{
	// Solo vista: reusa la lectura a demanda existente (localhost,
	// sin polling). El resultado llega por Op::RelaySignal y
	// repinta via updateFbHappyPath.
	requestRelaySignal();
	updateFbHappyPath();
}

// --- Fase A auto-preparacion Facebook + Plan v3 Fase 1 --------------------
// Arranque EXACTO, sin temporizadores de reintento: el unico punto de
// entrada es fbAutoStartFromConnected(), llamado SOLO desde
// finishManagedConnected(p == Facebook) y desde la rama "connected"
// del lambda de silentRevalidateManaged (post-GET fresco). JAMAS desde
// managedPollTimer_/onManagedPollTimeout, backoff/scheduleBackoff ni
// polls Twitch (onTwitchPollTimeout/TwPoll). El flag fbAutoActive_ se
// fija DENTRO, post-GET fresco (en fbAutoHandleRead/fbAutoCreateFresh),
// jamas desde snapshot. Un solo intento por arranque/sesion, sin loops,
// sin modales (todo por labels).
// Punto 1: auto-ensure del relay SOLO con sesion FB valida; ante
// BinaryMissing/PortBusy solo se informa (ensureStarted ya notifica,
// sin reintentar aqui). Punto 2: UNA lectura GET si hay persistido;
// UNPUBLISHED -> crear nuevo + DELETE del anterior (solo ese, nunca
// LIVE/VOD/ajeno); fallo de create -> fallback reutilizar anterior con
// mensaje honesto; no-UNPUBLISHED/404 -> crear sin borrar nada ajeno;
// sin persistido -> crear. Maximo 1 create + 1 delete por arranque.
// Punto 4: requestRelaySignal (localhost, a demanda + escucha v3).
// Punto 5: un solo create tras End confirmado (canRefire o diferir).

void MetadataDock::maybeFbAutoStart()
{
	// Retirado Plan v3: el arranque por timer queda prohibido (el
	// constructor ya no programa ningun singleShot a este metodo).
	// Se conserva como no-op para no dejar llamadas colgadas; el punto
	// unico real es fbAutoStartFromConnected().
	return;
}

void MetadataDock::fbAutoStartFromConnected(meta::Platform p)
{
	// INVARIANTE (Plan v3 Fase 1): este es el UNICO punto de arranque.
	// Llamadores permitidos: finishManagedConnected(p == Facebook) y
	// la rama "connected" del lambda de silentRevalidateManaged.
	// Prohibido: managedPollTimer_/onManagedPollTimeout,
	// scheduleBackoff/backoff, polls Twitch.
	if (p != meta::Platform::Facebook)
		return;
	if (!platformUsable(meta::Platform::Facebook))
		return;
	if (fbAutoActive_ || fbAutoArmed_ || fbAutoDidCreate_)
		return; // un solo intento por arranque/sesion
	if (!isManaged())
		return; // v3: el arranque exacto es Managed (post-GET fresco)
	if (!managedAuth_ || !managedAuth_->hasUserSession())
		return;
	if (!managedAccount(meta::Platform::Facebook).connected)
		return;
	if (relay_)
		relay_->ensureStarted(); // punto 1: informa, sin reintentar
	fbAutoArmed_ = true; // probe GET en vuelo; active se fija DENTRO
	fbAutoDidCreate_ = false;
	fbAutoDidDelete_ = false;
	fbAutoPrevId_.clear();
	fbAutoPrevStatus_.clear();
	fbAutoPrevTitle_.clear();
	fbAutoPrevDesc_.clear();
	fbAutoProbe();
}

void MetadataDock::fbAutoProbe()
{
	const QString persisted = currentFbLiveId();
	if (persisted.isEmpty()) {
		// Sin persistido: crear nuevo directo (texto honesto, sin
		// suponer estado). El flag se fija DENTRO del create.
		fbAutoPrevId_.clear();
		fbAutoPrevStatus_.clear();
		fbAutoPrevTitle_.clear();
		fbAutoPrevDesc_.clear();
		fbAutoCreateFresh(QString(), QString(), QString());
		return;
	}
	fbAutoPrevId_ = persisted;
	if (isManaged()) {
		if (!managedAccount(meta::Platform::Facebook).connected) {
			// Latch: sin conexion no hay ciclo; se baja todo
			// antes del return (no dejar armado colgado).
			fbAutoActive_ = false;
			fbAutoArmed_ = false;
			return;
		}
		fbQuietStatus_ = true;
		startManagedFbOp(QStringLiteral("status"), persisted);
		// El lambda de status (Managed) detecta fbAutoActive_/
		// fbAutoArmed_ y encamina a fbAutoHandleRead;
		// ver startManagedFbOp.
		return;
	}
	if (!fb_.connected) {
		// Latch: sin conexion no hay ciclo; se baja todo antes del
		// return por !connected.
		fbAutoActive_ = false;
		fbAutoArmed_ = false;
		return;
	}
	// UNA lectura GET (punto 2). Silenciosa: fbQuietStatus_ evita el
	// mensaje generico; el resultado lo redacta fbAutoHandleRead.
	fbQuietStatus_ = true;
	startFbStatusIndependent(persisted);
}

void MetadataDock::fbAutoCreateFresh(const QString &prevStatus,
				     const QString &prevTitle,
				     const QString &prevDesc)
{
	if (fbAutoDidCreate_)
		return; // presupuesto: maximo 1 create por arranque
	// El flag se fija DENTRO, post-GET fresco (o via directa sin
	// persistido): jamas desde snapshot.
	fbAutoActive_ = true;
	fbAutoArmed_ = false;
	const QString title = meta::fbAutoCreateTitle(
		titleEdit_ ? titleEdit_->text() : QString(), prevTitle);
	const QString desc = meta::fbAutoCreateDesc(
		descEdit_ ? descEdit_->toPlainText() : QString(), prevDesc);
	if (title.isEmpty() ||
	    title.size() > meta::kFacebookTitleMax) {
		fbAutoActive_ = false;
		// Sin titulo valido no hay create honesto: si el anterior era
		// UNPUBLISHED se reutiliza con mensaje honesto.
		fbAutoHandleCreateFailed(prevStatus);
		return;
	}
	fbAutoPrevStatus_ = prevStatus;
	fbAutoPrevTitle_ = prevTitle;
	fbAutoPrevDesc_ = prevDesc;
	fbAutoDidCreate_ = true;
	generalMsg_->setText(
		tr("Preparo el de trabajo — no tienes que hacer nada"));
	if (isManaged()) {
		// Managed v3: body parametrizado (NO muta widgets, como
		// Independent); el lambda de create detecta fbAutoActive_.
		startManagedFbCreateWithTitle(title, desc);
		return;
	}
	// Independent: body parametrizado (sin mutar widgets).
	clearFbVerify();
	fbExpectedTitle_ = title;
	fbExpectedDesc_ = desc;
	QString token = fb_.access;
	const QString target =
		fbTarget_.isEmpty() ? QStringLiteral("me") : fbTarget_;
	if (target != QStringLiteral("me"))
		token = fbPageTokens_.value(target, token);
	QUrl url(QString::fromLatin1(kFbGraph) + QStringLiteral("/") +
		 target + QStringLiteral("/live_videos"));
	const QJsonObject body = meta::facebookCreatePayload(
		title, desc, fbPersistent_);
	sendJson(url, QStringLiteral("POST"),
		 QString::fromUtf8(
			 QJsonDocument(body).toJson(QJsonDocument::Compact)),
		 Op::FbCreate, token);
}

void MetadataDock::fbAutoHandleRead(const QString &id,
				    const QString &status,
				    const QString &title, const QString &desc,
				    bool readOk, bool notFound)
{
	// El flag se fija DENTRO, post-GET fresco: el armado (probe en
	// vuelo) se convierte aqui en activo; jamas desde snapshot.
	if (fbAutoArmed_) {
		fbAutoActive_ = true;
		fbAutoArmed_ = false;
	}
	if (!fbAutoActive_ || fbAutoDidCreate_)
		return;
	if (!readOk) {
		if (notFound) {
			// Persistido ya no gestionable (404/VOD/ID ajeno):
			// crear nuevo SIN borrar nada ajeno.
			fbAutoCreateFresh(QString(), QString(),
					  QString());
		} else {
			// Fallo de red/rate: no inventar estado; difiere con
			// mensaje honesto y reutiliza el anterior si era
			// UNPUBLISHED (fallback), sin crear huerfano.
			fbAutoHandleCreateFailed(
				fbAutoPrevStatus_);
		}
		return;
	}
	const QString st = status.trimmed();
	if (meta::fbPrevIsReusablePreview(st)) {
		// UNPUBLISHED -> crear nuevo de todos modos (con fallback
		// de titulo/desc del anterior) y luego DELETE del anterior.
		fbAutoCreateFresh(st, title, desc);
		return;
	}
	// LIVE/STOPPED/VOD/otro: crear nuevo sin borrar nada ajeno (solo
	// el persistido si era nuestro UNPUBLISHED, que aqui no lo es).
	fbAutoCreateFresh(QString(), QString(), QString());
}

void MetadataDock::fbAutoHandleCreated(const QString &newId)
{
	if (!fbAutoActive_)
		return;
	const QString nid = newId.trimmed();
	if (nid.isEmpty()) {
		fbAutoHandleCreateFailed(fbAutoPrevStatus_);
		return;
	}
	fbAutoArmed_ = false;
	// Limpieza del huerfano: SOLO si el anterior era UNPUBLISHED
	// nuestro y distinto del nuevo (puro fbAutoShouldDeletePrev).
	if (!fbAutoDidDelete_ &&
	    meta::fbAutoShouldDeletePrev(fbAutoPrevStatus_,
					 fbAutoPrevId_, nid)) {
		fbAutoDidDelete_ = true;
		fbAutoActive_ = false; // el delete cierra el ciclo
		generalMsg_->setText(
			tr("Tu vídeo de trabajo está listo. Quité el anterior sin publicar."));
		fbAutoDeletePrev();
		return;
	}
	fbAutoActive_ = false;
	if (fbAutoPrevId_.isEmpty() || !meta::fbPrevIsReusablePreview(
					       fbAutoPrevStatus_)) {
		generalMsg_->setText(tr("Tu vídeo de trabajo está listo."));
	} else {
		// Anterior reutilizable pero sin delete (mismo ID u otro
		// motivo puro): mensaje honesto, sin borrar nada.
		generalMsg_->setText(
			tr("Tu vídeo de trabajo está listo. El anterior sigue ahí; "
			   "bórralo si quieres."));
	}
}

void MetadataDock::fbAutoHandleCreateFailed(const QString &prevStatus)
{
	fbAutoActive_ = false;
	fbAutoArmed_ = false;
	// Fallback honesto: reutilizar el anterior valido (UNPUBLISHED) con
	// mensaje de reenvio pendiente; jamas inventar un ID nuevo.
	if (meta::fbPrevIsReusablePreview(prevStatus.trimmed()) &&
	    !fbAutoPrevId_.trimmed().isEmpty()) {
		setFbLiveId(fbAutoPrevId_.trimmed());
		generalMsg_->setText(
			tr("No pude preparar uno nuevo. Sigo con el anterior — no "
			   "tienes que hacer nada."));
		obs_log(LOG_WARNING,
			"facebook auto-prepare: create failed, fallback "
			"reuse");
		return;
	}
	generalMsg_->setText(
		tr("No pude preparar el vídeo — pulsa Crear de nuevo."));
	obs_log(LOG_WARNING,
		"facebook auto-prepare: create failed, no fallback");
}

void MetadataDock::fbAutoDeletePrev()
{
	const QString prev = fbAutoPrevId_.trimmed();
	if (prev.isEmpty())
		return;
	fbAutoDeleteId_ = prev; // marca el delete automatico (punto 2)
	if (isManaged()) {
		if (!managedAccount(meta::Platform::Facebook).connected)
			return;
		startManagedFbOp(QStringLiteral("delete"), prev);
		return;
	}
	if (!fb_.connected)
		return;
	startFbDeleteIndependent(prev);
}

// Punto 3: tras UpFb OK, status silencioso -> runFbVerification sin
// pisar el mensaje de Apply. Patron fbQuietStatus_ del create.
void MetadataDock::fbChainSilentVerify(const QString &liveId)
{
	const QString id = liveId.trimmed();
	if (id.isEmpty())
		return;
	fbApplyChain_ = true;
	fbApplyChainId_ = id;
	fbApplyChainMsg_ = fbResult_ ? fbResult_->text() : QString();
	fbQuietStatus_ = true;
	if (isManaged()) {
		if (managedAccount(meta::Platform::Facebook).connected)
			startManagedFbOp(QStringLiteral("status"), id);
		else
			fbApplyChain_ = false;
		return;
	}
	if (fb_.connected)
		startFbStatusIndependent(id);
	else
		fbApplyChain_ = false;
}

// Punto 5: un solo create tras End confirmado (con canRefire). Sin
// opt-in nuevo: siempre con mensaje y limpieza posterior por el ciclo
// normal (el siguiente arranque/end gestiona el huerfano). Si el
// cooldown bloquea, difiere con mensaje + ID, sin crear huerfano.
// Presupuesto propio de un intento (fbEndAutoPending_); no consume ni
// reutiliza el del arranque (punto 2). El terminado queda como VOD:
// este create jamas borra nada.
void MetadataDock::fbEndAutoPrepare(const QString &endedId)
{
	const QString eid = endedId.trimmed();
	if (fbEndAutoPending_ || fbAutoActive_)
		return; // un solo intento; sin pisar el arranque
	// Plan v3 Fase 1: si la escucha ya pre-creo el siguiente al
	// detectar el corte, el End solo cierra el anterior (sin otro
		// create duplicado; el pre-creado es el siguiente).
	if (fbSigPrecreated_)
		return;
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	const qint64 lastOk =
		relay_ ? relay_->lastForwardOkMs() : 0;
	if (!relay::canRefire(lastOk, now)) {
		generalMsg_->setText(
			tr("Tu directo terminó. Espero un poco antes del siguiente "
			   "— pulsa Crear cuando quieras."));
		ensureCooldownTick();
		return;
	}
	const QString title = meta::fbAutoCreateTitle(
		titleEdit_ ? titleEdit_->text() : QString(), QString());
	const QString desc = meta::fbAutoCreateDesc(
		descEdit_ ? descEdit_->toPlainText() : QString(),
		QString());
	if (title.isEmpty() ||
	    title.size() > meta::kFacebookTitleMax) {
		generalMsg_->setText(
			tr("Tu directo terminó. Revisa el título y pulsa Crear."));
		return;
	}
	fbEndAutoPending_ = true;
	clearFbVerify();
	fbExpectedTitle_ = title;
	fbExpectedDesc_ = desc;
	generalMsg_->setText(
		tr("Tu directo terminó. Preparo el siguiente — no tienes que "
		   "hacer nada."));
	if (isManaged()) {
		if (!managedAccount(meta::Platform::Facebook).connected) {
			fbEndAutoPending_ = false;
			return;
		}
		if (titleEdit_ && titleEdit_->text().trimmed().isEmpty())
			titleEdit_->setText(title);
		if (descEdit_ &&
		    descEdit_->toPlainText().trimmed().isEmpty())
			descEdit_->setPlainText(desc);
		startManagedFbOp(QStringLiteral("create"), QString());
		return;
	}
	if (!fb_.connected) {
		fbEndAutoPending_ = false;
		return;
	}
	QString token = fb_.access;
	const QString target =
		fbTarget_.isEmpty() ? QStringLiteral("me") : fbTarget_;
	if (target != QStringLiteral("me"))
		token = fbPageTokens_.value(target, token);
	QUrl url(QString::fromLatin1(kFbGraph) + QStringLiteral("/") +
		 target + QStringLiteral("/live_videos"));
	const QJsonObject body = meta::facebookCreatePayload(
		title, desc, fbPersistent_);
	sendJson(url, QStringLiteral("POST"),
		 QString::fromUtf8(
			 QJsonDocument(body).toJson(QJsonDocument::Compact)),
		 Op::FbCreate, token);
}

void MetadataDock::requestRelaySignal()
{
	// Localhost unicamente (Control API del relay propio); jamas Graph.
	// A demanda de la vista + escucha v3 (respaldo 3 s / adaptativo).
	if (!relay_ ||
	    !relay::stateIsRunning(relayState_))
		return;
	const QUrl url(relay::controlBase() +
		       relay::controlSignalPath());
	generalMsg_->setText(tr("Miro la señal — no tienes que hacer nada."));
	sendGet(url, Op::RelaySignal, QString());
}

// --- Plan v3 Fase 1: create Managed parametrizado (sin mutar widgets) ---
void MetadataDock::startManagedFbCreateWithTitle(const QString &title,
						const QString &desc)
{
	// Override de un solo uso: startManagedFbOp("create") lo consume
	// para el body y el expected, con el parser/ingesta/forward/verify
	// intactos (sin mutar widgets, a diferencia del camino manual).
	fbCreateTitleOverride_ = title;
	fbCreateDescOverride_ = desc;
	fbUseCreateOverride_ = true;
	startManagedFbOp(QStringLiteral("create"), QString());
}

// --- Plan v3 Fase 1: escucha del repetidor --------------------------------
// Contrato de archivos-senal (release-tooling): %TEMP%/
// obs-stream-metadata-relay/signal.on existe = emisor en linea
// (via runOnOnline); signal.off = apagado (via runOnOffline).
// Usa QDir::tempPath() + "obs-stream-metadata-relay" (sin hardcodear
// %TEMP%). Evento primario: QFileSystemWatcher sobre el dir.
// Respaldo: pregunta local cada 3 s (GET paths/get/live, localhost).

QString MetadataDock::fbSignalDir() const
{
	return QDir(QDir::tempPath()).filePath(
		QString::fromLatin1(meta::fbSignalDirLeaf()));
}

void MetadataDock::ensureFbSignalWatch()
{
	const QString dir = fbSignalDir();
	QDir().mkpath(dir); // solo observa; no escribe senales
	if (!fbSigWatcher_) {
		fbSigWatcher_ = new QFileSystemWatcher(this);
		connect(fbSigWatcher_,
			&QFileSystemWatcher::directoryChanged, this,
			&MetadataDock::onFbSignalDirChanged);
	}
	if (!fbSigWatcher_->directories().contains(dir))
		fbSigWatcher_->addPath(dir);
	if (!fbSigBackup_) {
		fbSigBackup_ = new QTimer(this);
		fbSigBackup_->setSingleShot(false);
		connect(fbSigBackup_, &QTimer::timeout, this,
			&MetadataDock::onFbSignalBackup);
	}
	fbSigBackup_->start(static_cast<int>(meta::fbSignalBackupMs()));
	if (!fbSigFast_) {
		fbSigFast_ = new QTimer(this);
		fbSigFast_->setSingleShot(true);
		connect(fbSigFast_, &QTimer::timeout, this,
			&MetadataDock::onFbSignalFast);
	}
}

void MetadataDock::onFbSignalDirChanged(const QString &path)
{
	Q_UNUSED(path);
	// Evento primario: el repetidor toco signal.on/off (runOnOnline /
	// runOnOffline). No se decide por ficheros: se pide GET local
	// fresco (misma via que el respaldo) para una sola via de decision.
	obs_log(LOG_INFO, "relay signal dir changed");
	requestRelaySignal();
}

void MetadataDock::onFbSignalBackup()
{
	// Respaldo cada 3 s, solo con sesion FB y relay en pie (cero red
	// en otro caso). Localhost unicamente, jamas Graph.
	if (!platformUsable(meta::Platform::Facebook))
		return;
	if (!relay_ || !relay::stateIsRunning(relayState_))
		return;
	requestRelaySignal();
}

void MetadataDock::onFbSignalFast()
{
	// Re-pregunta adaptativa tras el primer corte (2 s, luego 1 s).
	if (!platformUsable(meta::Platform::Facebook))
		return;
	requestRelaySignal();
	if (fbSigFastStep_ == 0) {
		fbSigFastStep_ = 1;
		if (fbSigFast_)
			fbSigFast_->start(
				static_cast<int>(meta::fbSignalFastMs(1)));
	}
}

void MetadataDock::fbSignalNoteResult(bool hasEmitter)
{
	if (hasEmitter) {
		// Si la senal vuelve, cancela lo pendiente del corte
		// (re-preguntas, cuenta de caida, confirmacion de End)
		// pero conserva la memoria de votacion 2-de-3 (el flap
		// repetido debe leerse "inestable", no como cortes
		// nuevos). Lo ya pre-creado queda (no se deshace) y se
		// rearma el disparo para un corte futuro distinto.
		fbSigFails_ = 0;
		fbSigFastStep_ = 0;
		fbSigGoneSince_ = 0;
		fbSigEndConfirm_ = false;
		if (fbSigFast_)
			fbSigFast_->stop();
		fbSigBallot_.append(true);
		while (fbSigBallot_.size() > 3)
			fbSigBallot_.takeFirst();
		fbSigPrecreated_ = false;
		return;
	}
	fbSigFails_++;
	fbSigBallot_.append(false);
	while (fbSigBallot_.size() > 3)
		fbSigBallot_.takeFirst();
	if (fbSigFails_ == 1) {
		// Primer corte: re-pregunta a 2 s y 1 s (adaptativo).
		fbSigFastStep_ = 0;
		if (fbSigFast_) {
			fbSigFast_->stop();
			fbSigFast_->start(
				static_cast<int>(meta::fbSignalFastMs(0)));
		}
		if (fbSigGoneSince_ == 0)
			fbSigGoneSince_ =
				QDateTime::currentMSecsSinceEpoch();
		return;
	}
	if (meta::fbSignalDropCountsAsGone(fbSigFails_)) {
		if (fbSigGoneSince_ == 0)
			fbSigGoneSince_ =
				QDateTime::currentMSecsSinceEpoch();
		fbSignalMaybePrecreate();
		fbMaybeAutoEnd();
	}
}

void MetadataDock::fbSignalCancelPending()
{
	fbSigFails_ = 0;
	fbSigFastStep_ = 0;
	fbSigGoneSince_ = 0;
	fbSigBallot_.clear();
	fbSigEndConfirm_ = false;
	if (fbSigFast_)
		fbSigFast_->stop();
}

void MetadataDock::fbSignalMaybePrecreate()
{
	// Apagado probable (~6 s): pre-crea el siguiente con fallback de
	// titulo y mensaje citando el titulo usado. En Managed NO muta
	// widgets (body parametrizado); un solo disparo por corte.
	if (fbSigPrecreated_)
		return;
	if (!platformUsable(meta::Platform::Facebook))
		return;
	if (fbAutoDidCreate_ && fbAutoActive_)
		return; // el arranque ya prepara uno
	{
		int present = 0;
		for (bool b : fbSigBallot_)
			if (b)
				present++;
		if (meta::fbSignalBallotUnstable(present,
						 fbSigBallot_.size()))
			return; // votacion "inestable": espera, sin crear
	}
	const QString title = meta::fbAutoCreateTitle(
		titleEdit_ ? titleEdit_->text() : QString(),
		fbAutoPrevTitle_);
	const QString desc = meta::fbAutoCreateDesc(
		descEdit_ ? descEdit_->toPlainText() : QString(),
		fbAutoPrevDesc_);
	if (title.isEmpty() ||
	    title.size() > meta::kFacebookTitleMax)
		return;
	fbSigPrecreated_ = true;
	fbSigLastTitle_ = title;
		generalMsg_->setText(
			tr("Se perdió la señal. Preparo el siguiente — no tienes "
			   "que hacer nada."));
	obs_log(LOG_INFO, "relay signal gone: pre-creating next video");
	if (isManaged()) {
		if (!managedAccount(meta::Platform::Facebook).connected)
			return;
		startManagedFbCreateWithTitle(title, desc);
		return;
	}
	if (!fb_.connected)
		return;
	clearFbVerify();
	fbExpectedTitle_ = title;
	fbExpectedDesc_ = desc;
	QString token = fb_.access;
	const QString target =
		fbTarget_.isEmpty() ? QStringLiteral("me") : fbTarget_;
	if (target != QStringLiteral("me"))
		token = fbPageTokens_.value(target, token);
	QUrl url(QString::fromLatin1(kFbGraph) + QStringLiteral("/") +
		 target + QStringLiteral("/live_videos"));
	sendJson(url, QStringLiteral("POST"),
		 QString::fromUtf8(QJsonDocument(
			 meta::facebookCreatePayload(title, desc,
						     fbPersistent_))
					 .toJson(QJsonDocument::Compact)),
		 Op::FbCreate, token);
}

void MetadataDock::fbMaybeAutoEnd()
{
	// Para TERMINAR exige: votacion no inestable + 30 s continuos +
	// gracia 5 min post-GoLive + confirmacion Graph fresca del MISMO
	// ID + solo si el LIVE lo inicio el plugin en esta sesion.
	if (!fbAutoGoLiveSession_ || fbAutoGoLiveId_.isEmpty())
		return;
	const QString cur = currentFbLiveId();
	if (cur.trimmed() != fbAutoGoLiveId_.trimmed())
		return; // cambio de ID: no terminar lo ajeno
	int present = 0;
	for (bool b : fbSigBallot_)
		if (b)
			present++;
	if (meta::fbSignalBallotUnstable(present,
					 fbSigBallot_.size()))
		return; // "inestable": espera sin terminar
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	const qint64 goneMs =
		fbSigGoneSince_ > 0 ? now - fbSigGoneSince_ : 0;
	if (!meta::fbAutoEndGuardsOk(goneMs, fbAutoGoLiveAtMs_, now,
				     false, true))
		return; // aun no: 30 s / 5 min pendientes
	// Guardas temporales OK: pedir confirmacion Graph fresca del MISMO
	// ID (una sola lectura); el reply decide el End (ver onReply).
	fbSigEndConfirm_ = true;
	if (isManaged()) {
		if (managedAccount(meta::Platform::Facebook).connected)
			startManagedFbOp(QStringLiteral("status"),
					 fbAutoGoLiveId_);
		else
			fbSigEndConfirm_ = false;
		return;
	}
	if (fb_.connected)
		startFbStatusIndependent(fbAutoGoLiveId_);
	else
		fbSigEndConfirm_ = false;
}

void MetadataDock::noteFbGoLiveSession(const QString &liveId)
{
	const QString id = liveId.trimmed();
	if (id.isEmpty())
		return;
	fbAutoGoLiveSession_ = true;
	fbAutoGoLiveId_ = id;
	fbAutoGoLiveAtMs_ = QDateTime::currentMSecsSinceEpoch();
}

void MetadataDock::clearFbGoLiveSession()
{
	fbAutoGoLiveSession_ = false;
	fbAutoGoLiveId_.clear();
	fbAutoGoLiveAtMs_ = 0;
	fbSigEndConfirm_ = false;
}

// --- Plan v3 Fase 1: cola diferida de un disparo (refire cooldown) -------
void MetadataDock::fbScheduleRefire(const QString &liveId,
				   const QString &dest)
{
	fbCancelRefire();
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	const qint64 lastOk = relay_ ? relay_->lastForwardOkMs() : 0;
	const qint64 wait =
		relay::cooldownRemainingMs(lastOk, now);
	fbRefire_.liveId = liveId.trimmed();
	fbRefire_.dest = dest;
	fbRefire_.fireAtMs = now + (wait > 0 ? wait : 0);
	if (!fbRefire_.timer) {
		fbRefire_.timer = new QTimer(this);
		fbRefire_.timer->setSingleShot(true);
		connect(fbRefire_.timer, &QTimer::timeout, this,
			&MetadataDock::fbFireRefire);
	}
	fbRefire_.timer->start(
		static_cast<int>(wait > 0 ? wait : 0));
	ensureCooldownTick();
	obs_log(LOG_INFO, "fb refire deferred (single shot)");
}

void MetadataDock::fbFireRefire()
{
	// Un solo disparo, sin reintentos en bucle: revalida liveId ==
	// currentFbLiveId() y estado antes del PATCH.
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	if (!meta::fbRefireDue(fbRefire_.fireAtMs, now)) {
		fbCancelRefire();
		return;
	}
	const QString liveId = fbRefire_.liveId;
	const QString dest = fbRefire_.dest;
	fbCancelRefire();
	if (liveId.isEmpty() || dest.isEmpty())
		return;
	if (liveId.trimmed() != currentFbLiveId().trimmed())
		return; // el vigente cambio: no re-fijar lo viejo
	if (!platformUsable(meta::Platform::Facebook))
		return;
	if (!relay_ || !relay::stateIsRunning(relayState_))
		return;
	if (!relay::forwardDestLooksValid(dest))
		return;
	relay_->refireDest(dest);
}

void MetadataDock::fbCancelRefire()
{
	fbRefire_.liveId.clear();
	fbRefire_.dest.clear();
	fbRefire_.fireAtMs = 0;
	if (fbRefire_.timer)
		fbRefire_.timer->stop();
}

void MetadataDock::fbRefireOrDefer(const QString &liveId,
				   const QString &ingest)
{
	// Crea ya, difiere solo el PATCH: si el cooldown bloquea, un solo
	// disparo diferido con guarda de ID (sin reintentos en bucle).
	if (liveId.trimmed().isEmpty() || ingest.isEmpty() || !relay_)
		return;
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	if (relay::canRefire(relay_->lastForwardOkMs(), now)) {
		relay_->refireFromIngest(ingest);
		return;
	}
	const QString dest =
		relay::forwardDestFromIngest(ingest);
	if (dest.isEmpty() ||
	    !relay::forwardDestLooksValid(dest))
		return;
	fbScheduleRefire(liveId, dest);
	// Trío operador 2026-10-26 [D]: el mensaje de espera dice su
	// causa (este camino solo se alcanza con cooldown bloqueado, es
	// decir, tras un reenvío aceptado previo).
	generalMsg_->setText(
		tr("Espero un poco tras tu último reenvío aceptado y sigo "
		   "solo — no tienes que hacer nada."));
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
	silentRevalidateManaged();
	refreshManagedLoginUi(); // formulario al frente (login o gate)
}

// T-075: silent startup revalidation (no browser, single pass). Fase 4 +
// AUTORIZACION [D usuario 2026-10-26]: "si entraste antes, al abrir OBS
// vuelve directo al panel; solo pide login si vencio o cerraste sesion".
// Arranque Managed sin sesion en memoria: tryRestoreUserSession()
// (cero red) -> si true, me() silencioso: OK => PANEL directo (con la
// revalidacion de plataformas existente); 401/expirada => ENTRAR con
// mensaje normal. Sin restaurable => ENTRAR directo, CERO red.
// Reinicio con sesion valida NO borra visual: las tarjetas conservan su
// ultimo estado conocido hasta revalidar (sin parpadeo a gris/rojo).
void MetadataDock::silentRevalidateManaged()
{
	if (!isManaged() || !managedAuth_)
		return;
	// 1. Restaurar sesion persistida (cero red, territorio obs-plugin
	// intacto: solo llamada al cliente). Si ya habia sesion en memoria,
	// tryRestore es no-op (devuelve true sin tocar nada).
	if (!managedAuth_->hasUserSession())
		managedAuth_->tryRestoreUserSession();
	// 2. Gate puro (testeado): sin sesion tras el restore => ENTRAR
	// directo, CERO red, sin tocar tarjetas (conservan el repaint
	// inicial de loadStore: nada de parpadeo a gris/rojo ni gate de
	// error en plataformas).
	if (!meta::managedStartupShouldRevalidate(
		    managedAuth_->hasUserSession())) {
		obs_log(LOG_INFO,
			"managed silent revalidate skipped: login required");
		refreshManagedLoginUi();
		return;
	}
	// 3. Con sesion: PANEL directo ya (refresh muestra PANEL por
	// hasUserSession) antes de la revalidacion de red.
	obs_log(LOG_INFO, "managed silent revalidate started");
	refreshManagedLoginUi();
	managedAuth_->me([this](backend_auth::Result r,
			       const backend_auth::Client::UserInfo &) {
		if (!isManaged())
			return;
		if (r != backend_auth::Result::Ok) {
			// 401/expirada => ENTRAR con mensaje normal (sin
			// dramatismo): limpieza neutra "Not connected" /
			// "disconnected" (tono Neutral, jamas gate rojo en
			// tarjetas; el gate vive solo en userStatus_).
			// clearUserSession ya ocurrio en el cliente (401).
			obs_log(LOG_WARNING,
				"managed silent revalidate user failed");
			for (meta::Platform p :
			     {meta::Platform::Twitch,
			      meta::Platform::YouTube, meta::Platform::Kick,
			      meta::Platform::Facebook}) {
				if (managedAccount(p).connected) {
					ManagedConn &m =
						managedAccount(p);
					m.connected = false;
					m.userId.clear();
					m.display.clear();
					setStatus(p,
						  tr("Not connected"));
					setResult(p, true,
						  tr("disconnected"));
				} else {
					setStatus(p, tr("Not connected"));
				}
			}
			saveStore(); // drop stale snapshots
			refreshManagedLoginUi();
			return;
		}
		refreshManagedLoginUi();
		managedAuth_->ensureSession([this](backend_auth::Result r) {
			if (!isManaged())
				return;
			if (r != backend_auth::Result::Ok) {
				obs_log(LOG_WARNING,
					"managed silent revalidate session failed");
				return;
			}
			for (meta::Platform p :
			     {meta::Platform::Twitch,
			      meta::Platform::YouTube, meta::Platform::Kick,
			      meta::Platform::Facebook}) {
				if (!managedAccount(p).connected)
					continue;
				managedAuth_->apiGetUser(
					QStringLiteral("/connect/%1/status")
						.arg(providerSlug(p)),
					[this, p](const backend_auth::Client::
							  ApiReply &rep) {
						if (!isManaged())
							return;
						if (rep.result !=
						    backend_auth::Result::Ok) {
							obs_log(LOG_WARNING,
								"managed silent revalidate status failed: %s",
								meta::platformName(
									p));
							if (rep.result ==
							    backend_auth::
								    Result::
									    Unauthorized)
								noteManagedAuthFailure(
									p);
							return;
						}
						if (rep.body.value(QStringLiteral(
									"status"))
							    .toString() !=
						    QStringLiteral("connected")) {
							setStatus(
								p,
								tr("Needs reconnection."));
							return;
						}
						const QJsonObject acc =
							rep.body.value(QStringLiteral(
								"account"))
								.toObject();
						ManagedConn &m =
							managedAccount(p);
						m.userId = acc.value(QStringLiteral("id"))
								   .toString();
						m.display =
							acc.value(QStringLiteral(
									"displayName"))
								.toString();
						setStatus(p, tr("Connected as %1")
									     .arg(m.display));
						saveStore();
						// Plan v3 Fase 1 (arranque
						// exacto): segundo y unico otro
						// llamador permitido de
						// fbAutoStartFromConnected
						// (post-GET fresco). Sin timers.
						if (p == meta::Platform::
							    Facebook)
							fbAutoStartFromConnected(
								p);
					});
			}
		});
	});
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
	// Bugfix visual 2026-10-26 [D]: al repintar una plataforma
	// desconectada se restaura tambien su result a pristine neutro
	// ("Name —"): un ✗ heredado de un intento anterior no debe
	// pegarse como rojo en un estado ya limpio.
	auto resetPristine = [this](meta::Platform p) {
		setStatus(p, tr("Not connected"));
		if (QLabel *r = resultFor(p))
			r->setText(QStringLiteral("%1 \u2014")
					   .arg(QString::fromLatin1(
						   meta::platformName(p))));
		updateCardStyle(p);
	};
	if (isManaged()) {
		for (meta::Platform p :
		     {meta::Platform::Twitch, meta::Platform::YouTube,
		      meta::Platform::Kick, meta::Platform::Facebook}) {
			const ManagedConn &m = managedAccount(p);
			if (m.connected && !m.display.isEmpty())
				setStatus(p, tr("Connected as %1").arg(m.display));
			else
				resetPristine(p);
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
			resetPristine(p);
	}
}

void MetadataDock::onConnectManaged(meta::Platform p)
{
	if (!managedSupported(p)) {
		setResult(p, false,
			  tr("Managed is not available for this platform."));
		return;
	}
	// Fase 4: gate login previo (spec §5): sin user-session, mensaje
	// exacto y CERO red (sin fallback a Independent).
	if (!requireManagedLogin(p))
		return;
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
		managedAuth_->apiGetUser(QStringLiteral("/connect/%1/status").arg(
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
	// El poll posterior también gatea; esto cubre carreras
	// (logout entre el status y el connect).
	if (!requireManagedLogin(p))
		return;
	managedAuth_->apiPostUser(QStringLiteral("/connect/%1").arg(
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
	// Fase 4: logout durante el poll detiene sin red y pide login.
	if (!requireManagedLogin(p)) {
		if (managedPollTimer_)
			managedPollTimer_->stop();
		refreshManagedLoginUi();
		return;
	}
	if (--managedPollsLeft_ < 0) {
		managedPollTimer_->stop();
		finishManagedError(p, tr("Timed out waiting for authorization."));
		return;
	}
	managedAuth_->apiGetUser(QStringLiteral("/connect/%1/status").arg(
					     providerSlug(p)),
			     [this, p](const backend_auth::Client::ApiReply &rep) {
				     if (rep.result != backend_auth::Result::Ok) {
					     if (rep.result == backend_auth::Result::
								    Unauthorized) {
						     managedPollTimer_->stop();
						     // Auth doble normalizada:
						     // re-login, no solo Connect.
						     noteManagedAuthFailure(p);
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
	// Plan v3 Fase 1 (arranque exacto): unico punto de arranque junto
	// con la rama "connected" de silentRevalidateManaged. Sin timers
	// de reintento; el flag se fija DENTRO, post-GET fresco.
	if (p == meta::Platform::Facebook)
		fbAutoStartFromConnected(p);
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
	case backend_auth::Result::Unauthorized:
		return tr("Session expired. Retrying silently… (press Connect only if it persists).");
	case backend_auth::Result::ServerError:
		return tr("Managed backend error (temporary). Retrying once…");
	default:
		return tr("Unexpected backend reply. Retrying once…");
	}
}

QString MetadataDock::managedApiError(meta::Platform p,
				      const backend_auth::Client::ApiReply &rep)
{
	// Reuse the common UX error system (§22); backend bodies carry only
	// safe fields, and only the HTTP code leaves this function.
	switch (rep.result) {
	case backend_auth::Result::Unauthorized:
		// Auth doble normalizada (spec §4): sin distinguir
		// instalación/usuario; la recuperación es re-login.
		return meta::managedLoginRequiredMessage();
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

// --- Fase 4: gate login previo a Managed (spec §5) -----------------------
// Sin user-session en memoria, TODA acción Managed queda funcionalmente
// deshabilitada: mensaje exacto reutilizado en las 4 plataformas, cero
// red y ningún fallback silencioso a Independent.

bool MetadataDock::requireManagedLogin(meta::Platform p)
{
	if (!isManaged())
		return true; // Independent intacto: sin login, sin gate
	if (managedAuth_ && managedAuth_->hasUserSession())
		return true;
	setResult(p, false, meta::managedLoginRequiredMessage());
	return false;
}

bool MetadataDock::requireManagedLoginMsg()
{
	if (!isManaged())
		return true;
	if (managedAuth_ && managedAuth_->hasUserSession())
		return true;
	generalMsg_->setText(meta::managedLoginRequiredMessage());
	return false;
}

void MetadataDock::noteManagedAuthFailure(meta::Platform p)
{
	// 401 Managed normalizado: la conexión local cae (pertenece a la
	// sesión) y se pide re-login con el mensaje exacto. El snapshot
	// se borra; Independent intacto (sus records no se tocan).
	ManagedConn &m = managedAccount(p);
	m.connected = false;
	m.userId.clear();
	m.display.clear();
	setStatus(p, meta::managedLoginRequiredMessage());
	setResult(p, false, meta::managedLoginRequiredMessage());
	saveStore(); // drop the stale snapshot
	refreshManagedLoginUi();
}

// --- f6 ENTRAR-primero -------------------------------------------------
void MetadataDock::refreshManagedScreen()
{
	if (!managedStack_ || !loginPage_ || !panelPage_)
		return;
	// Gates puros (testeados en f6, src/metadata.cpp): excluyentes en
	// Managed, PANEL siempre en Independent. Sin red: solo lee la
	// sesión en memoria (territorio obs-plugin intacto).
	const bool logged = managedAuth_ && managedAuth_->hasUserSession();
	if (meta::managedEnterVisible(isManaged(), logged))
		managedStack_->setCurrentWidget(loginPage_);
	else
		managedStack_->setCurrentWidget(panelPage_);
	// Accesibilidad (doc Qt6 "Keyboard Focus in Widgets": foco inicial
	// explícito con setFocus): al mostrar ENTRAR, el foco va al primer
	// campo de la pestana visible para poder operar con teclado desde
	// el inicio.
	if (managedStack_->currentWidget() == loginPage_) {
		if (authMode() == meta::ManagedAuthMode::Signup &&
		    signupNameEdit_)
			signupNameEdit_->setFocus();
		else if (userNameEdit_)
			userNameEdit_->setFocus();
	}
}

// f8: modo Entrar|Crear cuenta = pestana visible del QTabWidget
// (excluyentes por construccion). Sin red, solo vista.
meta::ManagedAuthMode MetadataDock::authMode() const
{
	if (authTabs_ && authTabs_->currentWidget() == signupTab_)
		return meta::ManagedAuthMode::Signup;
	return meta::ManagedAuthMode::Login;
}

void MetadataDock::onAuthTabChanged(int)
{
	// Cambiar de modo no reintenta nada: solo limpia los errores
	// inline del otro modo y enfoca el primer campo (sin red).
	clearAuthInlineErrors();
	if (authMode() == meta::ManagedAuthMode::Signup &&
	    signupNameEdit_)
		signupNameEdit_->setFocus();
	else if (userNameEdit_)
		userNameEdit_->setFocus();
}

void MetadataDock::clearAuthInlineErrors()
{
	if (loginNameError_)
		loginNameError_->clear();
	if (loginPassError_)
		loginPassError_->clear();
	if (signupNameError_)
		signupNameError_->clear();
	if (signupPassError_)
		signupPassError_->clear();
}

void MetadataDock::refreshManagedLoginUi(const QString &statusOverride)
{
	// f6: la línea de sesión del PANEL solo existe en Managed con
	// sesión; en Independent (o sin sesión) queda oculta. userStatus_
	// (página ENTRAR) sigue dando el feedback de login/signup.
	const bool loggedNow = isManaged() && managedAuth_ &&
			       managedAuth_->hasUserSession();
	if (panelSession_) {
		panelSession_->setVisible(loggedNow);
		if (loggedNow) {
			const QString label = managedAuth_->userLabel();
			panelSession_->setText(
				label.isEmpty()
					? tr("Session active.")
					: tr("Session: %1").arg(label));
		} else {
			panelSession_->clear();
		}
	}
	if (!isManaged() || !managedAuth_) {
		refreshManagedScreen();
		return; // Independent: formulario oculto, nada que actualizar
	}
	const bool logged = managedAuth_->hasUserSession();
	if (userLoginBtn_)
		userLoginBtn_->setEnabled(!logged);
	if (userSignupBtn_)
		userSignupBtn_->setEnabled(!logged);
	if (userNameEdit_)
		userNameEdit_->setEnabled(!logged);
	if (userPassEdit_)
		userPassEdit_->setEnabled(!logged);
	// f8: los campos de Crear cuenta viven en su propia pestana.
	if (signupNameEdit_)
		signupNameEdit_->setEnabled(!logged);
	if (signupPassEdit_)
		signupPassEdit_->setEnabled(!logged);
	if (userLogoutBtn_)
		userLogoutBtn_->setEnabled(logged);
	if (logged)
		clearAuthInlineErrors();
	if (userStatus_) {
		if (logged) {
			const QString label = managedAuth_->userLabel();
			userStatus_->setText(
				label.isEmpty()
					? tr("Logged in.")
					: tr("Logged in as %1").arg(label));
		} else {
			// f7: el gate solo aparece sin mensaje vigente; un
			// error especifico recien fijado sobrevive (el
			// llamador lo pasa como override).
			userStatus_->setText(
				meta::managedLoggedOutStatus(statusOverride));
		}
	}
	refreshManagedScreen();
}

QString MetadataDock::userAuthError(backend_auth::Result r, bool isSignup)
{
	switch (r) {
	case backend_auth::Result::NetworkError:
		return tr("Could not reach the managed backend (network).");
	case backend_auth::Result::Unauthorized:
		return tr("Wrong username or password.");
	case backend_auth::Result::RateLimited:
		return tr("Too many attempts. Wait and retry.");
	case backend_auth::Result::ServerError:
		return tr("Managed backend error (temporary). Retry shortly.");
	case backend_auth::Result::StorageError:
		return tr("Local installation storage failed (DPAPI).");
	default:
		break;
	}
	// InvalidResponse cubre 400/409/duplicado según el llamador, sin
	// inventar el motivo exacto (el backend normaliza el login).
	// f7: el 409 de signup sugiere pulsar Log in en vez de reintentar
	// Sign up (texto puro meta::managedSignupTakenMessage, sin
	// secretos; el gate "login required" ya no lo pisa).
	if (isSignup)
		return meta::managedSignupTakenMessage();
	return tr("Login failed.");
}

void MetadataDock::onUserSignup()
{
	if (!isManaged() || !managedAuth_)
		return; // Independent: sin formulario, sin acción
	// f8: Crear cuenta lee SUS propios campos (jamas los de Entrar).
	const QString user =
		signupNameEdit_ ? field(signupNameEdit_) : QString();
	const QString pass =
		signupPassEdit_ ? signupPassEdit_->text() : QString();
	// f8: validación LOCAL inmediata (sin red): el 400 de signup por
	// password corta (politica 12-128) se frena aqui con mensaje
	// claro junto al campo, en vez de "username may be taken".
	clearAuthInlineErrors();
	const QString uErr = meta::validateManagedUsername(user);
	const QString pErr = meta::validateManagedPassword(pass);
	if (!uErr.isEmpty() || !pErr.isEmpty()) {
		if (!uErr.isEmpty() && signupNameError_)
			signupNameError_->setText(uErr);
		if (!pErr.isEmpty() && signupPassError_)
			signupPassError_->setText(pErr);
		if (userStatus_)
			userStatus_->setText(
				!uErr.isEmpty() ? uErr : pErr);
		return; // CERO red con entrada invalida
	}
	userSignupBtn_->setEnabled(false);
	userLoginBtn_->setEnabled(false);
	userStatus_->setText(tr("Signing up…"));
	managedAuth_->signup(
		user, pass,
		[this, user,
		 pass](backend_auth::Result r,
		       const backend_auth::Client::UserInfo &) {
			if (!isManaged())
				return; // user switched mode
			if (signupPassEdit_)
				signupPassEdit_->clear(); // jamás permanece
			if (r != backend_auth::Result::Ok) {
				// f7: el error especifico viaja como override;
				// el gate ya no lo pisa (bug 2026-10-26).
				// El 409 queda como respaldo y sugiere Entrar
				// (meta::managedSignupTakenMessage).
				refreshManagedLoginUi(userAuthError(r, true));
				return;
			}
			// 201 no abre sesión (backend Fase 2): login
			// inmediato con las mismas credenciales.
			userStatus_->setText(tr("Logging in…"));
			managedAuth_->login(
				user, pass,
				[this](backend_auth::Result lr) {
					userPassEdit_->clear();
					if (!isManaged())
						return;
					if (lr !=
					    backend_auth::Result::Ok) {
						// f7: idem tras el auto-login
						// encadenado.
						refreshManagedLoginUi(
							userAuthError(
								lr, false));
						return;
					}
					refreshManagedLoginUi();
					repaintModeStatuses();
					silentRevalidateManaged();
				});
		});
}

void MetadataDock::onUserLogin()
{
	if (!isManaged() || !managedAuth_)
		return;
	const QString user = field(userNameEdit_);
	const QString pass = userPassEdit_->text();
	// f8: validación LOCAL inmediata (sin red) con mensaje inline
	// junto al campo; CERO red con entrada invalida.
	clearAuthInlineErrors();
	const QString uErr = meta::validateManagedUsername(user);
	const QString pErr = meta::validateManagedPassword(pass);
	if (!uErr.isEmpty() || !pErr.isEmpty()) {
		if (!uErr.isEmpty() && loginNameError_)
			loginNameError_->setText(uErr);
		if (!pErr.isEmpty() && loginPassError_)
			loginPassError_->setText(pErr);
		if (userStatus_)
			userStatus_->setText(
				!uErr.isEmpty() ? uErr : pErr);
		return; // CERO red con entrada invalida
	}
	userLoginBtn_->setEnabled(false);
	userSignupBtn_->setEnabled(false);
	userStatus_->setText(tr("Logging in…"));
	managedAuth_->login(user, pass,
			    [this](backend_auth::Result r) {
				    userPassEdit_->clear();
				    if (!isManaged())
					    return;
				    if (r != backend_auth::Result::Ok) {
					    // f7: el error especifico viaja
					    // como override (bug 2026-10-26).
					    refreshManagedLoginUi(
						    userAuthError(r, false));
					    return;
				    }
				    refreshManagedLoginUi();
				    repaintModeStatuses();
				    silentRevalidateManaged();
			    });
}

void MetadataDock::onUserLogout()
{
	if (!isManaged() || !managedAuth_)
		return;
	userLogoutBtn_->setEnabled(false);
	userStatus_->setText(tr("Logging out…"));
	managedAuth_->logoutUser([this](backend_auth::Result) {
		if (!isManaged())
			return;
		userPassEdit_->clear();
		// f8: el password de Crear cuenta tampoco permanece.
		if (signupPassEdit_)
			signupPassEdit_->clear();
		clearAuthInlineErrors();
		// Las conexiones Managed pertenecen al usuario: al salir se
		// olvidan localmente (el backend revocó la sesión; la
		// propiedad por usuario invalida el resto en servidor).
		// Independent intacto por construcción.
		// Bugfix visual 2026-10-26 [D]: logout = estado LIMPIO
		// (gris neutro "Not connected" + result neutro), JAMAS el
		// gate login-required ni un ✗ heredado: el rojo solo vive
		// tras accion fallida o ruptura (regla del usuario).
		using P = meta::Platform;
		for (P p : {P::Twitch, P::YouTube, P::Kick, P::Facebook}) {
			ManagedConn &m = managedAccount(p);
			m.connected = false;
			m.userId.clear();
			m.display.clear();
			setStatus(p, tr("Not connected"));
			setResult(p, true, tr("disconnected"));
		}
		if (broadcastCombo_)
			broadcastCombo_->clear();
		clearFbIngest();
		// T-078 Fase 3: sin usuario no hay LiveVideo vigente que
		// reenviar; parada limpia del relay.
		if (relay_)
			relay_->stop();
		saveStore(); // drop snapshots; Independent records kept
		refreshManagedLoginUi();
		refreshContentVisibility();
	});
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
	// PoC ingestión: memoria de sesión, se olvida al desconectar.
	if (p == meta::Platform::Facebook) {
		clearFbIngest();
		clearFbVerify();
		// T-078 Fase 3: parada limpia del relay (el forward moria
		// con esa ingesta; sin tocar Aitum/OBS).
		if (relay_)
			relay_->stop();
	}
	setStatus(p, tr("Not connected"));
	setResult(p, true, tr("disconnected"));
	saveStore(); // drop the snapshot; Independent records untouched
	// Fase 4: sin login solo hay limpieza local (sin red). Con login,
	// desconexión remota best-effort con auth doble.
	if (!managedAuth_ || !managedAuth_->hasUserSession())
		return;
	managedAuth_->ensureSession([this, p](backend_auth::Result r) {
		if (r != backend_auth::Result::Ok)
			return; // local state already cleared
		managedAuth_->apiPostUser(QStringLiteral("/connect/%1/disconnect").arg(
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
	// Fase 4: borrado total también exige login (acción Managed).
	if (!requireManagedLoginMsg())
		return;
	eraseButton_->setEnabled(false);
	managedAuth_->ensureSession([this](backend_auth::Result r) {
		if (r != backend_auth::Result::Ok) {
			eraseButton_->setEnabled(true);
			generalMsg_->setText(
				tr("Data erasure failed: backend unreachable."));
			return;
		}
		managedAuth_->apiPostUser(
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
				// PoC ingestión: borrado total Managed también
				// olvida la URL de sesión.
				clearFbIngest();
				// T-078 Fase 3: parada limpia del relay.
				if (relay_)
					relay_->stop();
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

// --- Fase 5b: borrado de CUENTA (/users/erase) ---------------------------
// Distinto de POST /privacy/erase (F-C2, por instalación): este borra el
// usuario con TODAS sus instalaciones (cascada en servidor, revoke
// best-effort, idempotente). La respuesta lleva solo conteos por
// tabla + revocaciones por proveedor (jamás secretos). Tras el 200 se
// olvidan la sesión de usuario en memoria + snapshots Managed +
// backendInstall huérfano; Independent intacto por construcción.
void MetadataDock::onEraseAccount()
{
	using P = meta::Platform;
	if (!isManaged() || !eraseAccountButton_)
		return;
	// Exige login (acción Managed): sin user-session, mensaje exacto y
	// CERO red (sin fallback a Independent).
	if (!requireManagedLoginMsg())
		return;
	eraseAccountButton_->setEnabled(false);
	generalMsg_->setText(tr("Erasing account…"));
	managedAuth_->ensureSession([this](backend_auth::Result r) {
		if (!isManaged()) {
			eraseAccountButton_->setEnabled(true);
			return; // user switched mode
		}
		if (r != backend_auth::Result::Ok) {
			eraseAccountButton_->setEnabled(true);
			generalMsg_->setText(
				tr("Account erasure failed: backend "
				   "unreachable."));
			return;
		}
		managedAuth_->apiPostUser(
			QString::fromLatin1(meta::accountErasePath()),
			QJsonObject(),
			[this](const backend_auth::Client::ApiReply &rep) {
				eraseAccountButton_->setEnabled(true);
				if (!isManaged())
					return; // user switched mode
				if (rep.result ==
				    backend_auth::Result::Unauthorized) {
					// Auth doble normalizada: la sesión
					// cayó, re-login (mensaje exacto).
					generalMsg_->setText(
						meta::managedLoginRequiredMessage());
					refreshManagedLoginUi();
					return;
				}
				if (rep.result !=
				    backend_auth::Result::Ok) {
					obs_log(LOG_WARNING,
						"account erase failed: "
						"result=%d http=%d",
						static_cast<int>(rep.result),
						rep.http);
					generalMsg_->setText(
						tr("Account erasure failed "
						   "(retry or Disconnect "
						   "each account)."));
					return;
				}
				// 200: el servidor ya borró usuario +
				// instalaciones + sesiones (incluida la
				// llamante). Olvidar todo lo Managed local.
				managedAuth_->clearUserSession();
				using P = meta::Platform;
				for (P p : {P::Twitch, P::YouTube, P::Kick,
					    P::Facebook}) {
					ManagedConn &m = managedAccount(p);
					m.connected = false;
					m.userId.clear();
					m.display.clear();
					setStatus(p, tr("Not connected"));
					setResult(p, true,
						  tr("account erased"));
				}
				broadcastCombo_->clear();
				// PoC ingestión: olvida la URL de sesión.
				clearFbIngest();
				// T-078 Fase 3: sin cuenta no hay LiveVideo
				// vigente; parada limpia del relay.
				if (relay_)
					relay_->stop();
				// Las filas installation ya no existen en el
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
				// El bearer en memoria murió con sus
				// sessions: olvidarlo sin revocar (el
				// servidor ya lo borró).
				managedAuth_->revokeSession(
					[](backend_auth::Result) {});
				// Solo conteos por tabla/proveedor: el cuerpo
				// solo trae {erased:{...}, revoked:{p:bool}}.
				const QJsonObject erased =
					rep.body
						.value(QStringLiteral(
							"erased"))
						.toObject();
				const QJsonObject revoked =
					rep.body
						.value(QStringLiteral(
							"revoked"))
						.toObject();
				QStringList rev;
				for (auto it = revoked.begin();
				     it != revoked.end(); ++it) {
					if (it.value().toBool())
						rev << it.key();
				}
				QString msg =
					meta::accountEraseCountsMessage(
						erased.value(QStringLiteral(
							       "users"))
							.toInt(),
						erased.value(QStringLiteral(
							       "installations"))
							.toInt(),
						erased.value(QStringLiteral(
							       "sessions"))
							.toInt(),
						erased.value(QStringLiteral(
							       "connections"))
							.toInt(),
						erased.value(QStringLiteral(
							       "tokens"))
							.toInt());
				if (rev.isEmpty())
					msg += tr(" Remote revocations: "
						  "none.");
				else
					msg += tr(" Revoked remotely: %1.")
						       .arg(rev.join(
							       QStringLiteral(
								       ", ")));
				generalMsg_->setText(msg);
				refreshManagedLoginUi();
				refreshContentVisibility();
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
	// Fase 4: gate login previo (sin red ni fallback).
	if (!requireManagedLogin(P::YouTube))
		return;
	refreshButton_->setEnabled(false);
	managedAuth_->apiGetUser(QStringLiteral("/metadata/youtube/broadcasts"),
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
					     if (rep.result ==
						 backend_auth::Result::
							 Unauthorized)
						     noteManagedAuthFailure(
							     P::YouTube);
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
					     // T-076: el backend ya ordena (activo, proximos por
					     // fecha, terminados). Mostrar estado para elegir.
					     const QString st = it.value(
						     QStringLiteral("status"))
							.toString();
					     if (id.isEmpty())
						continue;
					     broadcastCombo_->addItem(
						     st.isEmpty()
							     ? QStringLiteral("%1 (%2)")
							           .arg(title, id)
							     : QStringLiteral("%1 [%2] (%3)")
							           .arg(title, st, id),
						     id);
				     }
					     // T-076: seleccion lista por defecto (primero =
					     // activo o proximo mas cercano).
					     if (broadcastCombo_->count() > 0)
						broadcastCombo_->setCurrentIndex(0);
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
	// Fase 4: gate login previo (sin red ni fallback).
	if (!requireManagedLogin(p))
		return;
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
	managedAuth_->apiPostUser(QStringLiteral("/metadata/youtube"), body,
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
				      if (autoRetryManagedSession(p, rep.http))
					      return;
				      if (oc == meta::Outcome::AuthRequired) {
					      // Auth doble normalizada:
					      // re-login (mensaje exacto).
					      noteManagedAuthFailure(p);
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
	// Fase 4: gate login previo (sin red ni fallback).
	if (!requireManagedLogin(p))
		return;
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
	managedAuth_->apiPostUser(QStringLiteral("/metadata/twitch"), body,
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
				      if (autoRetryManagedSession(p, rep.http))
					      return;
				      if (oc == meta::Outcome::AuthRequired) {
					      // Auth doble normalizada:
					      // re-login (mensaje exacto).
					      noteManagedAuthFailure(p);
				      }
				      finishPlatform(
					      p, false,
					      meta::userMessage(oc, p));
				      startApplyNext();
			      });
}

// --- helpers ----------------------------------------------------------

// T-075: one silent plugin-session retry. Returns true when the retry
// was scheduled (caller must return without finishing the platform).
bool MetadataDock::autoRetryManagedSession(meta::Platform p, int http)
{
	if (http != 0 || retried_)
		return false;
	retried_ = true;
	obs_log(LOG_INFO, "managed apply auto-retry session: %s",
		meta::platformName(p));
	managedAuth_->ensureSession([this, p](backend_auth::Result sr) {
		if (!isManaged())
			return;
		if (sr != backend_auth::Result::Ok) {
			ManagedConn &mm = managedAccount(p);
			mm.connected = false;
			mm.userId.clear();
			mm.display.clear();
			setStatus(p, tr("Needs reconnection."));
			saveStore();
			finishPlatform(
				p, false,
				meta::userMessage(meta::Outcome::AuthRequired,
						  p));
			startApplyNext();
			return;
		}
		if (p == meta::Platform::YouTube)
			startManagedYouTubeApply();
		else if (p == meta::Platform::Twitch)
			startManagedTwitchApply();
		else if (p == meta::Platform::Kick)
			startManagedKickApply();
		else
			startManagedFacebookApply();
	});
	return true;
}

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
	// labels, no secrets). Plan v3 Fase 1: este restore NUNCA arranca el
	// ciclo automatico por si solo (nada se auto-fetch aqui); solo la
	// revalidacion fresca de silentRevalidateManaged (rama "connected")
	// o finishManagedConnected invocan fbAutoStartFromConnected, que
	// fija fbAutoActive_ DENTRO, post-GET fresco.
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
		// PoC ingestión: el secure_stream_url SÍ se olvida siempre
		// (solo sesión, jamás persistido).
		clearFbIngest();
		// T-078 Fase 3: el forward apuntaba a esa ingesta; parada
		// limpia del relay al desconectar (sin tocar Aitum/OBS).
		if (relay_)
			relay_->stop();
		// La verificacion es de sesion: muere con la conexion.
		clearFbVerify();
		fbTargetCombo_->clear();
		fbPageTokens_.clear();
		fbTarget_.clear();
		if (fbStateLabel_)
			fbStateLabel_->setText(tr("Estado: —"));
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
	// Fase 4: gate login previo en Managed (mensaje exacto, cero
	// red). En Independent es no-op (sin cambios).
	if (!requireManagedLogin(meta::Platform::YouTube))
		return;
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
	// Fase 4: gate login previo en Managed (mensaje exacto, cero
	// red). En Independent es no-op (sin cambios).
	if (!requireManagedLogin(meta::Platform::Facebook))
		return;
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
	// Plan v3 Fase 1: cambio de ID limpia la sesion GoLive propia
	// (el LIVE en seguimiento ya no es el vigente; jamas terminar lo
	// ajeno) y cancela el refire diferido del anterior.
	if (fbAutoGoLiveSession_ && clean != fbAutoGoLiveId_.trimmed())
		clearFbGoLiveSession();
	if (!fbRefire_.liveId.isEmpty() &&
	    clean != fbRefire_.liveId.trimmed())
		fbCancelRefire();
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
	// T-078 Fase B: el crudo manda en el resumen compuesto/visibilidad
	// (vista, sin secretos); se guarda antes del guard de etiqueta.
	fbLastStatus_ = status;
	if (!fbStateLabel_)
		return;
	fbStateLabel_->setText(
		tr("Estado: %1").arg(fbPersonStateWord(status)));
	updateFbHappyPath();
}

void MetadataDock::setFbVerifyLabel(const QString &text)
{
	if (fbVerifyLabel_)
		fbVerifyLabel_->setText(text);
	// T-078 Fase B: Verify manda en GoLive/resumen (visibilidad y motivo).
	updateFbHappyPath();
}

void MetadataDock::clearFbVerify()
{
	fbExpectedTitle_.clear();
	fbExpectedDesc_.clear();
	fbVerifyId_.clear();
	fbLastPrivacy_.clear();
	fbGoLiveConfirm_ = false;
	fbVerify_ = {meta::FbVerify::Blocked,
		     QStringLiteral("not-verified")};
	setFbVerifyLabel(tr("Comprobación: —"));
}

// Cambio A: sincroniza la referencia con lo ENVIADO en un update
// exitoso para el MISMO liveId (el de la request, no otro). Si la
// respuesta trae lectura autoritativa se prefiere (la elige el
// llamante); si no, los enviados. Deja verificacion pendiente:
// Blocked write-accepted + etiqueta, jamas Verified aqui. Un mismatch
// real posterior sigue bloqueando en la siguiente lectura. Sin red,
// sin secretos, sin valores en logs; secure_stream_url intacto.
void MetadataDock::noteFbUpdated(const QString &liveId,
				 const QString &title, const QString &desc)
{
	if (liveId.isEmpty())
		return;
	fbExpectedTitle_ = title;
	fbExpectedDesc_ = desc;
	fbVerifyId_ = liveId;
	fbLastPrivacy_.clear();
	fbVerify_ = {meta::FbVerify::Blocked,
		     QStringLiteral("write-accepted")};
	setFbVerifyLabel(
		tr("Guardado — pulsa Comprobar antes de salir al aire."));
}

// Cambio B: la relectura encadenada tras go_live manda. Solo el estado
// LIVE leido (LIVE/LIVE_NOW) afirma publicado; otro estado queda no
// confirmado y sigue bloqueado (reintentar exige nueva verificacion).
void MetadataDock::reportFbGoLiveObserved(const QString &status)
{
	fbGoLiveConfirm_ = false;
	setFbState(status);
	if (meta::fbLiveState(status) == meta::FbLiveState::Live) {
		// Plan v3 Fase 1: LIVE confirmado tras GoLive = LIVE
		// iniciado por el plugin en esta sesion (guarda de
		// auto-End; se limpia en End/Delete/Adopt/mode-switch/
		// cambio de ID).
		noteFbGoLiveSession(currentFbLiveId());
		generalMsg_->setText(tr("Estás al aire."));
	} else {
		generalMsg_->setText(
			tr("No pude confirmar que saliste al aire — abre Avanzado "
			   "y pulsa Comprobar."));
	}
}

void MetadataDock::runFbVerification(const QString &id,
				     const QString &actTitle,
				     const QString &actDesc,
				     const QString &actStatus,
				     const QString &actPrivacy)
{
	const QString rid = id.trimmed();
	// Solo el video pendiente del ultimo create se verifica contra su
	// expected; cualquier otro ID es solo lectura (sin marcar preparado).
	if (!rid.isEmpty() && !fbVerifyId_.isEmpty() &&
	    rid == fbVerifyId_ && !fbExpectedTitle_.isEmpty()) {
		fbVerify_ = meta::facebookVerifyPrepared(
			fbExpectedTitle_, fbExpectedDesc_, actTitle,
			actDesc, actStatus, actPrivacy);
		fbVerifyId_ = rid;
		fbLastPrivacy_ = actPrivacy;
		if (fbVerify_.outcome == meta::FbVerify::Verified) {
			setFbVerifyLabel(
				tr("Vídeo de trabajo listo ✓ / título ✓ / "
				   "descripción ✓"));
		} else if (fbVerify_.outcome ==
			   meta::FbVerify::AudienceUnverified) {
			if (actPrivacy.isEmpty())
				setFbVerifyLabel(tr("No pude confirmar quién podrá verlo"));
			else
				setFbVerifyLabel(tr("No pude confirmar quién podrá verlo"));
		} else if (fbVerify_.reason ==
			   QStringLiteral("status-missing")) {
			setFbVerifyLabel(tr("Falta el estado — pulsa Comprobar"));
		} else if (fbVerify_.reason ==
			   QStringLiteral("status-not-unpublished")) {
			setFbVerifyLabel(
				tr("El vídeo aún no está para salir — pulsa Comprobar"));
		} else {
			setFbVerifyLabel(
				tr("El título no coincide — revísalo y pulsa Aplicar"));
		}
		return;
	}
	if (fbVerifyId_.isEmpty() && fbExpectedTitle_.isEmpty()) {
		// Lectura sin expected (ID pegado antiguo): valores reales,
		// sin marcar preparado; audiencia ausente nunca es exito.
		fbVerify_ = {meta::FbVerify::Blocked,
			     QStringLiteral("no-expected")};
		fbLastPrivacy_ = actPrivacy;
		const QString st = fbPersonStateWord(actStatus);
		if (actPrivacy.isEmpty())
			setFbVerifyLabel(
				tr("%1 — no es el vídeo de trabajo.").arg(st));
		else
			setFbVerifyLabel(tr("%1 — no es el vídeo de trabajo.").arg(st));
		return;
	}
	// Expected de otro video: solo lectura, verify intacto.
	const QString st = fbPersonStateWord(actStatus);
	if (actPrivacy.isEmpty())
		setFbVerifyLabel(tr("%1 — no es el vídeo de trabajo.").arg(st));
	else
		setFbVerifyLabel(tr("%1 — no es el vídeo de trabajo.").arg(st));
}

// --- PoC ingestión (secure_stream_url del create, solo sesión) --------

void MetadataDock::setFbIngest(const QString &url)
{
	// Sin logs ni persistencia: el valor vive en fbIngest_ y en el
	// QLineEdit (Password) hasta disconnect/delete/mode-switch.
	fbIngest_ = url;
	if (fbIngestEdit_) {
		fbIngestEdit_->setText(url);
		fbIngestEdit_->setEchoMode(QLineEdit::Password);
	}
	if (fbIngestShowButton_)
		fbIngestShowButton_->setText(tr("Mostrar"));
	refreshContentVisibility();
}

void MetadataDock::clearFbIngest()
{
	// Olvido directo (oculta sin refresh global: los llamantes ya
	// están en transición de estado).
	fbIngest_.clear();
	if (fbIngestEdit_) {
		fbIngestEdit_->clear();
		fbIngestEdit_->setEchoMode(QLineEdit::Password);
		fbIngestEdit_->setVisible(false);
	}
	if (fbIngestLabel_)
		fbIngestLabel_->setVisible(false);
	if (fbIngestShowButton_) {
		fbIngestShowButton_->setText(tr("Mostrar"));
		fbIngestShowButton_->setVisible(false);
	}
	if (fbIngestCopyButton_)
		fbIngestCopyButton_->setVisible(false);
	// T-078 Fase B: la ingesta vive en Avanzado; re-evaluar su
	// visibilidad (colapsado => oculta aunque haya valor).
	updateFbHappyPath();
}

void MetadataDock::onFbIngestToggled()
{
	if (!fbIngestEdit_ || !fbIngestShowButton_ ||
	    fbIngest_.isEmpty())
		return;
	const bool showing =
		fbIngestEdit_->echoMode() == QLineEdit::Normal;
	fbIngestEdit_->setEchoMode(showing ? QLineEdit::Password
					  : QLineEdit::Normal);
	fbIngestShowButton_->setText(showing ? tr("Mostrar") : tr("Ocultar"));
}

void MetadataDock::onFbIngestCopy()
{
	if (fbIngest_.isEmpty())
		return;
	if (QClipboard *cb = QApplication::clipboard())
		cb->setText(fbIngest_);
	// Sin valor en el mensaje ni en logs: confirmación genérica.
	generalMsg_->setText(
		tr("Copiado — pégalo en tu programa a mano"));
}

void MetadataDock::onFbStatus()
{
	// Fase 4: gate login previo en Managed (no-op en Independent).
	if (!requireManagedLogin(meta::Platform::Facebook))
		return;
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
	// Fase 4: gate login previo en Managed (no-op en Independent).
	if (!requireManagedLogin(meta::Platform::Facebook))
		return;
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
	// Fase 4: gate login previo en Managed (no-op en Independent;
	// antes de cualquier confirmación u operación remota).
	if (!requireManagedLogin(meta::Platform::Facebook))
		return;
	const QString liveId = currentFbLiveId();
	if (liveId.isEmpty()) {
		generalMsg_->setText(meta::userMessage(
			meta::Outcome::NotFound, meta::Platform::Facebook));
		return;
	}
	// Gate previo a cualquier confirmacion u operacion remota. Reglas
	// que se mantienen: mismo ID que el verificado, estado leido
	// UNPUBLISHED y titulo/descripcion coincidentes. La lectura con
	// fallo, otro estado, otro ID o mismatch siguen bloqueando.
	// Excepcion con consentimiento: si fbVerify_ es AudienceUnverified
	// SOLO por privacy ausente/vacia (Meta no devolvio privacy, resto
	// ya verificado por construccion de facebookVerifyPrepared) se
	// pide confirmacion explicita y, solo con Si, se envia go_live
	// para ese MISMO ID por el camino existente (sin cambiar su
	// payload). Privacy explicita distinta de EVERYONE sigue
	// bloqueando. Sin LIVE_NOW en estos caminos.
	if (fbVerifyId_.isEmpty() || liveId != fbVerifyId_) {
		QString motive;
		if (fbVerifyId_.isEmpty())
			motive = tr("todavía no hay vídeo comprobado");
		else
			motive = tr("no es el vídeo comprobado");
		generalMsg_->setText(
			tr("Aún no está comprobado — abre Avanzado y pulsa "
			   "Comprobar."));
		setFbVerifyLabel(tr("Pendiente: %1").arg(motive));
		return;
	}
	if (fbVerify_.outcome == meta::FbVerify::Verified) {
		QMessageBox::StandardButton ok = QMessageBox::warning(
			this, tr("Salir al aire en Facebook"),
			tr("Tu vídeo saldrá en directo y podrá verlo tu público. "
			   "¿Seguir?"),
			QMessageBox::Yes | QMessageBox::No,
			QMessageBox::No);
		if (ok != QMessageBox::Yes)
			return;
	} else if (fbVerify_.outcome ==
			   meta::FbVerify::AudienceUnverified &&
		   fbVerify_.reason ==
			   QStringLiteral("privacy-unverified")) {
		// Plan v3 (vista): el aviso de audiencia se puede ocultar con
		// el check global de Avanzado (defecto OFF); solo salta ese
		// aviso, jamas cambia el envio (mismo camino).
		if (!fbAudienceNoticeHidden()) {
			QMessageBox::StandardButton ok = QMessageBox::warning(
				this, tr("Salir al aire sin confirmar el público"),
				tr("No pude confirmar quién podrá verlo: podría no ser "
				   "público. Revísalo en Facebook. ¿Seguir?"),
				QMessageBox::Yes | QMessageBox::No,
				QMessageBox::No);
			if (ok != QMessageBox::Yes)
				return; // sin operacion remota, estado intacto
		}
	} else if (fbVerify_.reason == QStringLiteral("mismatch")) {
		// Error 1 (título): el único botón es Aplicar.
		generalMsg_->setText(tr("Revisa el título y pulsa Aplicar."));
		setFbVerifyLabel(
			tr("El título no coincide — revísalo y pulsa Aplicar"));
		return;
	} else {
		// Error 3 (conexión): el único botón es Comprobar (Avanzado).
		generalMsg_->setText(
			tr("Se cortó la conexión — abre Avanzado y pulsa Comprobar."));
		setFbVerifyLabel(tr("Pendiente: pulsa Comprobar"));
		return;
	}
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
	// Fase 4: gate login previo en Managed (no-op en Independent;
	// antes de cualquier confirmación u operación remota).
	if (!requireManagedLogin(meta::Platform::Facebook))
		return;
	const QString liveId = currentFbLiveId();
	if (liveId.isEmpty()) {
		generalMsg_->setText(meta::userMessage(
			meta::Outcome::NotFound, meta::Platform::Facebook));
		return;
	}
	QMessageBox::StandardButton ok = QMessageBox::question(
		this, tr("Terminar el directo"),
			tr("¿Terminar el directo? El vídeo queda guardado salvo que "
			   "lo borres."),
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
	// Fase 4: gate login previo en Managed (no-op en Independent;
	// antes de cualquier confirmación u operación remota).
	if (!requireManagedLogin(meta::Platform::Facebook))
		return;
	const QString liveId = currentFbLiveId();
	if (liveId.isEmpty()) {
		generalMsg_->setText(meta::userMessage(
			meta::Outcome::NotFound, meta::Platform::Facebook));
		return;
	}
	QMessageBox::StandardButton ok = QMessageBox::warning(
		this, tr("Borrar el vídeo de Facebook"),
			tr("¿Borrar el vídeo? No se puede deshacer."),
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

// --- facebook adopt explicito (asocia la señal entrante al LiveVideo
// correcto; jamas silencioso, jamas en Apply/Check/GoLive). Solo bindea
// el ID: sin LIVE optimista, sin Aitum, sin cambiar expected (queda
// pendiente de Apply+Check).
void MetadataDock::onFbAdopt()
{
	// Fase 4: gate login previo en Managed (no-op en Independent).
	if (!requireManagedLogin(meta::Platform::Facebook))
		return;
	if (isManaged()) {
		if (!managedAccount(meta::Platform::Facebook).connected) {
			generalMsg_->setText(meta::userMessage(
				meta::Outcome::AuthRequired,
				meta::Platform::Facebook));
			return;
		}
		startManagedFbOp(QStringLiteral("adopt"), QString());
		return;
	}
	if (!fb_.connected) {
		generalMsg_->setText(meta::userMessage(
			meta::Outcome::AuthRequired,
			meta::Platform::Facebook));
		return;
	}
	startFbAdoptIndependent();
}

void MetadataDock::startFbAdoptIndependent()
{
	const QString target =
		fbTarget_.isEmpty() ? QStringLiteral("me") : fbTarget_;
	QString token = fb_.access;
	if (target != QStringLiteral("me")) {
		if (!fbPageTokens_.contains(target)) {
			generalMsg_->setText(meta::userMessage(
				meta::Outcome::AuthRequired,
				meta::Platform::Facebook));
			return;
		}
		token = fbPageTokens_.value(target);
	}
	QUrl url(QString::fromLatin1(kFbGraph) + QStringLiteral("/") +
		 target + QStringLiteral("/live_videos"));
	QUrlQuery q;
	q.addQueryItem(QStringLiteral("fields"),
		       QStringLiteral("id,title,status"));
	q.addQueryItem(QStringLiteral("limit"), QStringLiteral("25"));
	url.setQuery(q);
	generalMsg_->setText(tr("Looking for the active live video…"));
	sendGet(url, Op::FbAdopt, token);
}

void MetadataDock::startFbStatusIndependent(const QString &liveId) {
	fbStatusId_ = liveId;
	QUrl url(QString::fromLatin1(kFbGraph) + QStringLiteral("/") +
		 liveId);
	QUrlQuery q;
	// T-077 (Fase A): primer intento SIN privacy (paridad con
	// lab/facebook liveStatus). Meta suele responder 400 si se pide
	// privacy ilegible (F-093); sin pedirla el 200 trae valores
	// reales con audiencia NO verificada por lectura (ver
	// runFbVerification: "" nunca es Verified). FbStatusRetry queda
	// como relectura unica de compat ante error (mismo ID,
	// mismos campos, sin polling).
	q.addQueryItem(QStringLiteral("fields"),
		       meta::facebookStatusFields());
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
	// Nuevo create: limpia la verificacion previa y guarda el expected
	// (exactamente lo enviado) para verificar el estado real despues.
	clearFbVerify();
	fbExpectedTitle_ = title;
	fbExpectedDesc_ = descEdit_->toPlainText();
	const QString target =
		fbTarget_.isEmpty() ? QStringLiteral("me") : fbTarget_;
	QString token = fb_.access;
	if (target != QStringLiteral("me"))
		token = fbPageTokens_.value(target, token);
	QUrl url(QString::fromLatin1(kFbGraph) + QStringLiteral("/") +
		 target + QStringLiteral("/live_videos"));
	// T-077 (Fase A): body puro testeado; persistent solo con opt-in
	// explicito (defecto OFF). F-092: ni con ENABLE la URL es unica
	// (cada LiveVideo trae su propia ingesta): sin promesa en UI.
	const QJsonObject body = meta::facebookCreatePayload(
		title, descEdit_->toPlainText(), fbPersistent_);
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
	// Fase 4: gate login previo (status/broadcasts/ciclo de vida:
	// sin red ni fallback).
	if (!requireManagedLogin(meta::Platform::Facebook))
		return;
	QJsonObject body;
	body[QStringLiteral("op")] = op;
	if (!liveId.isEmpty())
		body[QStringLiteral("live_video_id")] = liveId;
	if (op == QStringLiteral("create")) {
		// Plan v3 Fase 1: override parametrizado (pre-creacion
		// tras corte: el titulo/desc llegan por parametro, SIN
		// mutar widgets en Managed). Un solo uso: se consume aqui.
		const QString cTitle = fbUseCreateOverride_
					   ? fbCreateTitleOverride_
					   : (titleEdit_ ? titleEdit_->text()
						       : QString());
		const QString cDesc = fbUseCreateOverride_
					  ? fbCreateDescOverride_
					  : (descEdit_ ? descEdit_->toPlainText()
						      : QString());
		fbUseCreateOverride_ = false;
		body[QStringLiteral("title")] = cTitle;
	body[QStringLiteral("description")] = cDesc;
		// T-077 (Fase A): persistent solo con opt-in explicito
		// (defecto OFF). El backend ya acepta persistent:true
		// (facebook.py create_live_video). F-092: sin promesa de
		// URL unica en UI ni comentarios.
		if (fbPersistent_)
			body[QStringLiteral("persistent")] = true;
		// Nuevo create: limpia la verificacion previa y guarda el
		// expected (exactamente lo enviado) para verificar despues.
		clearFbVerify();
		fbExpectedTitle_ = cTitle;
		fbExpectedDesc_ = cDesc;
	}
	generalMsg_->setText(tr("Facebook %1…").arg(op));
	managedAuth_->apiPostUser(
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
				// Plan v3 Fase 1: lectura fallida != confirmacion
				// fresca: el End pendiente se cancela (el proximo
				// ciclo de escucha lo reintentara si sigue caido).
				fbSigEndConfirm_ = false;
				// Fase A: el probe del arranque no inventa
				// estado. status fallido -> decide (404 =
				// crear sin borrar; otro fallo = diferir
				// honesto); create fallido -> fallback.
				if ((fbAutoActive_ || fbAutoArmed_ ||
				     fbEndAutoPending_) &&
				    op == QStringLiteral("status")) {
					const bool nf =
						(rep.http == 404 ||
						 meta::classifyStatus(
							 rep.http == 0
								 ? -1
								 : rep.http) ==
							 meta::Outcome::
								 NotFound);
					fbQuietStatus_ = false;
					fbAutoHandleRead(
						liveId, QString(),
						QString(), QString(),
						false, nf);
					return;
				}
				if ((fbAutoActive_ || fbAutoArmed_ ||
				     fbEndAutoPending_) &&
				    op == QStringLiteral("create")) {
					fbAutoHandleCreateFailed(
						fbAutoPrevStatus_);
					fbEndAutoPending_ = false;
					return;
				}
				// Fase A punto 2 (Managed): si el DELETE del
				// huerfano falla, se informa con el ID (sin
				// reintentar); el vigente queda intacto.
				if (op == QStringLiteral("delete") &&
				    !fbAutoDeleteId_.isEmpty()) {
					const QString prev = fbAutoDeleteId_;
					fbAutoDeleteId_.clear();
					generalMsg_->setText(
						tr("Facebook live video "
						   "prepared — previous video "
						   "%1 could not be removed "
						   "(delete it manually).")
							.arg(prev));
					return;
				}
				if (fbApplyChain_ &&
				    op == QStringLiteral("status")) {
					if (fbResult_ &&
					    !fbApplyChainMsg_.isEmpty())
						setResult(
							meta::Platform::
								Facebook,
							true,
							fbApplyChainMsg_ +
								tr(" — verification "
								   "read failed"));
					fbApplyChain_ = false;
					fbApplyChainId_.clear();
					fbApplyChainMsg_.clear();
					fbQuietStatus_ = false;
					return;
				}
				// T-077 (Fase A): el backend no reenvia el
				// detail interno (solo logs; el body publico
				// trae code generico). Si se pidio clave
				// persistente y el proveedor rechazo el
				// create (502 provider_rejected), el motivo
				// probable es facebook:
				// persistent-key-not-allowed: mensaje honesto
				// y condicional (reintentar con la opcion
				// off), sin afirmar el motivo como unico.
				const meta::Outcome oc =
					rep.result ==
							backend_auth::Result::
								NetworkError
						? meta::Outcome::NetworkError
						: meta::classifyStatus(
							  rep.http == 0 ? -1
									: rep.http);
				if (op == QStringLiteral("create") &&
				    fbPersistent_ && rep.http == 502)
					generalMsg_->setText(
						meta::
							fbPersistentNotAllowedMessage());
				else if (oc == meta::Outcome::RateLimited &&
					 (op == QStringLiteral("go_live") ||
					  op == QStringLiteral("end")))
					// F-097 (vista honesta, sin cambiar
					// logica): Meta limita re-handshakes
					// seguidos (~3 min observado); sin
					// prometer recuperacion ni latencia.
					generalMsg_->setText(
						meta::userMessage(
							oc,
							meta::Platform::
								Facebook) +
						tr(" Meta may limit quick "
						   "re-handshakes (~3 min "
						   "observed); wait before "
						   "retrying."));
				else
					generalMsg_->setText(
						meta::userMessage(
							oc,
							meta::Platform::
								Facebook));
				return;
			}
			const QJsonObject res =
				rep.body.value(QStringLiteral("result"))
					.toObject();
			// Adopt Managed (solo lectura, backend): outcome en
			// result.adopted. true + id -> bindea igual que
			// Independent; false + reason conocido -> bloqueado
			// con mensaje especifico (jamas enlaza); outcome
			// irreconocible -> fallo general clasificado.
			// Sin secure_stream_url ni privacy_fallback en adopt.
			if (op == QStringLiteral("adopt")) {
				const bool adopted =
					res.value(QStringLiteral("adopted")).toBool();
				const QString reason =
					res.value(QStringLiteral("reason")).toString();
				const QString aid =
					res.value(QStringLiteral("id"))
						.toString();
				if (adopted && !aid.isEmpty()) {
					setFbLiveId(aid);
					// Plan v3 Fase 1: Adopt manual: muere la sesion
					// GoLive propia (ver Adopt Independent).
					clearFbGoLiveSession();
					fbVerifyId_ = aid;
					setFbState(res.value(QStringLiteral(
									 "status"))
						.toString());
					generalMsg_->setText(
						tr("Adopted active live (%1) "
						   "— Check live status, then "
						   "Apply to update "
						   "title/description. Never "
						   "changes Aitum.")
							.arg(aid));
					return;
				}
				if (!adopted &&
				    reason ==
					    QStringLiteral("no-active-live")) {
					setFbState(QString());
					fbVerify_ = {meta::FbVerify::Blocked,
						     QStringLiteral(
							     "adopt-no-active")};
					setFbVerifyLabel(
						tr("No active LIVE video found — "
						   "Go live first (persistent key "
						   "or new dock video), then "
						   "Adopt."));
					generalMsg_->setText(
						tr("No active LIVE video found — "
						   "Go live first (persistent key "
						   "or new dock video), then "
						   "Adopt."));
					return;
				}
				if (!adopted &&
				    reason ==
					    QStringLiteral(
						    "ambiguous-active-live")) {
					const int n =
						res.value(QStringLiteral("count"))
							.toInt();
					setFbState(QString());
					fbVerify_ = {meta::FbVerify::Blocked,
						     QStringLiteral(
							     "adopt-ambiguous")};
					setFbVerifyLabel(
						tr("More than one LIVE video (%1) "
						   "— stop the extra one or paste "
						   "the ID manually.")
							.arg(n));
					generalMsg_->setText(
						tr("More than one LIVE video (%1) "
						   "— stop the extra one or paste "
						   "the ID manually.")
							.arg(n));
					return;
				}
				const meta::Outcome aoc =
					meta::classifyStatus(
						rep.http == 0
							? -1
							: rep.http);
				generalMsg_->setText(
					meta::userMessage(
						aoc,
						meta::Platform::
							Facebook));
				return;
			}
			const QString id =
				res.value(QStringLiteral("id")).toString();
			const QString st =
				res.value(QStringLiteral("status"))
					.toString();
			const QString rt =
				res.value(QStringLiteral("title")).toString();
			const QString rd = res.value(QStringLiteral("description"))
						   .toString();
			const QString rp = meta::facebookPrivacyValue(
				res.value(QStringLiteral("privacy")));
			if (!id.isEmpty() &&
			    (op == QStringLiteral("create") ||
			     op == QStringLiteral("status")))
				setFbLiveId(id);
			// El ID creado queda pendiente de verificacion contra
			// el expected guardado al crear.
			if (op == QStringLiteral("create") && !id.isEmpty())
				fbVerifyId_ = id;
			// Rama status: verifica el ID actual contra el
			// expected si existe (create reciente); sin expected
			// es solo lectura real, sin marcar preparado.
			// privacy_fallback (ausente=false por compat): Meta
			// no devolvio o no permitio privacy -> forzar ""
			// (ignorar cualquier privacy devuelta, sin reutilizar
			// previa) y etiquetar fallback; Go live bloqueado
			// (gate: solo Verified avanza).
			if (op == QStringLiteral("status")) {
				const QString rid =
					!id.isEmpty() ? id : liveId;
				const bool fbFallback =
					res.value(QStringLiteral(
							"privacy_fallback"))
						.toBool();
				runFbVerification(rid, rt, rd, st,
						  fbFallback ? QString()
							     : rp);
				if (fbFallback)
					setFbVerifyLabel(
						tr("Audience not verified "
						   "(Meta did not return/allow "
						   "privacy — fallback read)"));
				// Fase A punto 2 (Managed): el probe del
				// arranque decide con lo leido (una sola).
				if ((fbAutoActive_ || fbAutoArmed_) &&
				    !fbAutoDidCreate_) {
					fbQuietStatus_ = false;
					fbAutoHandleRead(rid, st, rt, rd,
							 true, false);
					return;
				}
				// Plan v3 Fase 1: confirmacion Graph fresca para
				// auto-End Managed: solo el MISMO ID decide; LIVE
				// con emisor caido => End; ya terminado => solo
				// constancia (VOD conservado, sin End redundante).
				if (fbSigEndConfirm_ &&
				    !fbAutoGoLiveId_.isEmpty() &&
				    rid.trimmed() ==
					    fbAutoGoLiveId_.trimmed()) {
					fbSigEndConfirm_ = false;
					if (meta::fbLiveState(st) ==
					    meta::FbLiveState::Live) {
						generalMsg_->setText(
							tr("Signal lost -- ending live "
							   "video."));
						fbQuietStatus_ = false;
						startManagedFbOp(
							QStringLiteral("end"),
							rid);
						return;
					}
					generalMsg_->setText(
						tr("Signal lost -- live already "
						   "ended."));
					fbQuietStatus_ = false;
					clearFbGoLiveSession();
					return;
				}
				// Fase A punto 3 (Managed): cadena silenciosa
				// tras Apply OK; restaura su mensaje + nota
				// honesta de audiencia (jamas verificado).
				if (fbApplyChain_ &&
				    !fbApplyChainId_.isEmpty() &&
				    rid.trimmed() ==
					    fbApplyChainId_.trimmed()) {
					const QString note =
						meta::fbApplyChainNote(
							fbVerify_);
					if (fbResult_ &&
					    !fbApplyChainMsg_.isEmpty())
						setResult(
							meta::Platform::
								Facebook,
							true,
							fbApplyChainMsg_ +
								note);
					fbApplyChain_ = false;
					fbApplyChainId_.clear();
					fbApplyChainMsg_.clear();
					fbQuietStatus_ = false;
					return;
				}
			}
			// PoC ingestión: solo en create (el backend la
			// reenvía únicamente ahí). Un create nuevo sin URL
			// limpia el valor anterior. Jamas en logs.
			// T-078 Fase 3: con ingesta en memoria se re-fija el
			// forward en caliente (PATCH :9997, sin reinicio);
			// respeta cooldown F-097 sin reintentos.
			if (op == QStringLiteral("create")) {
				const QString ingest =
					res.value(QStringLiteral(
							 "secure_stream_url"))
						.toString();
				setFbIngest(ingest);
				if (!ingest.isEmpty() && relay_)
					fbRefireOrDefer(!id.isEmpty() ? id : liveId, ingest);
			}
			// go_live Managed: NO pintar res.status ni LIVE optimista;
			// estado desconocido/pendiente hasta la lectura encadenada
			// del mismo ID (ella confirma o deja bloqueado).
			if (op == QStringLiteral("go_live")) {
				setFbState(QString());
			} else if (!st.isEmpty()) {
				setFbState(st);
				if (op == QStringLiteral("create"))
					setFbVerifyLabel(
						tr("Created — press Check live "
						   "status to verify before Go "
						   "live."));
			} else if (op == QStringLiteral("create")) {
				// Sin status en la respuesta: NO suponer
				// estado. Lectura encadenada (una vez, sin
				// polling) que deja el indicador real sin
				// pisar el mensaje del create.
				const QString cid = !id.isEmpty()
							    ? id
							    : liveId;
				if (!cid.isEmpty()) {
					fbQuietStatus_ = true;
					QJsonObject sbody;
					sbody[QStringLiteral("op")] =
						QStringLiteral("status");
					sbody[QStringLiteral(
						"live_video_id")] = cid;
					managedAuth_->apiPostUser(
						QStringLiteral(
							"/metadata/facebook"),
						sbody,
						[this, cid](const backend_auth::
							       Client::
							       ApiReply
								       &srep) {
							if (!isManaged())
								return;
							if (srep.result ==
							    backend_auth::
								    Result::
									    Ok) {
								const QJsonObject
									sres = srep.body
										       .value(QStringLiteral(
											       "result"))
										       .toObject();
								const QString sst =
									sres.value(QStringLiteral(
											"status"))
										.toString();
								setFbState(sst);
								// Encadenada tras create:
								// verifica contra el
								// expected (nunca
								// inventa UNPUBLISHED).
								// privacy_fallback
								// (ausente=false):
								// forzar "" y etiquetar
								// fallback (bloquea Go
								// live, como status).
								QString srid =
									sres.value(QStringLiteral(
											"id"))
										.toString();
								if (srid.isEmpty())
									srid = cid;
								const bool sfbFallback =
									sres.value(QStringLiteral(
											"privacy_fallback"))
										.toBool();
								runFbVerification(
									srid,
									sres.value(QStringLiteral(
											"title"))
										.toString(),
									sres.value(QStringLiteral(
											"description"))
										.toString(),
									sst,
									sfbFallback
										? QString()
										: meta::facebookPrivacyValue(
											sres.value(QStringLiteral(
												"privacy"))));
								if (sfbFallback)
									setFbVerifyLabel(
										tr("Audience not verified "
										   "(Meta did not return/allow "
										   "privacy — fallback read)"));
							}
							fbQuietStatus_ =
								false;
						});
				}
			}
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
					// Sin LIVE optimista: pendiente hasta leer el
					// mismo ID una vez (confirma o bloquea).
					// F-097: sin reintento agresivo tras
					// go_live (cooldown ~3min de Meta ante
					// re-handshakes; la vista redacta el
					// aviso, aqui solo constancia).
					setFbState(QString());
				else {
					// F-098: End -> LIVE_STOPPED genera VOD
					// salvo borrado explicito.
					setFbState(QStringLiteral("VOD"));
					// T-078 Fase 3: la senal se retira;
					// limpieza del forward ([]) sin cooldown.
					if (relay_)
						relay_->clearForward();
					fbCancelRefire();
					clearFbGoLiveSession();
				}
				fbQuietStatus_ = true;
				QJsonObject sbody;
				sbody[QStringLiteral("op")] =
					QStringLiteral("status");
				sbody[QStringLiteral("live_video_id")] =
					doneId;
				managedAuth_->apiPostUser(
					QStringLiteral("/metadata/facebook"),
					sbody,
					[this, doneId, op](
						const backend_auth::Client::
							ApiReply &srep) {
						if (!isManaged())
							return;
						if (srep.result !=
						    backend_auth::Result::
							    Ok) {
							// go_live sin lectura: NADA
							// confirmado. Estado pendiente,
							// verificacion bloqueada (sin
							// reutilizar privacy previa) y
							// mensaje explicito. end intacto.
							if (op == QStringLiteral(
								    "go_live")) {
								setFbState(
									QString());
								fbLastPrivacy_
									.clear();
								fbVerify_ = {
									meta::FbVerify::
										Blocked,
									QStringLiteral(
										"go-live-unconfirmed")};
								setFbVerifyLabel(
									tr("Blocked: go-live not confirmed (status read failed)"));
								generalMsg_->setText(
									tr("Facebook accepted go-live but publication NOT confirmed (status=%1) — still blocked; verify again.")
										.arg(tr("unknown")));
							}
							fbQuietStatus_ = false;
							return;
						}
						const QJsonObject sres =
							srep.body
								.value(QStringLiteral(
									"result"))
								.toObject();
						const QString sst =
							sres.value(QStringLiteral(
									"status"))
								.toString();
						setFbState(sst);
						// Encadenada tras on/off: deja el
						// indicador y la verificacion
						// reales (LIVE/VOD bloquea un Go
						// live posterior, como debe ser).
						// privacy_fallback
						// (ausente=false): forzar "" y
						// etiquetar fallback (bloquea Go
						// live, como status).
						const bool sfbFallback =
							sres.value(QStringLiteral(
									"privacy_fallback"))
								.toBool();
						runFbVerification(
							doneId,
							sres.value(QStringLiteral(
									"title"))
								.toString(),
							sres.value(QStringLiteral(
									"description"))
								.toString(),
							sst,
							sfbFallback
								? QString()
								: meta::facebookPrivacyValue(
									sres.value(QStringLiteral(
										"privacy"))));
						if (sfbFallback)
							setFbVerifyLabel(
								tr("Audience not verified "
								   "(Meta did not return/allow "
								   "privacy — fallback read)"));
						// Cambio B (solo go_live): la
						// relectura manda. Solo LIVE leido
						// afirma publicado; otro estado
						// queda no confirmado y sigue
						// bloqueado (reintentar exige
						// nueva verificacion). end/delete
						// intactos.
						if (op == QStringLiteral(
							    "go_live")) {
							if (meta::fbLiveState(
								    sst) ==
							    meta::FbLiveState::
								    Live) {
								// Plan v3 Fase 1: LIVE confirmado
								// tras go_live Managed = sesion
								// propia (guarda de auto-End).
								noteFbGoLiveSession(doneId);
								generalMsg_->setText(
									tr("Facebook is "
									   "LIVE."));
							}
							else
								generalMsg_->setText(
									tr("Facebook go-live not confirmed (status=%1) — still blocked; verify again before retrying.")
										.arg(sst.isEmpty()
											     ? tr("unknown")
											     : sst));
						}
						fbQuietStatus_ = false;
					});
			}
			if (op == QStringLiteral("delete")) {
				// Limpieza F-075: retirar el ID del combo y
				// del store persistido.
				if (!id.isEmpty()) {
					for (int i = fbLiveCombo_->count() - 1;
					     i >= 0; --i) {
						if (fbLiveCombo_->itemData(i)
							    .toString() == id ||
						    fbLiveCombo_->itemText(i)
							    .contains(id))
							fbLiveCombo_->removeItem(
								i);
					}
					if (store_) {
						secure::Data d;
						store_->load(d);
						if (d.facebookLiveId == id) {
							d.facebookLiveId
								.clear();
							store_->save(d);
						}
					}
				}
				// PoC ingestión: el objeto murió, la URL
				// muere con él (aunque falte el id).
				clearFbIngest();
				// T-078 Fase 3: sin LiveVideo no hay forward;
				// limpieza ([]) sin cooldown.
				if (relay_)
					relay_->clearForward();
					fbCancelRefire();
					clearFbGoLiveSession();
				// El objeto ya no existe: su verificacion tampoco.
				clearFbVerify();
				setFbState(QString());
			}
			// Fase A (Managed): create automatico del arranque o tras
			// End. Punto 2: mensaje honesto + limpieza del huerfano
			// (solo ese); punto 5: sin delete (el terminado queda
			// como VOD). Sin loops, sin reintentos.
			if (op == QStringLiteral("create") &&
			    fbEndAutoPending_) {
				fbEndAutoPending_ = false;
				fbAutoActive_ = false;
				generalMsg_->setText(
					tr("Facebook live ended — next video "
					   "prepared (%1).")
						.arg(id));
				return;
			}
			if (op == QStringLiteral("create") && fbAutoActive_) {
				fbAutoHandleCreated(id);
				return;
			}
			// Fase A (Managed): DELETE automatico del huerfano:
			// solo retira ESE ID del combo; el vigente, su
			// ingesta y su verificacion quedan intactos.
			if (op == QStringLiteral("delete") &&
			    !fbAutoDeleteId_.isEmpty()) {
				const QString prev = fbAutoDeleteId_;
				fbAutoDeleteId_.clear();
				for (int i = fbLiveCombo_->count() - 1;
				     i >= 0; --i) {
					if (fbLiveCombo_->itemData(i)
						    .toString() == prev ||
					    fbLiveCombo_->itemText(i).contains(
						    prev))
						fbLiveCombo_->removeItem(
							i);
				}
				saveStore();
				generalMsg_->setText(
					tr("Facebook live video prepared — "
					   "previous unpublished video %1 "
					   "removed.")
						.arg(prev));
				return;
			}
			// go_live Managed: nunca afirmar OK aqui; solo la lectura
			// encadenada del mismo ID confirma ("Facebook is
			// LIVE.") o deja bloqueado. Interino explicito.
			if (op == QStringLiteral("go_live"))
				generalMsg_->setText(
					tr("Facebook go-live request accepted — "
					   "confirming status…"));
			else if (op == QStringLiteral("end")) {
				// F-098 (vista honesta): End deja VOD salvo
				// borrado explicito.
				generalMsg_->setText(
					tr("Facebook end OK (VOD kept unless "
					   "you delete the video)."));
				// Fase A punto 5 (Managed): un solo create tras
				// End confirmado (canRefire o diferir honesto).
				fbEndAutoPrepare(doneId);
			} else
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
	// Fase 4: en Managed el Apply exige login ANTES de validar o
	// encolar (cero red, mensaje exacto). Independent intacto.
	if (isManaged() && managedAuth_ &&
	    !managedAuth_->hasUserSession()) {
		generalMsg_->setText(
			meta::managedLoginRequiredMessage());
		refreshManagedLoginUi();
		return;
	}
	const QString err = meta::validate(m, s);
	if (!err.isEmpty()) {
		generalMsg_->setText(err);
		return;
	}
	generalMsg_->clear();
	applyQueue_.clear();
	// T-076: solo entran plataformas conectadas en el modo activo.
	// Una marcada pero desconectada se salta en silencio (sin alarma
	// roja): la tarjeta ya se ve apagada y el Connect vive en ella.
	if (s.twitch && platformUsable(meta::Platform::Twitch))
		applyQueue_ << meta::Platform::Twitch;
	if (s.youtube && platformUsable(meta::Platform::YouTube))
		applyQueue_ << meta::Platform::YouTube;
	if (s.kick && platformUsable(meta::Platform::Kick))
		applyQueue_ << meta::Platform::Kick;
	if (s.facebook && platformUsable(meta::Platform::Facebook))
		applyQueue_ << meta::Platform::Facebook;
	if (applyQueue_.isEmpty()) {
		generalMsg_->setText(
			tr("Connect a platform first — disconnected platforms are skipped."));
		return;
	}
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
	// directa. Fase 4: además del provider conectado se exige la
	// user-session en memoria; sin ella, mensaje de login (no el
	// genérico de reconexión) y cero red.
	const bool managedLogged = isManaged() && managedAuth_ &&
				   managedAuth_->hasUserSession();
	const bool authorized =
		(isManaged() && (p == P::YouTube || p == P::Twitch ||
				 p == P::Kick || p == P::Facebook))
			? (managedLogged && managedAccount(p).connected)
			: account(p).connected;
	if (!authorized) {
		if (isManaged() && !managedLogged) {
			finishPlatform(p, false,
				       meta::managedLoginRequiredMessage());
		} else {
			finishPlatform(p, false,
				       meta::userMessage(meta::Outcome::AuthRequired,
							 p));
		}
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
		// Cambio A: captura lo ENVIADO (titulo/desc vigentes al
		// enviar) y el MISMO liveId para sincronizar la referencia
		// si el update tiene exito (el reply no trae el body
		// enviado; leer los widgets en el reply correria riesgo de
		// edicion intermedia).
		fbPendingApplyId_ = liveId;
		fbPendingApplyTitle_ = title;
		fbPendingApplyDesc_ = desc;
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
	// Fase 4: gate login previo (sin red ni fallback).
	if (!requireManagedLogin(p))
		return;
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
	managedAuth_->apiPostUser(QStringLiteral("/metadata/kick"), body,
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
				      if (autoRetryManagedSession(p, rep.http))
					      return;
				      if (oc == meta::Outcome::AuthRequired) {
					      // Auth doble normalizada:
					      // re-login (mensaje exacto).
					      noteManagedAuthFailure(p);
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
	// Fase 4: gate login previo (sin red ni fallback).
	if (!requireManagedLogin(P::Facebook))
		return;
	fbRefreshButton_->setEnabled(false);
	managedAuth_->apiGetUser(QStringLiteral("/metadata/facebook/broadcasts"),
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
					     if (rep.result ==
						 backend_auth::Result::
							 Unauthorized)
						     noteManagedAuthFailure(
							     P::Facebook);
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
	// Fase 4: gate login previo (sin red ni fallback).
	if (!requireManagedLogin(p))
		return;
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
	// Cambio A: captura lo ENVIADO para sincronizar la referencia si
	// el update tiene exito (el MISMO liveId de esta request).
	const QString sentTitle = titleEdit_->text();
	const QString sentDesc = descEdit_->toPlainText();
	QJsonObject body;
	body[QStringLiteral("live_video_id")] = liveId;
	body[QStringLiteral("title")] = sentTitle;
	body[QStringLiteral("description")] = sentDesc;
	if (!fbTarget_.isEmpty())
		body[QStringLiteral("target")] = fbTarget_;
	managedAuth_->apiPostUser(QStringLiteral("/metadata/facebook"), body,
			      [this, p, liveId, sentTitle, sentDesc](
				      const backend_auth::Client::ApiReply &rep) {
				      obs_log(LOG_INFO,
					      "managed apply reply: result=%d "
					      "http=%d",
					      static_cast<int>(rep.result),
					      rep.http);
				      if (!isManaged())
					      return; // user switched mode
				      if (rep.result ==
					  backend_auth::Result::Ok) {
					      // Cambio A: guarda los enviados
					      // (o la lectura autoritativa si
					      // la respuesta la trae), fija
					      // fbVerifyId_ y deja pendiente
					      // de Check live status. Sin
					      // tocar backend.
					      const QJsonObject res =
						      rep.body
							      .value(QStringLiteral(
								      "result"))
							      .toObject();
					      QString refTitle = sentTitle;
					      QString refDesc = sentDesc;
					      if (res.contains(QStringLiteral(
							      "title")))
						      refTitle =
							      res.value(QStringLiteral(
									      "title"))
								      .toString();
					      if (res.contains(QStringLiteral(
							      "description")))
						      refDesc =
							      res.value(QStringLiteral(
									      "description"))
								      .toString();
					      noteFbUpdated(liveId, refTitle,
							    refDesc);
					      finishPlatform(
						      p, true,
						      meta::userMessage(
							      meta::Outcome::Success,
							      p));
					      // Fase A punto 3 (Managed): tras Apply OK, verificacion silenciosa
					      // (patron fbQuietStatus_): status silencioso -> runFbVerification,
					      // sin pisar el mensaje de Apply.
					      if (managedAccount(meta::Platform::Facebook).connected)
					          fbChainSilentVerify(liveId);
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
				      if (autoRetryManagedSession(p, rep.http))
					      return;
				      if (oc == meta::Outcome::AuthRequired) {
					      // Auth doble normalizada:
					      // re-login (mensaje exacto).
					      noteManagedAuthFailure(p);
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
	case Op::FbStatusRetry:
	case Op::FbCreate:
	case Op::FbGoLive:
	case Op::FbEnd:
	case Op::FbDelete:
	case Op::FbAdopt:
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

	case Op::FbAdopt: {
		// Adopt explicito Independent (solo lectura, sin LIVE
		// optimista, sin Aitum): bindea el unico LIVE; bloqueos
		// no-active/ambiguous; error red/lista -> fallo general
		// (expected intacto, sin tocar verify).
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
			generalMsg_->setText(
				!spec.isEmpty()
					? spec
					: meta::userMessage(
						  oc, P::Facebook));
			obs_log(LOG_WARNING,
				"facebook adopt list failed http %d",
				http);
			return;
		}
		QList<meta::FbLiveItem> items;
		const QJsonArray data = replyJson(reply)
						.value(QStringLiteral("data"))
						.toArray();
		for (const auto &v : data) {
			const QJsonObject it = v.toObject();
			meta::FbLiveItem li;
			li.id =
				it.value(QStringLiteral("id")).toString();
			li.status = it.value(QStringLiteral("status"))
					    .toString();
			items.append(li);
		}
		const meta::FbAdoptResult ar =
			meta::facebookAdoptActive(items);
		if (ar.ok) {
			QString st;
			for (const auto &li : items) {
				if (li.id.trimmed() == ar.id) {
					st = li.status;
					break;
				}
			}
			setFbLiveId(ar.id);
			// Plan v3 Fase 1: Adopt vincula un ID manual, no un LIVE
			// iniciado por el plugin: la sesion GoLive propia muere
			// aqui aunque el ID coincida.
			clearFbGoLiveSession();
			// Sin expected: queda pendiente de Apply+Check
			// (el update sincroniza la referencia).
			fbVerifyId_ = ar.id;
			setFbState(st);
			generalMsg_->setText(
				tr("Adopted active live (%1) — Check live "
				   "status, then Apply to update "
				   "title/description. Never changes Aitum.")
					.arg(ar.id));
			obs_log(LOG_INFO, "facebook adopt ok");
			return;
		}
		if (ar.reason == QStringLiteral("no-active")) {
			generalMsg_->setText(
				tr("No active LIVE video found — Go live "
				   "first (persistent key or new dock video), "
				   "then Adopt."));
		} else {
			generalMsg_->setText(
				tr("More than one LIVE video — stop the extra "
				   "one or paste the ID manually."));
		}
		obs_log(LOG_INFO, "facebook adopt blocked");
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
		// Indicador D7 + verificacion previa a GoLive: lectura con
		// valores reales; el error de lectura nunca marca Verified.
		// Meta puede responder error en cuerpo con HTTP 200: el
		// objeto "error" no vacio tambien es fallo (fallback unico).
		// T-077 (Fase A): el primer intento YA va sin privacy
		// (F-093: Meta no la devuelve legible; pedirla daba 400).
		// Un 200 aqui trae valores reales con audiencia NO
		// verificada por lectura ("" nunca es Verified); el
		// mensaje honesto lo dice en vez de sugerir audiencia
		// publica. Aceptacion 200 != garantia de audiencia.
		const QJsonObject fbBody = replyJson(reply);
		const QJsonObject fbErr =
			fbBody.value(QStringLiteral("error")).toObject();
		if (netFail || http != 200 || !fbErr.isEmpty()) {
			// Plan v3 Fase 1: lectura fallida != confirmacion
			// fresca: el End pendiente se cancela (el proximo
			// ciclo de escucha lo reintentara si sigue caido).
			fbSigEndConfirm_ = false;
			// Fallback unico de compat (mismo criterio que
			// backend get_live_status): UNA sola relectura
			// del MISMO ID con los mismos campos sin
			// `privacy` (transitorio). Sin polling extra;
			// el fallo del retry si es definitivo.
			QString retryId = fbStatusId_;
			if (retryId.isEmpty())
				retryId = currentFbLiveId();
			if (!retryId.isEmpty()) {
				obs_log(LOG_INFO,
					"facebook status fallback retry "
					"http %d",
					http);
				QUrl rurl(QString::fromLatin1(kFbGraph) +
					  QStringLiteral("/") + retryId);
				QUrlQuery rq;
				rq.addQueryItem(
					QStringLiteral("fields"),
					meta::facebookStatusFields());
				rurl.setQuery(rq);
				sendGet(rurl, Op::FbStatusRetry,
					fb_.access);
				return;
			}
			const QJsonObject err = fbErr;
			int code = err.value(QStringLiteral("code"))
					   .toInt(0);
			const int sub = err.value(QStringLiteral(
							  "error_subcode"))
					      .toInt(0);
			if (code == 0)
				code = sub;
			else if (code == 100 && sub == 33)
				code = 33;
			// Fase A: el probe del arranque no inventa estado: si
			// el ID ya no es gestionable (NotFound/404) se crea
			// nuevo sin borrar nada; otro fallo difiere honesto.
			if (fbAutoActive_ && !fbAutoDidCreate_) {
				const bool nf =
					(http == 404 ||
					 meta::classifyFb(http, code) ==
						 meta::Outcome::NotFound);
				fbQuietStatus_ = false;
				fbAutoHandleRead(fbStatusId_.isEmpty()
							 ? currentFbLiveId()
							 : fbStatusId_,
						 QString(), QString(),
						 QString(), false, nf);
				return;
			}
			// Fase A punto 3: la cadena tras Apply no pisa su
			// mensaje ante un fallo de lectura: lo restaura.
			if (fbApplyChain_) {
				if (fbResult_ &&
				    !fbApplyChainMsg_.isEmpty())
					setResult(meta::Platform::Facebook,
						  true,
						  fbApplyChainMsg_ +
							  tr(" — verification "
							     "read failed"));
				fbApplyChain_ = false;
				fbApplyChainId_.clear();
				fbApplyChainMsg_.clear();
				fbQuietStatus_ = false;
				return;
			}
			const QString spec =
				meta::fbEligibilityMessage(code);
			generalMsg_->setText(
				!spec.isEmpty()
					? spec
					: meta::userMessage(
						  meta::classifyFb(http, code),
						  P::Facebook));
			setFbState(QString());
			// Mensaje de lectura distinto del de audiencia no
			// verificada; se conserva el expected para reintentar.
			// Cambio B: la lectura encadenada tras go_live fallo:
			// nada que informar como publicado.
			fbGoLiveConfirm_ = false;
			fbVerify_ = {meta::FbVerify::Blocked,
				     QStringLiteral("read-failed")};
			setFbVerifyLabel(
				tr("Status read failed — not verified."));
			return;
		}
		const QJsonObject o = fbBody;
		const QString st =
			o.value(QStringLiteral("status")).toString();
		setFbState(st);
		QString rid = o.value(QStringLiteral("id")).toString();
		if (rid.isEmpty())
			rid = currentFbLiveId();
		// Sin privacy pedida no hay privacy devuelta: audiencia no
		// verificada por lectura (F-093). runFbVerification lo
		// etiqueta; el mensaje general lo dice en vez de callar.
		runFbVerification(rid,
				  o.value(QStringLiteral("title")).toString(),
				  o.value(QStringLiteral("description"))
					  .toString(),
				  st,
				  meta::facebookPrivacyValue(
					  o.value(QStringLiteral("privacy"))));
		// Fase A punto 2: lectura del arranque -> decide (una sola).
		if ((fbAutoActive_ || fbAutoArmed_) && !fbAutoDidCreate_) {
			fbQuietStatus_ = false;
			fbAutoHandleRead(
				rid, st,
				o.value(QStringLiteral("title")).toString(),
				o.value(QStringLiteral("description"))
					.toString(),
				true, false);
			obs_log(LOG_INFO,
				"facebook auto-prepare: probe done");
			return;
		}
		// Fase A punto 3: verificacion silenciosa tras Apply OK:
		// restaura el mensaje de Apply + nota honesta de audiencia
		// (jamas infla a verificado), sin mensaje generico.
		if (fbApplyChain_ && !fbApplyChainId_.isEmpty() &&
		    rid.trimmed() == fbApplyChainId_.trimmed()) {
			const QString note =
				meta::fbApplyChainNote(fbVerify_);
			if (fbResult_ && !fbApplyChainMsg_.isEmpty())
				setResult(meta::Platform::Facebook, true,
					  fbApplyChainMsg_ + note);
			fbApplyChain_ = false;
			fbApplyChainId_.clear();
			fbApplyChainMsg_.clear();
			fbQuietStatus_ = false;
			obs_log(LOG_INFO,
				"facebook apply chained verify done");
			return;
		}
		// Plan v3 Fase 1: confirmacion Graph fresca para auto-End:
		// solo el MISMO ID decide; LIVE con emisor caido => End;
		// ya terminado => solo constancia (sin End redundante).
		if (fbSigEndConfirm_ && !fbAutoGoLiveId_.isEmpty() &&
		    rid.trimmed() == fbAutoGoLiveId_.trimmed()) {
			fbSigEndConfirm_ = false;
			if (meta::fbLiveState(st) ==
			    meta::FbLiveState::Live) {
				generalMsg_->setText(
					tr("Signal lost -- ending live video."));
				fbQuietStatus_ = false;
				startFbEndIndependent(rid);
				return;
			}
			generalMsg_->setText(
				tr("Signal lost -- live already ended."));
			fbQuietStatus_ = false;
			clearFbGoLiveSession();
			return;
		}
		// Cambio B: si esta lectura es la encadenada tras go_live,
		// informa lo observado (manda sobre el mensaje generico).
		if (fbGoLiveConfirm_)
			reportFbGoLiveObserved(st);
		else if (!fbQuietStatus_)
			generalMsg_->setText(
				tr("Facebook status: %1 — audience not "
				   "verified (privacy not requested; "
				   "values kept).")
					.arg(st.isEmpty() ? tr("unknown")
							 : st));
		fbQuietStatus_ = false;
		obs_log(LOG_INFO, "facebook status checked");
		return;
	}

	case Op::FbStatusRetry: {
		// Relectura unica de compat (mismos campos sin `privacy`):
		// exito con valores reales pero audiencia no verificable
		// (privacy "" fijado, sin reutilizar previo); el fallo es
		// general. Nunca marca Verified (Go live sigue bloqueado).
		// HTTP 200 con "error" no vacio tambien es fallo general
		// (read-failed, nunca audiencia no verificada).
		const QJsonObject fbBody = replyJson(reply);
		const QJsonObject fbErr =
			fbBody.value(QStringLiteral("error")).toObject();
		if (netFail || http != 200 || !fbErr.isEmpty()) {
			// Plan v3 Fase 1: lectura fallida != confirmacion
			// fresca: el End pendiente se cancela (el proximo
			// ciclo de escucha lo reintentara si sigue caido).
			fbSigEndConfirm_ = false;
			const QJsonObject err = fbErr;
			int code = err.value(QStringLiteral("code"))
					   .toInt(0);
			const int sub = err.value(QStringLiteral(
							  "error_subcode"))
					      .toInt(0);
			if (code == 0)
				code = sub;
			else if (code == 100 && sub == 33)
				code = 33;
			// Fase A: el retry es la ultima lectura del probe:
			// encamina igual que el fallo sin reintento.
			if (fbAutoActive_ && !fbAutoDidCreate_) {
				const bool nf =
					(http == 404 ||
					 meta::classifyFb(http, code) ==
						 meta::Outcome::NotFound);
				fbQuietStatus_ = false;
				fbAutoHandleRead(!fbStatusId_.isEmpty()
							 ? fbStatusId_
							 : currentFbLiveId(),
						 QString(), QString(),
						 QString(), false, nf);
				return;
			}
			if (fbApplyChain_) {
				if (fbResult_ &&
				    !fbApplyChainMsg_.isEmpty())
					setResult(meta::Platform::Facebook,
						  true,
						  fbApplyChainMsg_ +
							  tr(" — verification "
							     "read failed"));
				fbApplyChain_ = false;
				fbApplyChainId_.clear();
				fbApplyChainMsg_.clear();
				fbQuietStatus_ = false;
				return;
			}
			const QString spec =
				meta::fbEligibilityMessage(code);
			generalMsg_->setText(
				!spec.isEmpty()
					? spec
					: meta::userMessage(
						  meta::classifyFb(http, code),
						  P::Facebook));
			setFbState(QString());
			// Mensaje de lectura distinto del de audiencia no
			// verificada; se conserva el expected para reintentar.
			// Cambio B: la lectura encadenada tras go_live fallo:
			// nada que informar como publicado.
			fbGoLiveConfirm_ = false;
			fbVerify_ = {meta::FbVerify::Blocked,
				     QStringLiteral("read-failed")};
			setFbVerifyLabel(
				tr("Status read failed — not verified."));
			return;
		}
		const QJsonObject o = fbBody;
		const QString st =
			o.value(QStringLiteral("status")).toString();
		setFbState(st);
		QString rid = o.value(QStringLiteral("id")).toString();
		if (rid.isEmpty())
			rid = !fbStatusId_.isEmpty() ? fbStatusId_
						     : currentFbLiveId();
		runFbVerification(
			rid,
			o.value(QStringLiteral("title")).toString(),
			o.value(QStringLiteral("description")).toString(),
			st, QString());
		// privacy "" fijado (sin reutilizar previo ni loguear
		// valores); etiqueta existente + sufijo fallback.
		fbLastPrivacy_.clear();
		if (fbVerifyLabel_)
			setFbVerifyLabel(fbVerifyLabel_->text() +
					 tr(" — fallback"));
		// Fase A punto 2 (retry como probe definitivo del arranque).
		if ((fbAutoActive_ || fbAutoArmed_) && !fbAutoDidCreate_) {
			fbQuietStatus_ = false;
			fbAutoHandleRead(
				rid, st,
				o.value(QStringLiteral("title")).toString(),
				o.value(QStringLiteral("description"))
					.toString(),
				true, false);
			return;
		}
		// Fase A punto 3 (cadena tras Apply sobre el retry).
		if (fbApplyChain_ && !fbApplyChainId_.isEmpty() &&
		    rid.trimmed() == fbApplyChainId_.trimmed()) {
			const QString note =
				meta::fbApplyChainNote(fbVerify_);
			if (fbResult_ && !fbApplyChainMsg_.isEmpty())
				setResult(meta::Platform::Facebook, true,
					  fbApplyChainMsg_ + note);
			fbApplyChain_ = false;
			fbApplyChainId_.clear();
			fbApplyChainMsg_.clear();
			fbQuietStatus_ = false;
			return;
		}
		// Plan v3 Fase 1: confirmacion Graph fresca para auto-End:
		// solo el MISMO ID decide; LIVE con emisor caido => End;
		// ya terminado => solo constancia (sin End redundante).
		if (fbSigEndConfirm_ && !fbAutoGoLiveId_.isEmpty() &&
		    rid.trimmed() == fbAutoGoLiveId_.trimmed()) {
			fbSigEndConfirm_ = false;
			if (meta::fbLiveState(st) ==
			    meta::FbLiveState::Live) {
				generalMsg_->setText(
					tr("Signal lost -- ending live video."));
				fbQuietStatus_ = false;
				startFbEndIndependent(rid);
				return;
			}
			generalMsg_->setText(
				tr("Signal lost -- live already ended."));
			fbQuietStatus_ = false;
			clearFbGoLiveSession();
			return;
		}
		// Cambio B: si esta lectura es la encadenada tras go_live,
		// informa lo observado (manda sobre el mensaje generico).
		if (fbGoLiveConfirm_)
			reportFbGoLiveObserved(st);
		else if (!fbQuietStatus_)
			generalMsg_->setText(
				tr("Facebook status: %1 — audience not "
				   "verified (privacy not returned, "
				   "fallback read); values kept.")
					.arg(st.isEmpty() ? tr("unknown")
							 : st));
		fbQuietStatus_ = false;
		obs_log(LOG_INFO, "facebook status fallback checked");
		return;
	}

	case Op::FbCreate: {
		if (netFail || http != 200) {
			const QJsonObject err = replyJson(reply)
							    .value(QStringLiteral(
								    "error"))
							    .toObject();
			// T-077 (Fase A): rechazo de clave persistente
			// (solo posible con opt-in): el mensaje Meta
			// manda; mensaje honesto sin prometer nada (F-092).
			if (meta::facebookIsPersistentKeyError(
				    err.value(QStringLiteral("message"))
					    .toString())) {
				if ((fbAutoActive_ || fbAutoArmed_) || fbEndAutoPending_) {
					fbAutoHandleCreateFailed(
						fbAutoPrevStatus_);
					fbEndAutoPending_ = false;
				} else {
					generalMsg_->setText(
						meta::
							fbPersistentNotAllowedMessage());
				}
				return;
			}
			// Fase A: fallo del create automatico -> fallback
			// honesto (reutilizar anterior valido o diferir).
			if ((fbAutoActive_ || fbAutoArmed_) || fbEndAutoPending_) {
				fbAutoHandleCreateFailed(
					fbAutoPrevStatus_);
				fbEndAutoPending_ = false;
				return;
			}
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
			if ((fbAutoActive_ || fbAutoArmed_) || fbEndAutoPending_) {
				fbAutoHandleCreateFailed(
					fbAutoPrevStatus_);
				fbEndAutoPending_ = false;
				return;
			}
			generalMsg_->setText(meta::userMessage(
				meta::Outcome::ServerRetry, P::Facebook));
			return;
		}
		setFbLiveId(nid);
		// El ID creado queda pendiente de verificacion contra el
		// expected guardado al crear (titulo/desc/audiencia).
		fbVerifyId_ = nid;
		// PoC ingestión: valor completo del create, oculto por
		// defecto (Password). Jamas en logs.
		// T-078 Fase 3: con ingesta en memoria se re-fija el
		// forward en caliente (PATCH :9997, sin reinicio);
		// respeta cooldown F-097 sin reintentos.
		const QString ingest =
			o.value(QStringLiteral("secure_stream_url"))
				.toString();
		setFbIngest(ingest);
		if (!ingest.isEmpty() && relay_)
			fbRefireOrDefer(nid, ingest);
		// Estado real vía lectura Meta: si la respuesta trae status
		// se usa; si no, NO se supone UNPUBLISHED: lectura
		// encadenada (una vez, sin polling) que deja el indicador
		// real sin pisar el mensaje del create (fbQuietStatus_).
		const QString createdStatus =
			o.value(QStringLiteral("status")).toString();
		if (!createdStatus.isEmpty()) {
			setFbState(createdStatus);
			setFbVerifyLabel(
				tr("Created — press Check live status to "
				   "verify before Go live."));
			generalMsg_->setText(
				tr("Facebook live video created (%1).")
					.arg(nid));
		} else {
			fbQuietStatus_ = true;
			startFbStatusIndependent(nid);
			generalMsg_->setText(
				tr("Facebook live video created (%1).")
					.arg(nid));
		}
		// Fase A punto 5: create tras End (un solo intento, sin
		// delete: el terminado queda como VOD). Mensaje honesto.
		if (fbEndAutoPending_) {
			fbEndAutoPending_ = false;
			fbAutoActive_ = false;
			generalMsg_->setText(
				tr("Facebook live ended — next video "
				   "prepared (%1).")
					.arg(nid));
			obs_log(LOG_INFO,
				"facebook end auto-prepare done");
			return;
		}
		// Fase A punto 2: create del arranque -> mensaje honesto +
		// limpieza del huerfano (solo ese, nunca LIVE/VOD/ajeno).
		if (fbAutoActive_) {
			fbAutoHandleCreated(nid);
			obs_log(LOG_INFO,
				"facebook auto-prepare created");
			return;
		}
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
			// F-097 (vista honesta, sin cambiar logica): ante
			// rate-limit en re-handshakes (go_live/end), aviso
			// de cooldown ~3 min observado; sin prometer
			// recuperacion, latencia ni CPU.
			const meta::Outcome fbOc =
				meta::classifyFb(http, code);
			QString fbMsg = !spec.isEmpty()
						? spec
						: meta::userMessage(
							  fbOc,
							  P::Facebook);
			if (fbOc == meta::Outcome::RateLimited &&
			    op != Op::FbDelete)
				fbMsg += tr(" Meta may limit quick "
					    "re-handshakes (~3 min "
					    "observed); wait before "
					    "retrying.");
			// Fase A punto 2: si el DELETE del huerfano falla, se
			// informa con el ID (sin reintentar): el operador lo
			// borra a mano; el video vigente queda intacto.
			if (op == Op::FbDelete &&
			    !fbAutoDeleteId_.isEmpty()) {
				const QString prev = fbAutoDeleteId_;
				fbAutoDeleteId_.clear();
				generalMsg_->setText(
					tr("Facebook live video prepared — "
					   "previous video %1 could not be "
					   "removed (delete it manually).")
						.arg(prev));
				obs_log(LOG_WARNING,
					"facebook auto-prepare orphan delete "
					"failed");
				return;
			}
			generalMsg_->setText(fbMsg);
			obs_log(LOG_WARNING, "%s http %d", label, http);
			return;
		}
		{
			const QString liveId = currentFbLiveId();
			if (op == Op::FbGoLive && !liveId.isEmpty()) {
				// F-085 + Cambio B: refresco encadenado (una
				// lectura, sin polling). La relectura informa
				// lo observado (reportFbGoLiveObserved); aqui
				// no se afirma publicado sin LIVE leido.
				// F-097: sin reintento agresivo (cooldown
				// ~3min de Meta ante re-handshakes seguidos;
				// constancia para la vista, sin loop aqui).
				setFbState(QStringLiteral("LIVE"));
				fbGoLiveConfirm_ = true;
				fbQuietStatus_ = true;
				startFbStatusIndependent(liveId);
			} else if (op == Op::FbEnd && !liveId.isEmpty()) {
				// F-098: End -> LIVE_STOPPED genera VOD salvo
				// borrado explicito; el indicador queda en
				// VOD hasta la lectura encadenada (que
				// confirma el estado real sin inventarlo).
				setFbState(QStringLiteral("VOD"));
				// T-078 Fase 3: la senal se retira; limpieza
				// del forward ([]) sin cooldown.
				if (relay_)
					relay_->clearForward();
					fbCancelRefire();
					clearFbGoLiveSession();
				fbQuietStatus_ = true;
				startFbStatusIndependent(liveId);
			}
		}
		if (op == Op::FbDelete) {
			// Fase A punto 2: el DELETE automatico del huerfano
			// (fbAutoDeleteId_) solo retira ESE ID del combo; no
			// toca el video vigente (combo/store), ni su ingesta
			// en memoria ni su verificacion. Si falla, el reply
			// de error ya informo con el ID (ver rama de error).
			if (!fbAutoDeleteId_.isEmpty()) {
				const QString prev = fbAutoDeleteId_;
				fbAutoDeleteId_.clear();
				for (int i = fbLiveCombo_->count() - 1;
				     i >= 0; --i) {
					if (fbLiveCombo_->itemData(i)
						    .toString() == prev ||
					    fbLiveCombo_->itemText(i).contains(
						    prev))
						fbLiveCombo_->removeItem(
							i);
				}
				saveStore();
				generalMsg_->setText(
					tr("Facebook live video prepared — "
					   "previous unpublished video %1 "
					   "removed.")
						.arg(prev));
				obs_log(LOG_INFO,
					"facebook auto-prepare orphan removed");
				return;
			}
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
			// PoC ingestión: el objeto murió, la URL muere con él.
			clearFbIngest();
			// T-078 Fase 3: sin LiveVideo no hay forward;
			// limpieza ([]) sin cooldown.
			if (relay_)
				relay_->clearForward();
					fbCancelRefire();
					clearFbGoLiveSession();
			// El objeto ya no existe: su verificacion tampoco.
			clearFbVerify();
			setFbState(QString());
			generalMsg_->setText(
				tr("Facebook live video deleted."));
		} else if (op == Op::FbGoLive) {
			// Cambio B: no afirmar publicado sin LIVE leido; la
			// relectura encadenada (si la hay) informa lo
			// observado y manda.
			if (fbGoLiveConfirm_)
				generalMsg_->setText(
					tr("Facebook go-live accepted — "
					   "confirming status…"));
			else
				generalMsg_->setText(
					tr("Facebook go-live accepted."));
		} else {
			// F-098: End genera VOD salvo borrado explicito
			// (el objeto retirado de senal conserva su VOD).
			// Vista honesta (Fase B): sin prometer borrado.
			setFbState(QStringLiteral("VOD"));
			generalMsg_->setText(
				tr("Facebook live ended (VOD kept unless "
				   "you delete the video)."));
			// Fase A punto 5: un solo create tras End confirmado
			// (canRefire o diferir honesto; el terminado queda
			// como VOD, sin delete). Sin loops ni modales.
			fbEndAutoPrepare(currentFbLiveId());
		}
		obs_log(LOG_INFO, "%s ok", label);
		return;
	}

	case Op::UpFb:
	case Op::UpFbRetry: {
		const P p = P::Facebook;
		const QJsonObject okBody = replyJson(reply);
		const QJsonObject err =
			okBody.value(QStringLiteral("error")).toObject();
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
		if (oc == meta::Outcome::Success) {
			// Cambio A: sincroniza la referencia con lo ENVIADO
			// en este update para el MISMO liveId (capturado al
			// enviar). Preferir la lectura autoritativa si la
			// respuesta la trae; si no, los enviados. Excluye
			// backoff/auth (ya retornados arriba). Deja
			// verificacion pendiente de Check live status.
			QString refId = fbPendingApplyId_;
			if (refId.isEmpty())
				refId = currentFbLiveId();
			QString refTitle = fbPendingApplyTitle_;
			QString refDesc = fbPendingApplyDesc_;
			if (okBody.contains(QStringLiteral("title")))
				refTitle = okBody
						   .value(QStringLiteral(
							   "title"))
						   .toString();
			if (okBody.contains(
				    QStringLiteral("description")))
				refDesc = okBody
						  .value(QStringLiteral(
							  "description"))
						  .toString();
			noteFbUpdated(refId, refTitle, refDesc);
			// Fase A punto 3: tras UpFb OK, verificacion
			// silenciosa (patron fbQuietStatus_ del create):
			// status silencioso -> runFbVerification, sin pisar
			// el mensaje de Apply.
			if (!refId.isEmpty() &&
			    (fb_.connected || isManaged()))
				fbChainSilentVerify(refId);
		}
		finishPlatform(p, oc == meta::Outcome::Success,
			       !spec.isEmpty()
				       ? spec
				       : meta::userMessage(oc, p));
		startApplyNext();
		return;
	}

	case Op::RelaySignal: {
		// Fase A punto 4: senal del emisor (localhost unicamente,
		// jamas Graph). Solo estado para la vista + predicados
		// puros; el cuerpo (con dests) jamas se loguea ni persiste.
		const bool ok =
			!netFail && http >= 200 && http < 300;
		if (!ok) {
			obs_log(LOG_INFO, "relay signal http=%d",
				netFail ? 0 : http);
			generalMsg_->setText(
				tr("Relay signal unreadable (is the relay "
				   "running?)."));
			// Plan v3 Fase 1: lectura ilegible = corte (cuenta
			// para los 3 fallos seguidos / votacion 2-de-3).
			fbSignalNoteResult(false);
			updateFbHappyPath();
			return;
		}
		relaySignal_ = relay::parseSignal(replyJson(reply));
		// Trío operador 2026-10-26 [D]: resumen de diagnóstico con
		// ready/fuente/bytes (jamás URLs/dests/ingestas) para
		// distinguir "la API dice no-ready" (Aitum no publica de
		// verdad) de "parse roto o endpoint equivocado".
		obs_log(LOG_INFO, "relay signal http=%d %s", http,
			qPrintable(relay::signalSummary(relaySignal_)));
		// Plan v3 Fase 1: la escucha decide pre-create/auto-End
		// con este resultado (adaptativo 2 s/1 s, 3 fallos,
		// votacion 2-de-3, 30 s + 5 min + confirmacion Graph).
		fbSignalNoteResult(relay::signalHasEmitter(relaySignal_));
		if (relay::signalHasEmitter(relaySignal_))
			generalMsg_->setText(
				tr("Relay signal: live emitter detected."));
		else
			generalMsg_->setText(
				tr("Relay signal: no emitter — point your "
				   "encoder at rtmp://127.0.0.1:1935/live."));
		// T-078 Fase B: la senal manda en el indicador del emisor.
		updateFbHappyPath();
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
		// T-076: ordenar como en Managed (activo, proximos por
		// fecha, terminados). La API no garantiza orden.
		QList<QJsonObject> sorted;
		for (const auto &v : items)
			sorted.append(v.toObject());
		std::sort(sorted.begin(), sorted.end(),
			[](const QJsonObject &a, const QJsonObject &b) {
				auto life = [](const QJsonObject &it) {
					return it.value(QStringLiteral("status")).toObject()
					.value(QStringLiteral("lifeCycleStatus")).toString();
				};
				auto start = [](const QJsonObject &it) {
					return it.value(QStringLiteral("snippet")).toObject()
					.value(QStringLiteral("scheduledStartTime")).toString();
				};
				auto rank = [&](const QJsonObject &it) {
					const QString l = life(it);
					if (l == QStringLiteral("live") || l == QStringLiteral("liveStarting"))
							return 0;
					if (l == QStringLiteral("complete") || l == QStringLiteral("revoked"))
							return 2;
					return 1;
				};
				const int ra = rank(a), rb = rank(b);
				if (ra != rb)
					return ra < rb;
				if (ra == 1) {
					const QString sa = start(a), sb = start(b);
					if (sa.isEmpty() != sb.isEmpty())
							return !sa.isEmpty();
					if (sa != sb)
							return sa < sb;
				}
				return false;
			});
		for (const auto &it : sorted) {
			const QJsonObject sn =
				it.value(QStringLiteral("snippet")).toObject();
			const QString lf =
				it.value(QStringLiteral("status")).toObject()
				.value(QStringLiteral("lifeCycleStatus")).toString();
			const QString lb = (lf == QStringLiteral("live") ||
				lf == QStringLiteral("liveStarting"))
				? QStringLiteral("active")
				: ((lf == QStringLiteral("complete") ||
				lf == QStringLiteral("revoked"))
				? QStringLiteral("completed")
				: QStringLiteral("upcoming"));
			const QString title =
				sn.value(QStringLiteral("title")).toString();
			const QString id =
				it.value(QStringLiteral("id")).toString();
			if (id.isEmpty())
				continue;
			broadcastCombo_->addItem(
				QStringLiteral("%1 [%2] (%3)").arg(title, lb, id),
				QString::fromUtf8(QJsonDocument(it).toJson(
					QJsonDocument::Compact)));
		}
		if (broadcastCombo_->count() > 0)
			broadcastCombo_->setCurrentIndex(0);
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
