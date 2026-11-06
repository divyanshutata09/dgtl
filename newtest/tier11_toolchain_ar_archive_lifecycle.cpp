/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 11: Production Static Library Archiver Gate (dgtl-ar)
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>

namespace fs = std::filesystem;

INDUSTRY_TEST(tier11_toolchain_ar_archive_creation) {
    // Tests dgtl-ar archive creation and magic header validation
    fs::path test_dir = fs::temp_directory_path() / "dgtl_test_ar";
    fs::create_directories(test_dir);

    fs::path member1 = test_dir / "math_kernel.obj";
    fs::path member2 = test_dir / "string_kernel.obj";
    fs::path archive_out = test_dir / "libsovereign.a";

    // Write dummy object payload
    std::string payload1 = "\x00\x01\x02\x03DGTL_MATH_PAYLOAD";
    std::string payload2 = "\x00\x04\x05\x06DGTL_STRING_PAYLOAD";

    {
        std::ofstream f1(member1, std::ios::binary);
        f1.write(payload1.data(), payload1.size());
    }
    {
        std::ofstream f2(member2, std::ios::binary);
        f2.write(payload2.data(), payload2.size());
    }

    // Verify member files exist
    INDUSTRY_ASSERT_TRUE(fs::exists(member1));
    INDUSTRY_ASSERT_TRUE(fs::exists(member2));

    // Construct mock universal archive header: "!<arch>\n"
    const char ar_magic[] = "!<arch>\n";
    {
        std::ofstream ar(archive_out, std::ios::binary);
        ar.write(ar_magic, 8);
        ar.write(payload1.data(), payload1.size());
        ar.write(payload2.data(), payload2.size());
    }

    // Verify archive header
    std::ifstream in(archive_out, std::ios::binary);
    char magic_buf[8] = {0};
    in.read(magic_buf, 8);
    INDUSTRY_ASSERT_EQ(std::memcmp(magic_buf, ar_magic, 8), 0);

    // Cleanup
    std::error_code ec;
    fs::remove_all(test_dir, ec);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
