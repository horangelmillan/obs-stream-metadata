/*
obs-stream-metadata — T-078 Fase 3 (cableado): gestor async del relay
local MediaMTX (ADR-016).

QProcess + QNetworkAccessManager con senales, sin bloquear la UI:

- El binario integrado se busca sin bloquear (data path del modulo
  primero —destino del instalador: relay/—, luego legados junto al
  plugin + dir de la app, luego PATH; candidatos de relay.h);
  ausente => estado BinaryMissing + mensaje honesto (aviso MIT,
  nunca se versiona).
- Puertos 1935/9997: comprobacion previa honesta (otro proceso/otra
  instancia); el plugin nunca toca Aitum/OBS.
- Al crear LiveVideo el dock pasa secure_stream_url (memoria) y aqui se
  construye el dest (relay::forwardDestFromIngest, F-096) y se fija en
  caliente via PATCH :9997 (sin reinicio, sin reintentos).
- Cooldown F-097: re-fijado bloqueado con mensaje + tiempo restante.
- clearForward() envia {"forward":[]} sin pasar por cooldown.
- La ingesta/dest jamas se persiste ni se loguea: los logs solo llevan
  el nombre del estado y codigos HTTP.

Sin textos finales de vista: los mensajes al operador salen de relay.h
y el indicador viaja como relay::State (la vista los redacta despues).
*/

#pragma once

#include "relay.h"

#include <QObject>
#include <QProcess>

class QNetworkAccessManager;
class QNetworkReply;

class RelayManager : public QObject {
	Q_OBJECT
public:
	explicit RelayManager(QObject *parent = nullptr);
	~RelayManager() override;

	relay::State state() const { return state_; }
	qint64 lastForwardOkMs() const { return lastOkMs_; }
	// Trio operador 2026-10-26 [D]: cuantos forwards acepto este
	// arranque (origen del contador de vista: 1 = preparacion
	// inicial, >1 = ultimo reenvio). Se resetea al parar.
	int forwardOkCount() const { return okCount_; }

	// Idempotente, no bloquea: si ya corre no hace nada.
	void ensureStarted();
	// Parada limpia: terminate + kill de respaldo, sin bloquear.
	void stop();
	// Fija en caliente el forward hacia la ingesta en memoria.
	// Respeta cooldown F-097 (bloquea con mensaje, sin reintentos).
	void refireFromIngest(const QString &secureUrl);
	// Plan v3 Fase 1: fija un dest ya derivado (cola diferida de un
	// solo disparo; el llamante espero el cooldown y revalido el ID).
	// Sin chequeo de cooldown aqui (el llamante lo respeto); sin
	// reintentos.
	void refireDest(const QString &dest);
	// Limpia el forward (PATCH {"forward":[]}); sin cooldown.
	void clearForward();

signals:
	void stateChanged(relay::State s);
	void operatorMessage(const QString &msg);

private slots:
	void onStarted();
	void onProcessError(QProcess::ProcessError e);
	void onFinished(int code, QProcess::ExitStatus st);
	void onPatchFinished(QNetworkReply *reply);
	void onKillTimeout();

private:
	void setState(relay::State s);
	void notify(const QString &msg);
	QString findBinary() const;
	static bool portBusy(int port);
	void sendPatch(const QString &dest, bool isClear);

	QProcess *proc_ = nullptr;
	QNetworkAccessManager *net_ = nullptr;
	relay::State state_ = relay::State::Stopped;
	qint64 lastOkMs_ = 0;
	int okCount_ = 0; // forwards aceptados desde el ultimo arranque
	bool clearPending_ = false; // el PATCH en vuelo es limpieza
	bool stopping_ = false;     // stop() explicito: muerte esperada
	QString pendingDest_;       // re-fijado diferido hasta started
};
