DGTL Internal API World-Class Refactor Bundle

Updated:
- IRBuilder.ixx
- IR_Storage.ixx
- ExecutionEngine.ixx
- Config.ixx
- AOTSession.ixx
- JITSession.ixx
- HybridSession.ixx
- Api_all.ixx

Reviewed and intentionally unchanged:
- ABI.ixx
- ABI_Lowering.ixx
- Backend_Driver_API.ixx
- Backend_Driver_Shared_Utils.ixx
- Runtime_API.ixx

Reason: these are thin module/re-export facades; changing them without the underlying ownership modules would add indirection rather than improve the architecture.
