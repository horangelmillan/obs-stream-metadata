/*
T-078 Fase 3 (cableado): implementacion del gestor async del relay.
Ver relay_manager.h. Sin Aitum/OBS, sin secretos en logs.
*/

#include "relay_manager.h"

#include <obs-module.h>
#include <plugin-support.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTimer>

RelayManager::RelayManager(QObject *parent) : QObject(parent)
{
	proc_ = new QProcess(this);
	net_ = new QNetworkAccessManager(this);
	connect(proc_, &QProcess::started, this,
		&RelayManager::onStarted);
	connect(proc_, &QProcess::errorOccurred, this,
		&RelayManager::onProcessError);
	connect(proc_, &QProcess::finished, this,
		&RelayManager::onFinished);
	connect(net_, &QNetworkAccessManager::finished, this,
		&RelayManager::onPatchFinished);
}

RelayManager::~RelayManager()
{
	stop();
}

void RelayManager::setState(relay::State s)
{
	if (state_ == s)
		return;
	state_ = s;
	// Solo el nombre del estado: jamas URLs, dests ni ingestas.
	obs_log(LOG_INFO, "relay state: %s", relay::stateName(s));
	emit stateChanged(s);
}

void RelayManager::notify(const QString &msg)
{
	// Los mensajes de relay.h son honestos por construccion (sin
	// secretos ni ingestas); aqui no se anaden datos.
	obs_log(LOG_WARNING, "relay: operator message shown");
	emit operatorMessage(msg);
}

QString RelayManager::findBinary() const
{
	const QStringList names = relay::binaryCandidates();
	auto usable = [](const QFileInfo &fi, const QString &name) {
		if (!fi.isFile())
			return false;
#ifdef Q_OS_WIN
		// En Windows basta isFile para .exe (el bit ejecutable no
		// aplica igual que en POSIX).
		if (name.endsWith(QStringLiteral(".exe"),
				  Qt::CaseInsensitive))
			return true;
#endif
		return fi.isExecutable();
	};
	// 1. Data path del modulo (destino real del instalador:
	// data/obs-plugins/obs-stream-metadata/relay/mediamtx.exe).
	// Primero, antes que los legados y que PATH.
	const char *data =
		obs_get_module_data_path(obs_current_module());
	if (data && *data) {
		const QString dataDir = QString::fromUtf8(data);
		// <data>/relay/mediamtx[.exe], <data>/mediamtx[.exe];
		// <data>/relay/ actua ademas como dir base (prefijo de
		// las dos primeras relativas).
		for (const QString &abs :
		     relay::dataBinaryCandidates(dataDir)) {
			const QFileInfo fi(abs);
			if (usable(fi, abs))
				return fi.absoluteFilePath();
		}
	}
	// 2. Legados (junto al plugin instalado, junto a la app,
	// dir de trabajo): fallback, no destino del instalador.
	const QStringList dirs{
		QCoreApplication::applicationDirPath(),
		QCoreApplication::applicationDirPath() +
			QStringLiteral("/../obs-plugins/64bit"),
		QDir::currentPath(),
	};
	for (const QString &dir : dirs) {
		for (const QString &name : names) {
			const QFileInfo fi(QDir(dir).filePath(name));
			if (usable(fi, name))
				return fi.absoluteFilePath();
		}
	}
	// 3. PATH del sistema.
	for (const QString &name : names) {
		const QString found =
			QStandardPaths::findExecutable(name);
		if (!found.isEmpty())
			return found;
	}
	return QString();
}

bool RelayManager::portBusy(int port)
{
	QTcpServer probe;
	// Sin ReuseAddressHint: si otro proceso lo ocupa, listen falla.
	const bool free =
		probe.listen(QHostAddress::LocalHost,
			     static_cast<quint16>(port));
	probe.close();
	return !free;
}

void RelayManager::ensureStarted()
{
	if (proc_->state() != QProcess::NotRunning)
		return; // ya corre: idempotente
	const QString binary = findBinary();
	if (binary.isEmpty()) {
		setState(relay::State::BinaryMissing);
		notify(relay::binaryMissingMessage());
		return;
	}
	if (portBusy(relay::rtmpPort())) {
		setState(relay::State::PortBusy);
		notify(relay::portBusyMessage(relay::rtmpPort()));
		return;
	}
	if (portBusy(relay::controlPort())) {
		setState(relay::State::PortBusy);
		notify(relay::portBusyMessage(relay::controlPort()));
		return;
	}
	setState(relay::State::Starting);
	stopping_ = false;
	QStringList args;
	// Config empaquetada junto al binario (puertos 1935/9997 + path
	// live, forma de lab): si existe se usa; si no, arranque pelado
	// (la Control API puede no responder y el PATCH fallara honesto).
	const QString yml = QDir(QFileInfo(binary).absolutePath())
				    .filePath(QStringLiteral(
					    "mediamtx.yml"));
	if (QFileInfo(yml).isFile())
		args << yml;
	// Sin log del path completo del binario: solo constancia.
	obs_log(LOG_INFO, "relay starting");
	proc_->start(binary, args);
}

void RelayManager::stop()
{
	pendingDest_.clear();
	okCount_ = 0; // el proximo arranque cuenta desde su preparacion
	if (proc_->state() == QProcess::NotRunning) {
		setState(relay::State::Stopped);
		return;
	}
	stopping_ = true;
	proc_->terminate();
	QTimer::singleShot(3000, this, &RelayManager::onKillTimeout);
	setState(relay::State::Stopped);
}

