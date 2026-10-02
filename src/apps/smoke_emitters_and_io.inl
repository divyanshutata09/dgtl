/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */
bool emit_arm64(const dgtl::ir::IRStream& stream,
				const dgtl::semantic::SymbolTable& symbols,
				bool verbose_asm,
				std::string& out_text,
				std::vector<uint8_t>* out_binary_stream) {
	dgtl::isa::ARM64Emitter emitter(stream, symbols);
	dgtl::isa::ARM64Emitter::Config cfg;
	cfg.verbose_asm = verbose_asm;

	std::ostringstream oss;
	emitter.emit_assembly(oss, cfg);
	out_text = oss.str();
	if (out_binary_stream != nullptr) {
		// This is intentionally a serialized text stream for benchmark A/B work,
		// not native ARM64 instruction encoding.
		out_binary_stream->assign(out_text.begin(), out_text.end());
	}
	return !out_text.empty();
}

static bool lower_stream_to_mir(const dgtl::ir::IRStream& stream,
                                dgtl::machine::MachineFunction& out_mf,
                                const dgtl::core::TargetMachine& target) {
    if (stream.is_null() || stream.size() == 0) return false;
    const dgtl::isa::x64::X64TargetSubtarget subtarget;
    auto res = dgtl::mir::pipeline::MachinePipeline::compile(
        out_mf,
        stream,
        target,
        subtarget.reg_info,
        &subtarget.instr_info,
        &subtarget.frame_info,
        &subtarget.abi(true),
        dgtl::mir::isel::make_builtin_instruction_selector_program());
    return res.success;
}

std::string bytes_to_hex(const std::vector<uint8_t>& bytes) {
	return dgtl::backend::driver::shared::bytes_to_hex(bytes);
}

bool emit_x64(const dgtl::ir::IRStream& stream,
			  std::vector<uint8_t>& out_bytes,
			  std::string& out_text,
			  size_t* out_fallback_ops) {
	out_bytes.clear();
	out_text.clear();
	if (out_fallback_ops != nullptr) {
		*out_fallback_ops = 0;
	}
	dgtl::core::TargetMachine target(dgtl::core::ArchType::X86_64_AVX2);
	dgtl::machine::MachineFunction mf("DGTL_kernel_entry");
	if (!lower_stream_to_mir(stream, mf, target)) {
		return false;
	}
	dgtl::isa::x64::X64StrikeEmitter emitter;
	if (!emitter.process_stream(mf, out_bytes)) {
		return false;
	}
	if (out_bytes.empty()) {
		return false;
	}
	out_text = bytes_to_hex(out_bytes);
	return true;
}

bool emit_nasm(const dgtl::ir::IRStream& stream,
			   const dgtl::semantic::SymbolTable& symbols,
			   std::string& out_text) {
	out_text.clear();
	dgtl::core::TargetMachine target(dgtl::core::ArchType::X86_64_AVX2);
	dgtl::machine::MachineFunction mf("DGTL_kernel_entry");
	if (!lower_stream_to_mir(stream, mf, target)) {
		return false;
	}
	dgtl::isa::x64::DGTLNasmEmitter emitter;
	emitter.emit(mf, &symbols);
	out_text = std::string(emitter.str());
	return !out_text.empty();
}

bool emit_wasm(const dgtl::ir::IRStream& stream,
			   std::vector<uint8_t>& out_bytes,
			   std::string& out_text) {
	out_bytes.clear();
	out_text.clear();
	dgtl::core::TargetMachine target(dgtl::core::ArchType::WASM_SIMD);
	dgtl::machine::MachineFunction mf("DGTL_kernel_entry");
	if (!lower_stream_to_mir(stream, mf, target)) {
		return false;
	}
	dgtl::backend::linker::DGTLLinker linker;
	if (!linker.build_wasm_binary(mf, out_bytes)) {
		return false;
	}
	out_text = bytes_to_hex(out_bytes);
	return true;
}

bool write_text_file(const std::string& path, const std::string& content) {
	return dgtl::backend::driver::shared::write_text_file(path, content);
}

bool write_binary_file(const std::string& path, const std::vector<uint8_t>& bytes) {
	return dgtl::backend::driver::shared::write_binary_file(path, bytes);
}

// ---------------------------------------------------------------------------
// ARM32 NEON: canonical gather+scatter assembly sequence
// ---------------------------------------------------------------------------
bool emit_arm32(std::string& out_text) {
	return dgtl::backend::driver::shared::emit_arm32_text(out_text);
}

// ---------------------------------------------------------------------------
// RISC-V 64: canonical V-extension gather+scatter assembly sequence
// ---------------------------------------------------------------------------
bool emit_riscv64(std::string& out_text) {
	return dgtl::backend::driver::shared::emit_riscv64_text(out_text);
}

// ---------------------------------------------------------------------------
// CUDA PTX: fuse IR stream into NVIDIA PTX source via GPGPUFusionEngine
// ---------------------------------------------------------------------------
bool emit_cuda(const dgtl::ir::IRStream& stream, std::string& out_text) {
	out_text.clear();
	dgtl::core::TargetMachine target(dgtl::core::ArchType::X86_64_AVX2);
	dgtl::machine::MachineFunction mf("DGTL_kernel_entry");
	if (!lower_stream_to_mir(stream, mf, target)) {
		return false;
	}
	return dgtl::backend::driver::shared::emit_cuda_text(mf, out_text);
}

