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
{
	if (o.isEmpty() || !o.value(QStringLiteral("connected")).toBool())
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

} // namespace

Store::Store(const QString &filePath) : filePath_(filePath) {}

bool Store::save(const Data &d)
{
#ifdef Q_OS_WIN
	if (filePath_.isEmpty())
		return false;
	QDir().mkpath(QFileInfo(filePath_).absolutePath());
	const Record *recs[3] = {&d.twitch, &d.youtube, &d.kick};
	const char *keys[3] = {"twitch", "youtube", "kick"};
	QJsonObject root;
	for (int i = 0; i < 3; ++i) {
		if (!recs[i]->connected)
			continue;
		const QJsonObject o = recordToJson(*recs[i]);
		if (o.isEmpty())
			return false; // DPAPI failed: persist nothing
		root[QString::fromLatin1(keys[i])] = o;
	}
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
	if (!any)
		d = Data();
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
