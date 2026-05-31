#include "sir.h"
#include <iostream>
#include <sstream>

namespace dagger {

static std::string opToString(SIROp op) {
    switch (op) {
        case SIROp::Add: return "add";
        case SIROp::Sub: return "sub";
        case SIROp::Mul: return "mul";
        case SIROp::Div: return "div";
        case SIROp::Mod: return "mod";
        case SIROp::Neg: return "neg";
        case SIROp::Eq: return "eq";
        case SIROp::Neq: return "neq";
        case SIROp::Gt: return "gt";
        case SIROp::Lt: return "lt";
        case SIROp::Gte: return "gte";
        case SIROp::Lte: return "lte";
        case SIROp::And: return "and";
        case SIROp::Or: return "or";
        case SIROp::Not: return "not";
        case SIROp::Call: return "call";
        case SIROp::Syscall: return "syscall";
        case SIROp::Jump: return "jump";
        case SIROp::Branch: return "branch";
        case SIROp::Return: return "ret";
        case SIROp::Load: return "load";
        case SIROp::Store: return "store";
        case SIROp::Probe: return "probe";
        case SIROp::Burst: return "burst";
        case SIROp::Alloc: return "alloc";
        case SIROp::Free: return "free";
        case SIROp::Nop: return "nop";
    }
    return "unknown";
}

static std::string valueToString(const SIRValue& val) {
    switch (val.kind) {
        case SIRValueKind::Constant:
            if (val.type == "int") return std::to_string(val.intValue);
            if (val.type == "float") return std::to_string(val.floatValue);
            if (val.type == "bool") return val.boolValue ? "true" : "false";
            if (val.type == "text") return "\"" + val.textValue + "\"";
            return "null";
        case SIRValueKind::VirtualRegister:
            return "%" + val.name;
        case SIRValueKind::Parameter:
            return "@" + val.name;
        case SIRValueKind::Global:
            return "$" + val.name;
        case SIRValueKind::None:
            return "none";
    }
    return "";
}

void printSIR(const SIRProgram& program) {
    for (const auto& global : program.globals) {
        std::cout << "global " << valueToString(global) << " :: " << global.type << "\n";
    }
    if (!program.globals.empty()) std::cout << "\n";

    for (const auto& func : program.functions) {
        std::cout << "fn " << func.name << "(";
        for (size_t i = 0; i < func.params.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << valueToString(func.params[i]) << " :: " << func.params[i].type;
        }
        std::cout << ") => " << func.resultType << " {\n";

        for (const auto& block : func.blocks) {
            std::cout << block.name << ":\n";
            for (const auto& inst : block.instructions) {
                std::cout << "  ";
                if (inst.result) {
                    std::cout << valueToString(*inst.result) << " = ";
                }
                std::cout << opToString(inst.op);
                if (!inst.label.empty()) {
                    std::cout << " " << inst.label;
                }
                for (const auto& op : inst.operands) {
                    std::cout << " " << valueToString(op);
                }
                std::cout << "\n";
            }
        }
        std::cout << "}\n\n";
    }
}

} // namespace dagger
