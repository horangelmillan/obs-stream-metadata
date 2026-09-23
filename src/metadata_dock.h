/*
obs-stream-metadata — T-031 MVP dock (Twitch / YouTube / Kick),
T-032 hardening.

Minimal product UI: per-platform app credentials (BYO-app, ADR-008),
platform checkboxes, title, description (YouTube only), YouTube
broadcast selector, per-provider Connect/Disconnect, Apply with
independent per-platform results.

Async by design: a single QNetworkAccessManager (signal-driven, never
blocking) + single-shot timers; sequential platform updates (§19:
sequential is acceptable, correctness first).

Credentials: client IDs/secrets are typed once into the dock and kept
in memory + DPAPI-encrypted store (never plaintext, never logged).
Disconnect revokes server-side (best effort) then wipes local state.
Apply retries 429/5xx at most twice with backoff (no loops).

UX FASE 1 (Modelo B): the same widgets/state live inside one expandable
card per platform (single-open). No functional change: checkboxes keep
their Apply-selection semantics, OAuth/Apply/broadcast/persistence and
Managed behavior are untouched.
*/

#pragma once

#include "backend_auth.h"
#include "metadata.h"
#include "secure_store.h"
#include <QMap>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QPlainTextEdit;
class QPushButton;
class QTcpServer;
class QTimer;

namespace meta {
}

class MetadataDock : public QWidget {
	Q_OBJECT
public:
	explicit MetadataDock(QWidget *parent = nullptr);
	~MetadataDock() override;

	struct ManagedConn {
		bool connected = false;
		QString userId;  // provider user/channel id (non-secret label)
		QString display; // "Connected as" label (non-secret)
	};

private slots:
	void onApply();
	void onConnectTwitch();
	void onDisconnectTwitch();
	void onConnectYouTube();
	void onDisconnectYouTube();
	void onConnectKick();
	void onDisconnectKick();
	// T-071 FB-2: Facebook Independent directo (BYO-app, PKCE sin secret).
	void onConnectFacebook();
	void onDisconnectFacebook();
	void onRefreshBroadcasts();
	void onRefreshFacebook();
	void onFacebookTargetChanged(int index);
	// T-073 FB-4: ciclo de vida LiveVideo (indicador/on-off/creación).
	// ON solo sobre ID existente/creado-por-dock + confirmación UI;
	// OFF/DELETE también confirman; STATUS es solo lectura.
	void onFbCreate();
	void onFbStatus();
	void onFbGoLive();
	void onFbEndLive();
	void onFbDelete();
	void onReply(QNetworkReply *reply);
	void onTwitchPollTimeout();
	void onBackoffTimeout();
	void onCallbackConnection();

