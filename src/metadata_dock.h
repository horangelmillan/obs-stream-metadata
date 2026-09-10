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
*/

#pragma once

#include "backend_auth.h"
#include "metadata.h"
#include "secure_store.h"
#include <QWidget>

class QCheckBox;
class QComboBox;
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
		QString display;
	};

private slots:
	void onApply();
	void onConnectTwitch();
	void onDisconnectTwitch();
	void onConnectYouTube();
	void onDisconnectYouTube();
	void onConnectKick();
	void onDisconnectKick();
	void onRefreshBroadcasts();
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
	void onManagedPollTimeout();
	void finishManagedConnected(meta::Platform p, const QString &display);
	void finishManagedError(meta::Platform p, const QString &msg);
	void repaintModeStatuses();
	ManagedConn &managedAccount(meta::Platform p);
	void initManaged();
	QString managedAuthError(meta::Platform p, backend_auth::Result r, int);
	QString managedApiError(meta::Platform p,
				const backend_auth::Client::ApiReply &rep);

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
		UpTw,
		UpTwRetry,
		UpYt,
		UpYtRetry,
		UpKk,
		UpKkRetry,
		RevTw, // fire-and-forget revoke on Disconnect (best effort)
		RevYt,
		RevKk,
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
	QCheckBox *twCheck_ = nullptr;
	QCheckBox *ytCheck_ = nullptr;
	QCheckBox *kkCheck_ = nullptr;
	QLabel *twStatus_ = nullptr;
	QLabel *ytStatus_ = nullptr;
	QLabel *kkStatus_ = nullptr;
	QPushButton *twConnect_ = nullptr;
	QPushButton *ytConnect_ = nullptr;
	QPushButton *kkConnect_ = nullptr;
	QPushButton *twDisconnect_ = nullptr;
	QPushButton *ytDisconnect_ = nullptr;
	QPushButton *kkDisconnect_ = nullptr;
	QLineEdit *titleEdit_ = nullptr;
	QPlainTextEdit *descEdit_ = nullptr;
	QComboBox *broadcastCombo_ = nullptr;
	QPushButton *refreshButton_ = nullptr;
	QLabel *devicePrompt_ = nullptr;
	QPushButton *applyButton_ = nullptr;
	QLabel *twResult_ = nullptr;
	QLabel *ytResult_ = nullptr;
	QLabel *kkResult_ = nullptr;
	QLabel *generalMsg_ = nullptr;
	QNetworkAccessManager *net_ = nullptr;
	// T-041: connection mode (ADR-012). Global for the dock: a single
	// plugin installation uses one App-Identity source. Defaults to
	// Independent (all pre-T-041 behavior). T-048 wires Managed via
	// backend_auth (YouTube/Kick); Twitch stays direct-only.
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
};
