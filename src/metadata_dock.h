/*
obs-stream-metadata — T-031 MVP dock (Twitch / YouTube / Kick)

Minimal product UI: platform checkboxes, title, description (YouTube
only), YouTube broadcast selector, per-provider Connect/Disconnect,
Apply with independent per-platform results.

Async by design: a single QNetworkAccessManager (signal-driven, never
blocking) + single-shot timers; sequential platform updates (§19:
sequential is acceptable, correctness first).

Credentials: client IDs/secrets come from LOCAL env vars at Connect
time and tokens live ONLY in process memory. Nothing is persisted,
printed or logged (lengths at most). Documented provisional
limitation for P5 (F-017, F-019, §25).
*/

#pragma once

#include "metadata.h"
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

namespace meta {
}

class MetadataDock : public QWidget {
	Q_OBJECT
public:
	explicit MetadataDock(QWidget *parent = nullptr);

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
	void onCallbackConnection();

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
	int twPollsLeft_ = 0;  // remaining Twitch device-flow polls
	QString redirect_;     // exact redirect_uri of the current attempt
	QList<meta::Platform> applyQueue_;

	// UI
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
	QString pkceChallenge(const QString &verifier);
	QString randomUrlSafe(int chars);
	void openBrowser(const QString &url);
	bool listenCallback(quint16 &port, bool kick);
	void handleCallbackData(const QByteArray &request);
	void startYouTubeExchange(const QString &code);
	void startKickExchange(const QString &code);
	void refreshWithToken(meta::Platform p, Op resumeOp);
};
