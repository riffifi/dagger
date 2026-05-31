#include "ast.h"
#include "sir.h"
#include "semantics.h"
#include <set>

namespace dagger {

class Lowerer {
public:
    explicit Lowerer(const Program& program);

    SIRProgram lower();

private:
    const Program& program_;
    SIRProgram sir_;
    
    int vregCount_ = 0;
    int labelCount_ = 0;

    std::string nextVReg();
    std::string nextLabel();

    std::map<std::string, bool> streams_; // name -> isBurst

    void lowerStatement(const Statement& stmt, SIRFunction& currentFunc);
    SIRValue lowerExpression(const Expression& expr, SIRFunction& currentFunc, std::optional<SIRValue> input = std::nullopt);
    SIRValue lowerFork(const ForkExpr& fork, SIRValue input, SIRFunction& currentFunc);
    SIRValue lowerLoop(const LoopExpr& loop, SIRValue input, SIRFunction& currentFunc);
    
    void lowerFunction(const FunctionDecl& decl);
};

} // namespace dagger
