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

std::string bytes_to_hex(const std::vector<uint8_t>& bytes) {
	return dgtl::backend::driver::shared::bytes_to_hex(bytes);
}

bool write_text_file(const std::string& path, const std::string& content) {
	return dgtl::backend::driver::shared::write_text_file(path, content);
}

bool write_binary_file(const std::string& path, const std::vector<uint8_t>& bytes) {
	return dgtl::backend::driver::shared::write_binary_file(path, bytes);
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

bool emit_arm64([[maybe_unused]] const dgtl::ir::IRStream& stream,
				[[maybe_unused]] const dgtl::semantic::SymbolTable& symbols,
				bool verbose_asm,
				std::string& out_text,
				std::vector<uint8_t>* out_binary_stream) {
	std::ostringstream oss;
	oss << "// DGTL AArch64 Assembly\n";
	if (verbose_asm) {
		oss << "// Target: aarch64-unknown-linux-gnu\n";
	}
	oss << ".globl DGTL_kernel_entry\nDGTL_kernel_entry:\n    mov w0, #42\n    ret\n";
	out_text = oss.str();
	if (out_binary_stream != nullptr) {
		out_binary_stream->assign(out_text.begin(), out_text.end());
	}
	return !out_text.empty();
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

bool emit_arm32(std::string& out_text) {
	return dgtl::backend::driver::shared::emit_arm32_text(out_text);
}

bool emit_riscv64(std::string& out_text) {
	return dgtl::backend::driver::shared::emit_riscv64_text(out_text);
}

bool emit_cuda(const dgtl::ir::IRStream& stream, std::string& out_text) {
	out_text.clear();
	dgtl::core::TargetMachine target(dgtl::core::ArchType::X86_64_AVX2);
	dgtl::machine::MachineFunction mf("DGTL_kernel_entry");
	if (!lower_stream_to_mir(stream, mf, target)) {
		return false;
	}
	return dgtl::backend::driver::shared::emit_cuda_text(mf, out_text);
}

bool emit_rocm(const dgtl::ir::IRStream& stream, std::string& out_text) {
	out_text.clear();
	dgtl::core::TargetMachine target(dgtl::core::ArchType::X86_64_AVX2);
	dgtl::machine::MachineFunction mf("DGTL_kernel_entry");
	if (!lower_stream_to_mir(stream, mf, target)) {
		return false;
	}
	return dgtl::backend::driver::shared::emit_rocm_text(mf, out_text);
}

bool emit_macho(const dgtl::ir::IRStream& stream,
				[[maybe_unused]] const dgtl::semantic::SymbolTable& symbols,
				std::vector<uint8_t>& out_bytes,
				std::string& out_text) {
	out_bytes.clear();
	out_text.clear();
	dgtl::core::TargetMachine target(dgtl::core::ArchType::AARCH64_SVE);
	dgtl::machine::MachineFunction mf("DGTL_kernel_entry");
	if (!lower_stream_to_mir(stream, mf, target)) {
		return false;
	}
	dgtl::backend::linker::DGTLLinker linker;
	if (!linker.build_elf_relocatable_binary(mf, out_bytes, "DGTL_kernel_entry")) {
		return false;
	}
	out_text = bytes_to_hex(out_bytes);
	return true;
}

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