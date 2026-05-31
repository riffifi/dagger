#include "codegen.h"
#include <sstream>

namespace dagger {

std::string CodeGenerator::generate(const SIRProgram& program, const std::map<std::string, AllocationResult>& allocations) {
    std::ostringstream asmOut;
    asmOut << ".intel_syntax noprefix\n";
    asmOut << ".text\n";
    
    // Export all functions as global
    for (const auto& func : program.functions) {
        asmOut << ".global " << func.name << "\n";
    }
    asmOut << "\n";

    for (const auto& func : program.functions) {
        auto it = allocations.find(func.name);
        if (it != allocations.end()) {
            asmOut << generateFunction(func, it->second);
        }
    }

    asmOut << ".data\n";
    for (const auto& global : program.globals) {
        asmOut << global.name << ": .quad 0\n";
    }

    return asmOut.str();
}

std::string CodeGenerator::generateFunction(const SIRFunction& func, const AllocationResult& allocation) {
    std::ostringstream asmOut;
    asmOut << func.name << ":\n";
    
    // Prologue
    asmOut << "  push rbp\n";
    asmOut << "  mov rbp, rsp\n";
    if (allocation.totalStackSpace > 0) {
        asmOut << "  sub rsp, " << allocation.totalStackSpace << "\n";
    }

    for (const auto& block : func.blocks) {
        asmOut << "." << block.name << ":\n";
        for (const auto& inst : block.instructions) {
            asmOut << generateInstruction(inst, allocation, func);
        }
    }

    // Epilogue (usually reached by Return instruction)
    if (func.blocks.empty() || func.blocks.back().instructions.empty() || func.blocks.back().instructions.back().op != SIROp::Return) {
        if (allocation.totalStackSpace > 0) {
            asmOut << "  add rsp, " << allocation.totalStackSpace << "\n";
        }
        asmOut << "  pop rbp\n";
        asmOut << "  ret\n";
    }

    asmOut << "\n";
    return asmOut.str();
}

std::string CodeGenerator::operandToString(const SIRValue& val, const AllocationResult& allocation) {
    if (val.kind == SIRValueKind::Constant) {
        if (val.type == "int") return std::to_string(val.intValue);
        if (val.type == "bool") return val.boolValue ? "1" : "0";
        return "0";
    }
    if (val.kind == SIRValueKind::VirtualRegister || val.kind == SIRValueKind::Parameter) {
        auto it = allocation.assignments.find(val.name);
        if (it != allocation.assignments.end()) {
            return registerName(it->second);
        }
        auto sit = allocation.stackSlots.find(val.name);
        if (sit != allocation.stackSlots.end()) {
            return "[rbp - " + std::to_string(sit->second + 8) + "]";
        }
    }
    return "unknown";
}

std::string CodeGenerator::generateInstruction(const SIRInstruction& inst, const AllocationResult& allocation, const SIRFunction& func) {
    std::ostringstream asmOut;
    switch (inst.op) {
        case SIROp::Store: {
            std::string dest = operandToString(*inst.result, allocation);
            std::string src = operandToString(inst.operands[0], allocation);
            if (dest != src) {
                asmOut << "  mov " << dest << ", " << src << "\n";
            }
            break;
        }
        case SIROp::Probe:
        case SIROp::Burst: {
            std::string dest = operandToString(*inst.result, allocation);
            std::string src = operandToString(inst.operands[0], allocation);
            if (dest != src) {
                asmOut << "  mov " << dest << ", " << src << "\n";
            }
            break;
        }
        case SIROp::Add: {
            std::string dest = operandToString(*inst.result, allocation);
            std::string left = operandToString(inst.operands[0], allocation);
            std::string right = operandToString(inst.operands[1], allocation);
            if (dest != left) {
                asmOut << "  mov " << dest << ", " << left << "\n";
            }
            asmOut << "  add " << dest << ", " << right << "\n";
            break;
        }
        case SIROp::Sub: {
            std::string dest = operandToString(*inst.result, allocation);
            std::string left = operandToString(inst.operands[0], allocation);
            std::string right = operandToString(inst.operands[1], allocation);
            if (dest != left) {
                asmOut << "  mov " << dest << ", " << left << "\n";
            }
            asmOut << "  sub " << dest << ", " << right << "\n";
            break;
        }
        case SIROp::Mul: {
            std::string dest = operandToString(*inst.result, allocation);
            std::string left = operandToString(inst.operands[0], allocation);
            std::string right = operandToString(inst.operands[1], allocation);
            // Simple imul dest, right
            if (dest != left) {
                asmOut << "  mov " << dest << ", " << left << "\n";
            }
            asmOut << "  imul " << dest << ", " << right << "\n";
            break;
        }
        case SIROp::Return: {
            if (func.name == "main") {
                // If we are main, we can either return to libc or exit
                // For now, let's return to libc (ret) to be a good citizen
                if (!inst.operands.empty()) {
                    std::string src = operandToString(inst.operands[0], allocation);
                    if (src != "rax") {
                        asmOut << "  mov rax, " << src << "\n";
                    }
                }
                asmOut << "  pop rbp\n";
                asmOut << "  ret\n";
            } else {
                if (!inst.operands.empty()) {
                    std::string src = operandToString(inst.operands[0], allocation);
                    if (src != "rax") {
                        asmOut << "  mov rax, " << src << "\n";
                    }
                }
                if (allocation.totalStackSpace > 0) {
                    asmOut << "  add rsp, " << allocation.totalStackSpace << "\n";
                }
                asmOut << "  pop rbp\n";
                asmOut << "  ret\n";
            }
            break;
        }
        case SIROp::Call: {
            // System V ABI argument registers
            static const std::vector<std::string> abiArgRegs = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
            for (size_t i = 0; i < inst.operands.size() && i < abiArgRegs.size(); ++i) {
                std::string src = operandToString(inst.operands[i], allocation);
                if (src != abiArgRegs[i]) {
                    asmOut << "  mov " << abiArgRegs[i] << ", " << src << "\n";
                }
            }
            asmOut << "  call " << inst.label << "\n";
            if (inst.result) {
                std::string dest = operandToString(*inst.result, allocation);
                if (dest != "rax") {
                    asmOut << "  mov " << dest << ", rax\n";
                }
            }
            break;
        }
        case SIROp::Free: {
            // For stack values, Free is a no-op in code generation.
            // We just add a comment for debugging.
            if (!inst.operands.empty()) {
                asmOut << "  # free " << operandToString(inst.operands[0], allocation) << "\n";
            }
            break;
        }
        case SIROp::Nop:
            break;
        default:
            asmOut << "  # TODO: " << (int)inst.op << "\n";
            break;
    }
    return asmOut.str();
}

} // namespace dagger
