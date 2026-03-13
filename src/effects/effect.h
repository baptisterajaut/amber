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

#ifndef EFFECT_H
#define EFFECT_H

#include <memory>
#include <QObject>
#include <QString>
#include <QVector>
#include <QColor>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QMutex>
#include <QThread>
#include <QLabel>
#include <QWidget>
#include <QGridLayout>
#include <QPushButton>
#include <QMouseEvent>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <random>

#include "ui/collapsiblewidget.h"
#include "effectrow.h"
#include "effectgizmo.h"

class Clip;

class Effect;
using EffectPtr = std::shared_ptr<Effect>;

struct EffectMeta {
  QString name;
  QString category;
  QString filename;
  QString path;
  QString tooltip;
  int internal;
  int type;
  int subtype;
};
extern QVector<EffectMeta> effects;

double log_volume(double linear);

enum EffectType : uint8_t {
  EFFECT_TYPE_INVALID,
  EFFECT_TYPE_VIDEO,
  EFFECT_TYPE_AUDIO,
  EFFECT_TYPE_EFFECT,
  EFFECT_TYPE_TRANSITION
};

enum EffectKeyframeType : uint8_t {
  EFFECT_KEYFRAME_LINEAR,
  EFFECT_KEYFRAME_BEZIER,
  EFFECT_KEYFRAME_HOLD
};

enum EffectInternal : uint8_t {
  EFFECT_INTERNAL_TRANSFORM,
  EFFECT_INTERNAL_TEXT,
  EFFECT_INTERNAL_SOLID,
  EFFECT_INTERNAL_NOISE,
  EFFECT_INTERNAL_VOLUME,
  EFFECT_INTERNAL_PAN,
  EFFECT_INTERNAL_TONE,
  EFFECT_INTERNAL_SHAKE,
  EFFECT_INTERNAL_TIMECODE,
  EFFECT_INTERNAL_MASK,
  EFFECT_INTERNAL_FILLLEFTRIGHT,
  EFFECT_INTERNAL_VST,
  EFFECT_INTERNAL_CORNERPIN,
  EFFECT_INTERNAL_FREI0R,
  EFFECT_INTERNAL_RICHTEXT,
  EFFECT_INTERNAL_COUNT
};

struct GLTextureCoords {
  int grid_size;

  int vertexTopLeftX;
  int vertexTopLeftY;
  int vertexTopLeftZ;
  int vertexTopRightX;
  int vertexTopRightY;
  int vertexTopRightZ;
  int vertexBottomLeftX;
  int vertexBottomLeftY;
  int vertexBottomLeftZ;
  int vertexBottomRightX;
  int vertexBottomRightY;
  int vertexBottomRightZ;

  float textureTopLeftX;
  float textureTopLeftY;
  float textureTopLeftQ;
  float textureTopRightX;
  float textureTopRightY;
  float textureTopRightQ;
  float textureBottomRightX;
  float textureBottomRightY;
  float textureBottomRightQ;
  float textureBottomLeftX;
  float textureBottomLeftY;
  float textureBottomLeftQ;

  int blendmode;
  float opacity;

  // Accumulated CoordsFlag transform (replaces glTranslate/glRotate/glScale on GL matrix stack)
  QMatrix4x4 transform;
};

const EffectMeta* get_meta_from_name(const QString& input);

qint16 mix_audio_sample(qint16 a, qint16 b);

class Effect : public QObject {
  Q_OBJECT
public:
  Effect(Clip *c, const EffectMeta* em);
  ~Effect() override;

  Clip* parent_clip;
  const EffectMeta* meta;
  int id;
  QString name;

  void AddRow(EffectRow* row);

  EffectRow* row(int i);
  int row_count();

  EffectGizmo* add_gizmo(int type);
  EffectGizmo* gizmo(int i);
  int gizmo_count();

  bool IsEnabled();
  bool IsExpanded();

  virtual void refresh();

  virtual EffectPtr copy(Clip* c);
  void copy_field_keyframes(EffectPtr e);

  virtual void load(QXmlStreamReader& stream);
  virtual void custom_load(QXmlStreamReader& stream);
  virtual void save(QXmlStreamWriter& stream);

  void load_from_string(const QByteArray &s);
  QByteArray save_to_string();

  // glsl handling
  bool is_open();
  void open();
  void close();
  bool is_glsl_linked();
  QOpenGLShaderProgram* program() const { return glslProgram; }
  virtual void startEffect();
  virtual void endEffect();

  enum VideoEffectFlags : uint8_t {
    ShaderFlag        = 0x1,
    CoordsFlag        = 0x2,
    SuperimposeFlag   = 0x4,
    ImageFlag         = 0x8
  };
  int Flags();
  void SetFlags(int flags);

  int getIterations();
  void setIterations(int i);

  const char* ffmpeg_filter;

  virtual void process_image(double timecode, uint8_t* input, uint8_t* output, int size);
  virtual void process_shader(double timecode, GLTextureCoords&, int iteration);
  virtual void process_coords(double timecode, GLTextureCoords& coords, int data);
  virtual GLuint process_superimpose(double timecode);
  virtual void process_audio(double timecode_start, double timecode_end, quint8* samples, int nb_bytes, int channel_count);

  virtual void gizmo_draw(double timecode, GLTextureCoords& coords);
  void gizmo_move(EffectGizmo* sender, int x_movement, int y_movement, double timecode, bool done);
  void gizmo_world_to_screen(const QMatrix4x4& mvp = QMatrix4x4());
  bool are_gizmos_enabled();

  template <typename T>
  T randomNumber()
  {
    static std::random_device device;
    static std::mt19937 generator(device());
    static std::uniform_int_distribution<> distribution(std::numeric_limits<T>::min(), std::numeric_limits<T>::max());
    return distribution(generator);
  }

  static EffectPtr Create(Clip *c, const EffectMeta *em);
  static const EffectMeta* GetInternalMeta(int internal_id, int type);
public slots:
  void FieldChanged();
  void SetEnabled(bool b);
  void SetExpanded(bool e);
signals:
  void EnabledChanged(bool);
public slots:
  void delete_self();
  void move_up();
  void move_down();
  void save_to_file();
  void load_from_file();
protected:
  // glsl effect
  QOpenGLShaderProgram* glslProgram{nullptr};
  QString vertPath;
  QString fragPath;

  // superimpose effect
  QImage img;
  QOpenGLTexture* texture{nullptr};

  // enable effect to update constantly
  virtual bool AlwaysUpdate();

private:
  bool isOpen{false};
  QVector<EffectRow*> rows;
  QVector<EffectGizmo*> gizmos;
  bool bound{false};
  int iterations{1};

  bool enabled_{true};
  bool expanded_{true};

  int flags_{0};

  QVector<KeyframeDataChange*> gizmo_dragging_actions_;

  // superimpose functions
  virtual void redraw(double timecode);
  bool valueHasChanged(double timecode);
  QVector<QVariant> cachedValues;
  void delete_texture();
  void validate_meta_path();
};

#endif // EFFECT_H
