/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 */

module;

#include <cstdlib>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

export module dgtl.execution.loader;

import dgtl.object.file;
import dgtl.object.section;
import dgtl.object.symbol;
import dgtl.object.relocation;

export namespace dgtl::execution::loader {

enum class BinaryFormatType : uint8_t {
	ELF64 = 0,
	MachO64 = 1,
	WASM = 2,
	PE64 = 3,
	DSL_PAYLOAD = 4
};

struct Binary_Format {
	BinaryFormatType format_type = BinaryFormatType::ELF64;
	uint64_t entry_point = 0;
	std::vector<uint8_t> data;

	[[nodiscard]] bool is_valid() const noexcept {
		if (data.empty()) {
			return false;
		}

		switch (format_type) {
			case BinaryFormatType::ELF64:
				return data.size() >= 64 &&
				       data[0] == 0x7f && data[1] == 'E' && data[2] == 'L' && data[3] == 'F' &&
				       data[4] == 2u && data[5] == 1u;
			case BinaryFormatType::MachO64:
				return data.size() >= 32 && data[0] == 0xcf && data[1] == 0xfa && data[2] == 0xed && data[3] == 0xfe;
			case BinaryFormatType::PE64:
				return data.size() >= 64 && data[0] == 'M' && data[1] == 'Z';
			case BinaryFormatType::WASM:
				return data.size() >= 8 && data[0] == 0x00 && data[1] == 0x61 && data[2] == 0x73 && data[3] == 0x6d;
			case BinaryFormatType::DSL_PAYLOAD: {
				if (data.size() < 8) {
					return false;
				}
				uint32_t magic = 0;
				std::memcpy(&magic, data.data(), sizeof(uint32_t));
				return magic == 0x4D595A4E;
			}
			default:
				return false;
		}
	}

	[[nodiscard]] const void* get_code_ptr() const noexcept { return data.data(); }
	[[nodiscard]] void* get_code_ptr() noexcept { return data.data(); }
	[[nodiscard]] size_t get_code_size() const noexcept { return data.size(); }
	[[nodiscard]] uint64_t get_entry_point() const noexcept { return entry_point; }
};

enum class RelocationType : uint8_t {
	Absolute = 0,
	PCRelative = 1,
	Relative = 2,
	GOT = 3,
	PLT = 4
};

struct Symbol {
	std::string name;
	uint64_t address = 0;
	uint64_t size = 0;
	uint8_t binding = 0;
	uint8_t type = 0;
	uint16_t section_idx = 0;
	bool is_extern = false;

	[[nodiscard]] std::string_view get_name() const noexcept { return name; }
};

struct Relocation {
	uint64_t offset = 0;
	std::string symbol_name;
	int64_t addend = 0;
	RelocationType type = RelocationType::Absolute;

	[[nodiscard]] std::string_view get_symbol_name() const noexcept { return symbol_name; }
};

struct Section {
	std::vector<uint8_t> data;
	uint64_t base_address = 0;
	uint64_t alignment = 1;
	uint32_t flags = 0;
	uint64_t bss_size = 0;
};

struct StringViewHash {
	using is_transparent = void;
	[[nodiscard]] size_t operator()(std::string_view sv) const noexcept {
		return std::hash<std::string_view>{}(sv);
	}
};

struct StringViewEqual {
	using is_transparent = void;
	[[nodiscard]] bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
		return lhs == rhs;
	}
};

class Object_Format {
public:
	Object_Format() = default;
	~Object_Format() = default;

	Object_Format(const Object_Format&) = delete;
	Object_Format& operator=(const Object_Format&) = delete;

	[[nodiscard]] std::optional<uint64_t> find_symbol_address(std::string_view name) const noexcept {
		auto it = symbol_map_.find(name);
		if (it != symbol_map_.end()) {
			return it->second;
		}
		return std::nullopt;
	}

