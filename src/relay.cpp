/*
T-078 Fase 2 (logica): implementacion pura QtCore del relay local.
Ver relay.h para el contrato. Sin I/O, sin red, sin UI, sin logs.
*/

#include "relay.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace relay {

QString fixedDestination()
{
	return QStringLiteral("rtmp://127.0.0.1:1935/live");
}

int rtmpPort()
{
	return 1935;
}

int controlPort()
{
	return 9997;
}

QString pathName()
{
	return QStringLiteral("live");
}

QString controlPatchPath()
{
	return QStringLiteral("/v3/config/paths/patch/live");
}

QString controlBase()
{
	return QStringLiteral("http://127.0.0.1:9997");
}

QStringList binaryCandidates()
{
	return QStringList{QStringLiteral("mediamtx.exe"),
			   QStringLiteral("mediamtx")};
}

QStringList dataRelativeBinaryPaths()
{
	// Orden de busqueda: data-relay primero (destino del
	// instalador), luego la raiz del data dir.
	return QStringList{
		QStringLiteral("relay/mediamtx.exe"),
		QStringLiteral("relay/mediamtx"),
		QStringLiteral("mediamtx.exe"),
		QStringLiteral("mediamtx"),
	};
}

QStringList dataBinaryCandidates(const QString &dataDir)
{
	const QDir base(dataDir);
	QStringList out;
	for (const QString &rel : dataRelativeBinaryPaths())
		out << QDir::cleanPath(base.filePath(rel));
	return out;
}

QString forwardDestFromIngest(const QString &secureUrl)
{
	const QString u = secureUrl.trimmed();
	if (u.isEmpty())
		return QString();
	const QUrl url(u);
	if (!url.isValid() || url.host().isEmpty())
		return QString();
	if (url.scheme().compare(QStringLiteral("rtmps"),
				 Qt::CaseInsensitive) != 0 &&
	    url.scheme().compare(QStringLiteral("rtmp"),
				 Qt::CaseInsensitive) != 0)
		return QString();
	const QString path = url.path();
	const int slash = path.lastIndexOf(QLatin1Char('/'));
	if (slash <= 0)
		return QString();
	const QString basePath = path.left(slash);
	QString key = path.mid(slash + 1).trimmed();
	if (key.isEmpty())
		return QString();
	QString base = url.scheme().toLower() + QStringLiteral("://") +
		       url.host();
	if (url.port() != -1)
		base += QStringLiteral(":") + QString::number(url.port());
	base += basePath;
	const QString q = url.query().trimmed();
	if (!q.isEmpty())
		key += QStringLiteral("?") + q;
	if (base.isEmpty() || key.isEmpty())
		return QString();
	// Formato ganador F-096: base + '#' + clave. Jamas URL integra.
	return base + QStringLiteral("#") + key;
}

QString forwardPatchBody(const QString &dest)
{
	QJsonObject o;
	QJsonArray fwd;
	if (!dest.trimmed().isEmpty()) {
		QJsonObject one;
		one[QStringLiteral("dest")] = dest.trimmed();
		fwd.append(one);
	}
	o[QStringLiteral("forward")] = fwd;
	return QString::fromUtf8(
		QJsonDocument(o).toJson(QJsonDocument::Compact));
}

bool forwardDestLooksValid(const QString &dest)
{
	const QString d = dest.trimmed();
	if (d.isEmpty() || d.contains(QLatin1Char(' ')))
		return false;
	if (!d.startsWith(QStringLiteral("rtmps://")) &&
	    !d.startsWith(QStringLiteral("rtmp://")))
		return false;
	const int hash = d.indexOf(QLatin1Char('#'));
	if (hash <= 8 || hash >= d.size() - 1)
		return false;
	return true;
}

long long cooldownMs()
{
	// F-097: cooldown observado ~3 min entre re-handshakes RTMPS.
	return 180000LL;
}

bool canRefire(long long lastOkMs, long long nowMs)
{
	if (lastOkMs <= 0)
		return true;
	return (nowMs - lastOkMs) >= cooldownMs();
}

long long cooldownRemainingMs(long long lastOkMs, long long nowMs)
{
	if (lastOkMs <= 0)
		return 0;
	const long long left = cooldownMs() - (nowMs - lastOkMs);
	return left > 0 ? left : 0;
}

QString vodNoticeMessage()
{
	return QStringLiteral(
		"Relay: when the signal is withdrawn the live video "
		"becomes a VOD automatically (unless deleted). "
		"Keep streaming pointed at rtmp://127.0.0.1:1935/live; "
		"the plugin never touches OBS/Aitum settings.");
}

const char *stateName(State s)
{
	switch (s) {
	case State::Starting:
		return "starting";
	case State::Running:
		return "running";
	case State::ForwardOk:
		return "forward-ok";
	case State::ForwardFailed:
		return "forward-failed";
	case State::BinaryMissing:
		return "binary-missing";
	case State::PortBusy:
		return "port-busy";
	case State::Stopped:
	default:
		return "stopped";
	}
}

bool stateIsRunning(State s)
{
	return s == State::Starting || s == State::Running ||
	       s == State::ForwardOk || s == State::ForwardFailed;
}

bool stateIsError(State s)
{
	return s == State::BinaryMissing || s == State::PortBusy ||
	       s == State::ForwardFailed;
}

