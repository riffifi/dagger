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
    require(!result.ok, message);
    require(result.errorMessage.find(needle) != std::string::npos,
            message + " (expected error containing '" + needle + "', got '" + result.errorMessage + "')");
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

void testMultiInputGate() {
    const char* source = R"dag(
@fn add_scaled [~ a :: int, ~ b :: int, ~ factor :: int] => int [
  b -> mul(factor) -> add(a)
]

3, 4, 2 -> add_scaled -> out.writeln
)dag";
    auto result = dagger::runSource(source);
    require(result.ok, "multi-input gate should run");
    require(result.stdoutText == "11\n", "multi-input gate should print 11");
}

void testRouteCaptureAndAssignment() {
    const char* source = R"dag(
5 -> add(1) -> ~ result
result -> add(2) -> result
result -> out.writeln
)dag";
    auto result = dagger::runSource(source);
    require(result.ok, "route capture and assignment should run");
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
    require(result.ok, "recursive fork should run");
    require(result.stdoutText == "120\n", "recursive fork should print 120");
}

void testShapesAndFieldAccess() {
    const char* source = R"dag(
@type Point [
  x :: float
  y :: float
]

~ p :: Point = [ x = 1.5, y = 2.5 ]
p.x -> out.writeln
)dag";
    auto result = dagger::runSource(source);
    require(result.ok, "shape field access should run");
    require(result.stdoutText == "1.5\n", "shape field access should print 1.5");
}

void testNestedObjectAccessThroughGate() {
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
    require(result.ok, "nested object access through gate should run");
    require(result.stdoutText == "8\n", "nested object access through gate should print 8");
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
p -> twice -> block [~ value] [
  value -> out.writeln
]
)dag";
    auto result = dagger::runSource(source);
    require(result.ok, "modern aliases should run");
    require(result.stdoutText == "18\n", "modern aliases should print 18");
}

void testLoopExecution() {
    const char* source = R"dag(
~ i :: int = 0
~ total :: int = 0

loop [i -> lt(5)] block [
  total -> add(i) -> total
  i -> add(1) -> i
]

total -> out.writeln
)dag";
    auto result = dagger::runSource(source);
    require(result.ok, "loop should run");
    require(result.stdoutText == "10\n", "loop should print 10");
}

void testUnknownTypeFails() {
    auto result = dagger::runSource("~ someVar :: penis = 1\n");
    requireFailure(result, "unknown type: penis", "unknown declared type should fail");
}

void testPrimitiveTypeMismatchFails() {
    auto result = dagger::runSource("~ someVar :: int = 1.5\n");
    requireFailure(result, "type mismatch: expected int, got float", "primitive type mismatch should fail");
}

void testTypedReassignmentFails() {
    const char* source = R"dag(
~ x :: int = 1
2.5 -> x
)dag";
    auto result = dagger::runSource(source);
    requireFailure(result, "type mismatch: expected int, got float", "typed reassignment should fail");
}

void testGateParamAndReturnTypesFail() {
    auto paramResult = dagger::runSource(R"dag(
@fn takes_int [~ x :: int] => int [
  x
]

1.5 -> takes_int
)dag");
    requireFailure(paramResult, "type mismatch: expected int, got float", "typed gate parameter should fail");

    auto returnResult = dagger::runSource(R"dag(
@fn bad_return [~ x :: int] => int [
  1.5
]

1 -> bad_return
)dag");
    requireFailure(returnResult, "type mismatch: expected int, got float", "typed gate return should fail");
}

void testShapeValidationFails() {
    auto missingField = dagger::runSource(R"dag(
@type Point [
  x :: int
  y :: int
]

~ p :: Point = [ x = 1 ]
)dag");
    requireFailure(missingField, "missing field 'y' for Point", "shape missing field should fail");

    auto wrongFieldType = dagger::runSource(R"dag(
@type Point [
  x :: int
  y :: int
]

~ p :: Point = [ x = 1, y = "nope" ]
)dag");
    requireFailure(wrongFieldType, "type mismatch: expected int, got text", "shape field type mismatch should fail");
}

