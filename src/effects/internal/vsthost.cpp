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

#include "vsthost.h"

// adapted from http://teragonaudio.com/article/How-to-make-your-own-VST-host.html

#include <QDialog>
#include <QFile>
#include <QPushButton>
#include <QWindow>
#include <QXmlStreamWriter>

#include <cstring>

#include "core/appcontext.h"
#include "global/debug.h"
#include "rendering/audio.h"

// Load libraries for retrieving the native window handle. Used for VST plugins that have a separate window
// dedicated to controls.
#if defined(Q_OS_WIN)
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <CoreFoundation/CoreFoundation.h>
class NSWindow;
#elif defined(Q_OS_LINUX)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

#define BLOCK_SIZE 512
#define CHANNEL_COUNT 2

struct VSTRect {
  int16_t top;
  int16_t left;
  int16_t bottom;
  int16_t right;
};

#define effGetChunk 23
#define effSetChunk 24

static intptr_t hostCallbackGetTime() {
  static VstTimeInfo timeInfo;
  memset(&timeInfo, 0, sizeof(timeInfo));
  timeInfo.samplePos = 0.0;
  timeInfo.sampleRate = current_audio_freq();
  timeInfo.tempo = 120.0;
  timeInfo.timeSigNumerator = 4;
  timeInfo.timeSigDenominator = 4;
  timeInfo.flags = 0;
  return reinterpret_cast<intptr_t>(&timeInfo);
}

static intptr_t hostCallbackCanDo(void* ptr) {
  const char* str = static_cast<const char*>(ptr);
  if (strcmp(str, "sendVstTimeInfo") == 0) return 1;
  if (strcmp(str, "sizeWindow") == 0) return 1;
  if (strcmp(str, "acceptIOChanges") == 0) return 1;
  if (strcmp(str, "startStopProcess") == 0) return 1;
  if (strcmp(str, "sendVstEvents") == 0) return -1;
  if (strcmp(str, "sendVstMidiEvent") == 0) return -1;
  if (strcmp(str, "receiveVstEvents") == 0) return -1;
  if (strcmp(str, "receiveVstMidiEvent") == 0) return -1;
  if (strcmp(str, "offline") == 0) return -1;
  if (strcmp(str, "shellCategory") == 0) return -1;
  if (strcmp(str, "openFileSelector") == 0) return -1;
  if (strcmp(str, "closeFileSelector") == 0) return -1;
  return 0;
}

// C callbacks
extern "C" {
// Main host callback
intptr_t hostCallback(AEffect* effect, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt) {
  Q_UNUSED(value)

  switch (opcode) {
    case audioMasterAutomate:
      effect->setParameter(effect, index, opt);
      break;
    case audioMasterVersion:
      return 2400;
    case audioMasterIdle:
      effect->dispatcher(effect, effEditIdle, 0, 0, nullptr, 0);
      break;
    case audioMasterWantMidi:
      return 0;
    case audioMasterGetTime:
      return hostCallbackGetTime();
    case audioMasterIOChanged:
      return 1;
    case audioMasterSizeWindow:
      // would need a back-pointer from AEffect to VSTHost to resize the dialog
      return 0;
    case audioMasterGetSampleRate:
      return current_audio_freq();
    case audioMasterGetBlockSize:
      return BLOCK_SIZE;
    case audioMasterGetCurrentProcessLevel:
      return 0;
    case audioMasterGetVendorString:
      strncpy(static_cast<char*>(ptr), "Amber Video Editor", 63);
      static_cast<char*>(ptr)[63] = '\0';
      return 1;
    case audioMasterGetProductString:
      strncpy(static_cast<char*>(ptr), "Amber", 63);
      static_cast<char*>(ptr)[63] = '\0';
      return 1;
    case audioMasterGetVendorVersion:
      return 1400;
    case audioMasterCanDo:
      return hostCallbackCanDo(ptr);
    case audioMasterGetLanguage:
      return kVstLangEnglish;
    case audioMasterUpdateDisplay:
      return 0;
    case audioMasterBeginEdit:
      return 0;
    case audioMasterEndEdit:
      amber::app_ctx->setModified(true);
      return 0;
    default:
      qInfo() << "Plugin requested unhandled opcode" << opcode;
  }
  return 0;
}
}

