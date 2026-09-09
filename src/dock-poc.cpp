/*
obs-stream-metadata — Stream Metadata dock (T-031 MVP).
Hosts the product MetadataDock widget. Lifecycle/ownership unchanged
from the P2 PoC (F-013): OBS owns the widget, never delete it here.
*/

#include "dock-poc.h"

#include "metadata_dock.h"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QCoreApplication>
#include <QDockWidget>
#include <QSslSocket>
#include <QWidget>

/* Identificador estable del dock (no traducible). OBS persiste la
 * geometría/visibilidad por objectName, así que debe ser único y estable. */
#define DOCK_ID "obs-stream-metadata-dock"
#define DOCK_TITLE "Stream Metadata"

/* Propiedad de OBS tras obs_frontend_add_dock_by_id (el OBSDock reparenta
 * el widget vía setWidget). Nunca borrar a mano: tras destroy() el
 * puntero queda colgando y debe tratarse como nulo. */
static QWidget *dock_widget = nullptr;

bool stream_metadata_dock_create(void)
{
	if (dock_widget) {
		obs_log(LOG_WARNING, "dock already created");
		return true;
	}

	QWidget *widget = new MetadataDock();

	/* OBS ships no Qt TLS backend (no tls/ plugin dir in its Qt
	 * runtime): HTTPS from QNetworkAccessManager fails without it.
	 * Our install carries qschannelbackend.dll under our data dir
	 * (F-027); appending it is enough, QSslSocket resolves lazily. */
	const char *module_data =
		obs_get_module_data_path(obs_current_module());
	if (module_data) {
		QCoreApplication::addLibraryPath(
			QString::fromUtf8(module_data));
	}
	obs_log(LOG_INFO, "tls backend ready: %s",
		QSslSocket::supportsSsl() ? "yes" : "no");

	if (!obs_frontend_add_dock_by_id(DOCK_ID, DOCK_TITLE, widget)) {
		obs_log(LOG_WARNING, "dock registration failed, dropping widget");
		delete widget;
		return false;
	}

	dock_widget = widget;
	obs_log(LOG_INFO, "dock created (metadata mvp)");

	/* PoC: mostrar el dock al arrancar. El layout guardado de OBS
	 * (restoreState) se aplica DESPUÉS de cargar módulos, así que la
	 * preferencia del usuario prevalece en arranques posteriores. */
	QWidget *main = static_cast<QWidget *>(obs_frontend_get_main_window());
	QDockWidget *dock = main ? main->findChild<QDockWidget *>(DOCK_ID) : nullptr;
	if (dock) {
		dock->show();
		obs_log(LOG_INFO, "dock shown");
	} else {
		obs_log(LOG_WARNING, "dock wrapper not found");
	}
	return true;
}

void stream_metadata_dock_destroy(void)
{
	if (!dock_widget) {
		return;
	}

	/* OBS destruye el widget (hijo del OBSDock). No tocar dock_widget. */
	obs_frontend_remove_dock(DOCK_ID);
	dock_widget = nullptr;
	obs_log(LOG_INFO, "dock removed");
}