QString startErrorMessage()
{
	return QStringLiteral(
		"Relay process failed to start or stopped unexpectedly. "
		"Reinstall the plugin with the integrated relay "
		"(MIT-licensed, notice included) and retry. Nothing was "
		"changed; keep streaming pointed at "
		"rtmp://127.0.0.1:1935/live.");
}

QString binaryMissingMessage()
{
	return QStringLiteral(
		"Relay binary not found (mediamtx). Reinstall the plugin "
		"with the integrated relay (MIT-licensed, notice included) "
		"or place it next to the plugin. Nothing was changed; "
		"keep streaming pointed at rtmp://127.0.0.1:1935/live.");
}

QString portBusyMessage(int port)
{
	return QStringLiteral(
		       "Relay port %1 is already in use by another program "
		       "(another relay instance or OBS using it). Close it or "
		       "free the port, then retry. The plugin never changes "
		       "OBS/Aitum settings; your fixed destination stays "
		       "rtmp://127.0.0.1:1935/live.")
		.arg(port);
}

QString operatorFixedMessage()
{
	return QStringLiteral(
		"Point Aitum/OBS once, by hand, to "
		"rtmp://127.0.0.1:1935/live and never change it between "
		"emissions. The plugin only re-points the relay forward "
		"to the current live video; it never automates Aitum/OBS.");
}

QString cooldownMessage(long long remainingMs)
{
	const long long secs =
		(remainingMs + 999) / 1000; // techo, solo informativo
	return QStringLiteral(
		       "Relay forward rate-limited: wait about %1 s before "
		       "re-pointing (meta limits back-to-back handshakes, "
		       "~3 min cooldown). No automatic retry; keep the "
		       "signal on rtmp://127.0.0.1:1935/live.")
		.arg(secs > 0 ? secs : 1);
}

QString forwardErrorMessage(int httpStatus)
{
	if (httpStatus == 429)
		return cooldownMessage(cooldownMs());
	return QStringLiteral(
		       "Relay forward not updated (control API %1). The relay "
		       "keeps its previous destination; check it is running "
		       "and retry once. Your source stays on "
		       "rtmp://127.0.0.1:1935/live.")
		.arg(httpStatus);
}

// Fase A punto 4: senal del emisor (puro, tolerante a variantes del
// Control API: /v3/paths/get y /v3/config/paths/get). Sin I/O/red.
QString controlSignalPath()
{
	return QStringLiteral("/v3/config/paths/get/live");
}

Signal parseSignal(const QJsonObject &o)
{
	Signal s;
	if (o.isEmpty())
		return s;
	s.present = true;
	if (o.value(QStringLiteral("ready")).isBool())
		s.ready = o.value(QStringLiteral("ready")).toBool();
	else if (o.value(QStringLiteral("readyTime")).isString())
		s.ready = !o.value(QStringLiteral("readyTime"))
				   .toString()
				   .isEmpty();
	// Fuente entrante: objeto "source" no vacio, o tipo explicito.
	const QJsonValue src = o.value(QStringLiteral("source"));
	if (src.isObject()) {
		const QJsonObject so = src.toObject();
		const QString type =
			so.value(QStringLiteral("type")).toString();
		s.hasSource = !so.isEmpty() &&
			      type.compare(QStringLiteral("none"),
					   Qt::CaseInsensitive) != 0;
	} else if (src.isString()) {
		s.hasSource = !src.toString().trimmed().isEmpty() &&
			      src.toString().compare(QStringLiteral("none"),
						     Qt::CaseInsensitive) !=
				      0;
	} else {
		const QString st =
			o.value(QStringLiteral("sourceType")).toString();
		const QString srcReady =
			o.value(QStringLiteral("sourceReady")).toString();
		s.hasSource =
			!st.isEmpty() &&
			st.compare(QStringLiteral("none"),
				   Qt::CaseInsensitive) != 0;
		if (!srcReady.isEmpty())
			s.hasSource = s.hasSource ||
				      srcReady.compare(
					      QStringLiteral("true"),
					      Qt::CaseInsensitive) == 0;
	}
	// Bytes entrantes: nombre tolerante (varias formas del API).
	for (const char *k : {"inboundBytes", "bytesReceived",
			      "inBytes", "bytes_received"}) {
		const QJsonValue v =
			o.value(QString::fromLatin1(k));
		if (v.isDouble()) {
			s.inboundBytes =
				static_cast<long long>(v.toDouble());
			break;
		}
	}
	// Destinos forward configurados (array de {dest} o strings).
	const QJsonValue fwd = o.value(QStringLiteral("forward"));
	if (fwd.isArray()) {
		for (const QJsonValue &v : fwd.toArray()) {
			if (v.isObject()) {
				const QString d = v.toObject()
							  .value(QStringLiteral(
								  "dest"))
							  .toString()
							  .trimmed();
				if (!d.isEmpty())
					s.forwardDests << d;
			} else if (v.isString()) {
				const QString d =
					v.toString().trimmed();
				if (!d.isEmpty())
					s.forwardDests << d;
			}
		}
	}
	return s;
}

bool signalHasEmitter(const Signal &s)
{
	return s.present && s.ready && s.hasSource &&
	       s.inboundBytes > 0;
}

} // namespace relay
