#include "driver.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

namespace fs = std::filesystem;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void requireFailure(const dagger::RunResult& result, const std::string& needle, const std::string& message) {
    if (result.ok) {
        throw std::runtime_error(message + " (expected failure, but it succeeded)");
    }
    if (result.errorMessage.find(needle) == std::string::npos) {
        throw std::runtime_error(message + " (expected error containing '" + needle + "', got '" + result.errorMessage + "')");
    }
}

void testIntegerLiteralParsing() {
    auto hex = dagger::runSource("0xFF -> out.writeln\n");
    require(hex.ok, "hex literal should run");
    require(hex.stdoutText == "255\n", "hex literal should print 255");

    auto binary = dagger::runSource("0b1010 -> out.writeln\n");
    require(binary.ok, "binary literal should run");
    require(binary.stdoutText == "10\n", "binary literal should print 10");

    auto exponent = dagger::runSource("1.5e2 -> out.writeln\n");
    require(exponent.ok, "float exponent should run");
    require(exponent.stdoutText == "150\n", "float exponent should print 150");
}

void testMultiInputFunction() {
    const char* source = R"dag(
@fn add_scaled [~ a :: int, ~ b :: int, ~ factor :: int] => int [
  b -> mul(factor) -> add(a)
]

3, 4, 2 -> add_scaled -> out.writeln
)dag";
    auto result = dagger::runSource(source);
    require(result.ok, "multi-input function should run: " + result.errorMessage);
    require(result.stdoutText == "11\n", "multi-input function should print 11");
}

void testRouteCaptureAndAssignment() {
    const char* source = R"dag(
5 -> add(1) -> ~ result
result -> add(2) -> result
result -> out.writeln
)dag";
    auto result = dagger::runSource(source);
    require(result.ok, "route capture and assignment should run: " + result.errorMessage);
    require(result.stdoutText == "8\n", "route capture and assignment should print 8");
}

void testRecursiveFork() {
    const char* source = R"dag(
@fn factorial [~ n :: int] => int [
  n -> fork [
    ?<= 1 -> 1
    _ -> n -> sub(1) -> factorial -> mul(n)
  ]
]

5 -> factorial -> out.writeln
)dag";
    auto result = dagger::runSource(source);
    require(result.ok, "recursive fork should run: " + result.errorMessage);
    require(result.stdoutText == "120\n", "recursive fork should print 120");
}

void testTypesAndFieldAccess() {
    const char* source = R"dag(
@type Point [
  x :: float
  y :: float
]

~ p :: Point = [ x = 1.5, y = 2.5 ]
p.x -> out.writeln
)dag";
    auto result = dagger::runSource(source);
    require(result.ok, "type field access should run: " + result.errorMessage);
    require(result.stdoutText == "1.5\n", "type field access should print 1.5");
}

void testNestedObjectAccessThroughFunction() {
    const char* source = R"dag(
@type Point [
  x :: int
  y :: int
]

@type Rect [
  origin :: Point
]

@fn left [~ rect :: Rect] => int [
  rect.origin.x
]

~ rect :: Rect = [ origin = [ x = 8, y = 13 ] ]
rect -> left -> out.writeln
)dag";
    auto result = dagger::runSource(source);
    require(result.ok, "nested object access through function should run: " + result.errorMessage);
    require(result.stdoutText == "8\n", "nested object access through function should print 8");
}

void testModernAliases() {
    const char* source = R"dag(
@type Point [
  x :: int
  y :: int
]

@fn twice [~ p :: Point] => int [
  p.x -> mul(2)
]

~ p :: Point = [ x = 9, y = 1 ]
p -> twice -> [~ value] [
  value -> out.writeln
]
)dag";
    auto result = dagger::runSource(source);
    require(result.ok, "modern aliases should run: " + result.errorMessage);
    require(result.stdoutText == "18\n", "modern aliases should print 18");
}

void testLoopExecution() {
    const char* source = R"dag(
~ i :: int = 0
~ total :: int = 0

loop [i -> lt(5)] [
  total -> add(i) -> total
  i -> add(1) -> i
]

total -> out.writeln
)dag";
    auto result = dagger::runSource(source);
    require(result.ok, "loop should run: " + result.errorMessage);
    require(result.stdoutText == "10\n", "loop should print 10");
}

void testUnknownTypeFails() {
    auto result = dagger::runSource("~ someVar :: unknown_t = 1\n");
    requireFailure(result, "unknown type: unknown_t", "unknown declared type should fail");
}

void testPrimitiveTypeMismatchFails() {
    auto result = dagger::runSource("~ someVar :: int = 1.5\n");
    requireFailure(result, "type mismatch", "primitive type mismatch should fail");
}

void testTypedReassignmentFails() {
    const char* source = R"dag(
~ x :: int = 1
2.5 -> x
)dag";
    auto result = dagger::runSource(source);
    requireFailure(result, "type mismatch", "typed reassignment should fail");
}

void testFunctionParamAndReturnTypesFail() {
    auto paramResult = dagger::runSource(R"dag(
@fn takes_int [~ x :: int] => int [
  x
]

1.5 -> takes_int
)dag");
    requireFailure(paramResult, "type mismatch", "typed function parameter should fail");

    auto returnResult = dagger::runSource(R"dag(
@fn bad_return [~ x :: int] => int [
  1.5
]

1 -> bad_return
)dag");
    requireFailure(returnResult, "type mismatch", "typed function return should fail");
}

