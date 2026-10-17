// ShaderToy import: the pure preprocessor (core/shadertoy) and the CPU half of the effect plumbing (bake through
// Effect::bakeSource, runtime registration). No GPU: the render side lives in test_effects_gpu.cpp.

#include <QtTest>

#include "core/shadertoy.h"

using amber::shadertoy::Param;
using amber::shadertoy::to_amber_glsl;

namespace {

const QString kMinimal = QStringLiteral(
    "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
    "  fragColor = vec4(fragCoord / iResolution.xy, 0.0, 1.0);\n"
    "}\n");

// The user's lines as they reach the baker: everything after `#line 1`, up to the generated main()
QStringList body_lines(const QString& out) {
  const qsizetype start = out.indexOf(QLatin1String("#line 1\n"));
  const qsizetype end = out.lastIndexOf(QLatin1String("\nvoid main() {"));
  if (start < 0 || end < start) return {};
  return out.mid(start + 8, end - start - 8).split('\n');
}

QString convert(const QString& src, const QVector<Param>& params = {}) {
  QString err;
  QString out = to_amber_glsl(src, params, &err);
  if (out.isEmpty()) qWarning() << "unexpected reject:" << err;
  return out;
}

QString reject_message(const QString& src, const QVector<Param>& params = {}) {
  QString err;
  const QString out = to_amber_glsl(src, params, &err);
  return out.isEmpty() ? err : QString();
}

}  // namespace

class TestShaderToy : public QObject {
  Q_OBJECT

 private slots:
  void minimalBody() {
    const QString out = convert(kMinimal);
    QVERIFY(out.startsWith(QLatin1String("#version 440\n")));
    QVERIFY(out.contains(QLatin1String("layout(std140, binding = 1) uniform FragParams {")));
    QVERIFY(out.contains(QLatin1String("layout(binding = 2) uniform sampler2D iChannel0;")));
    QVERIFY(out.contains(QLatin1String("#line 1\n") + kMinimal));
    QVERIFY(out.contains(QLatin1String("mainImage(amber_c, amber_fc);")));
    QVERIFY(out.trimmed().endsWith('}'));
    // Exactly one #version: the user's code is put after the header
    QCOMPARE(out.count(QLatin1String("#version")), 1);
  }

  void blanksHeaderLines() {
    const QString src = QStringLiteral(
                            "#version 300 es\n"
                            "precision highp float;\n"
                            "#extension GL_OES_standard_derivatives : enable\n"
                            "uniform vec3 iResolution;\n"
                            "uniform sampler2D iChannel0;\n"
                            "uniform vec3      iChannelResolution[4];\n") +
                        kMinimal;
    const QString out = convert(src);
    QVERIFY(!out.isEmpty());
    const QStringList lines = body_lines(out);
    for (int i = 0; i < 6; i++) QVERIFY2(lines.at(i).trimmed().isEmpty(), qPrintable(lines.at(i)));
    QVERIFY(lines.at(6).startsWith(QLatin1String("void mainImage(")));
    QCOMPARE(out.count(QLatin1String("#version")), 1);
    QVERIFY(!out.contains(QLatin1String("precision")));
    QVERIFY(!out.contains(QLatin1String("#extension")));
  }

  void rewritesSamplingCalls() {
    const QString src = QStringLiteral(
        "#define T(u) texture(iChannel0, u)\n"
        "vec4 mytexture(vec2 u) { return vec4(u, 0.0, 1.0); }\n"
        "void mainImage(out vec4 o, in vec2 p) {\n"
        "  vec2 uv = p / iResolution.xy;\n"
        "  o = texture(iChannel0, uv) + texture2D(iChannel0, uv) + texture (iChannel0, uv, 1.0)\n"
        "    + textureLod(iChannel0, uv, 0.0) + texelFetch(iChannel0, ivec2(p), 0) + T(uv) + mytexture(uv)\n"
        "    + vec4(vec2(textureSize(iChannel0, 0)), gl_FragCoord.xy);\n"
        "}\n");
    const QString out = convert(src);
    const QStringList lines = body_lines(out);
    QCOMPARE(lines.at(0), QStringLiteral("#define T(u) amber_texture(iChannel0, u)"));
    QCOMPARE(lines.at(1), QStringLiteral("vec4 mytexture(vec2 u) { return vec4(u, 0.0, 1.0); }"));
    QCOMPARE(lines.at(4), QStringLiteral("  o = amber_texture(iChannel0, uv) + amber_texture(iChannel0, uv) + "
                                         "amber_texture (iChannel0, uv, 1.0)"));
    QCOMPARE(lines.at(5), QStringLiteral("    + amber_textureLod(iChannel0, uv, 0.0) + "
                                         "amber_texelFetch(iChannel0, ivec2(p), 0) + T(uv) + mytexture(uv)"));
    QCOMPARE(lines.at(6), QStringLiteral("    + vec4(vec2(textureSize(iChannel0, 0)), amber_FragCoord.xy);"));
  }

