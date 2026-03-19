#include "audio_ui.h"

#include <QComboBox>

void combobox_audio_sample_rates(QComboBox* combobox) {
  combobox->addItem("22050 Hz", 22050);
  combobox->addItem("24000 Hz", 24000);
  combobox->addItem("32000 Hz", 32000);
  combobox->addItem("44100 Hz", 44100);
  combobox->addItem("48000 Hz", 48000);
  combobox->addItem("88200 Hz", 88200);
  combobox->addItem("96000 Hz", 96000);
}