	void add_symbol(std::string_view name, uint64_t addr, uint64_t size,
					uint8_t binding = 0, uint8_t type = 0) {
		std::string str_name{name};
		symbols.push_back({str_name, addr, size, binding, type, 0, false});
		symbol_map_.try_emplace(std::move(str_name), addr);
	}

	void add_extern_symbol(std::string_view name, uint8_t binding = 1) {
		symbols.push_back({std::string(name), 0, 0, binding, 0, 0, true});
	}

	void add_relocation(uint64_t offset, std::string_view symbol,
						int64_t addend = 0, RelocationType type = RelocationType::Absolute) {
		relocations.push_back({offset, std::string(symbol), addend, type});
	}

	[[nodiscard]] dgtl::object::ObjectFile to_object_file(
		dgtl::object::ObjectFormat fmt = dgtl::object::ObjectFormat::ELF) const {
		dgtl::object::ObjectFile obj(fmt);

		if (!text_section.data.empty()) {
			auto& s = obj.add_section(".text", text_section.alignment,
				text_section.flags, dgtl::object::SectionKind::Text);
			s.address = text_section.base_address;
			s.bytes = text_section.data;
		}
		if (!data_section.data.empty()) {
			auto& s = obj.add_section(".data", data_section.alignment,
				data_section.flags, dgtl::object::SectionKind::Data);
			s.address = data_section.base_address;
			s.bytes = data_section.data;
		}
		if (!rodata_section.data.empty()) {
			auto& s = obj.add_section(".rodata", rodata_section.alignment,
				rodata_section.flags, dgtl::object::SectionKind::RData);
			s.address = rodata_section.base_address;
			s.bytes = rodata_section.data;
		}
		if (bss_section.bss_size != 0u) {
			auto& s = obj.add_section(".bss", bss_section.alignment,
				bss_section.flags | dgtl::object::SectionFlags::Write |
				dgtl::object::SectionFlags::ZeroFill,
				dgtl::object::SectionKind::BSS);
			s.address = bss_section.base_address;
			s.bss_size = bss_section.bss_size;
		}

		for (const auto& sym : symbols) {
			if (sym.is_extern) {
				obj.add_external_symbol(sym.name, sym.binding, sym.type);
			} else {
				obj.add_symbol(sym.name, sym.address, sym.size, sym.binding, sym.type);
			}
		}

		for (const auto& rel : relocations) {
			dgtl::object::RelocationKind rk = dgtl::object::RelocationKind::Absolute64;
			switch (rel.type) {
				case RelocationType::PCRelative:
					rk = dgtl::object::RelocationKind::PCRel32; break;
				case RelocationType::Relative:
					rk = dgtl::object::RelocationKind::Relative64; break;
				case RelocationType::GOT:
					rk = dgtl::object::RelocationKind::GOT64; break;
				case RelocationType::PLT:
					rk = dgtl::object::RelocationKind::PLT64; break;
				case RelocationType::Absolute:
				default:
					rk = dgtl::object::RelocationKind::Absolute64; break;
			}
			obj.add_relocation(rel.offset, rel.symbol_name, rel.addend, 0u, rk);
		}

		return obj;
	}

	[[nodiscard]] Section& get_text_section() noexcept { return text_section; }
	[[nodiscard]] Section& get_data_section() noexcept { return data_section; }
	[[nodiscard]] Section& get_rodata_section() noexcept { return rodata_section; }
	[[nodiscard]] Section& get_bss_section() noexcept { return bss_section; }

	[[nodiscard]] const Section& get_text_section() const noexcept { return text_section; }
	[[nodiscard]] const Section& get_data_section() const noexcept { return data_section; }
	[[nodiscard]] const Section& get_rodata_section() const noexcept { return rodata_section; }
	[[nodiscard]] const Section& get_bss_section() const noexcept { return bss_section; }