void testSemanticBuiltinChecksFail() {
    auto addResult = dagger::runSource("1 -> add(true)\n");
    requireFailure(addResult, "add expects numeric arguments", "add should reject non-numeric args");

    auto assertResult = dagger::runSource("1 -> assert\n");
    requireFailure(assertResult, "type mismatch: expected bool, got int", "assert should require bool");
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
    requireFailure(arityResult, "sum2 expects 2 arguments", "gate arity mismatch should fail");
}

void testUninitializedInferenceThroughAssignment() {
    auto result = dagger::runSource(R"dag(
~ value
1 -> value
value -> add(2) -> out.writeln
)dag");
    require(result.ok, "uninitialized value should pick up type on assignment");
    require(result.stdoutText == "3\n", "uninitialized value assignment should work");
}

void testTypedBlockInput() {
    auto ok = dagger::runSource(R"dag(
1 -> block [~ value :: int] [
  value -> add(2) -> out.writeln
]
)dag");
    require(ok.ok, "typed block input should accept matching values");
    require(ok.stdoutText == "3\n", "typed block input should run");

    auto bad = dagger::runSource(R"dag(
"nope" -> block [~ value :: int] [
  value -> out.writeln
]
)dag");
    requireFailure(bad, "type mismatch: expected int, got text", "typed block input should reject mismatched values");
}

void testPrimitiveAliases() {
    auto ok = dagger::runSource(R"dag(
~ width :: uint32 = 640
~ opacity :: float64 = 0.5
width -> add(10) -> out.writeln
opacity -> add(0.25) -> out.writeln
)dag");
    require(ok.ok, "primitive aliases should be accepted");
    require(ok.stdoutText == "650\n0.75\n", "primitive aliases should behave like numeric primitives");

    auto bad = dagger::runSource("~ flag :: uint16 = true\n");
    requireFailure(bad, "type mismatch: expected uint16, got bool", "integer aliases should reject non-integer values");
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

void testModuleLoading() {
    auto stdlibResult = dagger::runFile(fs::path("tests/programs/module_stdlib.dag"));
    require(stdlibResult.ok, "stdlib module import should run");
    require(stdlibResult.stdoutText == "100\n", "stdlib module import should print 100");

    auto localResult = dagger::runFile(fs::path("tests/programs/module_local.dag"));
    require(localResult.ok, "local module import should run");
    require(localResult.stdoutText == "21\n", "local module import should print 21");
}

void testUnsupportedSelectiveImportFails() {
    const fs::path tempDir = fs::temp_directory_path() / "dagger_selective_import_test";
    fs::create_directories(tempDir);
    const fs::path modulePath = tempDir / "math.dag";
    const fs::path mainPath = tempDir / "main.dag";

    {
        std::ofstream module(modulePath);
        module << "@fn double [~ n :: int] => int [\n  n -> add(n)\n]\n";
    }
    {
        std::ofstream main(mainPath);
        main << "@use math [ double ]\n1 -> double -> out.writeln\n";
    }

    auto result = dagger::runFile(mainPath);
    requireFailure(result, "selective imports are not implemented yet", "selective imports should fail explicitly");
}

} // namespace

int main() {
    try {
        testIntegerLiteralParsing();
        testMultiInputGate();
        testRouteCaptureAndAssignment();
        testRecursiveFork();
        testShapesAndFieldAccess();
        testNestedObjectAccessThroughGate();
        testModernAliases();
        testLoopExecution();
        testUnknownTypeFails();
        testPrimitiveTypeMismatchFails();
        testTypedReassignmentFails();
        testGateParamAndReturnTypesFail();
        testShapeValidationFails();
        testSemanticBuiltinChecksFail();
        testSemanticFieldAndArityChecksFail();
        testUninitializedInferenceThroughAssignment();
        testTypedBlockInput();
        testPrimitiveAliases();
        testStdlibNamespaceGuards();
        testInitializationAndDuplicateChecks();
        testModuleLoading();
        testUnsupportedSelectiveImportFails();
    } catch (const std::exception& ex) {
        std::cerr << "cpp_tests failed: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "cpp tests passed\n";
    return EXIT_SUCCESS;
}
