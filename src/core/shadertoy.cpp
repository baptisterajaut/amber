#include "core/shadertoy.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QSet>

namespace amber::shadertoy {

namespace {

QString tr(const char* s) { return QCoreApplication::translate("ShaderToy", s); }

// The header every generated shader starts with. The varying keeps common.vert's name `vTexCoord`: the GLSL 150
// output of the OpenGL backend has no location qualifiers, so GL matches it by name.
const char* const kHeader = R"(#version 440
layout(std140, binding = 1) uniform FragParams {
  vec3 iResolution;
  float iTime;
  float iTimeDelta;
  int iFrame;
  vec4 iDate;
  vec4 iMouse;
  vec3 iChannelResolution[4];
%1};
layout(binding = 2) uniform sampler2D iChannel0;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 amber_FragColor;
vec4 amber_FragCoord;
vec4 amber_unpremul(vec4 c) { return c.a > 0.0 ? vec4(c.rgb / c.a, c.a) : vec4(0.0); }
vec4 amber_texture(sampler2D s, vec2 uv) { return amber_unpremul(texture(s, vec2(uv.x, 1.0 - uv.y))); }
vec4 amber_texture(sampler2D s, vec2 uv, float b) { return amber_unpremul(texture(s, vec2(uv.x, 1.0 - uv.y), b)); }
vec4 amber_textureLod(sampler2D s, vec2 uv, float l) { return amber_unpremul(textureLod(s, vec2(uv.x, 1.0 - uv.y), l)); }
vec4 amber_texelFetch(sampler2D s, ivec2 p, int l) {
  return amber_unpremul(texelFetch(s, ivec2(p.x, textureSize(s, l).y - 1 - p.y), l));
}
#line 1
)";

// ShaderToy is y-up for fragCoord: vTexCoord.y = 0 is the image top (memory row 0) on every backend
const char* const kMain = R"(
void main() {
  vec2 amber_fc = vec2(vTexCoord.x, 1.0 - vTexCoord.y) * iResolution.xy;
  amber_FragCoord = vec4(amber_fc, 0.0, 1.0);
  vec4 amber_c = vec4(0.0);
  mainImage(amber_c, amber_fc);
  float amber_a = texture(iChannel0, vTexCoord).a;
  vec3 amber_rgb = mix(amber_c.rgb, vec3(0.0), isnan(amber_c.rgb));
  amber_FragColor = vec4(clamp(amber_rgb, 0.0, 1.0) * amber_a, amber_a);
}
)";

// Names the generated code declares (and a param can't take), besides the amber_ prefix
const char* const kReserved[] = {"iResolution",        "iTime",     "iTimeDelta", "iFrame", "iDate",    "iMouse",
                                 "iChannelResolution", "iChannel0", "vTexCoord",  "main",   "mainImage"};

// Replaces `//` and `/* */` comments with spaces, newlines kept (GLSL has no string literals)
QString blank_comments(const QString& in) {
  QString out = in;
  enum { Code, Line, Block } state = Code;
  for (int i = 0; i < out.size(); i++) {
    const QChar c = out.at(i);
    const QChar next = i + 1 < out.size() ? out.at(i + 1) : QChar();
    if (state == Code) {
      if (c == '/' && next == '/') {
        state = Line;
      } else if (c == '/' && next == '*') {
        state = Block;
        out[i] = ' ';
        out[++i] = ' ';
        continue;
      }
    }
    if (state == Line) {
      if (c == '\n') {
        state = Code;
      } else {
        out[i] = ' ';
      }
    } else if (state == Block) {
      if (c == '*' && next == '/') {
        out[i] = ' ';
        out[++i] = ' ';
        state = Code;
      } else if (c != '\n') {
        out[i] = ' ';
      }
    }
  }
  return out;
}

// Replaces every match of `re` by the newlines it spans, so the line count holds
void blank_matches(QString& text, const QRegularExpression& re) {
  QString out;
  qsizetype last = 0;
  auto it = re.globalMatch(text);
  while (it.hasNext()) {
    const QRegularExpressionMatch m = it.next();
    out += QStringView(text).mid(last, m.capturedStart() - last);
    out += QString(m.captured().count('\n'), '\n');
    last = m.capturedEnd();
  }
  out += QStringView(text).mid(last);
  text = out;
}

