#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <map>

namespace dagger {

enum class SIRValueKind {
    None,
    Constant,
    VirtualRegister,
    Parameter,
    Global,
};

struct SIRValue {
    SIRValueKind kind = SIRValueKind::None;
    std::string name;
    std::string type; // Type as string for now
    int64_t intValue = 0;
    double floatValue = 0.0;
    bool boolValue = false;
    std::string textValue;
    std::optional<std::string> pinnedRegister;

    static SIRValue makeInt(int64_t v) { SIRValue sv; sv.kind = SIRValueKind::Constant; sv.intValue = v; sv.type = "int"; return sv; }
    static SIRValue makeFloat(double v) { SIRValue sv; sv.kind = SIRValueKind::Constant; sv.floatValue = v; sv.type = "float"; return sv; }
    static SIRValue makeBool(bool v) { SIRValue sv; sv.kind = SIRValueKind::Constant; sv.boolValue = v; sv.type = "bool"; return sv; }
    static SIRValue makeText(std::string v) { SIRValue sv; sv.kind = SIRValueKind::Constant; sv.textValue = std::move(v); sv.type = "text"; return sv; }
    static SIRValue makeVReg(std::string name, std::string type) { SIRValue sv; sv.kind = SIRValueKind::VirtualRegister; sv.name = std::move(name); sv.type = std::move(type); return sv; }
    static SIRValue makeParam(std::string name, std::string type) { SIRValue sv; sv.kind = SIRValueKind::Parameter; sv.name = std::move(name); sv.type = std::move(type); return sv; }
};

enum class SIROp {
    Add, Sub, Mul, Div, Mod,
    Neg,
    Eq, Neq, Gt, Lt, Gte, Lte,
    And, Or, Not,
    Call,
    Syscall,
    Jump,
    Branch, // if-then-else
    Return,
    Load,
    Store,
    Probe,  // Non-consuming read
    Burst,  // Consuming read + free
    Alloc,
    Free,
    AddrOf,
    Nop
};

struct SIRInstruction {
    SIROp op = SIROp::Nop;
    std::optional<SIRValue> result;
    std::vector<SIRValue> operands;
    std::string label; // for jumps/branches
    
    // Metadata for lifetime tracking
    bool isBurst = false; // Does this instruction consume its operands?
    std::vector<std::string> bursts; // Specific names (streams) burst by this instruction
};

struct SIRBasicBlock {
    std::string name;
    std::vector<SIRInstruction> instructions;
};

struct SIRFunction {
    std::string name;
    std::string resultType;
    std::vector<SIRValue> params;
    std::vector<SIRBasicBlock> blocks;
    std::vector<std::string> annotations;
};

struct SIRProgram {
    std::vector<SIRFunction> functions;
    std::vector<SIRValue> globals;
};

void printSIR(const SIRProgram& program);

} // namespace dagger