// Plugin's entry point
using vstPluginFuncPtr = AEffect* (*)(audioMasterCallback host);
// Plugin's getParameter() method
using getParameterFuncPtr = float (*)(AEffect* effect, int32_t index);
// Plugin's setParameter() method
using setParameterFuncPtr = void (*)(AEffect* effect, int32_t index, float value);
// Plugin's processEvents() method
using processEventsFuncPtr = int32_t (*)(VstEvents* events);
// Plugin's process() method
using processFuncPtr = void (*)(AEffect* effect, float** inputs, float** outputs, int32_t sampleFrames);

void VSTHost::loadPlugin() {
  QString dll_fn = file_field->GetFileAt(0);

  if (dll_fn.isEmpty()) {
    return;
  }

  // Try to load the plugin
  modulePtr.setFileName(dll_fn);
  if (!modulePtr.load()) {
    // Show an error if the plugin fails to load

    qCritical() << "Failed to load VST plugin" << dll_fn << "-" << modulePtr.errorString();
    amber::app_ctx->showMessage(tr("Error loading VST plugin"),
                                tr("Failed to load VST plugin \"%1\": %2").arg(dll_fn, modulePtr.errorString()), 3);
    return;
  }

  // Try to find the VST entry point (first using VSTPluginMain() )
  vstPluginFuncPtr mainEntryPoint = reinterpret_cast<vstPluginFuncPtr>(modulePtr.resolve("VSTPluginMain"));

  if (mainEntryPoint == nullptr) {
    // If there's no VSTPluginMain(), the plugin may use main() instead
    mainEntryPoint = reinterpret_cast<vstPluginFuncPtr>(modulePtr.resolve("main"));
  }

  if (mainEntryPoint == nullptr) {
    amber::app_ctx->showMessage(tr("Error loading VST plugin"), tr("Failed to locate entry point for dynamic library."),
                                3);
    modulePtr.unload();
    return;
  }

  // Instantiate the plugin
  plugin = mainEntryPoint(hostCallback);
}

void VSTHost::freePlugin() {
  if (idle_timer) {
    idle_timer->stop();
  }
  if (plugin != nullptr) {
    stopPlugin();
    data_cache.clear();
    modulePtr.unload();
    plugin = nullptr;
  }
#if defined(Q_OS_LINUX)
  if (x11_display_ && x11_window_) {
    XDestroyWindow(static_cast<Display*>(x11_display_), static_cast<Window>(x11_window_));
    x11_window_ = 0;
  }
#endif
}

bool VSTHost::configurePluginCallbacks() {
  // Check plugin's magic number
  // If incorrect, then the file either was not loaded properly, is not a
  // real VST plugin, or is otherwise corrupt.
  if (plugin->magic != kEffectMagic) {
    qCritical() << "Plugin's magic number is bad";
    amber::app_ctx->showMessage(tr("VST Error"), tr("Plugin's magic number is invalid"), 3);
    return false;
  }

  // Create dispatcher handle
  dispatcher = reinterpret_cast<dispatcherFuncPtr>(plugin->dispatcher);

  // Set up plugin callback functions
  plugin->getParameter = reinterpret_cast<getParameterFuncPtr>(plugin->getParameter);
  plugin->processReplacing = reinterpret_cast<processFuncPtr>(plugin->processReplacing);
  plugin->setParameter = reinterpret_cast<setParameterFuncPtr>(plugin->setParameter);

  return true;
}

void VSTHost::startPlugin() {
  dispatcher(plugin, effOpen, 0, 0, nullptr, 0.0f);

  // Set some default properties
  dispatcher(plugin, effSetSampleRate, 0, 0, nullptr, current_audio_freq());
  dispatcher(plugin, effSetBlockSize, 0, BLOCK_SIZE, nullptr, 0.0f);

  resumePlugin();
}

void VSTHost::stopPlugin() {
  suspendPlugin();

  dispatcher(plugin, effClose, 0, 0, nullptr, 0);
}

void VSTHost::resumePlugin() { dispatcher(plugin, effMainsChanged, 0, 1, nullptr, 0.0f); }

