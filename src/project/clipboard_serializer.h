#ifndef CLIPBOARD_SERIALIZER_H
#define CLIPBOARD_SERIALIZER_H

#include <QByteArray>
#include <QVector>

#include "project/media.h"

namespace amber {

constexpr const char* kClipboardMime = "application/x-amber-clipboard";

QByteArray serialize_clipboard_to_xml();

bool deserialize_clipboard_from_xml(const QByteArray& xml, QVector<MediaPtr>& imported_media);

void push_clipboard_to_system();

bool pull_clipboard_from_system(QVector<MediaPtr>& imported_media);

}  // namespace amber

#endif  // CLIPBOARD_SERIALIZER_H