	void onModeChanged(int index);
	bool isManaged() const;
	void refreshModeUi();
	meta::ConnectionMode modeFromCombo() const;
	// T-048: Managed wiring (backend). Independent handlers untouched.
	void onConnectManaged(meta::Platform p);
	void onDisconnectManaged(meta::Platform p);
	void startManagedBrowserFlow(meta::Platform p);
	void onManagedPollTimeout();
	void finishManagedConnected(meta::Platform p, const QString &userId,
				    const QString &display);
	void finishManagedError(meta::Platform p, const QString &msg);
	void repaintModeStatuses();
	ManagedConn &managedAccount(meta::Platform p);
	void initManaged();
	// UX FASE 1 (Modelo B): single-open cards. Visibility only: closing
	// a card never disconnects, clears or reselects anything.
	void toggleCard(meta::Platform p);
	void updateCardVisibility();
	void updateCardStyle(meta::Platform p);
	void updateAllCardStyles();
	// FASE 1.1 progressive disclosure: visibility only, derived from
	// the existing Account/ManagedConn state. Never a new availability
	// model: usable == connected in the active mode.
	bool platformUsable(meta::Platform p);
	bool anyUsable();
	void refreshContentVisibility();
	// FASE 1.1 mode-switch safety: cancel in-flight connect flows so a
	// late reply cannot connect in the previous context. Never wipes
	// stored accounts, never revokes.
	void cancelPendingForModeSwitch();
	bool eventFilter(QObject *watched, QEvent *event) override;
	QString managedAuthError(meta::Platform p, backend_auth::Result r, int);
	QString managedApiError(meta::Platform p,
				const backend_auth::Client::ApiReply &rep);
	// FASE 2.1-C: Apply Managed YouTube via backend (tokens server-side).
	// Independent YouTube path untouched.
	void fetchManagedBroadcasts();
	void startManagedYouTubeApply();
	// Apply Managed Twitch via backend (solo título; Twitch no tiene
	// descripción de stream equivalente). Independent DCF intacto.
	void startManagedTwitchApply();
	// T-068: Apply Managed Kick via backend (solo título; Kick no tiene
	// descripción de stream equivalente, jamás channel_description).
	// Independent directo intacto.
	void startManagedKickApply();
	// FB-3 (T-072): Apply Managed Facebook via backend (título 1-254 +
	// descripción SÍ + privacy EVERYONE, token server-side).
	// Independent directo intacto.
	void startManagedFacebookApply();
	// FB-3 (T-072): listado Managed best-effort (F-074/F-075) hacia el
	// combo existente (vale item listado o ID pegado, como Independent).
	void fetchManagedFacebookVideos();
	// T-071 FB-2: Facebook Independent (OAuth PKCE + list + update).
	// Managed Facebook llega en FB-3 (T-072): aqui solo Independent.
	void startFacebookExchange(const QString &code);
	void startFacebookLongLived();
	void fetchFacebookVideos();
	void fetchFacebookTargets();
	// T-073 FB-4: helpers ciclo de vida (ambos modos, QNAM async).
	QString currentFbLiveId() const;
	void setFbLiveId(const QString &id);
	void setFbState(const QString &status);
	void startFbStatusIndependent(const QString &liveId);
	void startFbCreateIndependent();
	void startFbGoLiveIndependent(const QString &liveId);
	void startFbEndIndependent(const QString &liveId);
	void startFbDeleteIndependent(const QString &liveId);
	void startManagedFbOp(const QString &op, const QString &liveId);
	// F-C2: borrado total Managed (POST /privacy/erase + snapshots).
	void onEraseManagedData();

private:
	// One request at a time; replies carry their Op in a property.
	enum class Op {
		None,
		TwDevice,
		TwPoll,
		TwValidate,
		TwRefresh,
		YtExchange,
		YtRefresh,
		YtChannels,
		YtList,
		KkExchange,
		KkRefresh,
		KkChannels,
		FbExchange,
		FbLongLived,
		FbIdentity,
		FbTargets,
		FbList,
		FbRead,
		FbStatus,
		FbCreate,
		FbGoLive,
		FbEnd,
		FbDelete,
		UpFb,
		UpFbRetry,
		UpTw,
		UpTwRetry,
		UpYt,
		UpYtRetry,
		UpKk,
		UpKkRetry,
		RevTw, // fire-and-forget revoke on Disconnect (best effort)
		RevYt,
		RevKk,
		RevFb,
	};

	struct Account {
		bool connected = false;
		QString access;       // memory only
		QString refresh;      // memory only
		QString display;      // login/slug for the status label
		QString broadcaster;  // Twitch user_id
		QString clientId;     // memory only, from env
		QString secret;       // YouTube/Kick, memory only, from env
		QString verifier;     // transient PKCE (one attempt)
		QString state;        // transient OAuth state (one attempt)
		QString deviceCode;   // transient Twitch device flow
		int pollInterval = 5; // Twitch device poll seconds
		void clear()
		{
			connected = false;
			access.clear();
			refresh.clear();
			display.clear();
			broadcaster.clear();
			clientId.clear();
			secret.clear();
			verifier.clear();
			state.clear();
			deviceCode.clear();
			pollInterval = 5;
		}
	};

	Account tw_;
	Account yt_;
	Account kk_;
	// T-071 FB-2: cuenta Independent Facebook (BYO-app, PKCE sin secret
	// en el exchange; secret opcional DPAPI solo para long-lived).
	Account fb_;
	// T-071 FB-2: page tokens en memoria (de /me/accounts), jamas
	// persistidos ni logueados. Clave = page id.
	QMap<QString, QString> fbPageTokens_;
	QString fbTarget_;
	QTcpServer *callbackServer_ = nullptr;
	meta::Platform callbackFor_ = meta::Platform::YouTube;
	bool callbackDone_ = false; // first callback wins (favicon guard)
	Op pending_ = Op::None;
	bool applyAfterList_ = false;
	bool retried_ = false; // one refresh retry per platform update
	bool backoffResume_ = false; // resend after backoff: keep retried_
	int backoffCount_ = 0; // 429/5xx resends so far (<= kBackoffMaxRetries)
	int twPollsLeft_ = 0;  // remaining Twitch device-flow polls
	QString redirect_;     // exact redirect_uri of the current attempt
	QList<meta::Platform> applyQueue_;
	// Revoke chain state (Disconnect clears local first, revoke is
	// best-effort in the background; codes only in logs).
	meta::Platform revokeFor_ = meta::Platform::Twitch;
	QString revokeClientId_;
	QStringList revokeQueue_;
	secure::Store *store_ = nullptr; // DPAPI account store (may be null)

