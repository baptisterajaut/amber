#ifndef TEST_APPCONTEXT_STUB_H
#define TEST_APPCONTEXT_STUB_H

#include "core/headlessappcontext.h"

// HeadlessAppContext, but silent: messages are swallowed and every question is answered No.
class TestAppContext : public HeadlessAppContext {
 public:
  int showMessage(const QString&, const QString&, int) override { return 0; }
  bool showQuestion(const QString&, const QString&, bool, bool) override { return false; }
};

#endif  // TEST_APPCONTEXT_STUB_H
