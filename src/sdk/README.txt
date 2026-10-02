DGTL AOT diagnostic fix

Changes to dgtl_sdk_impl.cpp:
- dgtl_session_compile now preserves the real DGTL optimization/backend diagnostic.
- AOT linker failure no longer collapses silently to generic "Unknown error".
- Adds overflow-safe machine-code buffer sizing.
- Converts exceptions escaping the noexcept ABI boundary into DGTL error codes with TLS diagnostics.
- Reports stream diagnostics when the AOT linker rejects the IR.
- Null session/stream errors now populate the DGTL error message.

No optimization level, verifier, test, compiler warning, or backend behavior was weakened.
The public header is unchanged.
