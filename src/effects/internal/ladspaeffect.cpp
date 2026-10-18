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

#include "ladspaeffect.h"

#ifndef NOLADSPA

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <cmath>

#include "core/appcontext.h"
#include "core/audiodsp.h"
#include "effects/fields/boolfield.h"
#include "effects/fields/doublefield.h"
#include "rendering/audio.h"

namespace {

// The longest run() call: the instance buffers hold this many frames, a longer chunk runs in pieces
constexpr int kMaxRunFrames = 4096;

// The audio ports of d (in port order), and why Amber can't host it when it can't
bool classify_ports(const LADSPA_Descriptor* d, std::vector<unsigned long>* in, std::vector<unsigned long>* out,
                    QString* why) {
  in->clear();
  out->clear();
  if (d->instantiate == nullptr || d->connect_port == nullptr || d->run == nullptr) {
    *why = QStringLiteral("no instantiate/connect_port/run");
    return false;
  }
  for (unsigned long p = 0; p < d->PortCount; p++) {
    const LADSPA_PortDescriptor pd = d->PortDescriptors[p];
    if (!LADSPA_IS_PORT_AUDIO(pd)) continue;
    if (LADSPA_IS_PORT_INPUT(pd)) {
      in->push_back(p);
    } else if (LADSPA_IS_PORT_OUTPUT(pd)) {
      out->push_back(p);
    }
  }
  // CBA: 1→1 and 2→2 plugins only; other layouts and latency compensation if asked
  if (in->size() != out->size() || in->empty() || in->size() > 2) {
    *why = QStringLiteral("%1 audio in, %2 audio out").arg(in->size()).arg(out->size());
    return false;
  }
  return true;
}

QString descriptor_name(const LADSPA_Descriptor* d) {
  if (d->Name != nullptr && d->Name[0] != '\0') return QString::fromUtf8(d->Name);
  if (d->Label != nullptr && d->Label[0] != '\0') return QString::fromUtf8(d->Label);
  return QStringLiteral("LADSPA %1").arg(d->UniqueID);
}

}  // namespace