	[[nodiscard]] const std::vector<Symbol>& get_symbols() const noexcept { return symbols; }
	[[nodiscard]] const std::vector<Relocation>& get_relocations() const noexcept { return relocations; }
	[[nodiscard]] size_t symbol_count() const noexcept { return symbols.size(); }
	[[nodiscard]] size_t relocation_count() const noexcept { return relocations.size(); }

private:
	Section text_section;
	Section data_section;
	Section rodata_section;
	Section bss_section;

	std::vector<Symbol> symbols;
	std::vector<Relocation> relocations;
	std::unordered_map<std::string, uint64_t, StringViewHash, StringViewEqual> symbol_map_;
};

class Binary_Loader {
public:
	Binary_Loader() = default;
	~Binary_Loader() = default;

	Binary_Loader(const Binary_Loader&) = delete;
	Binary_Loader& operator=(const Binary_Loader&) = delete;

	[[nodiscard]] std::unique_ptr<Binary_Format> load(const std::string& filepath) {
		auto binary = std::make_unique<Binary_Format>();

		std::ifstream file(filepath, std::ios::binary | std::ios::ate);
		if (!file.is_open()) {
			throw std::runtime_error("Failed to open binary file: " + filepath);
		}

		const std::streamsize size = file.tellg();
		if (size < 64) {
			throw std::runtime_error("Binary file too small");
		}

		file.seekg(0, std::ios::beg);
		binary->data.resize(static_cast<size_t>(size));
		if (!file.read(reinterpret_cast<char*>(binary->data.data()), size)) {
			throw std::runtime_error("Failed to read binary file");
		}

		detect_format(binary.get());
		if (!binary->is_valid()) {
			throw std::runtime_error("Invalid binary format");
		}

		parse_entry_point(binary.get());
		return binary;
	}

	[[nodiscard]] bool load_frontend_payload(const std::string& filepath) {
		std::ifstream file(filepath, std::ios::binary | std::ios::ate);
		if (!file.is_open()) {
			return false;
		}

		const std::streamsize size = file.tellg();
		if (size < 8) {
			return false;
		}

		file.seekg(0, std::ios::beg);
		payload_buffer_.resize(static_cast<size_t>(size));
		if (!file.read(payload_buffer_.data(), size)) {
			return false;
		}

		const char* ptr = payload_buffer_.data();
		const char* end = ptr + size;

		const uint32_t magic = read_u32(ptr);
		ptr += sizeof(uint32_t);
		if (magic != 0x4D595A4E) {
			return false;
		}

		const uint32_t string_count = read_u32(ptr);
		ptr += sizeof(uint32_t);

		payload_string_ids_.clear();
		payload_string_ids_.reserve(string_count);

		for (uint32_t i = 0; i < string_count; ++i) {
			if (ptr + (sizeof(uint32_t) * 2) > end) {
				break;
			}

			const uint32_t id = read_u32(ptr);
			ptr += sizeof(uint32_t);

			const uint32_t len = read_u32(ptr);
			ptr += sizeof(uint32_t);

			if (ptr + len > end) {
				break;
			}
			payload_string_ids_.push_back(id);
			ptr += len;
		}

		return true;
	}

	[[nodiscard]] const std::vector<uint32_t>& get_payload_string_ids() const noexcept {
		return payload_string_ids_;
	}

	[[nodiscard]] std::unique_ptr<Binary_Format> load_from_memory(const void* data, size_t size) {
		if (data == nullptr || size < 64) {
			throw std::runtime_error("Invalid binary data");
		}

		auto binary = std::make_unique<Binary_Format>();
		binary->data.assign(static_cast<const uint8_t*>(data),
							static_cast<const uint8_t*>(data) + size);

		detect_format(binary.get());
		if (!binary->is_valid()) {
			throw std::runtime_error("Invalid binary format");
		}

		parse_entry_point(binary.get());
		return binary;
	}

private:
	std::vector<char> payload_buffer_;
	std::vector<uint32_t> payload_string_ids_;

	[[nodiscard]] static uint32_t read_u32(const void* ptr) noexcept {
		uint32_t v = 0;
		std::memcpy(&v, ptr, sizeof(uint32_t));
		return v;
	}

