#include "lowering.h"
#include <sstream>

namespace dagger {

Lowerer::Lowerer(const Program& program) : program_(program) {}

SIRProgram Lowerer::lower() {
    SIRFunction startFunc;
    startFunc.name = "main";
    startFunc.resultType = "int";
    
    SIRBasicBlock entry;
    entry.name = "entry";
    startFunc.blocks.push_back(std::move(entry));

    for (const auto& statement : program_.statements) {
        if (auto* func = dynamic_cast<FunctionDecl*>(statement.get())) {
            lowerFunction(*func);
        } else if (auto* type = dynamic_cast<TypeDecl*>(statement.get())) {
            // Types handled in semantics
        } else if (auto* stream = dynamic_cast<StreamDecl*>(statement.get())) {
            if (stream->isStatic) {
                for (const auto& name : stream->names) {
                    sir_.globals.push_back(SIRValue::makeVReg(name, stream->typeName.value_or("unknown")));
                }
            } else {
                lowerStatement(*stream, startFunc);
            }
        } else {
            lowerStatement(*statement, startFunc);
        }
    }

    // Free any remaining top-level streams
    for (auto& [name, burst] : streams_) {
        if (!burst) {
            SIRInstruction freeInst;
            freeInst.op = SIROp::Free;
            freeInst.operands = { SIRValue::makeVReg(name, "unknown") };
            startFunc.blocks.back().instructions.push_back(std::move(freeInst));
            burst = true;
        }
    }

    if (startFunc.blocks.back().instructions.empty() || startFunc.blocks.back().instructions.back().op != SIROp::Return) {
        SIRInstruction ret;
        ret.op = SIROp::Return;
        startFunc.blocks.back().instructions.push_back(std::move(ret));
    }
    sir_.functions.push_back(std::move(startFunc));

    return std::move(sir_);
}

std::string Lowerer::nextVReg() {
    return "v" + std::to_string(vregCount_++);
}

std::string Lowerer::nextLabel() {
    return "L" + std::to_string(labelCount_++);
}

void Lowerer::lowerFunction(const FunctionDecl& decl) {
    if (!decl.body) {
        return;
    }
    SIRFunction sirFunc;
    sirFunc.name = decl.name;
    sirFunc.resultType = decl.resultType.value_or("null");
    sirFunc.annotations = decl.annotations;
    
    streams_.clear();
    for (const auto& param : decl.params) {
        sirFunc.params.push_back(SIRValue::makeParam(param.name, param.typeName.value_or("unknown")));
        streams_[param.name] = false;
    }

    SIRBasicBlock entry;
    entry.name = "entry";
    sirFunc.blocks.push_back(std::move(entry));

    SIRValue res = lowerExpression(*decl.body, sirFunc);

    // Free any remaining parameters at function end
    for (auto& [name, burst] : streams_) {
        if (!burst) {
            SIRInstruction freeInst;
            freeInst.op = SIROp::Free;
            freeInst.operands = { SIRValue::makeVReg(name, "unknown") }; // type not used for free usually
            sirFunc.blocks.back().instructions.push_back(std::move(freeInst));
            burst = true;
        }
    }
    
    if (sirFunc.blocks.back().instructions.empty() || sirFunc.blocks.back().instructions.back().op != SIROp::Return) {
        SIRInstruction ret;
        ret.op = SIROp::Return;
        ret.operands = { res };
        sirFunc.blocks.back().instructions.push_back(std::move(ret));
    }

    sir_.functions.push_back(std::move(sirFunc));
}

void Lowerer::lowerStatement(const Statement& stmt, SIRFunction& currentFunc) {
    if (auto* exprStmt = dynamic_cast<const ExprStmt*>(&stmt)) {
        lowerExpression(*exprStmt->expression, currentFunc);
    } else if (auto* streamDecl = dynamic_cast<const StreamDecl*>(&stmt)) {
        for (size_t i = 0; i < streamDecl->names.size(); ++i) {
             streams_[streamDecl->names[i]] = false;
             if (i < streamDecl->initializers.size()) {
                 SIRValue val = lowerExpression(*streamDecl->initializers[i], currentFunc);
                 SIRInstruction store;
                 store.op = SIROp::Store;
                 store.operands = { val };
                 store.result = SIRValue::makeVReg(streamDecl->names[i], val.type);
                 if (streamDecl->registerPin) {
                     store.result->pinnedRegister = streamDecl->registerPin->substr(1); // remove '@'
                 }
                 currentFunc.blocks.back().instructions.push_back(std::move(store));
             }
        }
    }
}

SIRValue Lowerer::lowerExpression(const Expression& expr, SIRFunction& currentFunc, std::optional<SIRValue> input) {
    if (auto* literal = dynamic_cast<const LiteralExpr*>(&expr)) {
        if (std::holds_alternative<int64_t>(literal->value)) return SIRValue::makeInt(std::get<int64_t>(literal->value));
        if (std::holds_alternative<double>(literal->value)) return SIRValue::makeFloat(std::get<double>(literal->value));
        if (std::holds_alternative<bool>(literal->value)) return SIRValue::makeBool(std::get<bool>(literal->value));
        if (std::holds_alternative<std::string>(literal->value)) return SIRValue::makeText(std::get<std::string>(literal->value));
        return SIRValue();
    }

    if (auto* id = dynamic_cast<const IdentifierExpr*>(&expr)) {
        // If it's an identifier, it's an implicit Probe of a stream
        SIRInstruction inst;
        inst.op = SIROp::Probe;
        inst.operands = { SIRValue::makeVReg(id->name, expr.inferredType.describe()) };
        inst.result = SIRValue::makeVReg(nextVReg(), expr.inferredType.describe());
        SIRValue res = *inst.result;
        currentFunc.blocks.back().instructions.push_back(std::move(inst));
        return res;
    }

    if (auto* prefix = dynamic_cast<const PrefixExpr*>(&expr)) {
        if (prefix->op == "?" || prefix->op == "!") {
            auto* id = dynamic_cast<const IdentifierExpr*>(prefix->right.get());
            if (!id) throw std::runtime_error("Probe/Burst must be followed by an identifier");
            
            SIRInstruction inst;
            inst.op = (prefix->op == "?") ? SIROp::Probe : SIROp::Burst;
            inst.operands = { SIRValue::makeVReg(id->name, expr.inferredType.describe()) };
            inst.result = SIRValue::makeVReg(nextVReg(), expr.inferredType.describe());
            if (prefix->op == "!") {
                inst.isBurst = true;
                inst.bursts.push_back(id->name);
                streams_[id->name] = true;
            }
            SIRValue res = *inst.result;
            currentFunc.blocks.back().instructions.push_back(std::move(inst));
            return res;
        }
        if (prefix->op == "&") {
            auto* id = dynamic_cast<IdentifierExpr*>(prefix->right.get());
            if (!id) throw std::runtime_error("Address-of requires an identifier");
            
            SIRInstruction inst;
            inst.op = SIROp::AddrOf;
            inst.operands = { SIRValue::makeVReg(id->name, "unknown") };
            inst.result = SIRValue::makeVReg(nextVReg(), expr.inferredType.describe());
            SIRValue res = *inst.result;
            currentFunc.blocks.back().instructions.push_back(std::move(inst));
            return res;
        }
        if (prefix->op == "-") {
            SIRValue right = lowerExpression(*prefix->right, currentFunc, input);
            SIRInstruction inst;
            inst.op = SIROp::Neg;
            inst.operands = { right };
            inst.result = SIRValue::makeVReg(nextVReg(), expr.inferredType.describe());
            SIRValue res = *inst.result;
            currentFunc.blocks.back().instructions.push_back(std::move(inst));
            return res;
        }
    }

    if (auto* compare = dynamic_cast<const ProbeCompareExpr*>(&expr)) {
        if (!input) throw std::runtime_error("Probe comparison requires an input");
        SIRValue right = lowerExpression(*compare->right, currentFunc, input);
        SIRInstruction inst;
        if (compare->op == "?=") inst.op = SIROp::Eq;
        else if (compare->op == "?!=") inst.op = SIROp::Neq;
        else if (compare->op == "?>") inst.op = SIROp::Gt;
        else if (compare->op == "?<") inst.op = SIROp::Lt;
        else if (compare->op == "?>=") inst.op = SIROp::Gte;
        else if (compare->op == "?<=") inst.op = SIROp::Lte;
        else throw std::runtime_error("Unknown comparison operator: " + compare->op);

        inst.operands = { *input, right };
        inst.result = SIRValue::makeVReg(nextVReg(), "bool");
        SIRValue res = *inst.result;
        currentFunc.blocks.back().instructions.push_back(std::move(inst));
        return res;
    }

    if (auto* route = dynamic_cast<const RouteExpr*>(&expr)) {
        SIRValue current = lowerExpression(*route->source, currentFunc, input);
        for (auto& stage : route->stages) {
            if (auto* callStage = dynamic_cast<CallExpr*>(stage.get())) {
                SIRInstruction inst;
                if (auto* callee = dynamic_cast<IdentifierExpr*>(callStage->callee.get())) {
                    if (callee->name == "add") inst.op = SIROp::Add;
                    else if (callee->name == "sub") inst.op = SIROp::Sub;
                    else if (callee->name == "mul") inst.op = SIROp::Mul;
                    else if (callee->name == "div") inst.op = SIROp::Div;
                    else {
                        inst.op = SIROp::Call;
                        inst.label = callee->name;
                    }
                } else {
                    inst.op = SIROp::Call;
                }
                inst.operands.push_back(current);
                for (auto& arg : callStage->arguments) {
                    inst.operands.push_back(lowerExpression(*arg, currentFunc, input));
                }
                inst.result = SIRValue::makeVReg(nextVReg(), stage->inferredType.describe());
                current = *inst.result;
                currentFunc.blocks.back().instructions.push_back(std::move(inst));
            } else if (auto* capture = dynamic_cast<CaptureExpr*>(stage.get())) {
                SIRInstruction store;
                store.op = SIROp::Store;
                store.operands = { current };
                store.result = SIRValue::makeVReg(capture->name, current.type);
                currentFunc.blocks.back().instructions.push_back(std::move(store));
                streams_[capture->name] = false;
                current = SIRValue::makeInt(0); // result of capture is null
            } else if (auto* blockStage = dynamic_cast<BlockExpr*>(stage.get())) {
                // TODO: handle block input correctly
                current = lowerExpression(*blockStage, currentFunc);
            } else if (auto* forkStage = dynamic_cast<ForkExpr*>(stage.get())) {
                current = lowerFork(*forkStage, current, currentFunc);
            } else if (auto* loopStage = dynamic_cast<LoopExpr*>(stage.get())) {
                current = lowerLoop(*loopStage, current, currentFunc);
            } else if (auto* idStage = dynamic_cast<IdentifierExpr*>(stage.get())) {
                if (streams_.count(idStage->name)) {
                    // Reassignment to an existing stream
                    SIRInstruction store;
                    store.op = SIROp::Store;
                    store.operands = { current };
                    store.result = SIRValue::makeVReg(idStage->name, current.type);
                    currentFunc.blocks.back().instructions.push_back(std::move(store));
                    streams_[idStage->name] = false; // Reset burst state on reassignment? 
                    // Actually Dagger spec says reassignment is in-place.
                    current = SIRValue::makeInt(0);
                } else {
                    // Function call
                    SIRInstruction inst;
                    inst.op = SIROp::Call;
                    inst.label = idStage->name;
                    inst.operands = { current };
                    inst.result = SIRValue::makeVReg(nextVReg(), stage->inferredType.describe());
                    current = *inst.result;
                    currentFunc.blocks.back().instructions.push_back(std::move(inst));
                }
            }
        }
        return current;
    }

    if (auto* fork = dynamic_cast<const ForkExpr*>(&expr)) {
        return lowerFork(*fork, input.value_or(SIRValue::makeInt(0)), currentFunc);
    }

    if (auto* loop = dynamic_cast<const LoopExpr*>(&expr)) {
        return lowerLoop(*loop, input.value_or(SIRValue::makeInt(0)), currentFunc);
    }

    if (auto* block = dynamic_cast<const BlockExpr*>(&expr)) {
        auto oldStreams = streams_;
        if (block->inputName && input) {
            SIRInstruction store;
            store.op = SIROp::Store;
            store.operands = { *input };
            store.result = SIRValue::makeVReg(*block->inputName, input->type);
            currentFunc.blocks.back().instructions.push_back(std::move(store));
            streams_[*block->inputName] = false;
        }

        SIRValue lastVal = SIRValue::makeInt(0);
        for (size_t i = 0; i < block->body.size(); ++i) {
            if (i == block->body.size() - 1) {
                if (auto* exprStmt = dynamic_cast<ExprStmt*>(block->body[i].get())) {
                    lastVal = lowerExpression(*exprStmt->expression, currentFunc, input);
                    continue;
                }
            }
            lowerStatement(*block->body[i], currentFunc);
        }

        // Free block-local streams
        for (auto& [name, burst] : streams_) {
            if (oldStreams.find(name) == oldStreams.end()) {
                if (!burst) {
                    SIRInstruction freeInst;
                    freeInst.op = SIROp::Free;
                    freeInst.operands = { SIRValue::makeVReg(name, "unknown") };
                    currentFunc.blocks.back().instructions.push_back(std::move(freeInst));
                }
            }
        }

        streams_ = oldStreams;
        return lastVal;
    }

    if (auto* call = dynamic_cast<const CallExpr*>(&expr)) {
        SIRInstruction inst;
        if (auto* callee = dynamic_cast<IdentifierExpr*>(call->callee.get())) {
            if (callee->name == "add") inst.op = SIROp::Add;
            else if (callee->name == "sub") inst.op = SIROp::Sub;
            else if (callee->name == "mul") inst.op = SIROp::Mul;
            else if (callee->name == "div") inst.op = SIROp::Div;
            else {
                inst.op = SIROp::Call;
                inst.label = callee->name;
            }
        } else {
            inst.op = SIROp::Call;
        }
        for (auto& arg : call->arguments) {
            inst.operands.push_back(lowerExpression(*arg, currentFunc, input));
        }
        inst.result = SIRValue::makeVReg(nextVReg(), expr.inferredType.describe());
        SIRValue res = *inst.result;
        currentFunc.blocks.back().instructions.push_back(std::move(inst));
        return res;
    }

    return SIRValue();
}

SIRValue Lowerer::lowerFork(const ForkExpr& fork, SIRValue input, SIRFunction& currentFunc) {
    std::string endLabel = nextLabel();
    SIRValue result = SIRValue::makeVReg(nextVReg(), fork.inferredType.describe());
    
    for (size_t i = 0; i < fork.arms.size(); ++i) {
        auto& arm = fork.arms[i];
        std::string nextArmLabel = (i + 1 < fork.arms.size()) ? nextLabel() : endLabel;
        std::string bodyLabel = nextLabel();

        if (dynamic_cast<WildcardExpr*>(arm.condition.get())) {
            SIRInstruction jmp;
            jmp.op = SIROp::Jump;
            jmp.label = bodyLabel;
            currentFunc.blocks.back().instructions.push_back(std::move(jmp));
        } else {
            // For now, simplify condition matching. 
            // In a real implementation, ?= 0 becomes eq input, 0
            SIRValue cond = lowerExpression(*arm.condition, currentFunc);
            SIRInstruction br;
            br.op = SIROp::Branch;
            br.operands = { cond };
            br.label = bodyLabel;
            currentFunc.blocks.back().instructions.push_back(std::move(br));
            
            SIRInstruction jmpNext;
            jmpNext.op = SIROp::Jump;
            jmpNext.label = nextArmLabel;
            currentFunc.blocks.back().instructions.push_back(std::move(jmpNext));
        }

        SIRBasicBlock bodyBlock;
        bodyBlock.name = bodyLabel;
        currentFunc.blocks.push_back(std::move(bodyBlock));
        
        SIRValue armVal = lowerExpression(*arm.body, currentFunc);
        SIRInstruction store;
        store.op = SIROp::Store;
        store.operands = { armVal };
        store.result = result;
        currentFunc.blocks.back().instructions.push_back(std::move(store));
        
        SIRInstruction jmpEnd;
        jmpEnd.op = SIROp::Jump;
        jmpEnd.label = endLabel;
        currentFunc.blocks.back().instructions.push_back(std::move(jmpEnd));

        if (nextArmLabel != endLabel) {
            SIRBasicBlock nextArmBlock;
            nextArmBlock.name = nextArmLabel;
            currentFunc.blocks.push_back(std::move(nextArmBlock));
        }
    }

    SIRBasicBlock end;
    end.name = endLabel;
    currentFunc.blocks.push_back(std::move(end));
    return result;
}

SIRValue Lowerer::lowerLoop(const LoopExpr& loop, SIRValue input, SIRFunction& currentFunc) {
    std::string condLabel = nextLabel();
    std::string bodyLabel = nextLabel();
    std::string endLabel = nextLabel();

    SIRInstruction jmpCond;
    jmpCond.op = SIROp::Jump;
    jmpCond.label = condLabel;
    currentFunc.blocks.back().instructions.push_back(std::move(jmpCond));

    SIRBasicBlock condBlock;
    condBlock.name = condLabel;
    currentFunc.blocks.push_back(std::move(condBlock));
    
    SIRValue cond = lowerExpression(*loop.condition, currentFunc);
    SIRInstruction br;
    br.op = SIROp::Branch;
    br.operands = { cond };
    br.label = bodyLabel;
    currentFunc.blocks.back().instructions.push_back(std::move(br));

    SIRInstruction jmpEnd;
    jmpEnd.op = SIROp::Jump;
    jmpEnd.label = endLabel;
    currentFunc.blocks.back().instructions.push_back(std::move(jmpEnd));

    SIRBasicBlock bodyBlock;
    bodyBlock.name = bodyLabel;
    currentFunc.blocks.push_back(std::move(bodyBlock));
    
    lowerExpression(*loop.body, currentFunc);
    
    SIRInstruction jmpBack;
    jmpBack.op = SIROp::Jump;
    jmpBack.label = condLabel;
    currentFunc.blocks.back().instructions.push_back(std::move(jmpBack));

    SIRBasicBlock end;
    end.name = endLabel;
    currentFunc.blocks.push_back(std::move(end));
    
    return SIRValue::makeInt(0);
}

} // namespace dagger
