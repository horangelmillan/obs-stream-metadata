/*
T-032: DPAPI-backed account store. QtCore + Win32 only, no network.
*/

#include "secure_store.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dpapi.h>
#endif

namespace secure {

namespace {

// T-041: root-level mode key. Plaintext (context, not a secret).
const char *kConnectionMode = "connection_mode";

// Canonical wire strings (T-049 helpers are the single source of truth).
QString modeToString(meta::ConnectionMode m)
{
	return QString::fromLatin1(meta::connectionModeName(m)).toLower();
}

meta::ConnectionMode modeFromString(const QString &s)
{
	const std::optional<meta::ConnectionMode> m =
		meta::parseConnectionMode(s.trimmed().toLower());
	// Legacy (missing) and invalid values map to Independent, never to
	// Managed: no silent upgrade into the service-operated mode.
	return m.value_or(meta::defaultConnectionMode());
}

// Sensitive payload keys. Never written outside a DPAPI blob.
const char *kAccess = "a";
const char *kRefresh = "r";
const char *kClientId = "c";
const char *kSecret = "s";

QJsonObject sensitiveToJson(const Record &r)
{
	QJsonObject o;
	o[QString::fromLatin1(kAccess)] = r.access;
	o[QString::fromLatin1(kRefresh)] = r.refresh;
	o[QString::fromLatin1(kClientId)] = r.clientId;
	o[QString::fromLatin1(kSecret)] = r.secret;
	return o;
}

Record sensitiveFromJson(const Record &base, const QJsonObject &o)
{
	Record r = base;
	r.access = o.value(QString::fromLatin1(kAccess)).toString();
	r.refresh = o.value(QString::fromLatin1(kRefresh)).toString();
	r.clientId = o.value(QString::fromLatin1(kClientId)).toString();
	r.secret = o.value(QString::fromLatin1(kSecret)).toString();
	return r;
}

#ifdef Q_OS_WIN
// DPAPI round-trip on UTF-8 bytes. Empty input -> empty output (no call).
QByteArray protect(const QByteArray &plain)
{
	if (plain.isEmpty())
		return QByteArray();
	DATA_BLOB in, out;
	in.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(
		plain.constData()));
	in.cbData = static_cast<DWORD>(plain.size());
	out.pbData = nullptr;
	out.cbData = 0;
	if (!CryptProtectData(&in, nullptr, nullptr, nullptr, nullptr,
			     CRYPTPROTECT_UI_FORBIDDEN, &out))
		return QByteArray();
	QByteArray enc(reinterpret_cast<char *>(out.pbData),
		       static_cast<int>(out.cbData));
	LocalFree(out.pbData);
	return enc;
}

QByteArray unprotect(const QByteArray &enc)
{
	if (enc.isEmpty())
		return QByteArray();
	DATA_BLOB in, out;
	in.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(
		enc.constData()));
	in.cbData = static_cast<DWORD>(enc.size());
	out.pbData = nullptr;
	out.cbData = 0;
	if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
			       CRYPTPROTECT_UI_FORBIDDEN, &out))
		return QByteArray("__dpapi_fail__");
	QByteArray plain(reinterpret_cast<char *>(out.pbData),
			 static_cast<int>(out.cbData));
	LocalFree(out.pbData);
	return plain;
}
#endif

QJsonObject recordToJson(const Record &r)
{
	QJsonObject o;
	o[QStringLiteral("display")] = r.display;
	o[QStringLiteral("broadcaster")] = r.broadcaster;
	o[QStringLiteral("connected")] = r.connected;
#ifdef Q_OS_WIN
	const QByteArray blob = protect(QJsonDocument(sensitiveToJson(r))
						.toJson(QJsonDocument::Compact));
	if (r.connected && blob.isEmpty())
		return QJsonObject(); // signal failure via empty object
	o[QStringLiteral("blob")] = QString::fromLatin1(blob.toBase64());
#else
	Q_UNUSED(r);
	return QJsonObject();
#endif
	return o;
}

bool recordFromJson(const QJsonObject &o, Record &r)
{	if (o.isEmpty() || !o.value(QStringLiteral("connected")).toBool())
		return false;
	r.display = o.value(QStringLiteral("display")).toString();
	r.broadcaster =
		o.value(QStringLiteral("broadcaster")).toString();
#ifdef Q_OS_WIN
	const QByteArray blob = QByteArray::fromBase64(
		o.value(QStringLiteral("blob")).toString().toLatin1());
	const QByteArray plain = unprotect(blob);
	if (plain == QByteArray("__dpapi_fail__") || plain.isEmpty())
		return false;
	const QJsonObject s =
		QJsonDocument::fromJson(plain).object();
	if (s.isEmpty())
		return false;
	r = sensitiveFromJson(r, s);
	r.connected = true;
	return !r.access.isEmpty();
#else
	return false;
#endif
}

// T-044: installation identity record. Same DPAPI envelope as provider
// records, but success requires clientId (installation_id), not access.
bool installFromJson(const QJsonObject &o, Record &r)
{
	if (o.isEmpty() || !o.value(QStringLiteral("connected")).toBool())
		return false;
#ifdef Q_OS_WIN
	const QByteArray blob = QByteArray::fromBase64(
		o.value(QStringLiteral("blob")).toString().toLatin1());
	const QByteArray plain = unprotect(blob);
	if (plain == QByteArray("__dpapi_fail__") || plain.isEmpty())
		return false;
	const QJsonObject s =
		QJsonDocument::fromJson(plain).object();
	if (s.isEmpty())
		return false;
	r = sensitiveFromJson(r, s);
	r.connected = true;
	return !r.clientId.isEmpty() && !r.secret.isEmpty();
#else
	Q_UNUSED(o);
	Q_UNUSED(r);
	return false;
#endif
}

