#ifndef SHADERTOY_H
#define SHADERTOY_H

#include <QString>
#include <QVector>

namespace amber::shadertoy {

// A sidecar XML field that becomes a uniform of the generated FragParams block
struct Param {
  QString name;
  QString glsl_type;  // "float" | "bool" | "int" | "vec3"
};

// Turns a ShaderToy "Image" tab source (it defines void mainImage(out vec4, in vec2)) into the GLSL 440 fragment
// shader an XML shader effect would ship: FragParams UBO at binding 1 with the ShaderToy inputs then `params` in
// order, iChannel0 at binding 2, a generated main() that flips fragCoord and the iChannel0 reads to ShaderToy's
// y-up convention and writes premultiplied output masked by the input's alpha.
// Comments are blanked, #version/#extension/precision and loose input uniform declarations are blanked (line count
// kept, with #line 1, so baker errors carry the user's line numbers), texture/texture2D/textureLod/texelFetch and
// gl_FragCoord are rewritten to the flipping helpers.
// Returns an empty string and sets *error for a source Amber rejects (no mainImage, a main(), iChannel1-3,
// unflippable texture functions, a reserved or duplicate param name). GLSL errors are left to the baker.
QString to_amber_glsl(const QString& src, const QVector<Param>& params, QString* error);

}  // namespace amber::shadertoy

#endif  // SHADERTOY_H