	[[nodiscard]] static uint64_t read_u64(const void* ptr) noexcept {
		uint64_t v = 0;
		std::memcpy(&v, ptr, sizeof(uint64_t));
		return v;
	}

	static void detect_format(Binary_Format* fmt) noexcept {
		if (fmt == nullptr || fmt->data.size() < 4) {
			if (fmt != nullptr) {
				fmt->format_type = BinaryFormatType::ELF64;
			}
			return;
		}

		const uint8_t* bytes = fmt->data.data();
		if (bytes[0] == 0x7f && bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F') {
			fmt->format_type = BinaryFormatType::ELF64;
		} else if (bytes[0] == 0xcf && bytes[1] == 0xfa && bytes[2] == 0xed && bytes[3] == 0xfe) {
			fmt->format_type = BinaryFormatType::MachO64;
		} else if (bytes[0] == 'M' && bytes[1] == 'Z') {
			fmt->format_type = BinaryFormatType::PE64;
		} else if (bytes[0] == 0x00 && bytes[1] == 0x61 && bytes[2] == 0x73 && bytes[3] == 0x6d) {
			fmt->format_type = BinaryFormatType::WASM;
		} else if (bytes[0] == 'M' && bytes[1] == 'Y' && bytes[2] == 'Z' && bytes[3] == 'N') {
			fmt->format_type = BinaryFormatType::DSL_PAYLOAD;
		} else {
			fmt->format_type = BinaryFormatType::ELF64;
		}
	}

	static void parse_entry_point(Binary_Format* fmt) noexcept {
		if (fmt == nullptr || fmt->data.size() < 64) {
			if (fmt != nullptr) {
				fmt->entry_point = 0;
			}
			return;
		}

		const uint8_t* bytes = fmt->data.data();
		switch (fmt->format_type) {
			case BinaryFormatType::ELF64:
				if (fmt->data.size() >= 32) {
					fmt->entry_point = read_u64(&bytes[24]);
				}
				break;
			case BinaryFormatType::MachO64:
				if (fmt->data.size() >= 40) {
					fmt->entry_point = read_u64(&bytes[32]);
				}
				break;
			case BinaryFormatType::PE64: {
				if (fmt->data.size() >= 0x40) {
					const uint32_t lfanew = read_u32(&bytes[0x3C]);
					const std::size_t optional_header =
						static_cast<std::size_t>(lfanew) + 4u + 20u;
					if (optional_header + 20u <= fmt->data.size()) {
						fmt->entry_point = read_u32(&bytes[optional_header + 16u]);
					} else {
						fmt->entry_point = 0;
					}
				}
				break;
			}
			case BinaryFormatType::WASM:
				fmt->entry_point = 0;
				break;
			default:
				fmt->entry_point = 0;
				break;
		}
	}
};

class Object_Loader {
public:
	Object_Loader() = default;
	~Object_Loader() = default;

	Object_Loader(const Object_Loader&) = delete;
	Object_Loader& operator=(const Object_Loader&) = delete;

	[[nodiscard]] std::unique_ptr<Object_Format> load(const std::string& filepath) {
		auto obj = std::make_unique<Object_Format>();

		std::ifstream file(filepath, std::ios::binary | std::ios::ate);
		if (!file.is_open()) {
			throw std::runtime_error("Failed to open object file: " + filepath);
		}

		const std::streamsize size = file.tellg();
		if (size < 64) {
			throw std::runtime_error("Object file too small");
		}

		file.seekg(0, std::ios::beg);
		std::vector<uint8_t> buffer(static_cast<size_t>(size));
		if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
			throw std::runtime_error("Failed to read object file");
		}

		if (!is_elf_format(buffer)) {
			throw std::runtime_error("Unsupported object format (not ELF)");
		}