void testTypeValidationFails() {
    auto missingField = dagger::runSource(R"dag(
@type Point [
  x :: int
  y :: int
]

~ p :: Point = [ x = 1 ]
)dag");
    requireFailure(missingField, "missing field 'y' for Point", "type missing field should fail");

    auto wrongFieldType = dagger::runSource(R"dag(
@type Point [
  x :: int
  y :: int
]

~ p :: Point = [ x = 1, y = "nope" ]
)dag");
    requireFailure(wrongFieldType, "type mismatch", "type field type mismatch should fail");
}

void testSemanticBuiltinChecksFail() {
    auto addResult = dagger::runSource("1 -> add(true)\n");
    requireFailure(addResult, "add expects numeric arguments", "add should reject non-numeric args");

    auto assertResult = dagger::runSource("1 -> assert\n");
    requireFailure(assertResult, "type mismatch", "assert should require bool");
}

void testSemanticFieldAndArityChecksFail() {
    auto fieldResult = dagger::runSource(R"dag(
@type Point [
  x :: int
]

~ p :: Point = [ x = 1 ]
p.y
)dag");
    requireFailure(fieldResult, "unknown field: y", "field access should reject unknown fields");

    auto arityResult = dagger::runSource(R"dag(
@fn sum2 [~ a :: int, ~ b :: int] => int [
  a -> add(b)
]

1 -> sum2
)dag");
    requireFailure(arityResult, "sum2 expects 2 arguments", "function arity mismatch should fail");
}

void testUninitializedInferenceThroughAssignment() {
    auto result = dagger::runSource(R"dag(
~ value
1 -> value
value -> add(2) -> out.writeln
)dag");
    require(result.ok, "uninitialized value should pick up type on assignment: " + result.errorMessage);
    require(result.stdoutText == "3\n", "uninitialized value assignment should work");
}

void testTypedBlockInput() {
    auto ok = dagger::runSource(R"dag(
1 -> [~ value :: int] [
  value -> add(2) -> out.writeln
]
)dag");
    require(ok.ok, "typed block input should accept matching values: " + ok.errorMessage);
    require(ok.stdoutText == "3\n", "typed block input should run");

    auto bad = dagger::runSource(R"dag(
"nope" -> [~ value :: int] [
  value -> out.writeln
]
)dag");
    requireFailure(bad, "type mismatch", "typed block input should reject mismatched values");
}

void testPrimitiveAliases() {
    auto ok = dagger::runSource(R"dag(
~ width :: uint32 = 640
~ opacity :: float32 = 0.5
width -> add(10) -> out.writeln
opacity -> add(0.25) -> out.writeln
)dag");
    require(ok.ok, "primitive aliases should be accepted: " + ok.errorMessage);
    require(ok.stdoutText == "650\n0.75\n", "primitive aliases should behave like numeric primitives");

    auto bad = dagger::runSource("~ flag :: uint16 = true\n");
    requireFailure(bad, "type mismatch", "integer aliases should reject non-integer values");
}

void testStdlibNamespaceGuards() {
    auto namespaceValue = dagger::runSource("out\n");
    requireFailure(namespaceValue, "namespace 'out' is not a value", "stdlib namespace root should not be a value");

    auto unknownStdlib = dagger::runSource("1 -> out.nope\n");
    requireFailure(unknownStdlib, "unknown stdlib symbol: out.nope", "unknown stdlib symbol should fail cleanly");

    auto reservedVar = dagger::runSource("~ out = 1\n");
    requireFailure(reservedVar, "reserved stdlib namespace: out", "reserved stdlib root should not be redeclared as a variable");
}

void testInitializationAndDuplicateChecks() {
    auto uninitialized = dagger::runSource(R"dag(
~ value :: int
value -> add(1)
)dag");
    requireFailure(uninitialized, "use of uninitialized variable: value", "use-before-init should fail");

    auto duplicate = dagger::runSource(R"dag(
~ value = 1
~ value = 2
)dag");
    requireFailure(duplicate, "duplicate variable: value", "duplicate variable declaration should fail");
}

void testBurstTracking() {
    auto result = dagger::runSource(R"dag(
~ x = 10
!x -> out.writeln
?x -> out.writeln
)dag");
    requireFailure(result, "use of burst variable: x", "use-after-burst should fail");

    auto result2 = dagger::runSource(R"dag(
~ x = 10
!x -> out.writeln
x -> add(1) -> x
)dag");
    requireFailure(result2, "use of burst variable: x", "reassigning to burst variable should fail");
}

} // namespace

int main() {
    try {
        testIntegerLiteralParsing();
        testMultiInputFunction();
        testRouteCaptureAndAssignment();
        testRecursiveFork();
        testTypesAndFieldAccess();
        testNestedObjectAccessThroughFunction();
        testModernAliases();
        testLoopExecution();
        testUnknownTypeFails();
        testPrimitiveTypeMismatchFails();
        testTypedReassignmentFails();
        testFunctionParamAndReturnTypesFail();
        testTypeValidationFails();
        testSemanticBuiltinChecksFail();
        testSemanticFieldAndArityChecksFail();
        testUninitializedInferenceThroughAssignment();
        testTypedBlockInput();
        testPrimitiveAliases();
        testStdlibNamespaceGuards();
        testInitializationAndDuplicateChecks();
        testBurstTracking();
    } catch (const std::exception& ex) {
        std::cerr << "cpp_tests failed: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "cpp tests passed\n";
    return EXIT_SUCCESS;
}