void VSTHost::suspendPlugin() { dispatcher(plugin, effMainsChanged, 0, 0, nullptr, 0.0f); }

bool VSTHost::canPluginDo(char* canDoString) {
  return (dispatcher(plugin, effCanDo, 0, 0, static_cast<void*>(canDoString), 0.0f) > 0);
}

void VSTHost::processAudio(long numFrames) {
  // Always reset the output array before processing.
  for (int i = 0; i < CHANNEL_COUNT; i++) {
    memset(outputs[i], 0, BLOCK_SIZE * sizeof(float));
  }

  plugin->processReplacing(plugin, inputs, outputs, numFrames);
}

void VSTHost::CreateDialogIfNull() {
  if (dialog == nullptr) {
    dialog = new QDialog(amber::app_ctx->getMainWindow());
    dialog->setWindowTitle(tr("VST Plugin"));
    dialog->setAttribute(Qt::WA_NativeWindow, true);
    dialog->setWindowFlags(dialog->windowFlags() | Qt::MSWindowsFixedSizeDialogHint);
    connect(dialog, &QDialog::finished, this, &VSTHost::uncheck_show_button);
  }
}

void VSTHost::send_data_cache_to_plugin() {
  dispatcher(plugin, effSetChunk, 0, int32_t(data_cache.size()), static_cast<void*>(data_cache.data()), 0);
}

VSTHost::VSTHost(Clip* c, const EffectMeta* em)
    : Effect(c, em)

{
  plugin = nullptr;

  inputs = new float*[CHANNEL_COUNT];
  outputs = new float*[CHANNEL_COUNT];
  for (int channel = 0; channel < CHANNEL_COUNT; channel++) {
    inputs[channel] = new float[BLOCK_SIZE];
    outputs[channel] = new float[BLOCK_SIZE];
  }

  EffectRow* file_row = new EffectRow(this, tr("Plugin"), true, false);
  file_field = new FileField(file_row, "filename");
  connect(file_field, &FileField::Changed, this, &VSTHost::change_plugin, Qt::QueuedConnection);

  EffectRow* interface_row = new EffectRow(this, tr("Interface"), false, false);

  show_interface_btn = new ButtonField(interface_row, tr("Show"));
  show_interface_btn->SetCheckable(true);
  show_interface_btn->SetEnabled(false);
  connect(show_interface_btn, &ButtonField::Toggled, this, &VSTHost::show_interface);
}

EffectPtr VSTHost::copy(Clip* c) {
  EffectPtr cp = Effect::copy(c);
  VSTHost* vst_copy = static_cast<VSTHost*>(cp.get());

  // Copy plugin preset data (not handled by copy_field_keyframes)
  if (plugin != nullptr) {
    char* p = nullptr;
    int32_t length = int32_t(dispatcher(plugin, effGetChunk, 0, 0, &p, 0));
    vst_copy->data_cache = QByteArray(p, length);
  } else if (!data_cache.isEmpty()) {
    vst_copy->data_cache = data_cache;
  }

  // Load plugin on the copy — file path was copied to persistent_data_
  // by copy_field_keyframes() but change_plugin() was never triggered
  vst_copy->change_plugin();

  return cp;
}

VSTHost::~VSTHost() {
  for (int channel = 0; channel < CHANNEL_COUNT; channel++) {
    delete[] inputs[channel];
    delete[] outputs[channel];
  }
  delete[] outputs;
  delete[] inputs;

  freePlugin();

#if defined(Q_OS_LINUX)
  if (x11_display_) {
    XCloseDisplay(static_cast<Display*>(x11_display_));
    x11_display_ = nullptr;
  }
#endif
}