		parse_elf_object(obj.get(), buffer);
		return obj;
	}

	[[nodiscard]] std::unique_ptr<Object_Format> load_from_memory(const void* data, size_t size) {
		if (data == nullptr || size < 64) {
			throw std::runtime_error("Invalid object data");
		}

		std::vector<uint8_t> buffer(static_cast<const uint8_t*>(data),
									static_cast<const uint8_t*>(data) + size);
		if (!is_elf_format(buffer)) {
			throw std::runtime_error("Unsupported object format");
		}

		auto obj = std::make_unique<Object_Format>();
		parse_elf_object(obj.get(), buffer);
		return obj;
	}

private:
	struct ElfSectionHeaderView {
		uint32_t name_offset = 0;
		uint32_t type = 0;
		uint64_t flags = 0;
		uint64_t addr = 0;
		uint64_t offset = 0;
		uint64_t size = 0;
		uint32_t link = 0;
		uint32_t info = 0;
		uint64_t addralign = 1;
		uint64_t entsize = 0;
	};

	static constexpr uint32_t kShtNull = 0;
	static constexpr uint32_t kShtSymTab = 2;
	static constexpr uint32_t kShtStrTab = 3;
	static constexpr uint32_t kShtRela = 4;
	static constexpr uint32_t kShtNoBits = 8;
	static constexpr uint32_t kShtRel = 9;
	static constexpr uint32_t kShtDynSym = 11;
	static constexpr uint16_t kShnUndef = 0;

	[[nodiscard]] static std::string read_c_string(const uint8_t* table, size_t table_size, uint32_t offset) {
		if (table == nullptr || offset >= table_size) {
			return {};
		}
		const char* begin = reinterpret_cast<const char*>(table + offset);
		const char* end = reinterpret_cast<const char*>(table + table_size);
		const char* p = begin;
		while (p < end && *p != '\0') {
			++p;
		}
		return std::string(begin, static_cast<size_t>(p - begin));
	}

	[[nodiscard]] static std::vector<ElfSectionHeaderView> read_section_headers(
		const std::vector<uint8_t>& buffer,
		uint64_t sh_offset,
		uint16_t sh_entsize,
		uint16_t sh_num) {
		std::vector<ElfSectionHeaderView> headers;
		headers.reserve(sh_num);

		for (uint16_t i = 0; i < sh_num; ++i) {
			const uint64_t offset = sh_offset + (static_cast<uint64_t>(i) * sh_entsize);
			if (offset + 64 > buffer.size()) {
				break;
			}
			const uint8_t* sh = buffer.data() + offset;

			ElfSectionHeaderView view;
			std::memcpy(&view.name_offset, sh + 0, 4);
			std::memcpy(&view.type, sh + 4, 4);
			std::memcpy(&view.flags, sh + 8, 8);
			std::memcpy(&view.addr, sh + 16, 8);
			std::memcpy(&view.offset, sh + 24, 8);
			std::memcpy(&view.size, sh + 32, 8);
			std::memcpy(&view.link, sh + 40, 4);
			std::memcpy(&view.info, sh + 44, 4);
			std::memcpy(&view.addralign, sh + 48, 8);
			std::memcpy(&view.entsize, sh + 56, 8);
			headers.push_back(view);
		}
		return headers;
	}

	[[nodiscard]] static std::vector<std::string> build_section_name_table(
		const std::vector<uint8_t>& buffer,
		const std::vector<ElfSectionHeaderView>& headers,
		uint16_t shstrndx) {
		std::vector<std::string> names(headers.size());
		if (shstrndx >= headers.size()) {
			return names;
		}

		const auto& strtab = headers[shstrndx];
		if (strtab.type != kShtStrTab || strtab.offset + strtab.size > buffer.size()) {
			return names;
		}

		const uint8_t* table = buffer.data() + strtab.offset;
		const size_t table_size = static_cast<size_t>(strtab.size);
		for (size_t i = 0; i < headers.size(); ++i) {
			names[i] = read_c_string(table, table_size, headers[i].name_offset);
		}
		return names;
	}

	[[nodiscard]] static bool is_elf_format(const std::vector<uint8_t>& buffer) noexcept {
		return buffer.size() >= 4 && buffer[0] == 0x7f && buffer[1] == 'E' && buffer[2] == 'L' && buffer[3] == 'F';
	}

	static void parse_elf_object(Object_Format* obj, const std::vector<uint8_t>& buffer) {
		if (buffer.size() < 64) {
			throw std::runtime_error("ELF header too small");
		}

		uint64_t e_shoff = 0;
		uint16_t e_shnum = 0;
		uint16_t e_shentsize = 0;
		uint16_t e_shstrndx = 0;

		std::memcpy(&e_shoff, buffer.data() + 32, 8);
		std::memcpy(&e_shentsize, buffer.data() + 46, 2);
		std::memcpy(&e_shnum, buffer.data() + 48, 2);
		std::memcpy(&e_shstrndx, buffer.data() + 50, 2);

		if (e_shoff == 0 || e_shnum == 0 || e_shentsize < 64) {
			throw std::runtime_error("Invalid ELF section headers");
		}

		const auto section_headers = read_section_headers(buffer, e_shoff, e_shentsize, e_shnum);
		if (section_headers.empty()) {
			throw std::runtime_error("Unable to parse ELF section headers");
		}

		const auto section_names = build_section_name_table(buffer, section_headers, e_shstrndx);
		parse_sections(obj, buffer, section_headers, section_names);
		const auto symbol_tables = parse_symbols(obj, buffer, section_headers);
		parse_relocations(obj, buffer, section_headers, symbol_tables);
	}

	static void parse_sections(Object_Format* obj,
							   const std::vector<uint8_t>& buffer,
							   const std::vector<ElfSectionHeaderView>& headers,
							   const std::vector<std::string>& section_names) noexcept {
		for (size_t i = 0; i < headers.size(); ++i) {
			const auto& sh = headers[i];
			if (sh.type == kShtNull) {
				continue;
			}

			Section sec;
			sec.base_address = sh.addr;
			sec.alignment = (sh.addralign == 0) ? 1 : sh.addralign;
			sec.flags = static_cast<uint32_t>(sh.flags & 0xFFFFFFFFu);

			if (sh.type == kShtNoBits) {
				sec.data.clear();
				sec.bss_size = sh.size;
			} else {
				if (sh.offset + sh.size > buffer.size()) {
					continue;
				}
				sec.data.assign(buffer.begin() + sh.offset, buffer.begin() + sh.offset + sh.size);
			}

			const std::string_view name = (i < section_names.size()) ? std::string_view(section_names[i]) : std::string_view{};
			if (name == ".text") {
				obj->get_text_section() = std::move(sec);
			} else if (name == ".data") {
				obj->get_data_section() = std::move(sec);
			} else if (name == ".rodata") {
				obj->get_rodata_section() = std::move(sec);
			} else if (name == ".bss" || sh.type == kShtNoBits) {
				obj->get_bss_section() = std::move(sec);
			}
		}
	}

	static std::unordered_map<uint32_t, std::vector<std::string>> parse_symbols(
		Object_Format* obj,
		const std::vector<uint8_t>& buffer,
		const std::vector<ElfSectionHeaderView>& headers) noexcept {
		std::unordered_map<uint32_t, std::vector<std::string>> symbol_tables;
		symbol_tables.reserve(2);

		for (uint32_t section_index = 0; section_index < static_cast<uint32_t>(headers.size()); ++section_index) {
			const auto& sh = headers[section_index];
			if (sh.type != kShtSymTab && sh.type != kShtDynSym) {
				continue;
			}
			if (sh.entsize < 24 || sh.size == 0 || sh.offset + sh.size > buffer.size() || sh.link >= headers.size()) {
				continue;
			}

			const auto& strtab = headers[sh.link];
			if (strtab.type != kShtStrTab || strtab.offset + strtab.size > buffer.size()) {
				continue;
			}

			const uint8_t* strtab_data = buffer.data() + strtab.offset;
			const size_t strtab_size = static_cast<size_t>(strtab.size);
			const uint64_t sym_count = sh.size / sh.entsize;

			auto& symbol_names = symbol_tables[section_index];
			symbol_names.clear();
			symbol_names.resize(static_cast<size_t>(sym_count));

			for (uint64_t j = 0; j < sym_count; ++j) {
				const uint64_t sym_entry_offset = sh.offset + (j * sh.entsize);
				if (sym_entry_offset + 24 > buffer.size()) {
					break;
				}
				const uint8_t* sym = buffer.data() + sym_entry_offset;

				uint32_t st_name = 0;
				uint8_t st_info = 0;
				uint16_t st_shndx = 0;
				uint64_t st_value = 0;
				uint64_t st_size = 0;

				std::memcpy(&st_name, sym + 0, 4);
				std::memcpy(&st_info, sym + 4, 1);
				std::memcpy(&st_shndx, sym + 6, 2);
				std::memcpy(&st_value, sym + 8, 8);
				std::memcpy(&st_size, sym + 16, 8);

				std::string name = read_c_string(strtab_data, strtab_size, st_name);
				if (name.empty()) {
					name = std::string("symbol_") + std::to_string(j);
				}

				symbol_names[static_cast<size_t>(j)] = name;

				const uint8_t binding = st_info >> 4;
				const uint8_t type = st_info & 0x0F;
				if (st_shndx == kShnUndef) {
					obj->add_extern_symbol(name, binding == 0 ? 1 : binding);
				} else {
					obj->add_symbol(name, st_value, st_size, binding, type);
				}
			}
		}

		return symbol_tables;
	}

	static void parse_relocations(
		Object_Format* obj,
		const std::vector<uint8_t>& buffer,
		const std::vector<ElfSectionHeaderView>& headers,
		const std::unordered_map<uint32_t, std::vector<std::string>>& symbol_tables) noexcept {
		for (const auto& sh : headers) {
			const bool is_rela = (sh.type == kShtRela);
			if (sh.type != kShtRel && sh.type != kShtRela) {
				continue;
			}
			if (sh.entsize == 0 || sh.size == 0 || sh.offset + sh.size > buffer.size()) {
				continue;
			}

			const uint64_t rel_count = sh.size / sh.entsize;
			for (uint64_t j = 0; j < rel_count; ++j) {
				const uint64_t rel_entry_offset = sh.offset + (j * sh.entsize);
				if (rel_entry_offset + 16 > buffer.size()) {
					break;
				}

				const uint8_t* rel = buffer.data() + rel_entry_offset;
				uint64_t r_offset = 0;
				uint64_t r_info = 0;
				int64_t r_addend = 0;

				std::memcpy(&r_offset, rel + 0, 8);
				std::memcpy(&r_info, rel + 8, 8);
				if (is_rela && rel_entry_offset + 24 <= buffer.size()) {
					std::memcpy(&r_addend, rel + 16, 8);
				}

				const uint32_t rel_type = static_cast<uint32_t>(r_info & 0xFFFFFFFFu);
				const uint32_t sym_index = static_cast<uint32_t>((r_info >> 32) & 0xFFFFFFFFu);

				RelocationType reloc_type = RelocationType::Absolute;
				if (rel_type == 1u) {
					reloc_type = RelocationType::PCRelative;
				} else if (rel_type == 8u) {
					reloc_type = RelocationType::Relative;
				}

				std::string symbol_name;
				const auto table_it = symbol_tables.find(sh.link);
				if (table_it != symbol_tables.end() &&
					sym_index < table_it->second.size() &&
					!table_it->second[sym_index].empty()) {
					symbol_name = table_it->second[sym_index];
				} else {
					symbol_name = std::string("symbol_") + std::to_string(sym_index);
				}

				obj->add_relocation(r_offset, symbol_name, r_addend, reloc_type);
			}
		}
	}
};

} // namespace dgtl::execution::format


