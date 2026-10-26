/*
obs-stream-metadata — T-078 Fase 2 (logica): relay local MediaMTX.

Puro QtCore: sin I/O, sin red, sin UI, sin secretos. Solo construye
datos y mensajes honestos para el relay local (ADR-016):

- El operador mantiene UN destino fijo: rtmp://127.0.0.1:1935/live
  (configurado una vez a mano en Aitum/OBS; el plugin NUNCA toca
  configuracion RTMP, keys, outputs, servicios, escenas ni fuentes,
  y NUNCA automatiza Aitum).
- El plugin gobierna el binario MediaMTX como proceso hijo (arranque /
  parada async via QProcess en el dock; aqui solo rutas candidatas y
  mensajes) y fija el forward en caliente via Control API :9997
  (PATCH /v3/config/paths/patch/live) hacia la ingesta RTMPS del
  LiveVideo vigente.
- Formato ganador F-096: base + '#' + clave (jamas URL integra).
  La ingesta (secure_stream_url del create) vive SOLO en memoria /
  transito de sesion: jamas se persiste, jamas se loguea, jamas se
  divide ni se muestra server/key por separado salvo lo necesario
  para construir el dest del forward.
- Cooldown F-097: ~3 min entre re-handshakes RTMPS seguidos; el
  producto lo respeta sin reintentos agresivos (predicado puro).
- Auto-VOD F-098: al retirar la senal el objeto genera VOD (aviso).
- Puertos 1935/9997: mensajes claros si ocupados (la comprobacion
  real es en runtime; aqui solo el texto honesto).
- Binario ausente: mensaje honesto (el instalador lo incluye con
  aviso MIT; nunca se versiona el binario, ver .gitignore).

Wire real verificado en lab (lab/mediamtx-lab/t078_relayfmt.py):
PATCH :9997/v3/config/paths/patch/live con {"forward":[{"dest":...}]}
y GET de confirmacion; aqui solo se construye el body.
*/

#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace relay {

// Destino fijo del operador (documentado en mensajes; el plugin no lo
// configura: lo fija el operador una vez a mano en Aitum/OBS).
QString fixedDestination(); // "rtmp://127.0.0.1:1935/live"

// Puertos del relay local (config de lab mediamtx.yml).
int rtmpPort();    // 1935
int controlPort(); // 9997

// Path del relay + endpoint exacto de la Control API para el forward
// en caliente (sin reiniciar, sin cortar la fuente).
QString pathName();         // "live"
QString controlPatchPath(); // "/v3/config/paths/patch/live"
QString controlBase();      // "http://127.0.0.1:9997"

// Nombres del binario integrado (aviso MIT incluido; el binario jamas
// se versiona, ver .gitignore). El instalador lo coloca en el data path
// del modulo (data/obs-plugins/obs-stream-metadata/relay/); el dock lo
// busca ahi primero y luego en los legados, sin bloquear la UI.
QStringList binaryCandidates(); // ["mediamtx.exe", "mediamtx"]

// Rutas relativas del binario dentro del data path del modulo, en orden
// de busqueda: relay/ primero (destino del instalador), luego la raiz
// del data dir. Puro QtCore, testeable sin runtime OBS.
QStringList dataRelativeBinaryPaths();
// Expande un dataDir absoluto a candidatos absolutos (mismo orden).
// Puro (solo QDir::filePath), sin I/O ni runtime OBS.
QStringList dataBinaryCandidates(const QString &dataDir);

// F-096: secure_stream_url (memoria/transito) -> dest del forward
// "base#clave". base = scheme://host[:port]/path-sin-ultimo-segmento;
// clave = ultimo segmento (+ "?" + query si la hay). Vacio si no se
// puede dividir con seguridad (jamas devuelve la URL integra).
QString forwardDestFromIngest(const QString &secureUrl);

// Body del PATCH de forward: {"forward":[{"dest":...}]}; dest vacio
// => {"forward":[]} (limpieza). JSON compacto, sin secretos en claro
// mas alla del dest que el llamador ya posee en memoria.
QString forwardPatchBody(const QString &dest);

// Validez superficial del dest (sin red): rtmps:// + '#' + clave.
bool forwardDestLooksValid(const QString &dest);

// F-097: cooldown ~3 min entre re-handshakes RTMPS seguidos.
long long cooldownMs(); // 180000
// true si puede re-fijarse ahora (sin ultimo OK o fuera del cooldown).
bool canRefire(long long lastOkMs, long long nowMs);
// Restante del cooldown en ms (0 si ya puede).
long long cooldownRemainingMs(long long lastOkMs, long long nowMs);

// F-098: aviso auto-VOD (cierre deja VOD salvo borrado explicito).
QString vodNoticeMessage();

// Fase A punto 4: senal del emisor (solo localhost, sin Graph).
// Path de lectura del path live (config get, no patch) + parse
// tolerante para la vista (a demanda, sin polling aun).
QString controlSignalPath(); // "/v3/config/paths/get/live"
// Estado crudo del path live para la vista (puro, sin I/O/red).
// present=false si el objeto no trae el path; ready=emitiendo
// configurado; hasSource=hay fuente entrante; inboundBytes=-1 si
// no se informa; forwardDests=destinos forward configurados.
struct Signal {
	bool present = false;
	bool ready = false;
	bool hasSource = false;
	long long inboundBytes = -1;
	QStringList forwardDests;
};
Signal parseSignal(const QJsonObject &o);
// true si hay emisor real: path presente + ready + fuente + bytes>0.
// Puro para la vista (el dock expone el estado crudo + este predicado).
bool signalHasEmitter(const Signal &s);
// Fase 3 (T-078): estado del relay para el indicador. La vista redacta
// los textos finales; aqui solo estados (puros, testeables) + el nombre
// corto para logs (sin secretos). El cooldown bloqueado NO es estado:
// se queda en el estado vigente y solo emite mensaje (sin reintentos).
enum class State {
	Stopped,       // proceso propio no corre
	Starting,      // arranque en curso
	Running,       // proceso corre, forward sin fijar
	ForwardOk,     // ultimo PATCH :9997 aceptado (2xx)
	ForwardFailed, // ultimo PATCH rechazado o fallo de red/arranque
	BinaryMissing, // binario integrado no encontrado
	PortBusy,      // 1935 o 9997 ocupados por otro proceso
};

// Nombre corto ASCII para logs ("stopped", ...). Nunca un secreto.
const char *stateName(State s);
// true si el proceso propio esta en pie (Starting/Running/Forward*).
bool stateIsRunning(State s);
// true solo en fallo que exige accion del operador.
bool stateIsError(State s);
// Arranque/muerte inesperada del proceso hijo: honesto, sin secretos.
QString startErrorMessage();

// Binario ausente: honesto (instalador + aviso MIT, sin binario en repo).
QString binaryMissingMessage();
// Puerto ocupado: honesto (otro proceso/otra instancia; sin tocar nada).
QString portBusyMessage(int port);
// Recordatorio del destino fijo (el operador lo mantiene a mano).
QString operatorFixedMessage();
// Cooldown activo: honesto (esperar, sin reintentos agresivos).
QString cooldownMessage(long long remainingMs);
// Error generico de forward: honesto, sin secretos ni URLs.
QString forwardErrorMessage(int httpStatus);

} // namespace relay
