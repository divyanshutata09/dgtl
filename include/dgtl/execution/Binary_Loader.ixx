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
export import dgtl.object.readers.elf;
export import dgtl.object.readers.pe;

export namespace dgtl::execution::loader {

// Canonical aliases mapping legacy execution loader object helpers to dgtl::object
using Object_Format = dgtl::object::ObjectFile;
using Object_Loader = dgtl::object::readers::ELFObjectReader;
using RelocationType = dgtl::object::RelocationKind;
using Symbol = dgtl::object::Symbol;
using Relocation = dgtl::object::Relocation;
using Section = dgtl::object::Section;

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
	uint64_t image_base = 0;
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
				return data.size() >= 32 &&
				       ((data[0] == 0xcf && data[1] == 0xfa && data[2] == 0xed && data[3] == 0xfe) ||
				        (data[0] == 0xfe && data[1] == 0xed && data[2] == 0xfa && data[3] == 0xcf) ||
				        (data[0] == 0xce && data[1] == 0xfa && data[2] == 0xed && data[3] == 0xfe));
			case BinaryFormatType::PE64: {
				if (data.size() < 64 || data[0] != 'M' || data[1] != 'Z') {
					return false;
				}
				uint32_t lfanew = 0;
				std::memcpy(&lfanew, &data[0x3C], sizeof(uint32_t));
				if (lfanew > data.size() || data.size() - lfanew < 4) {
					return false;
				}
				return data[lfanew] == 'P' && data[lfanew + 1] == 'E' &&
				       data[lfanew + 2] == '\0' && data[lfanew + 3] == '\0';
			}
			case BinaryFormatType::WASM:
				return data.size() >= 8 && data[0] == 0x00 && data[1] == 0x61 && data[2] == 0x73 && data[3] == 0x6d &&
				       data[4] == 0x01 && data[5] == 0x00 && data[6] == 0x00 && data[7] == 0x00;
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
	[[nodiscard]] uint64_t get_image_base() const noexcept { return image_base; }
	[[nodiscard]] uint64_t get_entry_point_virtual_address() const noexcept {
		if (format_type == BinaryFormatType::PE64 && image_base != 0) {
			return image_base + entry_point;
		}
		return entry_point;
	}
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
			if (static_cast<size_t>(end - ptr) < sizeof(uint32_t) * 2) {
				break;
			}

			const uint32_t id = read_u32(ptr);
			ptr += sizeof(uint32_t);

			const uint32_t len = read_u32(ptr);
			ptr += sizeof(uint32_t);

			if (static_cast<size_t>(end - ptr) < len) {
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

	[[nodiscard]] std::unique_ptr<dgtl::object::ObjectFile> load_object(const std::string& filepath) const {
		std::ifstream file(filepath, std::ios::binary | std::ios::ate);
		if (!file.is_open()) {
			return nullptr;
		}
		const std::streamsize size = file.tellg();
		if (size < 4) {
			return nullptr;
		}
		file.seekg(0, std::ios::beg);
		std::vector<uint8_t> buffer(static_cast<size_t>(size));
		if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
			return nullptr;
		}
		return load_object_from_memory(buffer.data(), buffer.size());
	}

	[[nodiscard]] std::unique_ptr<dgtl::object::ObjectFile> load_object_from_memory(const void* data, size_t size) const {
		if (data == nullptr || size < 4) {
			return nullptr;
		}
		const auto* bytes = static_cast<const uint8_t*>(data);
		if (dgtl::object::readers::ELFObjectReader::is_elf_format(bytes, size)) {
			dgtl::object::readers::ELFObjectReader elf_reader;
			return elf_reader.load_from_memory(data, size);
		}
		dgtl::object::readers::PEObjectReader pe_reader;
		return pe_reader.load_from_memory(data, size);
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
		} else if ((bytes[0] == 0xcf && bytes[1] == 0xfa && bytes[2] == 0xed && bytes[3] == 0xfe) ||
		           (bytes[0] == 0xfe && bytes[1] == 0xed && bytes[2] == 0xfa && bytes[3] == 0xcf) ||
		           (bytes[0] == 0xce && bytes[1] == 0xfa && bytes[2] == 0xed && bytes[3] == 0xfe)) {
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
		if (fmt == nullptr || fmt->data.size() < 32) {
			if (fmt != nullptr) {
				fmt->entry_point = 0;
				fmt->image_base = 0;
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
			case BinaryFormatType::MachO64: {
				if (fmt->data.size() >= 32) {
					const uint32_t ncmds = read_u32(&bytes[16]);
					const uint32_t sizeofcmds = read_u32(&bytes[20]);
					if (sizeofcmds <= fmt->data.size() - 32u) {
						size_t offset = 32;
						for (uint32_t i = 0; i < ncmds; ++i) {
							if (offset + 8 > fmt->data.size()) break;
							const uint32_t cmd = read_u32(&bytes[offset]);
							const uint32_t cmdsize = read_u32(&bytes[offset + 4]);
							if (cmdsize < 8 || offset + cmdsize > fmt->data.size()) break;

							// LC_MAIN = 0x80000028
							if (cmd == 0x80000028u && cmdsize >= 24) {
								fmt->entry_point = read_u64(&bytes[offset + 8]);
								break;
							}
							// LC_UNIXTHREAD = 0x5
							if (cmd == 0x5u && cmdsize >= 144 + 8) {
								// x86_64 flavor RIP is at offset 16 + 16*8 = 144
								fmt->entry_point = read_u64(&bytes[offset + 144]);
								break;
							}
							offset += cmdsize;
						}
					}
				}
				break;
			}
			case BinaryFormatType::PE64: {
				if (fmt->data.size() >= 0x40) {
					const uint32_t lfanew = read_u32(&bytes[0x3C]);
					if (lfanew <= fmt->data.size() - 4u &&
					    bytes[lfanew] == 'P' && bytes[lfanew + 1] == 'E' &&
					    bytes[lfanew + 2] == '\0' && bytes[lfanew + 3] == '\0') {
						const std::size_t optional_header =
							static_cast<std::size_t>(lfanew) + 4u + 20u;
						if (optional_header + 32u <= fmt->data.size()) {
							const uint16_t magic = *reinterpret_cast<const uint16_t*>(&bytes[optional_header]);
							if (magic == 0x020B) { // PE32+ (64-bit)
								fmt->entry_point = read_u32(&bytes[optional_header + 16u]);
								fmt->image_base = read_u64(&bytes[optional_header + 24u]);
							} else if (magic == 0x010B) { // PE32 (32-bit)
								fmt->entry_point = read_u32(&bytes[optional_header + 16u]);
								fmt->image_base = read_u32(&bytes[optional_header + 28u]);
							}
						}
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

} // namespace dgtl::execution::loader



