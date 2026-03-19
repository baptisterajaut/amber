#ifndef MEDIAIMPORTDATA_H
#define MEDIAIMPORTDATA_H

#include "project/media.h"

namespace amber {
namespace timeline {

enum MediaImportType : uint8_t {
  kImportVideoOnly,
  kImportAudioOnly,
  kImportBoth
};

class MediaImportData {
public:
  MediaImportData(Media* media = nullptr, MediaImportType import_type = kImportBoth);
  [[nodiscard]] Media* media() const;
  [[nodiscard]] MediaImportType type() const;
private:
  Media* media_;
  MediaImportType import_type_;
};

}
}


#endif // MEDIAIMPORTDATA_H
