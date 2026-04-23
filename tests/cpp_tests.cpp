#include "driver.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
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
    } catch (const std::exception& ex) {
        std::cerr << "cpp_tests failed: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "cpp tests passed\n";
    return EXIT_SUCCESS;
}
