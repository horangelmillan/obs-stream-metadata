/*
 T-048: console live test for the C++ Managed wiring (no OBS, no Widgets).

 Exercises the REAL plugin-side stack against a REAL backend:
 backend_auth::Client (bootstrap/session/HMAC) -> POST /connect/<provider>
 -> operator opens the printed authorization URL in the system browser ->
 backend callback/exchange -> status polling -> Connected as -> disconnect.

 Usage:
   managed-link-test --base-url http://127.0.0.1:9004 --provider youtube
                     --store <temp-accounts.json> [--timeout-s 360]

 Output never contains secrets, tokens, codes or URLs with credentials:
 only HTTP codes, lengths and PASS/FAIL. Temporary store file is removed
 on exit. This is a dev/test tool, excluded from the default build.
*/

#include "backend_auth.h"
#include "secure_store.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTimer>
#include <cstdarg>
#include <cstdio>

namespace {

struct Args {
	QString baseUrl;
	QString provider; // "youtube" | "kick"
	QString storePath;
	int timeoutS = 360;
};

bool parseArgs(const QStringList &in, Args &out, QString &err)
{
	for (int i = 1; i < in.size(); ++i) {
		const QString &a = in[i];
		if ((a == QStringLiteral("--base-url") ||
		     a == QStringLiteral("--provider") ||
		     a == QStringLiteral("--store")) &&
		    i + 1 < in.size()) {
			if (a == QStringLiteral("--base-url"))
				out.baseUrl = in[++i];
			else if (a == QStringLiteral("--provider"))
				out.provider = in[++i].toLower();
			else
				out.storePath = in[++i];
		} else if (a == QStringLiteral("--timeout-s") && i + 1 < in.size()) {
			out.timeoutS = in[++i].toInt();
		} else {
			err = QStringLiteral("bad arg: ") + a;
			return false;
		}
	}
	if (out.baseUrl.isEmpty() || out.storePath.isEmpty() ||
	    (out.provider != QStringLiteral("youtube") &&
	     out.provider != QStringLiteral("kick")) ||
	    out.timeoutS <= 0) {
		err = QStringLiteral("usage: --base-url URL --provider "
				     "youtube|kick --store PATH [--timeout-s N]");
		return false;
	}
	while (out.baseUrl.endsWith(QLatin1Char('/')))
		out.baseUrl.chop(1);
	return true;
}

void say(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	std::vprintf(fmt, ap);
	va_end(ap);
	std::printf("\n");
	std::fflush(stdout);
}

} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	Args args;
	QString err;
	if (!parseArgs(app.arguments(), args, err)) {
		say("FAIL %s", qPrintable(err));
		return 2;
	}

	QNetworkAccessManager nam;
	secure::Store store(args.storePath);
	backend_auth::Client client(&nam);
	client.setBaseUrl(args.baseUrl);
	client.setStore(&store);

	const QString connectPath =
		QStringLiteral("/connect/") + args.provider;
	const QString statusPath = connectPath + QStringLiteral("/status");
	const QString discPath = connectPath + QStringLiteral("/disconnect");

	int exitCode = 3;
	QTimer deadline;
	deadline.setSingleShot(true);
	deadline.setInterval(args.timeoutS * 1000);
	QObject::connect(&deadline, &QTimer::timeout, &app, [&]() {
		say("FAIL timeout waiting for operator/browser");
		exitCode = 3;
		app.quit();
	});
	deadline.start();

	std::function<void()> pollStatus;
	QTimer pollTimer;
	pollTimer.setSingleShot(false);
	int pollsLeft = 120; // 5 s x 10 min upper bound, deadline wins first

	std::function<void()> finishDisconnect = [&]() {
		client.apiPost(discPath, QJsonObject(),
			       [&](const backend_auth::Client::ApiReply &rep) {
				       say("DISCONNECT: http=%d", rep.http);
				       client.apiGet(statusPath,
						     [&](const backend_auth::Client::
							     ApiReply &st) {
							     const bool gone =
								     st.result ==
									     backend_auth::
										     Result::
											     Ok &&
								     st.body.value(QStringLiteral(
											     "status"))
									     .toString() ==
									     QStringLiteral(
										     "disconnected");
							     say("POST-DISCONNECT: http=%d "
								 "status-gone=%d",
								 st.http, gone ? 1 : 0);
							     say(gone ? "MANAGED LINK: PASS" :
									"MANAGED LINK: FAIL");
							     exitCode = gone ? 0 : 4;
							     app.quit();
						     });
			       });
	};

	pollStatus = [&]() {
		if (--pollsLeft < 0)
			return; // deadline handles the timeout message
		client.apiGet(statusPath,
			      [&](const backend_auth::Client::ApiReply &rep) {
				      if (rep.result !=
					      backend_auth::Result::Ok)
					      return; // keep polling
				      if (rep.body.value(QStringLiteral("status"))
						      .toString() ==
					  QStringLiteral("connected")) {
					      pollTimer.stop();
					      const int nameLen = rep.body
								  .value(QStringLiteral(
									  "account"))
								  .toObject()
								  .value(QStringLiteral(
									  "displayName"))
								  .toString()
								  .size();
					      say("CONNECTED AS: display-name len=%d "
						  "(value not printed)",
						  nameLen);
					      finishDisconnect();
				      }
			      });
	};
	QObject::connect(&pollTimer, &QTimer::timeout, &app, pollStatus);

	// bootstrap (fresh temp store: always provisions) -> session ->
	// connect -> browser (operator) -> poll -> disconnect.
	client.bootstrap([&](backend_auth::Result r) {
		if (r != backend_auth::Result::Ok) {
			say("FAIL bootstrap result=%d", static_cast<int>(r));
			exitCode = 4;
			app.quit();
			return;
		}
		say("BOOTSTRAP: ok (identity in temp DPAPI store)");
		client.ensureSession([&](backend_auth::Result s) {
			if (s != backend_auth::Result::Ok) {
				say("FAIL session result=%d",
				    static_cast<int>(s));
				exitCode = 4;
				app.quit();
				return;
			}
			say("SESSION: ok");
			client.apiPost(connectPath, QJsonObject(),
				       [&](const backend_auth::Client::ApiReply &rep) {
					       if (rep.result !=
						       backend_auth::Result::Ok) {
						       say("FAIL connect http=%d "
							   "result=%d",
							   rep.http,
							   static_cast<int>(
								   rep.result));
						       exitCode = 4;
						       app.quit();
						       return;
					       }
					       const QString url = rep.body
						       .value(QStringLiteral(
							       "authorization_url"))
						       .toString();
					       if (url.isEmpty()) {
						       say("FAIL empty authorization_url");
						       exitCode = 4;
						       app.quit();
						       return;
					       }
					       say("OPEN IN SYSTEM BROWSER (operator):");
					       say("%s", qPrintable(url));
					       say("Polling status every 5 s...");
					       pollTimer.start(5000);
				       });
		});
	});

	const int rc = app.exec();
	QFile::remove(args.storePath); // temp identity never stays on disk
	(void)rc;
	return exitCode;
}
