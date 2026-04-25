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