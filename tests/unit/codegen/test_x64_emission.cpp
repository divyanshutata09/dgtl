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

import dgtl;
import dgtl.arena;
import dgtl.mir.machine_ir;
import dgtl.isa.x64.mc_encoder;
import dgtl.isa.x64.x64_instruction_info;

#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

void print_hex(const std::vector<uint8_t>& code) {
    std::cout << "--- Generated Machine Code (" << code.size() << " bytes) ---\n";
    for (size_t i = 0; i < code.size(); ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << static_cast<int>(code[i]) << " ";
        if ((i + 1) % 8 == 0) std::cout << "  ";
        if ((i + 1) % 16 == 0) std::cout << "\n";
    }
    std::cout << std::dec << "\n\n";
}

void run_X64_Emitter_Test() {
    dgtl::isa::X64TargetRegisterInfo reg_info;

    using dgtl::machine::MachineRegister;
    using dgtl::machine::RegisterClassKind;
    using dgtl::machine::RegisterKind;
    const MachineRegister rax{0u, RegisterKind::Physical, RegisterClassKind::General};
    const MachineRegister xmm0{0u, RegisterKind::Physical, RegisterClassKind::FloatingPoint};
    const MachineRegister xmm4{4u, RegisterKind::Physical, RegisterClassKind::FloatingPoint};
    const MachineRegister xmm5{5u, RegisterKind::Physical, RegisterClassKind::FloatingPoint};
    const MachineRegister zmm0{16u, RegisterKind::Physical, RegisterClassKind::Vector};
    if (reg_info.aliases(rax, xmm0) || !reg_info.aliases(xmm0, zmm0) ||
        reg_info.is_reserved(xmm4) || reg_info.is_reserved(xmm5)) {
        std::cerr << "[!] FAIL: x64 register-bank aliasing or reserved-register contract is invalid.\n";
        return;
    }

    std::vector<uint8_t> machine_code;

    std::cout << "--- Starting DGTL Strike Test (X64 MC Encoder) ---\n";

    dgtl::machine::MachineFunction mf("DGTL_test_entry");
    auto& b0 = mf.create_block();
    mf.set_entry_block(b0.id);

    // 1. LOAD_IMM: RAX = 0x1122334455667788
    auto& inst1 = mf.create_instruction(dgtl::machine::GM_IMMEDIATE);
    inst1.form = dgtl::machine::MachineForm::LoadImmediate;
    inst1.add_operand(dgtl::machine::MachineOperand::def(rax));
    inst1.add_operand(dgtl::machine::MachineOperand::imm(0x1122334455667788LL));
    b0.append_instruction(inst1.id);
    inst1.parent_block = b0.id;

    // 2. ADD_I32: RAX += RCX
    const MachineRegister rcx{1u, RegisterKind::Physical, RegisterClassKind::General};
    auto& inst2 = mf.create_instruction(dgtl::machine::GM_IADD32);
    inst2.form = dgtl::machine::MachineForm::IntegerBinary32;
    inst2.semantic_kind = dgtl::machine::MachineSemanticKind::IntegerAdd;
    inst2.add_operand(dgtl::machine::MachineOperand::def(rax));
    inst2.add_operand(dgtl::machine::MachineOperand::use(rax));
    inst2.add_operand(dgtl::machine::MachineOperand::use(rcx));
    b0.append_instruction(inst2.id);
    inst2.parent_block = b0.id;

    // 3. REG_MOVE: RDX = RAX
    const MachineRegister rdx{2u, RegisterKind::Physical, RegisterClassKind::General};
    auto& inst3 = mf.create_instruction(dgtl::machine::GM_COPY);
    inst3.form = dgtl::machine::MachineForm::Move;
    inst3.add_operand(dgtl::machine::MachineOperand::def(rdx));
    inst3.add_operand(dgtl::machine::MachineOperand::use(rax));
    b0.append_instruction(inst3.id);
    inst3.parent_block = b0.id;

    // 4. RET
    auto& inst4 = mf.create_instruction(dgtl::machine::GM_RETURN);
    inst4.form = dgtl::machine::MachineForm::Return;
    b0.append_instruction(inst4.id);
    inst4.parent_block = b0.id;

    // Run Emission via X64MCEncoder
    std::string diag;
    if (!dgtl::isa::x64::X64MCEncoder::encode_to_bytes(mf, machine_code, "DGTL_test_entry", {}, &diag)) {
        std::cerr << "[!] FAIL: X64MCEncoder failed: " << diag << "\n";
        return;
    }

    if (machine_code.empty()) {
        std::cout << "[!] FAIL: Emitter returned 0 bytes.\n";
        return;
    }

    print_hex(machine_code);
    std::cout << "[SUCCESS] DGTL x64 Strike Test Complete.\n";

    return;
}