namespace amber {
namespace ladspa {

double PortRange::clamp(double v) const {
  if (!std::isfinite(v)) v = def;
  if (toggled) return v > 0 ? 1 : 0;
  if (integer) v = std::round(v);
  if (has_min && v < min) v = min;
  if (has_max && v > max) v = max;
  return v;
}

PortRange port_range(const LADSPA_PortRangeHint& hint, int sample_rate) {
  const LADSPA_PortRangeHintDescriptor d = hint.HintDescriptor;
  PortRange r;
  r.toggled = LADSPA_IS_HINT_TOGGLED(d);
  r.integer = LADSPA_IS_HINT_INTEGER(d);
  const double scale = LADSPA_IS_HINT_SAMPLE_RATE(d) ? double(sample_rate) : 1.0;
  r.has_min = LADSPA_IS_HINT_BOUNDED_BELOW(d);
  r.has_max = LADSPA_IS_HINT_BOUNDED_ABOVE(d);
  if (r.has_min) r.min = double(hint.LowerBound) * scale;
  if (r.has_max) r.max = double(hint.UpperBound) * scale;
  if (r.has_min && r.has_max && r.max < r.min) r.max = r.min;  // a broken plugin: an empty range, not an inverted one

  const bool both = r.has_min && r.has_max;
  const bool geometric = LADSPA_IS_HINT_LOGARITHMIC(d) && both && r.min > 0 && r.max > 0;
  auto between = [&](double t) {
    return geometric ? std::exp(std::log(r.min) * (1 - t) + std::log(r.max) * t) : r.min * (1 - t) + r.max * t;
  };
  bool has_default = true;
  switch (d & LADSPA_HINT_DEFAULT_MASK) {
    case LADSPA_HINT_DEFAULT_MINIMUM:
      has_default = r.has_min;
      r.def = r.min;
      break;
    case LADSPA_HINT_DEFAULT_LOW:
      has_default = both;
      r.def = between(0.25);
      break;
    case LADSPA_HINT_DEFAULT_MIDDLE:
      has_default = both;
      r.def = between(0.5);
      break;
    case LADSPA_HINT_DEFAULT_HIGH:
      has_default = both;
      r.def = between(0.75);
      break;
    case LADSPA_HINT_DEFAULT_MAXIMUM:
      has_default = r.has_max;
      r.def = r.max;
      break;
    case LADSPA_HINT_DEFAULT_0:
      r.def = 0;
      break;
    case LADSPA_HINT_DEFAULT_1:
      r.def = 1;
      break;
    case LADSPA_HINT_DEFAULT_100:
      r.def = 100;
      break;
    case LADSPA_HINT_DEFAULT_440:
      r.def = 440;
      break;
    default:
      has_default = false;
      break;
  }
  if (!has_default) r.def = r.has_min ? r.min : 0;
  r.def = r.clamp(r.def);

  if (r.toggled || r.integer) {
    r.decimals = 0;
  } else if (both && r.max - r.min <= 1) {
    r.decimals = 3;
  } else {
    r.decimals = 2;
  }
  return r;
}

QStringList search_paths() {
  const QString env = qEnvironmentVariable("LADSPA_PATH");
  if (!env.isEmpty()) return env.split(QDir::listSeparator(), Qt::SkipEmptyParts);
#if defined(Q_OS_WIN)
  return {};
#elif defined(Q_OS_MACOS)
  return {QStringLiteral("/Library/Audio/Plug-Ins/LADSPA"), QDir::homePath() + "/Library/Audio/Plug-Ins/LADSPA"};
#else
  return {QStringLiteral("/usr/lib/ladspa"), QStringLiteral("/usr/local/lib/ladspa"),
          QStringLiteral("/usr/lib64/ladspa"), QDir::homePath() + "/.ladspa"};
#endif
}

void finish_names(std::vector<EffectMeta>& metas) {
  std::stable_sort(metas.begin(), metas.end(),
                   [](const EffectMeta& a, const EffectMeta& b) { return a.plugin_id < b.plugin_id; });
  QSet<QString> names;
  for (EffectMeta& em : metas) {
    if (names.contains(em.name)) em.name += QStringLiteral(" (%1)").arg(em.plugin_id);
    names.insert(em.name);
  }
  std::stable_sort(metas.begin(), metas.end(),
                   [](const EffectMeta& a, const EffectMeta& b) { return a.name < b.name; });
}

std::vector<EffectMeta> scan(const QStringList& dirs) {
  std::vector<EffectMeta> found;
  QSet<unsigned long> ids;
  QStringList scanned;  // canonical: /usr/lib64 is a link to /usr/lib on some distributions
  for (const QString& dir : dirs) {
    const QString canonical = QFileInfo(dir).canonicalFilePath();
    if (canonical.isEmpty() || scanned.contains(canonical) || !QFileInfo(canonical).isDir()) continue;
    scanned.append(canonical);
    const QDir folder(canonical);
    for (const QString& file : folder.entryList(QDir::Files | QDir::Readable, QDir::Name)) {
      if (!QLibrary::isLibrary(file)) continue;
      const QString path = folder.filePath(file);
      QLibrary library(path);
      if (!library.load()) {
        qDebug().noquote() << "LADSPA: skipped" << path << "(" + library.errorString() + ")";
        continue;
      }
      auto descriptor_fn = reinterpret_cast<LADSPA_Descriptor_Function>(library.resolve("ladspa_descriptor"));
      if (descriptor_fn == nullptr) {
        qDebug().noquote() << "LADSPA: skipped" << path << "(no ladspa_descriptor)";
      } else {
        for (unsigned long i = 0;; i++) {
          const LADSPA_Descriptor* d = descriptor_fn(i);
          if (d == nullptr) break;
          std::vector<unsigned long> in, out;
          QString why;
          if (!classify_ports(d, &in, &out, &why)) {
            qDebug().noquote() << "LADSPA: skipped" << descriptor_name(d) << d->UniqueID << "in" << path
                               << "(" + why + ")";
            continue;
          }
          if (ids.contains(d->UniqueID)) {
            qDebug().noquote() << "LADSPA: skipped" << descriptor_name(d) << d->UniqueID << "in" << path
                               << "(UniqueID already found)";
            continue;
          }
          ids.insert(d->UniqueID);
          EffectMeta em;
          em.type = EFFECT_TYPE_EFFECT;
          em.subtype = EFFECT_TYPE_AUDIO;
          em.category = QStringLiteral("LADSPA");
          em.internal = EFFECT_INTERNAL_LADSPA;
          em.name = descriptor_name(d);
          em.path = canonical;
          em.filename = file;
          em.plugin_index = int(i);
          em.plugin_id = d->UniqueID;
          em.tooltip = QStringLiteral("%1 %2 %3 (%4)")
                           .arg(QString::fromUtf8(d->Maker ? d->Maker : ""), QString(QChar(0x2014)),
                                QString::fromUtf8(d->Label ? d->Label : ""))
                           .arg(d->UniqueID);
          found.push_back(em);
        }
      }
      library.unload();  // only drops this scan's reference: a loaded effect keeps its own
    }
  }

  finish_names(found);
  return found;
}

}  // namespace ladspa
}  // namespace amber

