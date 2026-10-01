// Stub implementations of UI-layer symbols referenced by amber-engine objects
// but not available in headless test binaries.
//
// These are never called by the tests — they exist solely to satisfy the
// linker when the amber-engine OBJECT library is linked into test targets.
//
// Currently empty: amber-engine no longer references any UI-layer symbol at link time.
// Engine→UI calls go through amber::app_ctx (core/appcontext.h), stubbed in tests/test_appcontext_stub.h.