  void rejects_data() {
    QTest::addColumn<QString>("src");
    QTest::addColumn<QString>("needle");
    QTest::newRow("no mainImage") << QStringLiteral("void image(out vec4 c, in vec2 p) { c = vec4(1.0); }")
                                  << QStringLiteral("mainImage");
    QTest::newRow("main()") << kMinimal + QStringLiteral("void main() { }\n") << QStringLiteral("main()");
    QTest::newRow("iChannel1") << QStringLiteral("void mainImage(out vec4 c, in vec2 p) { c = texture(iChannel1, p); }")
                               << QStringLiteral("iChannel1");
    QTest::newRow("textureOffset")
        << QStringLiteral("void mainImage(out vec4 c, in vec2 p) { c = textureOffset(iChannel0, p, ivec2(1)); }")
        << QStringLiteral("textureOffset");
    QTest::newRow("textureGather") << QStringLiteral(
                                          "void mainImage(out vec4 c, in vec2 p) { c = textureGather(iChannel0, p); }")
                                   << QStringLiteral("textureGather");
    QTest::newRow("textureGrad")
        << QStringLiteral("void mainImage(out vec4 c, in vec2 p) { c = textureGrad(iChannel0, p, p, p); }")
        << QStringLiteral("textureGrad");
  }
  void rejects() {
    QFETCH(QString, src);
    QFETCH(QString, needle);
    const QString err = reject_message(src);
    QVERIFY2(err.contains(needle), qPrintable(QStringLiteral("message \"%1\" lacks \"%2\"").arg(err, needle)));
  }

  void rejectsBadParams() {
    QVERIFY(reject_message(kMinimal, {{QStringLiteral("iTime"), QStringLiteral("float")}}).contains("reserved"));
    QVERIFY(reject_message(kMinimal, {{QStringLiteral("amber_x"), QStringLiteral("float")}}).contains("reserved"));
    QVERIFY(reject_message(kMinimal, {{QStringLiteral("speed"), QStringLiteral("float")},
                                      {QStringLiteral("speed"), QStringLiteral("int")}})
                .contains("twice"));
  }

  void commentsNeitherRejectNorRewrite() {
    const QString src = QStringLiteral(
        "// uses iChannel1 one day\n"
        "/* void main() {\n"
        "   texture(iChannel0, p) */\n"
        "void mainImage(out vec4 c, in vec2 p) { // texture( textureOffset(\n"
        "  c = vec4(1.0); /* iChannel2 */ }\n");
    QString err;
    const QString out = to_amber_glsl(src, {}, &err);
    QVERIFY2(!out.isEmpty(), qPrintable(err));
    const QStringList lines = body_lines(out);
    // line numbers hold after the multi-line /* */
    QVERIFY(lines.at(0).trimmed().isEmpty());
    QVERIFY(lines.at(1).trimmed().isEmpty());
    QVERIFY(lines.at(2).trimmed().isEmpty());
    QCOMPARE(lines.at(3).trimmed(), QStringLiteral("void mainImage(out vec4 c, in vec2 p) {"));
    QCOMPARE(lines.at(4).simplified(), QStringLiteral("c = vec4(1.0); }"));
    QVERIFY(!body_lines(out).join('\n').contains(QLatin1String("amber_texture")));
  }

  void paramsAppendedInOrder() {
    const QString out = convert(kMinimal, {{QStringLiteral("speed"), QStringLiteral("float")},
                                           {QStringLiteral("invert"), QStringLiteral("bool")},
                                           {QStringLiteral("mode"), QStringLiteral("int")},
                                           {QStringLiteral("tint"), QStringLiteral("vec3")}});
    const qsizetype block = out.indexOf(QLatin1String("vec3 iChannelResolution[4];"));
    const qsizetype end = out.indexOf(QLatin1String("};"), block);
    QVERIFY(block > 0 && end > block);
    const QString members = out.mid(block, end - block);
    const qsizetype a = members.indexOf(QLatin1String("float speed;"));
    const qsizetype b = members.indexOf(QLatin1String("bool invert;"));
    const qsizetype c = members.indexOf(QLatin1String("int mode;"));
    const qsizetype d = members.indexOf(QLatin1String("vec3 tint;"));
    QVERIFY(a > 0 && b > a && c > b && d > c);
  }

  void crlfInput() {
    QString crlf = kMinimal;
    crlf.replace(QLatin1String("\n"), QLatin1String("\r\n"));
    const QString out = convert(QStringLiteral("#version 300 es\r\n") + crlf);
    QVERIFY(!out.isEmpty());
    QVERIFY(!out.contains('\r'));
    QVERIFY(out.contains(QLatin1String("#line 1\n\n") + kMinimal));
  }
};

QTEST_GUILESS_MAIN(TestShaderToy)
#include "test_shadertoy.moc"