void RelayManager::onKillTimeout()
{
	if (stopping_ && proc_->state() != QProcess::NotRunning)
		proc_->kill();
}

void RelayManager::refireFromIngest(const QString &secureUrl)
{
	// La ingesta vive solo aqui en transito: se deriva el dest y se
	// olvida (miembro local, jamas logueado ni persistido).
	const QString dest =
		relay::forwardDestFromIngest(secureUrl);
	if (dest.isEmpty() ||
	    !relay::forwardDestLooksValid(dest)) {
		setState(relay::State::ForwardFailed);
		notify(relay::forwardErrorMessage(0));
		return;
	}
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	if (!relay::canRefire(lastOkMs_, now)) {
		// F-097: bloqueado con tiempo restante, sin reintentos y
		// sin cambiar el estado vigente.
		notify(relay::cooldownMessage(
			relay::cooldownRemainingMs(lastOkMs_, now)));
		return;
	}
	ensureStarted();
	if (state_ == relay::State::BinaryMissing ||
	    state_ == relay::State::PortBusy)
		return; // ensureStarted ya notifico
	if (proc_->state() == QProcess::NotRunning ||
	    state_ == relay::State::Starting) {
		// Arranque en curso o fallo silencioso pendiente de
		// senal: un unico envio diferido (no es reintento).
		pendingDest_ = dest;
		return;
	}
	sendPatch(dest, false);
}

void RelayManager::refireDest(const QString &dest)
{
	// Dest ya derivado y revalidado por el llamante (vive solo en
	// memoria/transito; jamas se loguea ni se persiste). Sin cooldown
	// aqui (el llamante espero el disparo diferido); sin reintentos.
	if (dest.isEmpty() ||
	    !relay::forwardDestLooksValid(dest)) {
		setState(relay::State::ForwardFailed);
		notify(relay::forwardErrorMessage(0));
		return;
	}
	ensureStarted();
	if (state_ == relay::State::BinaryMissing ||
	    state_ == relay::State::PortBusy)
		return; // ensureStarted ya notifico
	if (proc_->state() == QProcess::NotRunning ||
	    state_ == relay::State::Starting) {
		pendingDest_ = dest;
		return;
	}
	sendPatch(dest, false);
}

void RelayManager::clearForward()
{	if (proc_->state() == QProcess::NotRunning) {
		setState(relay::State::Stopped);
		return;
	}
	// La limpieza no es re-handshake: sin cooldown.
	sendPatch(QString(), true);
}

void RelayManager::sendPatch(const QString &dest, bool isClear)
{
	clearPending_ = isClear;
	const QUrl url(relay::controlBase() +
		       relay::controlPatchPath());
	QNetworkRequest req(url);
	req.setHeader(QNetworkRequest::ContentTypeHeader,
		      QStringLiteral("application/json"));
	// Body minimo {"forward":[...]}; jamas se loguea.
	net_->sendCustomRequest(req, "PATCH",
				relay::forwardPatchBody(dest).toUtf8());
}

void RelayManager::onStarted()
{
	obs_log(LOG_INFO, "relay process started");
	setState(relay::State::Running);
	if (!pendingDest_.isEmpty()) {
		const QString dest = pendingDest_;
		pendingDest_.clear();
		sendPatch(dest, false);
	}
}

void RelayManager::onProcessError(QProcess::ProcessError e)
{
	if (stopping_)
		return; // parada explicita: muerte esperada
	pendingDest_.clear();
	obs_log(LOG_WARNING, "relay process error %d",
		static_cast<int>(e));
	setState(relay::State::Stopped);
	// FailedToStart no emite finished: notificar aqui. Los demas
	// errores (Crashed...) los notifica onFinished (un solo aviso).
	if (e == QProcess::FailedToStart)
		notify(relay::startErrorMessage());
}

void RelayManager::onFinished(int code, QProcess::ExitStatus st)
{
	pendingDest_.clear();
	const bool expected = stopping_;
	stopping_ = false;
	// Solo codigos: jamas rutas, args ni salidas del hijo.
	obs_log(LOG_INFO, "relay process finished code=%d status=%d",
		code, static_cast<int>(st));
	setState(relay::State::Stopped);
	// Muerte inesperada: un solo aviso (FailedToStart ya aviso en
	// onProcessError y aqui error() lo delata).
	if (!expected && proc_->error() != QProcess::FailedToStart)
		notify(relay::startErrorMessage());
}

void RelayManager::onPatchFinished(QNetworkReply *reply)
{
	if (!reply)
		return;
	const bool netFail =
		(reply->error() != QNetworkReply::NoError);
	const int http = netFail
				 ? 0
				 : reply->attribute(QNetworkRequest::
							    HttpStatusCodeAttribute)
					       .toInt();
	reply->deleteLater();
	// Solo el codigo al log: el body lleva el dest (clave tras '#').
	obs_log(LOG_INFO, "relay patch http=%d", http);
	const bool ok = !netFail && http >= 200 && http < 300;
	if (ok) {
		if (clearPending_) {
			setState(relay::State::Running);
		} else {
			lastOkMs_ =
				QDateTime::currentMSecsSinceEpoch();
			++okCount_; // origen del contador de vista
			setState(relay::State::ForwardOk);
		}
		return;
	}
	setState(relay::State::ForwardFailed);
	notify(relay::forwardErrorMessage(http));
}
