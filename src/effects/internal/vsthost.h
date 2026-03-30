/***

    Olive - Non-Linear Video Editor
    Copyright (C) 2019  Olive Team

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#ifndef VSTHOSTWIN_H
#define VSTHOSTWIN_H

#include <QDialog>
#include <QLibrary>
#include <QTimer>

#include "effects/effect.h"
#include "include/vestige.h"

// Plugin's dispatcher function
using dispatcherFuncPtr = intptr_t (*)(AEffect *effect, int32_t opCode, int32_t index, int32_t value, void *ptr, float opt);

class VSTHost : public Effect {
  Q_OBJECT
public:
  VSTHost(Clip* c, const EffectMeta* em);
  ~VSTHost() override;
  void process_audio(double timecode_start, double timecode_end, quint8* samples, int nb_bytes, int channel_count) override;

  void custom_load(QXmlStreamReader& stream) override;
  void save(QXmlStreamWriter& stream) override;
private slots:
  void show_interface(bool show);
  void uncheck_show_button();
  void change_plugin();
private:
  FileField* file_field;
  ButtonField* show_interface_btn;

  void loadPlugin();
  void freePlugin();
  dispatcherFuncPtr dispatcher;
  AEffect* plugin{nullptr};
  bool configurePluginCallbacks();
  void startPlugin();
  void stopPlugin();
  void resumePlugin();
  void suspendPlugin();
  bool canPluginDo(char *canDoString);
  void processAudio(long numFrames);
  void CreateDialogIfNull();
  float** inputs;
  float** outputs;
  QDialog* dialog{nullptr};
  QByteArray data_cache;

  void send_data_cache_to_plugin();

  QLibrary modulePtr;
  QTimer* idle_timer{nullptr};

#if defined(Q_OS_LINUX)
  void* x11_display_{nullptr};
  unsigned long x11_window_{0};
  unsigned long x11_wm_delete_{0};
#endif
};

#endif // VSTHOSTWIN_H
