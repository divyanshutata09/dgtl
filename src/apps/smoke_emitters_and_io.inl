bool emit_arm64(const dgtl::ir::IRStream& stream,
				const dgtl::core::SymbolTable& symbols,
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

std::string bytes_to_hex(const std::vector<uint8_t>& bytes) {
	static constexpr char kHex[] = "0123456789ABCDEF";
	std::string out;
	out.reserve(bytes.size() * 3);
	for (size_t i = 0; i < bytes.size(); ++i) {
		const uint8_t b = bytes[i];
		out.push_back(kHex[(b >> 4) & 0xF]);
		out.push_back(kHex[b & 0xF]);
		if (i + 1 < bytes.size()) {
			out.push_back(' ');
		}
	}
	return out;
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
	dgtl::isa::X64StrikeEmitter emitter;
	emitter.process_stream(stream, out_bytes);
	if (out_fallback_ops != nullptr) {
		*out_fallback_ops = emitter.fallback_hits();
	}
	if (out_bytes.empty()) {
		return false;
	}
	out_text = bytes_to_hex(out_bytes);
	return true;
}

bool emit_nasm(const dgtl::ir::IRStream& stream,
			   const dgtl::core::SymbolTable& symbols,
			   std::string& out_text) {
	out_text.clear();
	dgtl::isa::DGTLNasmEmitter emitter;
	emitter.emit(stream, symbols);
	out_text = emitter.get_output();
	return !out_text.empty();
}

bool emit_wasm(const dgtl::ir::IRStream& stream,
			   std::vector<uint8_t>& out_bytes,
			   std::string& out_text) {
	out_bytes.clear();
	out_text.clear();
	dgtl::exec::DGTLWasmEncoder encoder;
	encoder.encode(stream);
	out_bytes = encoder.get_binary();
	if (out_bytes.empty()) {
		return false;
	}
	out_text = bytes_to_hex(out_bytes);
	return true;
}

bool write_text_file(const std::string& path, const std::string& content) {
	const std::filesystem::path p(path);
	if (p.has_parent_path()) {
		std::error_code ec;
		std::filesystem::create_directories(p.parent_path(), ec);
		if (ec) return false;
	}

	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out.is_open()) {
		return false;
	}
	out.write(content.data(), static_cast<std::streamsize>(content.size()));
	return out.good();
}

bool write_binary_file(const std::string& path, const std::vector<uint8_t>& bytes) {
	const std::filesystem::path p(path);
	if (p.has_parent_path()) {
		std::error_code ec;
		std::filesystem::create_directories(p.parent_path(), ec);
		if (ec) return false;
	}

	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out.is_open()) {
		return false;
	}
	if (!bytes.empty()) {
		out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	}
	return out.good();
}

// ---------------------------------------------------------------------------
// ARM32 NEON: canonical gather+scatter assembly sequence
// ---------------------------------------------------------------------------
bool emit_arm32(std::string& out_text) {
	std::vector<std::string> lines;
	lines.reserve(64);
	lines.push_back(".arch armv7-a");
	lines.push_back(".fpu neon");
	lines.push_back(".syntax unified");
	lines.push_back("DGTL_arm32_entry:");
	// Canonical gather: base=r0, indices=q0, data=q1, elem_size=4, num_lanes=4
	dgtl::isa::ARM32NEONGatherScatter::emit_gather(lines, 0, 0, 1, 4, 4);
	lines.push_back("@ scatter result");
	// Canonical scatter: base=r2, indices=q2, data=q1, elem_size=4
	dgtl::isa::ARM32NEONGatherScatter::emit_scatter(lines, 2, 2, 1, 4);
	lines.push_back("bx lr");

	std::ostringstream oss;
	for (const auto& line : lines) {
		oss << line << '\n';
	}
	out_text = oss.str();
	return !out_text.empty();
}

// ---------------------------------------------------------------------------
// RISC-V 64: canonical V-extension gather+scatter assembly sequence
// ---------------------------------------------------------------------------
bool emit_riscv64(std::string& out_text) {
	std::vector<std::string> lines;
	lines.reserve(64);
	lines.push_back("# RISC-V 64 (RV64GC + V-extension) Emission");
	lines.push_back(".arch rv64gcv");
	lines.push_back("DGTL_riscv64_entry:");
	// Canonical gather: base=a0(0), indices=v0(0), data=v1(1), elem_size=4, use_v_ext=true
	dgtl::isa::RiscV64GatherScatter::emit_gather(lines, 0, 0, 1, 4, true);
	lines.push_back("# scatter result");
	// Canonical scatter: base=a1(1), indices=v0(0), data=v1(1), elem_size=4, use_v_ext=true
	dgtl::isa::RiscV64GatherScatter::emit_scatter(lines, 1, 0, 1, 4, true);
	lines.push_back("ret");

	std::ostringstream oss;
	for (const auto& line : lines) {
		oss << line << '\n';
	}
	out_text = oss.str();
	return !out_text.empty();
}

// ---------------------------------------------------------------------------
// CUDA PTX: fuse IR stream into NVIDIA PTX source via GPGPUFusionEngine
// ---------------------------------------------------------------------------
bool emit_cuda(const dgtl::ir::IRStream& stream, std::string& out_text) {
	dgtl::core::TargetMachine target(dgtl::core::ArchType::X86_64_AVX512);
	dgtl::isa::GPGPUFusionEngine engine(dgtl::isa::DeviceType::NVIDIA_PTX, target);
	out_text = engine.fuse_to_source(stream);
	return !out_text.empty();
}

// ---------------------------------------------------------------------------
// ROCm AMD GCN CDNA: fuse IR stream into AMD kernel source via GPGPUFusionEngine
// ---------------------------------------------------------------------------
bool emit_rocm(const dgtl::ir::IRStream& stream, std::string& out_text) {
	dgtl::core::TargetMachine target(dgtl::core::ArchType::AARCH64_SVE);
	dgtl::isa::GPGPUFusionEngine engine(dgtl::isa::DeviceType::AMD_GCN_CDNA, target);
	out_text = engine.fuse_to_source(stream);
	return !out_text.empty();
}

// ---------------------------------------------------------------------------
// Mach-O ARM64: produce Mach-O 64-bit relocatable binary via MachOEmitter
// ---------------------------------------------------------------------------
bool emit_macho(const dgtl::ir::IRStream& stream,
				const dgtl::core::SymbolTable& symbols,
				std::vector<uint8_t>& out_bytes,
				std::string& out_text) {
	dgtl::isa::MachOEmitter emitter(stream, symbols);
	dgtl::isa::MachOEmitter::MachOResult result = emitter.emit_to_binary();
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
	dgtl::exec::DGTLLinker linker;
	if (!linker.build_elf_relocatable(out_bytes, stream, "DGTL_kernel_entry")) {
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
	dgtl::exec::DGTLLinker linker;
	if (!linker.build_static_archive(out_bytes, stream, "DGTL_kernel.o")) {
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
	dgtl::exec::DGTLLinker linker;
	if (!linker.build_coff_relocatable(out_bytes, stream, "DGTL_kernel_entry")) {
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
	dgtl::exec::DGTLLinker linker;
	if (!linker.build_coff_archive(out_bytes, stream, "DGTL_kernel.obj", "DGTL_kernel_entry")) {
		return false;
	}
	out_text = bytes_to_hex(out_bytes);
	return !out_bytes.empty();
}