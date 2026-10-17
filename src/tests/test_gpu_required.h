#ifndef TEST_GPU_REQUIRED_H
#define TEST_GPU_REQUIRED_H

#include <QTest>
#include <QtGlobal>

// No GL / QRhi is a skip on a dev box but a failure when AMBER_TEST_REQUIRE_GPU=1 (CI), so a runner without Mesa
// cannot report a green run that rendered nothing. Codec-availability skips stay plain QSKIPs.
#define AMBER_SKIP_NO_GPU(msg)                                                   \
  do {                                                                           \
    if (qEnvironmentVariableIntValue("AMBER_TEST_REQUIRE_GPU") == 1) QFAIL(msg); \
    QSKIP(msg);                                                                  \
  } while (0)

#endif  // TEST_GPU_REQUIRED_H