LadspaEffect::Plugin::~Plugin() {
  if (library.isLoaded()) library.unload();
}

bool LadspaEffect::Instance::instantiate(const std::shared_ptr<Plugin>& p, int sample_rate) {
  release();
  plugin = p;
  rate = sample_rate;
  const LADSPA_Descriptor* d = p->descriptor;
  const bool mono = p->audio_in.size() == 1;
  for (int c = 0; c < 2; c++) {
    in[c].assign(kMaxRunFrames, 0.0f);
    out[c].assign(kMaxRunFrames, 0.0f);
  }
  const int count = mono ? 2 : 1;
  for (int h = 0; h < count; h++) {
    handles[h] = d->instantiate(d, static_cast<unsigned long>(sample_rate));
    if (handles[h] == nullptr) {
      qWarning() << "LADSPA:" << d->Name << "refused to instantiate at" << sample_rate << "Hz";
      release();
      return false;
    }
    handle_count = h + 1;
    ports[h].assign(d->PortCount, 0.0f);
    // Control outputs (latency, meters) land in their own float and are ignored
    for (unsigned long port = 0; port < d->PortCount; port++) {
      if (LADSPA_IS_PORT_CONTROL(d->PortDescriptors[port])) d->connect_port(handles[h], port, &ports[h][port]);
    }
    // Separate input and output buffers: LADSPA_PROPERTY_INPLACE_BROKEN never matters
    if (mono) {
      d->connect_port(handles[h], p->audio_in[0], in[h].data());
      d->connect_port(handles[h], p->audio_out[0], out[h].data());
    } else {
      for (int c = 0; c < 2; c++) {
        d->connect_port(handles[h], p->audio_in[c], in[c].data());
        d->connect_port(handles[h], p->audio_out[c], out[c].data());
      }
    }
    if (d->activate != nullptr) d->activate(handles[h]);
  }
  return true;
}

void LadspaEffect::Instance::restart() {
  const LADSPA_Descriptor* d = plugin->descriptor;
  if (d->activate == nullptr) {
    // Nothing to reset its state with: start over from a new instance
    std::shared_ptr<Plugin> p = plugin;
    instantiate(p, rate);
    return;
  }
  for (int h = 0; h < handle_count; h++) {
    if (d->deactivate != nullptr) d->deactivate(handles[h]);
    d->activate(handles[h]);
  }
}

void LadspaEffect::Instance::release() {
  if (plugin != nullptr) {
    const LADSPA_Descriptor* d = plugin->descriptor;
    for (int h = 0; h < handle_count; h++) {
      if (d->activate != nullptr && d->deactivate != nullptr) d->deactivate(handles[h]);
      if (d->cleanup != nullptr) d->cleanup(handles[h]);
      handles[h] = nullptr;
    }
  }
  handle_count = 0;
  plugin.reset();
}

