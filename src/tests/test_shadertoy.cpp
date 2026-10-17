// ShaderToy import: the pure preprocessor (core/shadertoy) and the CPU half of the effect plumbing (bake through
// Effect::bakeSource, runtime registration). No GPU: the render side lives in test_effects_gpu.cpp.

#include <QtTest>

#include <QTemporaryDir>

#include "core/shadertoy.h"
#include "effects/effect.h"
#include "effects/effectloaders.h"

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

QString read_fixture(const char* name) {
  QFile f(QStringLiteral(SOURCE_DIR "/tests/fixtures/shadertoy/") + QLatin1String(name));
  if (!f.open(QIODevice::ReadOnly)) return {};
  return QString::fromUtf8(f.readAll());
}

}  // namespace

class TestShaderToy : public QObject {
  Q_OBJECT

  QTemporaryDir cache_;

 private slots:
  void initTestCase() {
    // Bakes write the .qsb disk cache: keep it out of the user's cache, and empty so the baker really runs
    QVERIFY(cache_.isValid());
    qputenv("XDG_CACHE_HOME", cache_.path().toLocal8Bit());
  }

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

  void rejectsEveryUnflippableBuiltin() {
    for (const char* fn :
         {"textureGrad", "textureGradOffset", "textureOffset", "textureLodOffset", "textureProj", "textureProjOffset",
          "textureProjLod", "textureProjLodOffset", "textureProjGrad", "textureProjGradOffset", "textureGather",
          "textureGatherOffset", "textureGatherOffsets", "texelFetchOffset", "textureQueryLod"}) {
      const QString src =
          QStringLiteral("void mainImage(out vec4 c, in vec2 p) { c = vec4(%1 (iChannel0, p)); }").arg(fn);
      QVERIFY2(reject_message(src).contains(QLatin1String(fn) + QLatin1String("()")), fn);
    }
  }

  void userFunctionsNamedLikeBuiltinsPass() {
    // Only the exact GLSL builtin names are rejected: a user's textureGradient() / textureProjector() is fine
    const QString src = QStringLiteral(
        "vec4 textureGradient(vec2 p) { return vec4(p, 0.0, 1.0); }\n"
        "vec4 textureProjector(vec2 p) { return vec4(p.yx, 0.0, 1.0); }\n"
        "void mainImage(out vec4 c, in vec2 p) { c = textureGradient(p) + textureProjector(p); }\n");
    QString err;
    const QString out = to_amber_glsl(src, {}, &err);
    QVERIFY2(!out.isEmpty(), qPrintable(err));
    QVERIFY(Effect::bakeSource(out.toUtf8(), QShader::FragmentStage, &err).isValid());
  }

  void unterminatedBlockComment() {
    const QString err = reject_message(kMinimal + QStringLiteral("\n/* never closed\nvoid main() {}\n"));
    QCOMPARE(err, QStringLiteral("Unterminated /* comment starting at line 5."));
  }