	// UI
	QLineEdit *twIdEdit_ = nullptr;
	QLineEdit *ytIdEdit_ = nullptr;
	QLineEdit *ytSecretEdit_ = nullptr;
	QLineEdit *kkIdEdit_ = nullptr;
	QLineEdit *kkSecretEdit_ = nullptr;
	QLineEdit *fbIdEdit_ = nullptr;
	QLineEdit *fbSecretEdit_ = nullptr;
	QCheckBox *twCheck_ = nullptr;
	QCheckBox *ytCheck_ = nullptr;
	QCheckBox *kkCheck_ = nullptr;
	QCheckBox *fbCheck_ = nullptr;
	QLabel *twStatus_ = nullptr;
	QLabel *ytStatus_ = nullptr;
	QLabel *kkStatus_ = nullptr;
	QLabel *fbStatus_ = nullptr;
	QPushButton *twConnect_ = nullptr;
	QPushButton *ytConnect_ = nullptr;
	QPushButton *kkConnect_ = nullptr;
	QPushButton *fbConnect_ = nullptr;
	QPushButton *twDisconnect_ = nullptr;
	QPushButton *ytDisconnect_ = nullptr;
	QPushButton *kkDisconnect_ = nullptr;
	QPushButton *fbDisconnect_ = nullptr;
	QLineEdit *titleEdit_ = nullptr;
	QPlainTextEdit *descEdit_ = nullptr;
	QComboBox *broadcastCombo_ = nullptr;
	QPushButton *refreshButton_ = nullptr;
	QComboBox *fbTargetCombo_ = nullptr;
	QComboBox *fbLiveCombo_ = nullptr;
	QPushButton *fbRefreshButton_ = nullptr;
	QLabel *fbTargetLabel_ = nullptr;
	QLabel *fbLiveLabel_ = nullptr;
	// T-073 FB-4: indicador + ciclo de vida (solo UI, sin estado nuevo
	// sensible; el ID persiste en secure::Data.facebookLiveId).
	QLabel *fbStateLabel_ = nullptr;
	QPushButton *fbStatusButton_ = nullptr;
	QPushButton *fbCreateButton_ = nullptr;
	QPushButton *fbGoLiveButton_ = nullptr;
	QPushButton *fbEndButton_ = nullptr;
	QPushButton *fbDeleteButton_ = nullptr;
	// T-073 FB-4: status encadenado tras on/off (F-085). Cuando es true,
	// el read-back actualiza el indicador sin pisar el mensaje de la
	// operacion (sigue siendo una lectura a peticion, sin polling).
	bool fbQuietStatus_ = false;
	QLabel *devicePrompt_ = nullptr;
	QPushButton *applyButton_ = nullptr;
	// F-C2: "Borrar mis datos" (solo Managed; Independent intacto).
	QPushButton *eraseButton_ = nullptr;
	QLabel *twResult_ = nullptr;
	QLabel *ytResult_ = nullptr;
	QLabel *kkResult_ = nullptr;
	QLabel *fbResult_ = nullptr;
	QLabel *generalMsg_ = nullptr;
	// UX FASE 1 (Modelo B): one card per platform. The functional
	// widgets above are reparented into these containers; no new state.
	QFrame *twCard_ = nullptr;
	QFrame *ytCard_ = nullptr;
	QFrame *kkCard_ = nullptr;
	QFrame *fbCard_ = nullptr;
	QWidget *twDetail_ = nullptr;
	QWidget *ytDetail_ = nullptr;
	QWidget *kkDetail_ = nullptr;
	QWidget *fbDetail_ = nullptr;
	QPushButton *twHeader_ = nullptr;
	QPushButton *ytHeader_ = nullptr;
	QPushButton *kkHeader_ = nullptr;
	QPushButton *fbHeader_ = nullptr;
	// FASE 1.2: no separate close buttons. The header alone expands /
	// collapses (chevron shows the state); the Apply check stays an
	// independent control so header-click and check-click never mix.
	// FASE 1.1: Content widgets need handles for disclosure (same
	// objects as before, only shown/hidden as a group).
	QLabel *titleLabel_ = nullptr;
	QLabel *descLabel_ = nullptr;
	QLabel *descCaps_ = nullptr;
	QLabel *bcLabel_ = nullptr;
	// FASE 1.1 note, kept hidden since FASE 2 (Twitch connects in
	// Managed like the other platforms; see refreshContentVisibility).
	QLabel *twManagedNote_ = nullptr;
	std::optional<meta::Platform> expanded_; // none = all cards closed
	QNetworkAccessManager *net_ = nullptr;
	// T-041: connection mode (ADR-012). Global for the dock: a single
	// plugin installation uses one App-Identity source. Defaults to
	// Independent (all pre-T-041 behavior). T-048 wires Managed via
	// backend_auth (YouTube/Kick); FASE 2 adds Twitch Managed
	// (Independent DCF untouched).
	meta::ConnectionMode mode_ = meta::defaultConnectionMode();
	QComboBox *modeCombo_ = nullptr;
	QLabel *credTitle_ = nullptr;
	QLabel *credNote_ = nullptr;
	QLabel *managedNote_ = nullptr;
	// T-048: Managed auth client (backend sessions) + per-provider
	// Managed state. Memory only: display names are re-queried, never
	// persisted; provider tokens never reach this process.
	backend_auth::Client *managedAuth_ = nullptr;
	QString managedBaseUrl_;
	ManagedConn mYt_;
	ManagedConn mKk_;
	ManagedConn mTw_;
	// FB-3 (T-072): Managed Facebook cableado (Connect/Apply/listado
	// por backend + snapshot managed_facebook). Sin tokens por
	// construcción, como el resto de ManagedConn.
	ManagedConn mFb_;
	QTimer *managedPollTimer_ = nullptr;
	meta::Platform managedPollFor_ = meta::Platform::YouTube;
	int managedPollsLeft_ = 0;