// T-051: Managed snapshot serialization. Plaintext by design (identity
// labels only, same class as display/broadcaster). Strict shape: anything
// malformed (wrong types, missing fields) means disconnected, never a
// half-restored connection. No DPAPI involved on any platform.
QJsonObject managedToJson(const ManagedSnapshot &m)
{
	QJsonObject o;
	o[QStringLiteral("connected")] = m.connected;
	o[QStringLiteral("user_id")] = m.userId;
	o[QStringLiteral("display")] = m.display;
	return o;
}

bool managedFromJson(const QJsonObject &o, ManagedSnapshot &m)
{
	m = ManagedSnapshot();
	if (o.isEmpty() || !o.value(QStringLiteral("connected")).isBool() ||
	    !o.value(QStringLiteral("connected")).toBool())
		return false;
	if (!o.value(QStringLiteral("user_id")).isString() ||
	    !o.value(QStringLiteral("display")).isString())
		return false;
	m.connected = true;
	m.userId = o.value(QStringLiteral("user_id")).toString();
	m.display = o.value(QStringLiteral("display")).toString();
	return !m.userId.isEmpty() && !m.display.isEmpty();
}

} // namespace

Store::Store(const QString &filePath) : filePath_(filePath) {}

bool Store::save(const Data &d)
{
#ifdef Q_OS_WIN
	if (filePath_.isEmpty())
		return false;
	QDir().mkpath(QFileInfo(filePath_).absolutePath());
	const Record *recs[4] = {&d.twitch, &d.youtube, &d.kick,
				 &d.backendInstall};
	const char *keys[4] = {"twitch", "youtube", "kick", "backend"};
	QJsonObject root;
	for (int i = 0; i < 4; ++i) {
		if (!recs[i]->connected)
			continue;
		const QJsonObject o = recordToJson(*recs[i]);
		if (o.isEmpty())
			return false; // DPAPI failed: persist nothing
		root[QString::fromLatin1(keys[i])] = o;
	}
	// T-041: the mode is always explicit once saved (idempotent
	// migration); empty input means legacy Independent.
	const QString mode = d.connectionMode.isEmpty()
				     ? modeToString(
					       meta::defaultConnectionMode())
				     : d.connectionMode;
	root[QString::fromLatin1(kConnectionMode)] = mode;
	// T-051: managed snapshots are plaintext identity labels (no blob,
	// no DPAPI). Omitted when disconnected so the file carries no stale
	// or half-restored connections.
	if (d.managedYoutube.connected)
		root[QStringLiteral("managed_youtube")] =
			managedToJson(d.managedYoutube);
	if (d.managedKick.connected)
		root[QStringLiteral("managed_kick")] =
			managedToJson(d.managedKick);
	QSaveFile f(filePath_);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
		return false;
	f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
	return f.commit();
#else
	Q_UNUSED(d);
	return false;
#endif
}

bool Store::load(Data &d)
{
#ifdef Q_OS_WIN
	d = Data();
	// The mode is always canonical on the way out (legacy callers see
	// Independent for missing/invalid files, never Managed).
	d.connectionMode = modeToString(meta::defaultConnectionMode());
	if (filePath_.isEmpty())
		return false;
	QFile f(filePath_);
	if (!f.open(QIODevice::ReadOnly))
		return false; // no file yet: fresh start, not an error
	const QJsonObject root =
		QJsonDocument::fromJson(f.readAll()).object();
	if (root.isEmpty())
		return false;
	bool any = false;
	any |= recordFromJson(root.value(QStringLiteral("twitch"))
				      .toObject(),
			      d.twitch);
	any |= recordFromJson(root.value(QStringLiteral("youtube"))
				      .toObject(),
			      d.youtube);
	any |= recordFromJson(root.value(QStringLiteral("kick")).toObject(),
			      d.kick);
	// Missing "backend" key in pre-T-044 files: installFromJson on an
	// empty object returns false, existing behavior unchanged. The
	// installation identity is preserved even when no provider is
	// connected (any stays provider-only by design).
	const bool backendOk = installFromJson(
		root.value(QStringLiteral("backend")).toObject(),
		d.backendInstall);
	// T-041: missing/invalid mode (pre-T-041 files) maps to Independent,
	// never to Managed. Parsed after the reset below so it survives it.
	const meta::ConnectionMode mode = modeFromString(
		root.value(QString::fromLatin1(kConnectionMode)).toString());
	// T-051: pre-T-051 files simply lack these keys (-> disconnected).
	// Parsed before the reset so snapshots survive it, like backendInstall.
	const bool ytManaged = managedFromJson(
		root.value(QStringLiteral("managed_youtube")).toObject(),
		d.managedYoutube);
	const bool kkManaged = managedFromJson(
		root.value(QStringLiteral("managed_kick")).toObject(),
		d.managedKick);
	if (!any && !backendOk && !ytManaged && !kkManaged)
		d = Data();
	d.connectionMode = modeToString(mode);
	return any;
#else
	d = Data();
	return false;
#endif
}

bool Store::clear()
{
	if (filePath_.isEmpty())
		return false;
	if (!QFile::exists(filePath_))
		return true;
	return QFile::remove(filePath_);
}

} // namespace secure
