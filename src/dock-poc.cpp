/*
obs-stream-metadata — Stream Metadata dock PoC (T-020)
Contenido mínimo del dock nativo. Sin red, sin cuentas, sin OAuth.
*/

#include "dock-poc.h"

#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QDockWidget>
#include <QLabel>
#include <QVBoxLayout>
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

	QWidget *widget = new QWidget();
	QVBoxLayout *layout = new QVBoxLayout(widget);

	QLabel *title = new QLabel("Stream Metadata", widget);
	QLabel *subtitle = new QLabel("Dock PoC", widget);
	QLabel *status = new QLabel("OBS 32.2.2\nFrontend API OK", widget);

	layout->addWidget(title);
	layout->addWidget(subtitle);
	layout->addWidget(status);
	widget->setLayout(layout);

	if (!obs_frontend_add_dock_by_id(DOCK_ID, DOCK_TITLE, widget)) {
		obs_log(LOG_WARNING, "dock registration failed, dropping widget");
		delete widget;
		return false;
	}

	dock_widget = widget;
	const QList<QLabel *> labels = widget->findChildren<QLabel *>();
	obs_log(LOG_INFO, "dock created (%d labels)", labels.size());

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
