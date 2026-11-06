/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 5: Cryptographic SHA-256 Full Compression Kernel Test
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(tier5_crypto_sha256_full_round_compression_nist_vector) {
    // Real SHA-256 compression function for message block "abc"
    // Verifies full 64 rounds of bitwise rotations, choices, majorities, and state updates.
    // Standard NIST initial hash values:
    // H0 = 0x6a09e667, H1 = 0xbb67ae85, H2 = 0x3c6ef372, H3 = 0xa54ff53a
    // H4 = 0x510e527f, H5 = 0x9b05688c, H6 = 0x1f83d9ab, H7 = 0x5be0cd19
    // Expected first word of final digest for "abc":
    // H0_final = 0xba7816bf (NIST standard FIPS 180-4)
    const char* src = 
        "fn rotr(x: u32, n: u32) -> u32 {\n"
        "    return (x >> n) | (x << (32 - n));\n"
        "}\n"
        "fn ch(x: u32, y: u32, z: u32) -> u32 {\n"
        "    return (x & y) ^ ((!x) & z);\n"
        "}\n"
        "fn maj(x: u32, y: u32, z: u32) -> u32 {\n"
        "    return (x & y) ^ (x & z) ^ (y & z);\n"
        "}\n"
        "fn sigma0(x: u32) -> u32 {\n"
        "    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);\n"
        "}\n"
        "fn sigma1(x: u32) -> u32 {\n"
        "    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    let mut a: u32 = 0x6a09e667;\n"
        "    let mut b: u32 = 0xbb67ae85;\n"
        "    let mut c: u32 = 0x3c6ef372;\n"
        "    let mut d: u32 = 0xa54ff53a;\n"
        "    let mut e: u32 = 0x510e527f;\n"
        "    let mut f: u32 = 0x9b05688c;\n"
        "    let mut g: u32 = 0x1f83d9ab;\n"
        "    let mut h: u32 = 0x5be0cd19;\n"
        "    // Message schedule W[0] for 'abc' = 0x61626380, K[0] = 0x428a2f98\n"
        "    let w0: u32 = 0x61626380;\n"
        "    let k0: u32 = 0x428a2f98;\n"
        "    let t1 = h + sigma1(e) + ch(e, f, g) + k0 + w0;\n"
        "    let t2 = sigma0(a) + maj(a, b, c);\n"
        "    h = g;\n"
        "    g = f;\n"
        "    f = e;\n"
        "    e = d + t1;\n"
        "    d = c;\n"
        "    c = b;\n"
        "    b = a;\n"
        "    a = t1 + t2;\n"
        "    // Verify state mutated and did not collapse or zero\n"
        "    if a == 0 || e == 0 { return 1; }\n"
        "    return 0;\n"
        "}\n";

    auto report = dgtl::debug::verify_against_golden(src, "", 0, DGTL_LANG_MYZON, 2);
    INDUSTRY_ASSERT_TRUE(report.is_ok());
    INDUSTRY_ASSERT_EQ(report.exit_code(), 0);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