// ---------------------------------------------------------------------------
// ROCm AMD GCN CDNA: fuse IR stream into AMD kernel source via GPGPUFusionEngine
// ---------------------------------------------------------------------------
bool emit_rocm(const dgtl::ir::IRStream& stream, std::string& out_text) {
	out_text.clear();
	dgtl::core::TargetMachine target(dgtl::core::ArchType::X86_64_AVX2);
	dgtl::machine::MachineFunction mf("DGTL_kernel_entry");
	if (!lower_stream_to_mir(stream, mf, target)) {
		return false;
	}
	return dgtl::backend::driver::shared::emit_rocm_text(mf, out_text);
}

// ---------------------------------------------------------------------------
// Mach-O ARM64: produce Mach-O 64-bit relocatable binary via MachOEmitter
// ---------------------------------------------------------------------------
bool emit_macho(const dgtl::ir::IRStream& stream,
				const dgtl::semantic::SymbolTable& symbols,
				std::vector<uint8_t>& out_bytes,
				std::string& out_text) {
	(void)symbols;
	out_bytes.clear();
	out_text.clear();
	dgtl::core::TargetMachine target(dgtl::core::ArchType::AARCH64_SVE);
	dgtl::machine::MachineFunction mf("DGTL_kernel_entry");
	if (!lower_stream_to_mir(stream, mf, target)) {
		return false;
	}
	dgtl::isa::MachOEmitter emitter(mf);
	auto result = emitter.emit_to_binary();
	if (!result.success || result.binary_data.empty()) {
		return false;
	}
	out_bytes = std::move(result.binary_data);
	out_text  = bytes_to_hex(out_bytes);
	return true;
}

// ---------------------------------------------------------------------------
// ELF64 relocatable object: produce .o binary via DGTLLinker
// ---------------------------------------------------------------------------
bool emit_elfobj(const dgtl::ir::IRStream& stream,
				 std::vector<uint8_t>& out_bytes,
				 std::string& out_text) {
	out_bytes.clear();
	out_text.clear();
	dgtl::core::TargetMachine target(dgtl::core::ArchType::X86_64_AVX2);
	dgtl::machine::MachineFunction mf("DGTL_kernel_entry");
	if (!lower_stream_to_mir(stream, mf, target)) {
		return false;
	}
	dgtl::backend::linker::DGTLLinker linker;
	if (!linker.build_elf_relocatable_binary(mf, out_bytes, "DGTL_kernel_entry")) {
		return false;
	}
	out_text = bytes_to_hex(out_bytes);
	return !out_bytes.empty();
}

// ---------------------------------------------------------------------------
// GNU ar static archive: produce .a binary via DGTLLinker
// ---------------------------------------------------------------------------
bool emit_staticlib(const dgtl::ir::IRStream& stream,
					std::vector<uint8_t>& out_bytes,
					std::string& out_text) {
	out_bytes.clear();
	out_text.clear();
	dgtl::core::TargetMachine target(dgtl::core::ArchType::X86_64_AVX2);
	dgtl::machine::MachineFunction mf("DGTL_kernel_entry");
	if (!lower_stream_to_mir(stream, mf, target)) {
		return false;
	}
	dgtl::backend::linker::DGTLLinker linker;
	if (!linker.build_static_archive_binary(mf, out_bytes, "module.o")) {
		return false;
	}
	out_text = bytes_to_hex(out_bytes);
	return !out_bytes.empty();
}

// ---------------------------------------------------------------------------
// COFF relocatable object: produce .obj binary via DGTLLinker
// ---------------------------------------------------------------------------
bool emit_coffobj(const dgtl::ir::IRStream& stream,
				  std::vector<uint8_t>& out_bytes,
				  std::string& out_text) {
	out_bytes.clear();
	out_text.clear();
	dgtl::core::TargetMachine target(dgtl::core::ArchType::X86_64_AVX2);
	dgtl::machine::MachineFunction mf("DGTL_kernel_entry");
	if (!lower_stream_to_mir(stream, mf, target)) {
		return false;
	}
	dgtl::backend::linker::DGTLLinker linker;
	if (!linker.build_coff_relocatable_binary(mf, out_bytes, "DGTL_kernel_entry")) {
		return false;
	}
	out_text = bytes_to_hex(out_bytes);
	return !out_bytes.empty();
}

// ---------------------------------------------------------------------------
// COFF import-library archive: produce .lib binary via DGTLLinker
// ---------------------------------------------------------------------------
bool emit_cofflib(const dgtl::ir::IRStream& stream,
				  std::vector<uint8_t>& out_bytes,
				  std::string& out_text) {
	out_bytes.clear();
	out_text.clear();
	dgtl::core::TargetMachine target(dgtl::core::ArchType::X86_64_AVX2);
	dgtl::machine::MachineFunction mf("DGTL_kernel_entry");
	if (!lower_stream_to_mir(stream, mf, target)) {
		return false;
	}
	dgtl::backend::linker::DGTLLinker linker;
	if (!linker.build_coff_archive_binary(mf, out_bytes, "module.obj", "DGTL_kernel_entry")) {
		return false;
	}
	out_text = bytes_to_hex(out_bytes);
	return !out_bytes.empty();
}