void VSTHost::process_audio(double, double, quint8* samples, int nb_bytes, int) {
  if (plugin != nullptr) {
    int interval = BLOCK_SIZE * 4;
    for (int i = 0; i < nb_bytes; i += interval) {
      int process_size = qMin(interval, nb_bytes - i);
      int lim = i + process_size;

      // convert to float
      for (int j = i; j < lim; j += 4) {
        qint16 left_sample = qint16(((samples[j + 1] & 0xFF) << 8) | (samples[j] & 0xFF));
        qint16 right_sample = qint16(((samples[j + 3] & 0xFF) << 8) | (samples[j + 2] & 0xFF));

        int index = (j - i) >> 2;
        inputs[0][index] = float(left_sample) / float(INT16_MAX);
        inputs[1][index] = float(right_sample) / float(INT16_MAX);
      }

      // send to VST
      processAudio(process_size >> 2);

      // convert back to int16
      for (int j = i; j < lim; j += 4) {
        int index = (j - i) >> 2;

        qint16 left_sample = qint16(qRound(outputs[0][index] * INT16_MAX));
        qint16 right_sample = qint16(qRound(outputs[1][index] * INT16_MAX));

        samples[j + 3] = quint8(right_sample >> 8);
        samples[j + 2] = quint8(right_sample);
        samples[j + 1] = quint8(left_sample >> 8);
        samples[j] = quint8(left_sample);
      }
    }
  }
}

void VSTHost::custom_load(QXmlStreamReader& stream) {
  if (stream.name() == QLatin1String("plugindata")) {
    stream.readNext();
    data_cache = QByteArray::fromBase64(stream.text().toUtf8());
    if (plugin != nullptr) {
      send_data_cache_to_plugin();
    }
  }
}

void VSTHost::save(QXmlStreamWriter& stream) {
  Effect::save(stream);
  if (plugin != nullptr) {
    char* p = nullptr;
    int32_t length = int32_t(dispatcher(plugin, effGetChunk, 0, 0, &p, 0));
    data_cache = QByteArray(p, length);
  }
  if (data_cache.size() > 0) {
    stream.writeTextElement("plugindata", data_cache.toBase64());
  }
}

#if defined(Q_OS_LINUX)
// Ensure the X11 display is open and x11_window_ is created.
// Returns false and unchecks the button on failure.
bool VSTHost::EnsureX11Window() {
  if (!x11_display_) {
    x11_display_ = XOpenDisplay(nullptr);
    if (!x11_display_) {
      amber::app_ctx->showMessage(tr("VST Error"), tr("Cannot open X11 display. VST plugin interfaces require X11."),
                                  3);
      show_interface_btn->SetChecked(false);
      return false;
    }
  }

  if (x11_window_) return true;

  auto dpy = static_cast<Display*>(x11_display_);

  VSTRect* eRect = nullptr;
  dispatcher(plugin, effEditGetRect, 0, 0, &eRect, 0);
  int w = 400, h = 300;
  if (eRect && eRect->right > eRect->left && eRect->bottom > eRect->top) {
    w = eRect->right - eRect->left;
    h = eRect->bottom - eRect->top;
  }

  // Scale for HiDPI — Xft.dpi is set by the DE on both X11 and XWayland
  char* dpi_str = XGetDefault(dpy, "Xft", "dpi");
  if (dpi_str) {
    int xft_dpi = 0;
    for (const char* p = dpi_str; *p >= '0' && *p <= '9'; p++) xft_dpi = xft_dpi * 10 + (*p - '0');
    if (xft_dpi > 96) {
      w = w * xft_dpi / 96;
      h = h * xft_dpi / 96;
    }
  }

  int screen = DefaultScreen(dpy);

  // 24-bit TrueColor = opaque, no alpha channel for the compositor
  XVisualInfo vinfo;
  if (!XMatchVisualInfo(dpy, screen, 24, TrueColor, &vinfo)) {
    vinfo.visual = DefaultVisual(dpy, screen);
    vinfo.depth = DefaultDepth(dpy, screen);
  }

  Colormap cmap = XCreateColormap(dpy, RootWindow(dpy, screen), vinfo.visual, AllocNone);
  XSetWindowAttributes attrs = {};
  attrs.colormap = cmap;
  attrs.background_pixel = BlackPixel(dpy, screen);
  attrs.border_pixel = 0;

  x11_window_ = XCreateWindow(dpy, RootWindow(dpy, screen), 0, 0, w, h, 0, vinfo.depth, InputOutput, vinfo.visual,
                              CWColormap | CWBackPixel | CWBorderPixel, &attrs);

  XStoreName(dpy, static_cast<Window>(x11_window_), "VST Plugin");

  // WM close button support
  Atom wmDelete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
  x11_wm_delete_ = wmDelete;
  XSetWMProtocols(dpy, static_cast<Window>(x11_window_), &wmDelete, 1);

  return true;
}
#endif  // Q_OS_LINUX

