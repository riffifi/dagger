#include "regalloc.h"
#include <algorithm>
#include <set>
#include <map>

namespace dagger {

std::string registerName(Register reg) {
    switch (reg) {
        case Register::RAX: return "rax";
        case Register::RBX: return "rbx";
        case Register::RCX: return "rcx";
        case Register::RDX: return "rdx";
        case Register::RSI: return "rsi";
        case Register::RDI: return "rdi";
        case Register::RBP: return "rbp";
        case Register::RSP: return "rsp";
        case Register::R8: return "r8";
        case Register::R9: return "r9";
        case Register::R10: return "r10";
        case Register::R11: return "r11";
        case Register::R12: return "r12";
        case Register::R13: return "r13";
        case Register::R14: return "r14";
        case Register::R15: return "r15";
        case Register::None: return "none";
    }
    return "unknown";
}

static Register stringToRegister(std::string_view name) {
    if (name == "rax") return Register::RAX;
    if (name == "rbx") return Register::RBX;
    if (name == "rcx") return Register::RCX;
    if (name == "rdx") return Register::RDX;
    if (name == "rsi") return Register::RSI;
    if (name == "rdi") return Register::RDI;
    if (name == "rbp") return Register::RBP;
    if (name == "rsp") return Register::RSP;
    if (name == "r8") return Register::R8;
    if (name == "r9") return Register::R9;
    if (name == "r10") return Register::R10;
    if (name == "r11") return Register::R11;
    if (name == "r12") return Register::R12;
    if (name == "r13") return Register::R13;
    if (name == "r14") return Register::R14;
    if (name == "r15") return Register::R15;
    return Register::None;
}

AllocationResult GreedyRegisterAllocator::allocate(const SIRFunction& func) {
    AllocationResult result;
    
    // Available registers for allocation (excluding RSP, RBP for now)
    std::vector<Register> generalPurpose = {
        Register::RBX, Register::R10, Register::R11, Register::R12, 
        Register::R13, Register::R14, Register::R15
    };
    
    // System V ABI argument registers
    std::vector<Register> argRegs = {
        Register::RDI, Register::RSI, Register::RDX, Register::RCX, Register::R8, Register::R9
    };

    std::map<Register, std::string> currentOwners;
    std::set<Register> freeRegs(generalPurpose.begin(), generalPurpose.end());
    // Also include RAX and RDX for general use if they are not pinned
    freeRegs.insert(Register::RAX);
    freeRegs.insert(Register::RDX);

    // 0. Handle parameters (pin to ABI registers)
    for (size_t i = 0; i < func.params.size(); ++i) {
        if (i < argRegs.size()) {
            Register reg = argRegs[i];
            result.assignments[func.params[i].name] = reg;
            currentOwners[reg] = func.params[i].name;
            freeRegs.erase(reg); // Definitely not free
        } else {
            // Handle stack parameters (TODO)
        }
    }

    auto allocateReg = [&](const std::string& vreg, Register preferred = Register::None) -> Register {
        if (preferred != Register::None) {
            if (freeRegs.count(preferred)) {
                 freeRegs.erase(preferred);
                 currentOwners[preferred] = vreg;
                 result.assignments[vreg] = preferred;
                 return preferred;
            }
            // If it's already owned by something else, we have a conflict
            return Register::None;
        }

        if (freeRegs.empty()) return Register::None;
        Register reg = *freeRegs.begin();
        freeRegs.erase(freeRegs.begin());
        currentOwners[reg] = vreg;
        result.assignments[vreg] = reg;
        return reg;
    };

    auto freeReg = [&](const std::string& vreg) {
        for (auto it = currentOwners.begin(); it != currentOwners.end(); ++it) {
            if (it->second == vreg) {
                freeRegs.insert(it->first);
                currentOwners.erase(it);
                return;
            }
        }
    };

    int nextStackOffset = 0;
    auto allocateStack = [&](const std::string& vreg) {
        result.stackSlots[vreg] = nextStackOffset;
        nextStackOffset += 8;
    };

    for (const auto& block : func.blocks) {
        for (const auto& inst : block.instructions) {
            // 1. Identify registers that are burst/freed in this instruction
            if (inst.op == SIROp::Burst || inst.op == SIROp::Free) {
                for (const auto& operand : inst.operands) {
                    if (operand.kind == SIRValueKind::VirtualRegister) {
                        freeReg(operand.name);
                    }
                }
            }

            // 2. Allocate register for the result
            if (inst.result && inst.result->kind == SIRValueKind::VirtualRegister) {
                if (result.assignments.find(inst.result->name) == result.assignments.end()) {
                    Register preferred = Register::None;
                    if (inst.result->pinnedRegister) {
                        preferred = stringToRegister(*inst.result->pinnedRegister);
                    }
                    Register reg = allocateReg(inst.result->name, preferred);
                    if (reg == Register::None && preferred == Register::None) {
                        allocateStack(inst.result->name);
                    } else if (reg == Register::None && preferred != Register::None) {
                        // Error: pinned register unavailable
                        // For now just spill but real compiler should error
                        allocateStack(inst.result->name);
                    }
                }
            }
        }
    }

    result.totalStackSpace = nextStackOffset;
    return result;
}

} // namespace dagger
