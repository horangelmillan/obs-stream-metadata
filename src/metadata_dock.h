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
#include "relay.h"
#include "secure_store.h"
#include <QMap>
#include <QWidget>

class RelayManager;

class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QTcpServer;
class QTimer;
class QFileSystemWatcher;

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

	// T-077 (Fase A): opt-in de clave persistente en create (ambos
	// modos, defecto OFF). Lo fija la vista (checkbox futuro ux-ui);
	// la logica solo lo lee al construir el body. F-092: ni con
	// ENABLE la URL es unica: jamas prometer persistencia.
	void setFbPersistent(bool on) { fbPersistent_ = on; }
	bool fbPersistent() const { return fbPersistent_; }

	// T-078 Fase 3 (cableado): estado del relay local (ADR-016). Solo
	// estados (relay::State); la vista redacta los textos finales.
	// Update/Apply jamas tocan el forward (solo metadatos).
	relay::State relayState() const { return relayState_; }

signals:
	// T-078 Fase 3: la vista observa el relay por estados, no por
	// textos (los redacta despues).
	void relayStateChanged(relay::State s);

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
	void onFbAdopt();
	// PoC ingestión: secure_stream_url de create, solo memoria de
	// sesión (fbIngest_, jamás persistido ni logueado). Oculto por
	// defecto (Password); Show/Copy son acción explícita. Copy pega
	// el valor completo para Aitum manual (sin dividir server/key).
	void onFbIngestToggled();
	void onFbIngestCopy();
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
	// T-075: silent revalidation on startup (no browser, single pass).
	void silentRevalidateManaged();
	// T-075: one silent plugin-session retry (http==0 only). Provider
	// 401/403 (http!=0) means revoked → manual Connect, no retry.
	// Dispatches internally to the same platform Apply (no loops:
	// single shot via retried_).
	bool autoRetryManagedSession(meta::Platform p, int http);
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
	// Fase 4: gate login previo a Managed (spec §5). Independent:
	// siempre true (sin cambios). Managed: exige user-session en
	// memoria; sin ella, mensaje exacto reutilizado y CERO red (el
	// llamador retorna sin fallback a Independent).
	bool requireManagedLogin(meta::Platform p);
	bool requireManagedLoginMsg();
	// 401 Managed normalizado (auth doble): cae la conexión local,
	// mensaje de login y formulario al frente. Independent intacto.
	void noteManagedAuthFailure(meta::Platform p);
	// Motivo del fin de sesion Managed ([D 2026-10-26]): obs_log con
	// codigo (sin secretos) + mensaje de persona para la UI. El
	// mensaje basta por si solo (jamas "mira los logs").
	QString reportUserSessionEnd(const char *where);
	// Estados del formulario de cuenta Managed (solo Managed). f7 (bug
	// 2026-10-26): statusOverride preserva el mensaje vigente (error
	// especifico) cuando no hay sesion; vacio => gate "login required".
	// Sin override, QLabel::setText limpiaria el error recien fijado
	// (https://doc.qt.io/qt-6/qlabel.html: "Setting the text clears
	// any previous content").
	void refreshManagedLoginUi(
		const QString &statusOverride = QString());
	// f6 ENTRAR-primero: pagina visible del QStackedWidget segun los
	// predicados puros meta::managedEnterVisible/PanelVisible
	// (excluyentes en Managed; Independent siempre PANEL). Sin red.
	void refreshManagedScreen();
	// Error de signup/login a mensaje UI (sin secretos, sin detalles).
	QString userAuthError(backend_auth::Result r, bool isSignup);
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
	// T-073 FB-4: indicador + ciclo de vida (D6R/D7/F-075). Sin
	// polling auto: todo a peticion (Refresh/Apply/botones).
	QString currentFbLiveId() const;
	void setFbLiveId(const QString &id);
	void setFbState(const QString &status);
	// Verificacion previa a GoLive (solo sesion, sin persistencia ni
	// red): expected del ultimo create + resultado puro. El gate de
	// onFbGoLive exige Verified sobre el ID actual.
	void setFbVerifyLabel(const QString &text);
	void runFbVerification(const QString &id,
			       const QString &actTitle,
			       const QString &actDesc,
			       const QString &actStatus,
			       const QString &actPrivacy);
	void clearFbVerify();
	void startFbStatusIndependent(const QString &liveId);
	void startFbAdoptIndependent();
	void startFbCreateIndependent();
	void startFbGoLiveIndependent(const QString &liveId);
	void startFbEndIndependent(const QString &liveId);
	void startFbDeleteIndependent(const QString &liveId);
	void startManagedFbOp(const QString &op, const QString &liveId);
	// Cambio A: sincroniza la referencia expected con lo ENVIADO en un
	// update exitoso (MISMO liveId), dejando verificacion pendiente.
	void noteFbUpdated(const QString &liveId, const QString &title,
			   const QString &desc);
	// Fase A auto-preparacion Facebook (un intento por arranque, sin
	// loops ni modales; maximo 1 create + 1 delete; ingesta jamas en
	// logs/persistencia). Decision tras End (punto 5): SIEMPRE un solo
	// create tras End confirmado (sin opt-in nuevo: el checkbox
	// persistente solo gobierna la clave, no el auto-prepare); con
	// guarda canRefire (si el cooldown bloquea, difiere con mensaje +
	// ID, sin crear huerfano inutil).
	void maybeFbAutoStart();
	void fbAutoProbe();
	// Plan v3 Fase 1: punto UNICO de arranque exacto (sin timers de
	// reintento para el arranque). INVARIANTE: llamar SOLO desde
	// finishManagedConnected(p == Facebook) y desde la rama
	// "connected" del lambda de silentRevalidateManaged. JAMAS desde
	// managedPollTimer_/onManagedPollTimeout, backoff/scheduleBackoff
	// ni polls Twitch (onTwitchPollTimeout/TwPoll). El flag
	// fbAutoActive_ se fija DENTRO, post-GET fresco (en
	// fbAutoHandleRead/fbAutoCreateFresh), jamas desde snapshot.
	void fbAutoStartFromConnected(meta::Platform p);
	void fbAutoCreateFresh(const QString &prevStatus,
			       const QString &prevTitle,
			       const QString &prevDesc);
	void fbAutoHandleRead(const QString &id, const QString &status,
			      const QString &title, const QString &desc,
			      bool readOk, bool notFound);
	void fbAutoHandleCreated(const QString &newId);
	void fbAutoHandleCreateFailed(const QString &prevStatus);
	void fbAutoDeletePrev();
	// Punto 3: encadena verificacion silenciosa tras UpFb OK (patron
	// fbQuietStatus_ del create): status silencioso -> runFbVerification
	// sin pisar el mensaje de Apply; audiencia honesta, jamas inflada.
	void fbChainSilentVerify(const QString &liveId);
	// Punto 5: un solo create tras End confirmado (con canRefire).
	void fbEndAutoPrepare(const QString &endedId);
	// Cambio B: la relectura encadenada tras go_live informa lo
	// observado (LIVE leido => publicado; otro estado => no
	// confirmado, sigue bloqueado). Consume fbGoLiveConfirm_.
	void reportFbGoLiveObserved(const QString &status);
	// Plan v3 Fase 1: create Managed parametrizado (body con titulo/
	// desc dados). En Managed NO muta widgets (a diferencia del camino
	// manual que los lee): la pre-creacion tras corte cita el titulo
	// usado sin tocar la UI.
	void startManagedFbCreateWithTitle(const QString &title,
					  const QString &desc);
	// PoC ingestión: fija/limpia fbIngest_ + visibilidad (sin logs,
	// sin persistencia). clearFbIngest oculta directo (sin refresh
	// global) para usarlo en disconnect/delete/mode-switch.
	void setFbIngest(const QString &url);
	void clearFbIngest();
	// F-C2: borrado total Managed (POST /privacy/erase + snapshots).
	void onEraseManagedData();
	// Fase 5b: borrado de CUENTA Managed (POST /users/erase en cascada
	// + sesión/snapshots locales). Operación distinta de F-C2.
	void onEraseAccount();
	// Fase 4: cuenta de usuario Managed (v1: username+password, sin
	// email; aviso "sin recuperación en v1"). Solo memoria.
	void onUserLogin();
	void onUserSignup();
	void onUserLogout();
	// f8: cambio de pestana Entrar|Crear cuenta: limpia errores
	// inline y lleva el foco al primer campo (sin red, solo vista).
	void onAuthTabChanged(int index);
	void clearAuthInlineErrors();
	// T-078 Fase 3 (cableado): el gestor emite estados y mensajes
	// honestos (relay.h); aqui solo se registra el estado y se
	// muestra el mensaje, sin textos nuevos de vista.
	void onRelayStateChanged(relay::State s);
	void onRelayOperatorMessage(const QString &msg);
	// Fase A punto 4: senal del emisor a demanda (localhost unicamente,
	// jamas Graph). Sin polling: la vista la pide (requestRelaySignal)
	// y lee el ultimo estado via relaySignal()/relaySignalHasEmitter().
	// Solo memoria de sesion; el cuerpo del Control API jamas se loguea
	// (solo codigos) ni se persiste.
	void requestRelaySignal();
	relay::Signal relaySignal() const { return relaySignal_; }
	bool relaySignalHasEmitter() const
	{
		return relay::signalHasEmitter(relaySignal_);
	}
	// T-078 Fase 4 (vista): seccion relay en la tarjeta Facebook
	// (ADR-016). Solo vista: Iniciar/Detener + URL fija copiable +
	// indicador por estados + mensajes honestos del gestor tal cual.
	// Sin logica auth/red/secretos; el forward lo gobierna Fase 3.
	void onRelayToggle();
	void onRelayFixedCopy();
	void updateRelayUi();
	// T-078 Fase B (vista simplificada Facebook): resumen compuesto
	// global + senal del emisor + Avanzado colapsado. Solo vista (sin
	// logica auth/red/secretos): reordena widgets existentes y decide
	// visibilidad por estados Fase A (verify/relay/senal). Sin botones
	// sin sentido: cada boton solo visible con su condicion cumplida.
	void buildFbSimplifiedView();
	void updateFbHappyPath();
	// Plan v3 (vista): semaforo en cabecera FB (icono sin texto +
	// AccessibleName/tooltip con frase, formas distintas por estado,
	// cache para no repintar) + contador ~mm:ss junto al semaforo
	// (visible solo con restante>0, tick 1 s solo-aritmetica, se
	// detiene en 0 sin afirmar nada). Solo vista: jamas red, jamas
	// secretos, jamas afirma estado del proveedor.
	void buildFbHeaderBadges();
	void updateFbSemaphore();
	void ensureCooldownTick();
	void tickFbCooldown();
	QString fbPersonStateWord(const QString &raw) const;
	// Plan v3 (vista): check global no-volver-a-mostrar del aviso de
	// audiencia en Avanzado (defecto OFF, persistente plaintext en
	// QSettings que la logica puede leer via fbAudienceNoticeHidden();
	// la vista solo expone el widget + el detalle de riesgo F-093 en
	// lenguaje de persona). Sin secretos, sin red.
	void setFbAudienceNoticeHidden(bool on);
	bool fbAudienceNoticeHidden() const { return fbAudienceHide_; }
	void toggleFbAdvanced();
	void onFbSignalCheck();
	// Plan v3 Fase 1: escucha de senal del repetidor (evento primario
	// QFileSystemWatcher sobre el dir de senales + pregunta local cada
	// 3 s de respaldo via GET paths/get/live, localhost, sin Graph).
	// Contrato de archivos-senal: signal.on = emisor en linea
	// (runOnOnline); signal.off = apagado (runOnOffline).
	// Adaptativo: al primer corte re-pregunta a 2 s y 1 s; 3 fallos
	// seguidos (~6 s) = apagado probable -> pre-crea siguiente
	// (fallback titulo + mensaje citando el titulo usado; en Managed
	// NO muta widgets); votacion 2-de-3 repetida = "inestable"
	// (espera sin terminar); para TERMINAR exige ademas 30 s continuos
	// + gracia 5 min post-GoLive + confirmacion Graph fresca del MISMO
	// ID + solo si el LIVE lo inicio el plugin en esta sesion (flags
	// fbAutoGoLiveSession_/Id_/AtMs, limpiados en End/Delete/Adopt/
	// mode-switch/cambio de ID). Si la senal vuelve, cancela pendientes.
	QString fbSignalDir() const;
	void ensureFbSignalWatch();
	void onFbSignalDirChanged(const QString &path);
	void onFbSignalBackup();
	void onFbSignalFast();
	void fbSignalNoteResult(bool hasEmitter);
	void fbSignalCancelPending();
	void fbSignalMaybePrecreate();
	void fbMaybeAutoEnd();
	void noteFbGoLiveSession(const QString &liveId);
	void clearFbGoLiveSession();
	// Cola diferida de un disparo para el refire en cooldown: crea ya,
	// difiere solo el PATCH. Al disparar revalida liveId ==
	// currentFbLiveId() y estado; cancela en clearForward/nuevo
	// create/Delete/mode-switch. Sin reintentos en bucle.
	void fbScheduleRefire(const QString &liveId, const QString &dest);
	void fbFireRefire();
	void fbCancelRefire();
	// Crea ya, difiere solo el PATCH (un disparo, guarda de ID).
	void fbRefireOrDefer(const QString &liveId,
			     const QString &ingest);

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
		FbStatusRetry, // relectura sin privacy (un intento)
		FbCreate,
		FbGoLive,
		FbEnd,
		FbDelete,
		FbAdopt,
		UpFb,
		UpFbRetry,
		UpTw,
		UpTwRetry,
		UpYt,
		UpYtRetry,
		UpKk,
		UpKkRetry,
		RelaySignal, // Fase A.4: GET localhost control (senal emisor)
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
	// PoC ingestión: secure_stream_url del create (solo sesión).
	QString fbIngest_;
	QString fbExpectedTitle_;
	QString fbExpectedDesc_;
	QString fbVerifyId_;
	QString fbLastPrivacy_;
	// ID en curso de la lectura de estado (reintento fallback sin
	// privacy sobre el MISMO ID; una sola relectura, sin polling).
	QString fbStatusId_;
	// T-077 (Fase A): opt-in de clave persistente (defecto OFF).
	// Solo se envia con activacion explicita via setFbPersistent
	// (checkbox de la vista, ux-ui); F-092 sin promesa de URL unica.
	bool fbPersistent_ = false;
	// Cambio A (Independent): valores ENVIADOS en el update en curso
	// (titulo/desc vigentes al enviar + MISMO liveId). Se fijan al
	// enviar y se consumen en el reply de exito; leer los widgets en
	// el reply correria riesgo de edicion intermedia.
	QString fbPendingApplyId_;
	QString fbPendingApplyTitle_;
	QString fbPendingApplyDesc_;
	// Cambio B: go_live aceptado con relectura encadenada pendiente.
	// La relectura informa lo observado y consume este flag.
	bool fbGoLiveConfirm_ = false;
	// Fase A auto-preparacion (un solo intento por arranque, sin loops,
	// sin modales: todo por labels). fbAutoActive_=ciclo en curso;
	// prev*=anterior persistido/probado; didCreate/didDelete=presupuesto
	// maximo 1 create + 1 delete por arranque. fbApplyChain_=UpFb OK con
	// verificacion silenciosa pendiente (no pisa el mensaje de Apply).
	// fbEndAutoArmed_=create pendiente tras End confirmado.
	bool fbAutoActive_ = false;
	// Plan v3 Fase 1: armado del arranque exacto (probe GET en vuelo).
	// fbAutoActive_ SOLO se fija DENTRO, post-GET fresco (en
	// fbAutoHandleRead/fbAutoCreateFresh); jamas desde snapshot.
	bool fbAutoArmed_ = false;
	QString fbAutoPrevId_;
	QString fbAutoPrevStatus_;
	QString fbAutoPrevTitle_;
	QString fbAutoPrevDesc_;
	bool fbAutoDidCreate_ = false;
	bool fbAutoDidDelete_ = false;
	bool fbApplyChain_ = false;
	QString fbApplyChainMsg_;
	QString fbApplyChainId_;
	// Punto 5: create tras End en vuelo (un solo intento, sin tocar el
	// presupuesto del arranque; el terminado queda como VOD, sin delete).
	bool fbEndAutoPending_ = false;
	// ID del DELETE automatico del huerfano (punto 2): distingue el
	// delete del arranque del borrado manual del operador.
	QString fbAutoDeleteId_;
	// Fase A punto 4: ultima senal del relay (sesion, sin persistencia).
	relay::Signal relaySignal_;
	// Override de un solo uso para el create Managed parametrizado
	// (pre-creacion v3 sin mutar widgets): lo consume
	// startManagedFbOp("create") para body + expected.
	QString fbCreateTitleOverride_;
	QString fbCreateDescOverride_;
	bool fbUseCreateOverride_ = false;
	// Plan v3 Fase 1: escucha del repetidor (QFileSystemWatcher +
	// respaldo 3 s) + sesion GoLive propia + cola diferida refire.
	// Sin Graph en el respaldo (solo localhost paths/get/live).
	QFileSystemWatcher *fbSigWatcher_ = nullptr;
	QTimer *fbSigBackup_ = nullptr;
	QTimer *fbSigFast_ = nullptr;
	int fbSigFails_ = 0;
	int fbSigFastStep_ = 0;
	qint64 fbSigGoneSince_ = 0;
	QList<bool> fbSigBallot_;
	QString fbSigLastTitle_;
	bool fbSigPrecreated_ = false;
	// LIVE iniciado por el plugin en ESTA sesion (guardas de auto-End).
	// Se fija en noteFbGoLiveSession tras GoLive aceptado+confirmado;
	// se limpia en End/Delete/Adopt/mode-switch/cambio de ID.
	bool fbAutoGoLiveSession_ = false;
	QString fbAutoGoLiveId_;
	qint64 fbAutoGoLiveAtMs_ = 0;
	// Confirmacion Graph fresca pendiente para el auto-End (un solo
	// status del MISMO ID; el reply decide el End).
	bool fbSigEndConfirm_ = false;
	// Cola diferida de un disparo para el refire en cooldown.
	struct FbRefire {
		QString liveId;
		QString dest;
		qint64 fireAtMs = 0;
		QTimer *timer = nullptr;
	};
	FbRefire fbRefire_;
	meta::FbVerifyResult fbVerify_ = {meta::FbVerify::Blocked,
					  QStringLiteral("not-verified")};
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
	QLabel *fbVerifyLabel_ = nullptr;
	QPushButton *fbStatusButton_ = nullptr;
	QPushButton *fbAdoptButton_ = nullptr;
	QPushButton *fbCreateButton_ = nullptr;
	// T-078 Fase B (vista): opt-in de clave persistente (defecto OFF).
	// Solo vista: el checkbox fija fbPersistent_ via setFbPersistent();
	// la logica solo lo lee al construir el body. Texto honesto F-092.
	QCheckBox *fbPersistentCheck_ = nullptr;
	QPushButton *fbGoLiveButton_ = nullptr;
	QPushButton *fbEndButton_ = nullptr;
	QPushButton *fbDeleteButton_ = nullptr;
	// T-073 FB-4: status encadenado tras on/off (F-085). Cuando es true,
	// el read-back actualiza el indicador sin pisar el mensaje de la
	// operacion (sigue siendo una lectura a peticion, sin polling).
	bool fbQuietStatus_ = false;
	// PoC ingestión: valor completo visible/copiable, oculto por
	// defecto. Solo memoria de sesión (fbIngest_); jamás en Store.
	QLabel *fbIngestLabel_ = nullptr;
	QLineEdit *fbIngestEdit_ = nullptr;
	QPushButton *fbIngestShowButton_ = nullptr;
	QPushButton *fbIngestCopyButton_ = nullptr;
	QLabel *devicePrompt_ = nullptr;
	QPushButton *applyButton_ = nullptr;
	// F-C2: "Borrar mis datos" (solo Managed; Independent intacto).
	QPushButton *eraseButton_ = nullptr;
	// Fase 4: formulario de cuenta Managed (username+password v1, sin
	// email). Solo memoria, jamás Store/DPAPI. Visible solo en Managed.
	// f6 ENTRAR-primero: el formulario de cuenta vive en la página
	// ENTRAR del QStackedWidget (Managed sin sesión); el botón Salir
	// vive en la página PANEL. Independent intacto (siempre PANEL).
	QLabel *modeLabel_ = nullptr;
	QStackedWidget *managedStack_ = nullptr;
	QWidget *loginPage_ = nullptr;
	QWidget *panelPage_ = nullptr;
	QLabel *panelSession_ = nullptr;
	QLineEdit *userNameEdit_ = nullptr;
	QLineEdit *userPassEdit_ = nullptr;
	QPushButton *userLoginBtn_ = nullptr;
	QPushButton *userSignupBtn_ = nullptr;
	QPushButton *userLogoutBtn_ = nullptr;
	QLabel *userStatus_ = nullptr;
	QLabel *userNotice_ = nullptr;
	// f8 REDISENO ENTRAR (2026-10-26 [D]): ENTRAR en dos modos
	// excluyentes con QTabWidget ("Entrar | Crear cuenta"). El modo
	// Entrar reusa userNameEdit_/userPassEdit_/userLoginBtn_; el modo
	// Crear cuenta tiene sus propios campos (signupNameEdit_/
	// signupPassEdit_) + el boton Crear cuenta (userSignupBtn_
	// reparentado) + aviso visible de la politica (signupPolicy_).
	// Errores inline junto a cada campo; jamas red sin validacion.
	QTabWidget *authTabs_ = nullptr;
	QWidget *loginTab_ = nullptr;
	QWidget *signupTab_ = nullptr;
	QLineEdit *signupNameEdit_ = nullptr;
	QLineEdit *signupPassEdit_ = nullptr;
	QLabel *loginNameError_ = nullptr;
	QLabel *loginPassError_ = nullptr;
	QLabel *signupNameError_ = nullptr;
	QLabel *signupPassError_ = nullptr;
	QLabel *signupPolicy_ = nullptr;
	meta::ManagedAuthMode authMode() const;
	// Fase 5b: borrado de CUENTA Managed (POST /users/erase en cascada).
	// Operación distinta de POST /privacy/erase (F-C2, por
	// instalación): aquí cae el usuario con TODAS sus instalaciones.
	QPushButton *eraseAccountButton_ = nullptr;
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
	// T-078 Fase 3 (cableado): gestor async del proceso hijo MediaMTX
	// (ADR-016) + ultimo estado conocido (indicador por estados; la
	// vista redacta). La ingesta (fbIngest_) jamas llega al Store.
	RelayManager *relay_ = nullptr;
	relay::State relayState_ = relay::State::Stopped;
	// T-078 Fase 4 (vista): seccion relay en la tarjeta Facebook.
	// Solo widgets/visibilidad; sin estado nuevo sensible. Los
	// mensajes del gestor se muestran tal cual en relayMsgLabel_
	// (jamas pisan fbState_/fbVerify_/generalMsg_).
	QLabel *relaySectionLabel_ = nullptr;
	QLineEdit *relayFixedEdit_ = nullptr;
	QPushButton *relayFixedCopy_ = nullptr;
	QLabel *relayStateLabel_ = nullptr;
	QPushButton *relayToggleButton_ = nullptr;
	QLabel *relayMsgLabel_ = nullptr;
	QLabel *relayVodLabel_ = nullptr;
	// T-078 Fase B (vista simplificada): resumen compuesto global
	// (encima de titulo/Aplicar) + senal del emisor + Avanzado
	// colapsado por defecto. Solo widgets/visibilidad; el estado
	// crudo sigue en fbLastStatus_/fbVerify_/relaySignal_.
	QLabel *fbSummary_ = nullptr;
	QLabel *fbSignalLabel_ = nullptr;
	QPushButton *fbSignalCheck_ = nullptr;
	QPushButton *fbAdvancedToggle_ = nullptr;
	QWidget *fbAdvanced_ = nullptr;
	// Plan v3 (vista): semaforo en cabecera FB (icono sin texto) +
	// contador ~mm:ss junto al semaforo. Solo widgets + cache; el
	// estado crudo sigue en fbLastStatus_/fbVerify_/relaySignal_.
	QLabel *fbSemaLabel_ = nullptr;
	QLabel *fbCooldownLabel_ = nullptr;
	QTimer *fbCooldownTick_ = nullptr;
	QString fbSemaCache_;
	QString fbCooldownCache_;
	// Plan v3 (vista): check global no-volver-a-mostrar del aviso de
	// audiencia (defecto OFF) + detalle de riesgo F-093 en lenguaje
	// de persona. Persistente plaintext (QSettings); sin secretos.
	QCheckBox *fbAudienceHideCheck_ = nullptr;
	QLabel *fbAudienceRisk_ = nullptr;
	bool fbAudienceHide_ = false;
	QString fbLastStatus_;
	bool fbAdvancedOpen_ = false;
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
