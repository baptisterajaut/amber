#include "layoutpresets.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <algorithm>

namespace amber {

QString sanitize_layout_name(const QString& name) {
  QString out = name;
  for (QChar& c : out) {
    const char16_t u = c.unicode();
    const bool ok =
        (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || u == ' ' || u == '_' || u == '-';
    if (!ok) c = QLatin1Char('_');
  }
  return out;
}

bool write_layout_file(const QString& path, const LayoutData& data) {
  QFile file(path);
  if (!file.open(QFile::WriteOnly)) return false;

  QXmlStreamWriter stream(&file);
  stream.setAutoFormatting(true);
  stream.writeStartDocument();

  stream.writeStartElement("layout");
  if (!data.name.isEmpty()) stream.writeAttribute("name", data.name);

  stream.writeTextElement("panels", data.state.toBase64());

  for (const auto& panel : data.panels) {
    stream.writeStartElement("panel");
    stream.writeAttribute("name", panel.first);
    stream.writeCharacters(panel.second.toBase64());
    stream.writeEndElement();
  }

  stream.writeEndElement();  // layout

  stream.writeEndDocument();
  file.close();
  return !stream.hasError() && file.error() == QFile::NoError;
}

bool read_layout_file(const QString& path, LayoutData* data) {
  *data = LayoutData();
  QFile file(path);
  if (!file.open(QFile::ReadOnly)) return false;

  QXmlStreamReader stream(&file);
  if (!stream.readNextStartElement() || stream.name() != QLatin1String("layout")) return false;
  data->name = stream.attributes().value(QLatin1String("name")).toString();

  while (stream.readNextStartElement()) {
    if (stream.name() == QLatin1String("panels")) {
      data->state = QByteArray::fromBase64(stream.readElementText().toUtf8());
    } else if (stream.name() == QLatin1String("panel")) {
      QString panel_name = stream.attributes().value(QLatin1String("name")).toString();
      QByteArray panel_data = QByteArray::fromBase64(stream.readElementText().toUtf8());
      if (panel_name.isEmpty()) {
        qWarning() << "Layout file specified data for a panel but didn't specify a name. Panel layout wasn't loaded.";
      } else {
        data->panels.append({panel_name, panel_data});
      }
    } else {
      stream.skipCurrentElement();
    }
  }

  return !stream.hasError() && !data->state.isEmpty();
}

QVector<LayoutEntry> list_layouts(const QDir& dir) {
  QVector<LayoutEntry> list;
  for (const QString& entry : dir.entryList({"*.xml"}, QDir::Files)) {
    LayoutEntry layout;
    layout.path = dir.filePath(entry);
    LayoutData data;
    if (!read_layout_file(layout.path, &data)) continue;
    layout.name = data.name.isEmpty() ? QFileInfo(entry).completeBaseName() : data.name;
    list.append(layout);
  }
  std::sort(list.begin(), list.end(),
            [](const LayoutEntry& a, const LayoutEntry& b) { return a.name.compare(b.name, Qt::CaseInsensitive) < 0; });
  return list;
}

}  // namespace amber
