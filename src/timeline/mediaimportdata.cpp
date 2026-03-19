#include "mediaimportdata.h"

amber::timeline::MediaImportData::MediaImportData(Media *media, amber::timeline::MediaImportType import_type) :
  media_(media),
  import_type_(import_type)
{
}

Media *amber::timeline::MediaImportData::media() const
{
  return media_;
}

amber::timeline::MediaImportType amber::timeline::MediaImportData::type() const
{
  return import_type_;
}