	void setStatus(meta::Platform p, const QString &text);
	void setResult(meta::Platform p, bool ok, const QString &text);
	Account &account(meta::Platform p);
	void sendForm(const QUrl &url, const QString &body, Op op,
		      const QString &bearer = QString(),
		      bool browserUa = false);
	void sendJson(const QUrl &url, const QString &verb,
		      const QString &body, Op op, const QString &bearer,
		      const QString &clientId = QString(),
		      bool browserUa = false);
	void sendGet(const QUrl &url, Op op, const QString &bearer,
		     const QString &clientId = QString(),
		     bool browserUa = false);
	void startConnectBusy(meta::Platform p);
	void finishConnectOk(meta::Platform p, const QString &display);
	void finishConnectError(meta::Platform p, const QString &msg);
	// Connect-phase HTTP error: user message + redacted log with code.
	void connectHttpError(meta::Platform p, const char *step, int http,
			      bool netFail);
	void startApplyNext();
	void finishPlatform(meta::Platform p, bool ok, const QString &msg);
	void finishApply();
	// 429/5xx during Apply: bounded resend (true) or terminal (false).
	bool scheduleBackoff(meta::Platform p, meta::Outcome oc, int http);
	QString pkceChallenge(const QString &verifier);
	QString randomUrlSafe(int chars);
	void openBrowser(const QString &url);
	bool listenCallback(quint16 &port, bool kick);
	void handleCallbackData(const QByteArray &request);
	void startYouTubeExchange(const QString &code);
	void startKickExchange(const QString &code);
	void refreshWithToken(meta::Platform p, Op resumeOp);
	void saveStore();
	void loadStore();
	QString storePath();
	// Fire-and-forget server-side revoke, then wipe local state.
	// The reply handlers only log the HTTP code (best effort).
	void startRevoke(meta::Platform p, const Account &snapshot);
	void sendNextRevoke();
	void wipeLocal(meta::Platform p);
	// Card widget accessors (no logic, only lookup).
	QFrame *cardFor(meta::Platform p) const;
	QWidget *detailFor(meta::Platform p) const;
	QPushButton *headerFor(meta::Platform p) const;
	QCheckBox *checkFor(meta::Platform p) const;
	QLabel *statusFor(meta::Platform p) const;
	QLabel *resultFor(meta::Platform p) const;
};