  void effectNames_data() {
    QTest::addColumn<QString>("name");
    QTest::addColumn<bool>("valid");
    QTest::newRow("plain") << QStringLiteral("Plasma 2") << true;
    QTest::newRow("dotted") << QStringLiteral("v1.2 glow") << true;
    QTest::newRow("CONSOLE is not CON") << QStringLiteral("CONSOLE") << true;
    QTest::newRow("COM10 is not reserved") << QStringLiteral("COM10") << true;
    QTest::newRow("empty") << QString() << false;
    QTest::newRow("slash") << QStringLiteral("a/b") << false;
    QTest::newRow("backslash") << QStringLiteral("a\\b") << false;
    QTest::newRow("colon") << QStringLiteral("a:b") << false;
    QTest::newRow("star") << QStringLiteral("a*") << false;
    QTest::newRow("question") << QStringLiteral("a?") << false;
    QTest::newRow("quote") << QStringLiteral("a\"b") << false;
    QTest::newRow("angle") << QStringLiteral("<a>") << false;
    QTest::newRow("pipe") << QStringLiteral("a|b") << false;
    QTest::newRow("tab") << QStringLiteral("a\tb") << false;
    QTest::newRow("newline") << QStringLiteral("a\nb") << false;
    QTest::newRow("leading dot") << QStringLiteral(".hidden") << false;
    QTest::newRow("trailing dot") << QStringLiteral("name.") << false;
    QTest::newRow("trailing space") << QStringLiteral("name ") << false;
    QTest::newRow("CON") << QStringLiteral("CON") << false;
    QTest::newRow("nul lowercase") << QStringLiteral("nul") << false;
    QTest::newRow("Aux.txt") << QStringLiteral("Aux.txt") << false;
    QTest::newRow("PRN") << QStringLiteral("PRN") << false;
    QTest::newRow("COM1") << QStringLiteral("COM1") << false;
    QTest::newRow("lpt9.x") << QStringLiteral("lpt9.x") << false;
  }
  void effectNames() {
    QFETCH(QString, name);
    QFETCH(bool, valid);
    QCOMPARE(amber::shadertoy::effect_name_error(name).isEmpty(), valid);
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

  void bakesValid_data() {
    QTest::addColumn<QString>("src");
    QTest::newRow("passthrough") << read_fixture("st_read_texture.shadertoy");
    QTest::newRow("plasma") << read_fixture("st_plasma.shadertoy");
    QTest::newRow("defines") << QStringLiteral(
        "#define R iResolution\n"
        "#define T(u) texture(iChannel0, u)\n"
        "#define ROT(a) mat2(cos(a), -sin(a), sin(a), cos(a))\n"
        "#define S(a, b, t) smoothstep(a, b, t)\n"
        "void mainImage(out vec4 O, vec2 U) {\n"
        "  vec2 uv = U / R.xy;\n"
        "  uv = ROT(iTime) * (uv - 0.5) + 0.5;\n"
        "  O = T(uv) * S(0.0, 1.0, uv.x) + vec4(gl_FragCoord.xy / R.xy, float(iFrame), iDate.w);\n"
        "}\n");
    QTest::newRow("bias") << QStringLiteral(
        "void mainImage(out vec4 c, in vec2 p) { c = texture(iChannel0, p / iResolution.xy, 1.0) + iMouse; }\n");
    QTest::newRow("precision") << QStringLiteral(
        "#version 300 es\n"
        "precision highp float;\n"
        "precision mediump int;\n"
        "uniform vec3 iResolution;\n"
        "uniform float iTime;\n"
        "void mainImage(out vec4 c, in vec2 p) { c = vec4(p / iResolution.xy, sin(iTime), 1.0); }\n");
  }
  void bakesValid() {
    QFETCH(QString, src);
    QVERIFY(!src.isEmpty());
    QString err;
    const QString glsl = to_amber_glsl(src, {{QStringLiteral("speed"), QStringLiteral("float")}}, &err);
    QVERIFY2(!glsl.isEmpty(), qPrintable(err));
    const QShader shader = Effect::bakeSource(glsl.toUtf8(), QShader::FragmentStage, &err);
    QVERIFY2(shader.isValid(), qPrintable(err + "\n" + glsl));
    // The cache entry the import dialog leaves for the first render: a second bake of the same source loads it
    QVERIFY(Effect::bakeSource(glsl.toUtf8(), QShader::FragmentStage).isValid());
  }

  void bakeErrorCitesUserLine() {
    QString err;
    const QString glsl = to_amber_glsl(read_fixture("st_broken.shadertoy"), {}, &err);
    QVERIFY2(!glsl.isEmpty(), qPrintable(err));
    QVERIFY(!Effect::bakeSource(glsl.toUtf8(), QShader::FragmentStage, &err).isValid());
    // The fixture's syntax error is on its line 3: #line 1 maps baker lines back onto the user's
    QVERIFY2(err.contains(QLatin1String(":3:")), qPrintable(err));
  }

  void registryKeepsPointers() {
    const EffectMeta* first = register_shadertoy_effect(QStringLiteral("/nowhere/st_first.shadertoy"));
    QVERIFY(first != nullptr);
    QCOMPARE(first->name, QStringLiteral("st_first"));
    QCOMPARE(first->category, QStringLiteral("ShaderToy"));
    QCOMPARE(first->path, QStringLiteral("/nowhere"));
    QCOMPARE(first->internal, -1);
    const size_t index = effects.size() - 1;
    for (int i = 0; i < 100; i++) register_shadertoy_effect(QStringLiteral("/nowhere/st_%1.shadertoy").arg(i));
    QCOMPARE(&effects[index], first);
    QCOMPARE(first->name, QStringLiteral("st_first"));
    // A duplicate name (another folder) returns the first entry: the first effects folder wins
    const size_t count = effects.size();
    QCOMPARE(register_shadertoy_effect(QStringLiteral("/elsewhere/st_first.shadertoy")), first);
    QCOMPARE(effects.size(), count);
  }
};

QTEST_GUILESS_MAIN(TestShaderToy)
#include "test_shadertoy.moc"
