/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   DGTL Canonical HIR Generator Validator
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

// DGTL canonical HIR generator validator — C++26.
// The validator checks the schema, generated projections and architectural
// invariants. It does not encode operation semantics independently.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <string>
#include <vector>
namespace fs = std::filesystem;
static std::string read(const fs::path& p){ std::ifstream f(p); return {std::istreambuf_iterator<char>(f),{}}; }
static bool contains(const std::string& s,std::string_view x){return s.find(x)!=std::string::npos;}
static std::size_t count_re(const std::string&s,const std::regex&r){return std::distance(std::sregex_iterator(s.begin(),s.end(),r),std::sregex_iterator{});}
int main(int argc,char**argv){
    const fs::path root = argc>1 ? fs::path(argv[1]) : fs::path(".");
    const fs::path ir = root/"include/dgtl/ir";
    fs::path dsl = ir/"gen/defs/phase1_core_ops.dgtl_def";
    if (!fs::exists(dsl)) {
        dsl = root/"defs/ir/phase1_core_ops.dgtl_def";
    }
    const fs::path meta = ir/"gen/DGTL_Gen_Ops.ixx";
    fs::path en = ir/"gen/DGTL_Gen_Enums.ixx";
    if (!fs::exists(en)) {
        en = ir/"base/IR_Enums.ixx";
    }
    const fs::path wrappers = ir/"gen/DGTL_Gen_Op_Wrappers.ixx";
    bool ok=true;
    auto fail=[&](const std::string&m){std::cerr<<"[FAIL] "<<m<<'\n';ok=false;};
    if(!fs::exists(dsl)||!fs::exists(meta)||!fs::exists(en)||!fs::exists(wrappers)){fail("required generated artifacts are missing");return 2;}
    const auto ds=read(dsl), ms=read(meta), es=read(en), ws=read(wrappers);
    const std::regex op_re(R"(^\s*op\s+[A-Za-z_][A-Za-z0-9_]*:.*\bvalue=0x[0-9A-Fa-f]+\b)",std::regex::ECMAScript|std::regex::multiline);
    const std::regex meta_re(R"DGTL(\{"([^"]+)",\s*dgtl::ir::OpCode::([A-Z0-9_]+),\s*static_cast<uint16_t>\((0x[0-9A-Fa-f]+)\))DGTL");
    const std::regex wrap_re(R"(struct\s+[A-Za-z_][A-Za-z0-9_]*Op\s*:\s*public\s+OpWrapperBase)");
    const auto n=count_re(ds,op_re), m=count_re(ms,meta_re), w=count_re(ws,wrap_re);
    std::cout<<"[0] canonical operations: "<<n<<"\n";
    std::cout<<"[1] generated metadata rows: "<<m<<"\n";
    std::cout<<"[2] generated typed wrappers: "<<w<<"\n";
    if(n!=183) fail("canonical schema operation count is not 183");
    if(m!=n) fail("generated metadata count does not equal canonical schema count");
    if(w!=n) fail("typed wrapper count does not equal canonical schema count");
    for(const auto& d:{"canonical_source: true","opcode_values: explicit","stable_abi: preserve_existing_values"}) if(!contains(ds,d)) fail(std::string("missing directive: ")+d);
    for(const auto& a:{"alias MOV = REG_MOVE","alias CONST = LOAD_IMM","alias OP_ADD = ADD_I32","alias RETURN = RET","alias ASYNC_LAUNCH = ASYNC_SPAWN","alias SYNC_BARRIER = BARRIER_SYNC"}) if(!contains(ds,a)) fail(std::string("missing canonical alias: ")+a);
    if(contains(ds,"[opcode_aliases]")||contains(ds,"compat ")) fail("old compatibility grammar remains in the canonical DSL");
    if(!contains(ms,"kGeneratedOpAliases")||!contains(ms,"find_op_alias")) fail("generated alias metadata API is missing");
    if(!contains(ms,"kNumGeneratedOps = 183")) fail("generated operation count constant is stale");
    if(!contains(es,"AND_I64 = 0x41")||!contains(es,"SHR_I64 = 0x45")) fail("canonicalized I64 bitwise operations are missing from generated enum");
    if(fs::exists(ir/"base/IR_Verifier_stack_safe_from_uploaded.ixx")) fail("duplicate verifier artifact remains");
    const auto schema=read(ir/"IR_Schema.ixx"), verifier=read(ir/"base/IR_Verifier.ixx");
    if(contains(schema,"LegacyCompatibility")||contains(schema,"legacy")||contains(schema,"compatibility")) fail("schema contains a second semantic compatibility authority");
    if(contains(verifier,"legacy")||contains(verifier,"Legacy")||contains(verifier,"compatibility")||contains(verifier,"Compatibility")) fail("verifier contains an alternate compatibility path");
    for(const auto& f:{"IR_Node.ixx","IR_Stream.ixx","IR_SSA.ixx","IR_Verifier.ixx","IR_Traits_Static.ixx","Op_Traits.ixx"}){
        const auto s=read(ir/"base"/f); if(std::regex_search(s,std::regex(R"(import\s+dgtl\.target)"))) fail(std::string("target dependency leaked into HIR core: ")+f);
    }
    std::cout<<"[3] schema/alias invariants: "<<(ok?"OK":"FAIL")<<"\n";
    if(ok){std::cout<<"DGTL-Gen Validation PASSED — canonical IR schema is the sole semantic authority.\n";return 0;}
    return 1;
}