// Rewrites `name(` (word boundary, any whitespace before the parenthesis kept) to `replacement(`
void rewrite_call(QString& text, const char* name, const char* replacement) {
  text.replace(QRegularExpression(QStringLiteral("\\b%1(\\s*)\\(").arg(QLatin1String(name))),
               QStringLiteral("%1\\1(").arg(QLatin1String(replacement)));
}

QString reject(QString* error, const QString& message) {
  if (error) *error = message;
  return {};
}

}  // namespace

QString to_amber_glsl(const QString& src, const QVector<Param>& params, QString* error) {
  QString text = src;
  text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
  text.replace('\r', '\n');
  text = blank_comments(text);

  // Ports and copy-pastes carry these; the header already declares all of them
  blank_matches(text, QRegularExpression(QStringLiteral("^[ \\t]*#[ \\t]*(version|extension)\\b[^\\n]*"),
                                         QRegularExpression::MultilineOption));
  blank_matches(text, QRegularExpression(QStringLiteral("\\bprecision\\s+(lowp|mediump|highp)\\s+\\w+\\s*;")));
  blank_matches(text, QRegularExpression(QStringLiteral(
                          "\\buniform\\b[^;{}]*\\b(iResolution|iTime|iTimeDelta|iFrameRate|iFrame|iDate|iMouse|"
                          "iChannelResolution|iChannelTime|iSampleRate|iChannel[0-3])\\b[^;{}]*;")));

  if (!text.contains(QRegularExpression(QStringLiteral("\\bvoid\\s+mainImage\\s*\\(")))) {
    return reject(error, tr("No mainImage() found: a ShaderToy shader defines "
                            "void mainImage(out vec4 fragColor, in vec2 fragCoord)."));
  }
  if (text.contains(QRegularExpression(QStringLiteral("\\bvoid\\s+main\\s*\\(")))) {
    return reject(error, tr("Remove main(): Amber generates main() and calls mainImage()."));
  }
  const QRegularExpressionMatch channel = QRegularExpression(QStringLiteral("\\biChannel[1-3]\\b")).match(text);
  if (channel.hasMatch()) {
    return reject(error, tr("%1 isn't available: only iChannel0 (the clip) in this version.").arg(channel.captured()));
  }
  // These sample with coordinates the helpers can't flip (gradients, offsets, gathers, projective coordinates)
  const QRegularExpressionMatch unflippable =
      QRegularExpression(QStringLiteral("\\b(textureGrad\\w*|textureGather\\w*|textureProj\\w*|textureQueryLod|"
                                        "texture\\w*Offset|texelFetchOffset)\\s*\\("))
          .match(text);
  if (unflippable.hasMatch()) {
    return reject(error, tr("%1() on iChannel0 isn't supported in this version.").arg(unflippable.captured(1)));
  }

  QSet<QString> reserved;
  for (const char* r : kReserved) reserved.insert(QLatin1String(r));
  QSet<QString> seen;
  QString members;
  for (const Param& p : params) {
    if (reserved.contains(p.name) || p.name.startsWith(QLatin1String("amber_"))) {
      return reject(error, tr("Parameter \"%1\" uses a name reserved by Amber.").arg(p.name));
    }
    if (seen.contains(p.name)) {
      return reject(error, tr("Parameter \"%1\" is declared twice.").arg(p.name));
    }
    seen.insert(p.name);
    members += QStringLiteral("  %1 %2;\n").arg(p.glsl_type, p.name);
  }

  // Also inside #define bodies, which is what makes `#define T(u) texture(iChannel0, u)` work
  rewrite_call(text, "texture2D", "amber_texture");
  rewrite_call(text, "texture", "amber_texture");
  rewrite_call(text, "textureLod", "amber_textureLod");
  rewrite_call(text, "texelFetch", "amber_texelFetch");
  text.replace(QRegularExpression(QStringLiteral("\\bgl_FragCoord\\b")), QStringLiteral("amber_FragCoord"));

  if (!text.endsWith('\n')) text += '\n';
  return QString::fromLatin1(kHeader).arg(members) + text + QString::fromLatin1(kMain);
}

}  // namespace amber::shadertoy
