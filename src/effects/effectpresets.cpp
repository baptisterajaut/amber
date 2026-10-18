#include "effectpresets.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <algorithm>

#include "core/path.h"

namespace {

const QLatin1String kRootTag("effectpreset");

// The display name and subtype of the preset file `fi`, false if it isn't one. Only the root element is read.
bool ReadPresetRoot(const QFileInfo& fi, QString* name, int* subtype) {
  QFile f(fi.filePath());
  if (!f.open(QFile::ReadOnly)) return false;
  QXmlStreamReader stream(&f);
  if (!stream.readNextStartElement() || stream.name() != kRootTag) return false;
  const QXmlStreamAttributes attrs = stream.attributes();
  bool ok = false;
  *subtype = attrs.value("subtype").toInt(&ok);
  if (!ok) return false;
  *name = attrs.value("name").toString();
  if (name->isEmpty()) *name = fi.completeBaseName();
  return true;
}

}  // namespace

QString EffectPresetDir() { return get_data_path() + "/effectpresets"; }

QString EffectPresetPathForName(const QString& name) {
  QString file_name = name.left(64);
  file_name.replace(QRegularExpression("[^a-zA-Z0-9 _-]"), "_");
  const QByteArray hash = QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha1).toHex().left(8);
  return EffectPresetDir() + "/" + file_name + "-" + QString::fromLatin1(hash) + ".xml";
}

QString EffectPresetSavePath(const QString& name, QString* replaced) {
  replaced->clear();
  QString stored;
  int subtype = 0;
  for (const QFileInfo& fi : QDir(EffectPresetDir()).entryInfoList({"*.xml"}, QDir::Files)) {
    if (ReadPresetRoot(fi, &stored, &subtype) && stored == name) {
      *replaced = stored;
      return fi.filePath();
    }
  }
  // No preset shows this name, but the file can still exist (not a preset, or unreadable): name what it holds
  const QString path = EffectPresetPathForName(name);
  const QFileInfo fi(path);
  if (fi.exists()) *replaced = ReadPresetRoot(fi, &stored, &subtype) ? stored : fi.completeBaseName();
  return path;
}

QVector<EffectPresetInfo> ListEffectPresets(int subtype) {
  QVector<EffectPresetInfo> presets;
  QDir dir(EffectPresetDir());
  for (const QFileInfo& fi : dir.entryInfoList({"*.xml"}, QDir::Files)) {
    QString name;
    int file_subtype = 0;
    if (!ReadPresetRoot(fi, &name, &file_subtype) || file_subtype != subtype) continue;
    presets.append({name, fi.filePath(), file_subtype});
  }
  std::sort(presets.begin(), presets.end(), [](const EffectPresetInfo& a, const EffectPresetInfo& b) {
    return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
  });
  return presets;
}

bool SaveEffectPreset(const QString& path, const QString& name, int subtype, const QVector<Effect*>& effects) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile f(path);
  if (!f.open(QFile::WriteOnly)) return false;

  QXmlStreamWriter stream(&f);
  stream.setAutoFormatting(true);
  stream.writeStartDocument();
  stream.writeStartElement(kRootTag);
  stream.writeAttribute("version", "1");
  stream.writeAttribute("name", name);
  stream.writeAttribute("subtype", QString::number(subtype));
  for (Effect* e : effects) {
    stream.writeStartElement("effect");
    e->save(stream);
    stream.writeEndElement();
  }
  stream.writeEndElement();  // effectpreset
  stream.writeEndDocument();

  return !stream.hasError() && f.commit();
}

QVector<EffectPtr> LoadEffectPreset(const QString& path, Clip* parent, int* missing) {
  *missing = 0;
  QVector<EffectPtr> loaded;
  QFile f(path);
  if (!f.open(QFile::ReadOnly)) return loaded;

  // Same rebuild as parse_effect_into_clip (project/clipboard_serializer.cpp), minus the VoidEffect placeholder: a
  // preset has no project to keep an unknown effect's data for
  QXmlStreamReader stream(&f);
  if (!stream.readNextStartElement() || stream.name() != kRootTag) return loaded;
  while (stream.readNextStartElement()) {
    if (stream.name() != QLatin1String("effect")) {
      stream.skipCurrentElement();
      continue;
    }
    const QXmlStreamAttributes attrs = stream.attributes();
    const QString effect_name = attrs.value("name").toString();
    const EffectMeta* meta = effect_name.isEmpty() ? nullptr : get_meta_from_attributes(attrs);
    EffectPtr e = (meta != nullptr && meta->type == EFFECT_TYPE_EFFECT) ? Effect::Create(parent, meta) : nullptr;
    if (e == nullptr) {
      (*missing)++;
      stream.skipCurrentElement();
      continue;
    }
    e->SetEnabled(attrs.value("enabled") != QLatin1String("0"));
    e->load(stream);  // consumes up to </effect>
    loaded.append(e);
  }
  return loaded;
}