LadspaEffect::LadspaEffect(Clip* c, const EffectMeta* em) : Effect(c, em) {
  const QString file = QDir(em->path).filePath(em->filename);
  auto plugin = std::make_shared<Plugin>();
  plugin->library.setFileName(file);
  if (!plugin->library.load()) {
    amber::app_ctx->showMessage(tr("Error loading LADSPA plugin"),
                                tr("Failed to load LADSPA plugin \"%1\": %2").arg(file, plugin->library.errorString()),
                                3);
    return;
  }
  auto descriptor_fn = reinterpret_cast<LADSPA_Descriptor_Function>(plugin->library.resolve("ladspa_descriptor"));
  if (descriptor_fn == nullptr) {
    amber::app_ctx->showMessage(tr("Error loading LADSPA plugin"),
                                tr("Symbol ladspa_descriptor not found in \"%1\"").arg(file), 3);
    return;
  }
  // The scan's index first; the UniqueID decides, in case the library changed since
  const LADSPA_Descriptor* d =
      em->plugin_index >= 0 ? descriptor_fn(static_cast<unsigned long>(em->plugin_index)) : nullptr;
  if (d == nullptr || d->UniqueID != em->plugin_id) {
    d = nullptr;
    for (unsigned long i = 0;; i++) {
      const LADSPA_Descriptor* candidate = descriptor_fn(i);
      if (candidate == nullptr) break;
      if (candidate->UniqueID == em->plugin_id) {
        d = candidate;
        break;
      }
    }
  }
  QString why;
  if (d == nullptr || !classify_ports(d, &plugin->audio_in, &plugin->audio_out, &why)) {
    amber::app_ctx->showMessage(
        tr("Error loading LADSPA plugin"),
        tr("LADSPA plugin %1 (%2) can't be used from \"%3\"").arg(em->name).arg(em->plugin_id).arg(file), 3);
    return;
  }
  plugin->descriptor = d;

  // No audio device: the output's format has no rate. Sample-rate bounds then assume the default 48 kHz.
  const int rate = current_audio_freq() > 0 ? current_audio_freq() : 48000;
  for (unsigned long port = 0; port < d->PortCount; port++) {
    const LADSPA_PortDescriptor pd = d->PortDescriptors[port];
    if (!LADSPA_IS_PORT_CONTROL(pd) || !LADSPA_IS_PORT_INPUT(pd)) continue;
    const amber::ladspa::PortRange range = amber::ladspa::port_range(d->PortRangeHints[port], rate);
    EffectRow* row = new EffectRow(this, QString::fromUtf8(d->PortNames[port]));
    const QString id = QString::number(port);
    EffectField* field;
    if (range.toggled) {
      field = new BoolField(row, id);
      field->SetValueAt(0, range.def > 0);
      field->SetDefaultData(range.def > 0);
    } else {
      DoubleField* df = new DoubleField(row, id);
      if (range.has_min) df->SetMinimum(range.min);
      if (range.has_max) df->SetMaximum(range.max);
      df->SetDecimalPlaces(range.decimals);
      df->SetDefault(range.def);
      field = df;
    }
    controls_.push_back({port, field, range});
  }
  plugin_ = plugin;
}

void LadspaEffect::process_audio(double timecode_start, double, quint8* samples, int nb_bytes, int) {
  const int frames = nb_bytes / 4;
  if (frames <= 0 || plugin_ == nullptr) return;
  const amber::AudioProcessContext& ctx = amber::current_audio_context();
  if (ctx.sample_rate <= 0) return;  // no audio device: a plugin instantiated at 0 Hz would divide by it

  bool reset;
  std::shared_ptr<Instance> inst = states_.acquire(ctx, timecode_start, frames, &reset);
  if (inst->handle_count == 0 || inst->rate != ctx.sample_rate) {
    if (!inst->instantiate(plugin_, ctx.sample_rate)) return;
  } else if (reset) {
    inst->restart();  // no tail or delay line carried over from another position
  }
  if (inst->handle_count == 0) return;

  // CBA: control ports read once per chunk (≤ 4096 frames); per-sample automation and port groups if asked
  for (const Control& c : controls_) {
    const double v = c.range.clamp(c.field->GetValueAt(timecode_start).toDouble());
    for (int h = 0; h < inst->handle_count; h++) inst->ports[h][c.port] = LADSPA_Data(v);
  }

  const LADSPA_Descriptor* d = plugin_->descriptor;
  std::vector<float> buf(size_t(frames) * 2);
  amber::audiodsp::s16_to_float(samples, buf.data(), frames);
  for (int done = 0; done < frames; done += kMaxRunFrames) {
    const int n = std::min(kMaxRunFrames, frames - done);
    float* chunk = buf.data() + size_t(done) * 2;
    for (int i = 0; i < n; i++) {
      inst->in[0][i] = chunk[i * 2];
      inst->in[1][i] = chunk[i * 2 + 1];
    }
    for (int h = 0; h < inst->handle_count; h++) d->run(inst->handles[h], static_cast<unsigned long>(n));
    for (int i = 0; i < n; i++) {
      const float l = inst->out[0][i];
      const float r = inst->out[1][i];
      chunk[i * 2] = std::isfinite(l) ? l : 0.0f;
      chunk[i * 2 + 1] = std::isfinite(r) ? r : 0.0f;
    }
  }
  amber::audiodsp::float_to_s16(buf.data(), samples, frames);
}

#endif  // NOLADSPA