void VSTHost::StartIdleTimer() {
  if (!idle_timer) {
    idle_timer = new QTimer(this);
    connect(idle_timer, &QTimer::timeout, this, [this]() {
      if (!plugin) return;
      dispatcher(plugin, effEditIdle, 0, 0, nullptr, 0);
#if defined(Q_OS_LINUX)
      // Poll for WM close button
      if (x11_display_ && x11_window_) {
        XEvent event;
        while (XCheckTypedWindowEvent(static_cast<Display*>(x11_display_), static_cast<Window>(x11_window_),
                                      ClientMessage, &event)) {
          if (static_cast<unsigned long>(event.xclient.data.l[0]) == x11_wm_delete_) {
            show_interface_btn->SetChecked(false);
            return;
          }
        }
      }
#endif
    });
  }
  idle_timer->start(50);
}

void VSTHost::HideInterface() {
  if (idle_timer) idle_timer->stop();
  dispatcher(plugin, effEditClose, 0, 0, nullptr, 0);
#if defined(Q_OS_LINUX)
  if (x11_display_ && x11_window_) {
    auto dpy = static_cast<Display*>(x11_display_);
    XUnmapWindow(dpy, static_cast<Window>(x11_window_));
    XSync(dpy, False);
  }
#else
  if (dialog) dialog->hide();
#endif
}

// Returns false if the platform setup failed (caller should abort show).
bool VSTHost::ShowInterfacePlatform() {
#if defined(Q_OS_LINUX)
  // Direct X11 window for plugin embedding.
  // Works on native X11 and Wayland (via XWayland).
  // 24-bit visual (no alpha) prevents compositor transparency.
  if (!EnsureX11Window()) return false;

  auto dpy = static_cast<Display*>(x11_display_);
  XMapRaised(dpy, static_cast<Window>(x11_window_));
  XSync(dpy, False);
  dispatcher(plugin, effEditOpen, 0, 0, reinterpret_cast<void*>(x11_window_), 0);
#else  // Windows, macOS, Haiku — use QDialog
  CreateDialogIfNull();
  dialog->show();
  WId nativeWin = dialog->winId();
#if defined(Q_OS_WIN)
  dispatcher(plugin, effEditOpen, 0, 0, reinterpret_cast<HWND>(nativeWin), 0);
#elif defined(Q_OS_MACOS)
  dispatcher(plugin, effEditOpen, 0, 0, reinterpret_cast<NSWindow*>(nativeWin), 0);
#else
  dispatcher(plugin, effEditOpen, 0, 0, reinterpret_cast<void*>(nativeWin), 0);
#endif
#endif  // Q_OS_LINUX
  return true;
}

void VSTHost::show_interface(bool show) {
  if (!show) {
    HideInterface();
    return;
  }

  if (!ShowInterfacePlatform()) return;
  StartIdleTimer();
}

void VSTHost::uncheck_show_button() { show_interface_btn->SetChecked(false); }

void VSTHost::change_plugin() {
  freePlugin();
  loadPlugin();
  if (plugin != nullptr) {
    if (configurePluginCallbacks()) {
      startPlugin();

      if (!data_cache.isEmpty()) {
        send_data_cache_to_plugin();
      }

      VSTRect* eRect = nullptr;
      plugin->dispatcher(plugin, effEditGetRect, 0, 0, &eRect, 0);

#if !defined(Q_OS_LINUX)
      if (eRect != nullptr && eRect->right > eRect->left && eRect->bottom > eRect->top) {
        CreateDialogIfNull();
        dialog->setFixedSize(eRect->right - eRect->left, eRect->bottom - eRect->top);
      }
#endif

    } else {
      modulePtr.unload();
      plugin = nullptr;
    }
  }
  bool has_editor = (plugin != nullptr) && (plugin->flags & effFlagsHasEditor);
  show_interface_btn->SetEnabled(has_editor);
}
