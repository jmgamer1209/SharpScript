#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "sharpscript/bytecode.hpp"
#include "sharpscript/compiler.hpp"
#include "sharpscript/context.hpp"
#include "sharpscript/disassembler.hpp"
#include "sharpscript/engine.hpp"
#include "sharpscript/unicode.hpp"

using namespace sharpscript;

namespace {

int g_passed = 0;
int g_failed = 0;
std::vector<std::string> g_failures;

struct Counter {
    int value = 0;
    explicit Counter(int start) : value(start) {}
    void add(int amount) { value += amount; }
    int get() const { return value; }
    std::string describe() const { return "counter=" + std::to_string(value); }
};

struct TestEngine {
    std::vector<std::string> output;
    std::unique_ptr<ScriptEngine> engine;

    TestEngine() {
        engine = std::make_unique<ScriptEngine>();
        engine->settings.output = [this](const std::string& line) { output.push_back(line); };
        registerMath();
        registerHost();
        registerCounter();
    }

    std::shared_ptr<ScriptModule> load(const std::string& source, const std::string& moduleName = "test") {
        return engine->loadModule(moduleName, source, moduleName + ".cs");
    }

    void run(const std::string& source, const std::string& moduleName = "test") {
        load(source, moduleName);
        engine->invokeEntryPoint(moduleName);
    }

private:
    void registerMath() {
        auto& math = engine->createNativeType("Math", true);
        engine->addMethod(math, "Abs", primI32(), {primI32()},
                          [](ExecutionContext&, const Value&, const std::vector<Value>& args) {
                              return Value::i32(std::abs(args[0].asI32()));
                          },
                          true);
        engine->addMethod(math, "Max", primI32(), {primI32(), primI32()},
                          [](ExecutionContext&, const Value&, const std::vector<Value>& args) {
                              return Value::i32(std::max(args[0].asI32(), args[1].asI32()));
                          },
                          true);
        engine->addMethod(math, "Sqrt", primF64(), {primF64()},
                          [](ExecutionContext&, const Value&, const std::vector<Value>& args) {
                              return Value::f64(std::sqrt(args[0].asF64()));
                          },
                          true);
    }

    void registerHost() {
        auto& host = engine->createNativeType("Host", true);
        engine->addMethod(host, "Emit", primVoid(), {primString()},
                          [this](ExecutionContext&, const Value&, const std::vector<Value>& args) {
                              output.push_back("host:" + utf16ToUtf8(args[0].asString()));
                              return Value::voidValue();
                          },
                          true);
        engine->addMethod(host, "Ping", primI32(), {primI32()},
                          [](ExecutionContext&, const Value&, const std::vector<Value>& args) {
                              return Value::i32(args[0].asI32() + 1);
                          },
                          true);
        engine->addMethod(
            host, "Throw", primVoid(), {primString()},
            [](ExecutionContext& context, const Value&, const std::vector<Value>& args) {
                context.throwException(context.engine().createException(
                    "ArgumentException", utf16ToUtf8(args[0].asString())));
                return Value::voidValue();
            },
            true);
    }

    void registerCounter() {
        auto& type = engine->createNativeType("ReflectedCounter", false);
        engine->addConstructor(type, {primI32()},
                               [](ExecutionContext&, const Value&, const std::vector<Value>& args) {
                                   return Value::native(std::make_shared<Counter>(args[0].asI32()));
                               });
        engine->addMethod(type, "Add", primVoid(), {primI32()},
                          [](ExecutionContext&, const Value& self, const std::vector<Value>& args) {
                              self.asNative<Counter>()->add(args[0].asI32());
                              return Value::voidValue();
                          });
        engine->addMethod(type, "Get", primI32(), {},
                          [](ExecutionContext&, const Value& self, const std::vector<Value>&) {
                              return Value::i32(self.asNative<Counter>()->get());
                          });
        engine->addMethod(type, "Describe", primString(), {},
                          [](ExecutionContext&, const Value& self, const std::vector<Value>&) {
                              return Value::stringUtf8(self.asNative<Counter>()->describe());
                          });
        engine->addMethod(type, "Twice", primI32(), {primI32()},
                          [](ExecutionContext&, const Value&, const std::vector<Value>& args) {
                              return Value::i32(args[0].asI32() * 2);
                          },
                          true);
        engine->addProperty(
            type, "Value", primI32(),
            [](const Value& self) { return Value::i32(self.asNative<Counter>()->value); },
            [](const Value& self, const Value& value) { self.asNative<Counter>()->value = value.asI32(); },
            false);
    }
};

void test(const std::string& name, const std::function<void()>& body) {
    try {
        body();
        ++g_passed;
        std::cout << "  ok   " << name << std::endl;
    } catch (const std::exception& ex) {
        ++g_failed;
        g_failures.push_back(name + ": " + ex.what());
        std::cout << "  FAIL " << name << ": " << ex.what() << std::endl;
    }
}

void expectTrue(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename T>
void expectEqual(const T& expected, const T& actual, const std::string& context = "value") {
    if (!(expected == actual)) {
        std::ostringstream out;
        out << context << ": expected '" << expected << "' but got '" << actual << "'";
        throw std::runtime_error(out.str());
    }
}

void expectOutput(const std::string& source, const std::vector<std::string>& expected) {
    TestEngine harness;
    harness.run(source);
    expectEqual(expected.size(), harness.output.size(), "line count");
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (expected[i] != harness.output[i]) {
            std::string actual;
            for (const auto& line : harness.output) actual += "[" + line + "]";
            throw std::runtime_error("line " + std::to_string(i + 1) + ": expected '" + expected[i] +
                                     "' but got '" + harness.output[i] + "' (all: " + actual + ")");
        }
    }
}

void expectContains(const std::string& text, const std::string& needle) {
    if (text.find(needle) == std::string::npos) {
        throw std::runtime_error("'" + needle + "' not found in:\n" + text);
    }
}

ScriptRuntimeException expectRuntimeError(const std::string& source) {
    TestEngine harness;
    harness.load(source);
    try {
        harness.engine->invokeEntryPoint("test");
    } catch (const ScriptRuntimeException& ex) {
        return ex;
    }
    throw std::runtime_error("expected a ScriptRuntimeException");
}

void testArithmetic() {
    test("arithmetic and precedence", [] {
        expectOutput(R"(
void main() {
    print(1 + 2 * 3);
    print((1 + 2) * 3);
    print(7 / 2);
    print(-7 / 2);
    print(7 % 3);
    print(1 << 5);
    print(255 >> 4);
    print(5 & 3);
    print(5 | 3);
    print(5 ^ 3);
    print(~5);
})",
                     {"7", "9", "3", "-3", "1", "32", "15", "1", "7", "6", "-6"});
    });

    test("int64 and floating point", [] {
        expectOutput(R"(
void main() {
    long big = 1L << 40;
    print(big);
    double d = 1.5 + 2;
    print(d);
    float f = 0.5f * 4;
    print(f);
    print(10.0 / 4.0);
    print(-2.5 % 2.0);
})",
                     {"1099511627776", "3.5", "2", "2.5", "-0.5"});
    });

    test("comparison operators", [] {
        expectOutput(R"(
void main() {
    print(3 < 4);
    print(3 <= 3);
    print(4 > 5);
    print(4 >= 4);
    print(3 == 3);
    print(3 != 3);
    print(1 + 1 == 2 && 2 < 3);
    print(false || true);
    print(string.IsNullOrEmpty(""));
})",
                     {"true", "true", "false", "true", "true", "false", "true", "true", "true"});
    });
}

void testConversions() {
    test("explicit and implicit numeric conversions", [] {
        expectOutput(R"(
void main() {
    int i = (int)3.9;
    print(i);
    double d = 7;
    print(d);
    long l = 3000000000;
    print(l);
    byte b = (byte)300;
    print(b);
    char c = 'A';
    print((int)c);
    print("v=" + 1.5);
    int h = 'z';
    print(h);
})",
                     {"3", "7", "3000000000", "44", "65", "v=1.5", "122"});
    });

    test("ternary and null", [] {
        expectOutput(R"(
void main() {
    int a = 5;
    int b = a > 3 ? 100 : 200;
    print(b);
    string s = null;
    print(s == null);
    print(s != null);
    string t = s == null ? "default" : s;
    print(t);
})",
                     {"100", "true", "false", "default"});
    });
}

void testStrings() {
    test("string members and concatenation", [] {
        expectOutput(R"(
void main() {
    string s = "Hello";
    print(s.Length);
    print(s.ToUpper());
    print(s.Substring(1, 3));
    print(s.IndexOf("l"));
    print(s.Contains("ell"));
    print(s + " world");
    print("x" + 1 + 2);
    print(1 + 2 + "x");
    print("a" + true);
    string[] parts = "a,b,c".Split(",");
    print(parts.Length);
    print(parts[1]);
})",
                     {"5", "HELLO", "ell", "2", "true", "Hello world", "x12", "3x", "aTrue", "3", "b"});
    });

    test("string equality", [] {
        expectOutput(R"(
void main() {
    string a = "abc";
    string b = "ab" + "c";
    print(a == b);
    print(a != b);
    switch (b) {
        case "abc": print("match"); break;
        default: print("no"); break;
    }
})",
                     {"true", "false", "match"});
    });
}

void testCSharpStringSemantics() {
    test("UTF-16 length and indexing", [] {
        expectOutput(R"(
void main() {
    string s = "你好A😀";
    print(s.Length);
    print(s.Substring(0, 2));
    print(s.IndexOf("A"));
    print(s[0]);
    print("A😀B".Length);
    print("A😀B".IndexOf("B"));
    print("\u4F60\u597D");
    char c = '你';
    print((int)c);
    print(c);
})",
                     {"5", "你好", "2", "你", "4", "3", "你好", "20320", "你"});
    });

    test("string methods reject camelCase spellings", [] {
        auto harness = std::make_unique<TestEngine>();
        std::vector<Diagnostic> diagnostics;
        auto module = harness->engine->tryLoadModule(
            "bad", "void main() { string s = \"abc\"; print(s.toUpper()); }", "bad.cs", diagnostics);
        expectTrue(module == nullptr, "camelCase must not resolve");
        bool found = false;
        for (const auto& diagnostic : diagnostics) {
            if (diagnostic.code == "E0340") found = true;
        }
        expectTrue(found, "expected E0340 for 'toUpper'");
    });

    test("C# PascalCase string methods", [] {
        expectOutput(R"(
void main() {
    string s = "  Hello World  ";
    print(s.Trim());
    print(s.Trim().ToUpper());
    print(s.Contains("World"));
    print(s.StartsWith("  He"));
    print(s.EndsWith("  "));
    print("a,b,c".Split(",").Length);
    print("abcabc".Replace("a", "X"));
    print(s.Trim().LastIndexOf("o"));
    print(string.IsNullOrEmpty(""));
    print("x".ToString());
})",
                     {"Hello World", "HELLO WORLD", "true", "true", "true", "3", "XbcXbc", "7", "true",
                      "x"});
    });

    test("null string semantics", [] {
        expectOutput(R"(
void main() {
    string s = null;
    print(s == null);
    print(s == "");
    print("[" + s + "]");
    print("x" + s);
    print(string.IsNullOrEmpty(s));
    print(string.IsNullOrEmpty(""));
    print(string.IsNullOrEmpty("a"));
    print(string.Empty.Length);
})",
                     {"true", "false", "[]", "x", "true", "true", "false", "0"});
    });

    test("null string member access throws", [] {
        auto ex = expectRuntimeError(R"(
void main() { string s = null; print(s.Length); }
)");
        expectContains(ex.what(), "null reference");

        auto ex2 = expectRuntimeError(R"(
void main() { string s = null; print(s.ToUpper()); }
)");
        expectContains(ex2.what(), "null reference");
    });

    test("substring bounds throw", [] {
        auto ex = expectRuntimeError(R"(
void main() { string s = "abc"; print(s.Substring(5)); }
)");
        expectContains(ex.what(), "ArgumentOutOfRangeException");
    });

    test("non-BMP char literal is rejected", [] {
        TestEngine harness;
        std::vector<Diagnostic> diagnostics;
        auto module = harness.engine->tryLoadModule("bad", "void main() { char c = '😀'; }", "bad.cs",
                                                    diagnostics);
        expectTrue(module == nullptr, "expected compilation failure");
        bool found = false;
        for (const auto& diagnostic : diagnostics) {
            if (diagnostic.code == "E0014") found = true;
        }
        expectTrue(found, "expected E0014 for a non-BMP character literal");
    });
}

void testControlFlow() {
    test("loops with break and continue", [] {
        expectOutput(R"(
void main() {
    int sum = 0;
    for (int i = 0; i < 10; i++) {
        if (i % 2 == 0) { continue; }
        if (i > 7) { break; }
        sum += i;
    }
    print(sum);
    int n = 0;
    while (n < 3) { n++; }
    print(n);
    do { n--; } while (n > 0);
    print(n);
})",
                     {"16", "3", "0"});
    });

    test("switch on int and string", [] {
        expectOutput(R"(
void main() {
    for (int i = 0; i < 4; i++) {
        switch (i) {
            case 0: print("zero"); break;
            case 1: print("one"); break;
            default: print("many"); break;
        }
    }
    string key = "b";
    switch (key) {
        case "a": print("A"); break;
        case "b": print("B"); break;
    }
})",
                     {"zero", "one", "many", "many", "B"});
    });

    test("nested loops and postfix increments", [] {
        expectOutput(R"(
void main() {
    int total = 0;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            total += i * j;
        }
    }
    print(total);
    int x = 5;
    print(x++);
    print(x);
    print(++x);
    print(x--);
    print(--x);
})",
                     {"9", "5", "6", "7", "7", "5"});
    });
}

void testFunctions() {
    test("recursion and calls", [] {
        expectOutput(R"(
int fib(int n) {
    if (n < 2) { return n; }
    return fib(n - 1) + fib(n - 2);
}
int add(int a, int b) { return a + b; }
void main() {
    print(fib(10));
    print(add(2, 3));
    print(add(fib(5), 1));
})",
                     {"55", "5", "6"});
    });

    test("void function call as statement", [] {
        expectOutput(R"(
void ping() { print("ping"); }
void main() { ping(); ping(); }
)",
                     {"ping", "ping"});
    });
}

void testGlobals() {
    test("global initialization order and mutation", [] {
        expectOutput(R"(
int counter = 10;
string label = "c=";
const int limit = 5;
int doubled = counter * 2;
void bump() { counter += 1; }
void main() {
    bump();
    print(label + counter);
    print(doubled);
    print(limit);
})",
                     {"c=11", "20", "5"});
    });

    test("global declared after use in initializer", [] {
        expectOutput(R"(
int a = b + 1;
int b = 4;
void main() { print(a); print(b); }
)",
                     {"1", "4"});
    });
}

void testClasses() {
    test("fields, constructor, methods", [] {
        expectOutput(R"(
class Point {
    public int x;
    public int y;
    public Point(int x, int y) {
        this.x = x;
        this.y = y;
    }
    public int sum() { return x + y; }
}
void main() {
    Point p = new Point(2, 3);
    print(p.x);
    print(p.sum());
    p.x = 10;
    print(p.sum());
})",
                     {"2", "5", "13"});
    });

    test("inheritance and virtual dispatch", [] {
        expectOutput(R"(
class Base {
    public string name = "base";
    public virtual int value() { return 1; }
    public string describe() { return name + ":" + value(); }
}
class Derived : Base {
    public Derived() { name = "derived"; }
    public override int value() { return 2; }
}
void main() {
    Base b = new Base();
    Base d = new Derived();
    print(b.describe());
    print(d.describe());
    print(d is Derived);
    print(b is Derived);
    Derived cast = d as Derived;
    print(cast != null);
    Derived invalid = b as Derived;
    print(invalid == null);
})",
                     {"base:1", "derived:2", "true", "false", "true", "true"});
    });

    test("static fields and methods", [] {
        expectOutput(R"(
class Counter {
    public static int created = 0;
    public int value = 0;
    public Counter() { created += 1; }
    public void inc() { value += 1; }
    public static int total() { return created; }
}
void main() {
    Counter a = new Counter();
    Counter b = new Counter();
    a.inc();
    a.inc();
    print(a.value);
    print(Counter.created);
    print(Counter.total());
    Counter.created = 10;
    print(Counter.total());
})",
                     {"2", "2", "2", "10"});
    });

    test("field initializers run per instance", [] {
        expectOutput(R"(
class Item {
    public int id = 7;
    public string tag = "x";
    public Item() { }
}
void main() {
    Item a = new Item();
    Item b = new Item();
    a.id = 9;
    print(a.id);
    print(b.id);
    print(a.tag + b.tag);
})",
                     {"9", "7", "xx"});
    });
}

void testArrays() {
    test("array creation, indexing and growth", [] {
        expectOutput(R"(
void main() {
    int[] a = new int[3];
    a[0] = 10;
    a[1] = 20;
    a[2] = 30;
    print(a.Length);
    print(a[1]);
    print(a[2]);

    Array<int> growable = new Array<int>();
    growable.Add(40);
    growable.Add(50);
    print(growable.Count);
    print(growable.Get(1));
})",
                     {"3", "20", "30", "2", "50"});
    });

    test("array literals and foreach", [] {
        expectOutput(R"(
void main() {
    int[] values = new int[] { 2, 4, 6 };
    int sum = 0;
    foreach (var v in values) { sum += v; }
    print(sum);
    string[] names = new string[] { "a", "b" };
    foreach (string name in names) { print(name); }
})",
                     {"12", "a", "b"});
    });

    test("array of objects", [] {
        expectOutput(R"(
class Box {
    public int v;
    public Box(int v) { this.v = v; }
}
void main() {
    Box[] boxes = new Box[2];
    boxes[0] = new Box(1);
    boxes[1] = new Box(2);
    print(boxes[0].v + boxes[1].v);
    foreach (var box in boxes) { print(box.v); }
})",
                     {"3", "1", "2"});
    });

    test("compound assignment to elements and fields", [] {
        expectOutput(R"(
class Counter { public int n = 0; }
void main() {
    int[] a = new int[] { 1, 2, 3 };
    a[0] += 10;
    a[1]++;
    a[2] *= 3;
    print(a[0], a[1], a[2]);
    Counter c = new Counter();
    c.n += 5;
    c.n++;
    print(c.n);
    string s = "x";
    string[] names = new string[] { "a" };
    names[0] += "b";
    print(names[0] + s);
})",
                     {"11 3 9", "6", "abx"});
    });

    test("string characters and foreach over string", [] {
        expectOutput(R"(
void main() {
    string s = "abc";
    print(s[1]);
    int count = 0;
    foreach (var ch in s) { count += 1; }
    print(count);
})",
                     {"b", "3"});
    });
}

void testNativeBindings() {
    test("built-in print and Math", [] {
        expectOutput(R"(
void main() {
    print(Math.Abs(-5));
    print(Math.Max(3, 8));
    print(Math.Sqrt(9.0));
    assert(1 == 1, "must hold");
})",
                     {"5", "8", "3"});
    });

    test("host singleton methods", [] {
        expectOutput(R"(
void main() {
    Host.Emit("hello");
    print(Host.Ping(4));
})",
                     {"host:hello", "5"});
    });

    test("native type construction and members", [] {
        expectOutput(R"(
void main() {
    ReflectedCounter c = new ReflectedCounter(5);
    c.Add(2);
    print(c.Get());
    print(c.Value);
    c.Value = 9;
    print(c.Get());
    print(ReflectedCounter.Twice(4));
    print(c.Describe());
})",
                     {"7", "7", "9", "8", "counter=9"});
    });
}

void testNamespaces() {
    test("namespaced functions, globals and classes", [] {
        expectOutput(R"(
namespace Game.Core {
    int score = 10;
    int Add(int a, int b) { return a + b; }
    class Player {
        public string name = "p";
        public string Label() { return "player:" + name; }
    }
    void Bump() { score += 1; }
}
void main() {
    Game.Core.Bump();
    print(Game.Core.score);
    print(Game.Core.Add(2, 3));
    Game.Core.Player p = new Game.Core.Player();
    print(p.Label());
    Game.Core.Player[] players = new Game.Core.Player[2];
    players[0] = new Game.Core.Player();
    print(players.Length);
})",
                     {"11", "5", "player:p", "2"});
    });

    test("using directives at global and namespace scope", [] {
        expectOutput(R"(
namespace Game.AI {
    int speed = 3;
    int Next() { return speed + 1; }
}
namespace Game {
    int health = 7;
}
namespace Other {
    using Game.AI;
    int bonus = speed + 100;
}
using Game.AI;
using Game;
void main() {
    print(speed);
    print(Next());
    print(health);
    print(Other.bonus);
    Game.AI.speed = 5;
    print(Game.AI.speed);
})",
                     {"3", "4", "7", "103", "5"});
    });

    test("nested namespaces resolve relative names", [] {
        expectOutput(R"(
namespace Game {
    int top = 1;
    namespace AI {
        int speed = 2;
        int Sum() { return top + speed; }
        void Bump() { speed += 1; top += 1; }
        namespace Deep {
            int DeepValue() { return speed * 10; }
        }
    }
}
void main() {
    Game.AI.Bump();
    print(Game.AI.speed);
    print(Game.top);
    print(Game.AI.Sum());
    print(Game.AI.Deep.DeepValue());
})",
                     {"3", "2", "5", "30"});
    });

    test("namespaced static members and inheritance", [] {
        expectOutput(R"(
namespace Shapes {
    class Shape {
        public static int Created = 0;
        public Shape() { Shape.Created += 1; }
        public virtual double Area() { return 0; }
    }
    class Circle : Shape {
        public double r;
        public Circle(double r) { this.r = r; Shape.Created += 1; }
        public override double Area() { return 3 * r * r; }
    }
    class Factory {
        public static Shape Make() { return new Circle(2); }
    }
}
void main() {
    Shapes.Shape s = Shapes.Factory.Make();
    print(s.Area());
    print(Shapes.Shape.Created);
    print(s is Shapes.Circle);
})",
                     {"12", "2", "true"});
    });

    test("same class name in different namespaces", [] {
        expectOutput(R"(
namespace Left { class Box { public int v = 1; } }
namespace Right { class Box { public int v = 2; } }
void main() {
    Left.Box a = new Left.Box();
    Right.Box b = new Right.Box();
    print(a.v + b.v);
})",
                     {"3"});
    });

    test("namespaced static field initializers resolve in scope", [] {
        expectOutput(R"(
namespace A {
    class Helper { public static int Make() { return 7; } }
    class C { public static int Value = Helper.Make(); }
}
void main() { print(A.C.Value); print(A.Helper.Make()); }
)",
                     {"7", "7"});
    });

    test("file-scoped namespace", [] {
        TestEngine harness;
        harness.engine->settings.autoGarbageCollect = false;
        harness.load(R"(
namespace Game.Util;

int value = 4;
int Twice(int x) { return x * 2; }
int Sum() { return value + Twice(3); }
class Box { public int v = 1; }
int Make() { Box box = new Box(); return box.v; }
)");
        expectEqual(6, harness.engine->invoke("test", "Game.Util.Twice", {Value::i32(3)}).asI32());
        expectEqual(10, harness.engine->invoke("test", "Game.Util.Sum").asI32());
        expectEqual(1, harness.engine->invoke("test", "Game.Util.Make").asI32());

        auto module = harness.engine->getModule("test");
        expectTrue(module->globalsByName.count("Game.Util.value") == 1, "qualified global name");
        expectTrue(module->findClass("Game.Util.Box") != nullptr, "qualified class name");
        expectTrue(module->findFunction("Game.Util.Twice", 1) != nullptr, "qualified function name");
    });

    test("namespaced globals survive hot reload", [] {
        TestEngine harness;
        harness.run(R"(
namespace A { int counter = 1; int other = 1; }
void main() { A.counter = 5; }
)");
        auto result = harness.engine->reloadModule(
            "test", "namespace A { int counter = 2; int other = 2; } void main() {}",
            "test.cs");
        expectTrue(result.success, "reload should succeed");

        bool migratedCounter = false;
        for (const auto& name : result.migratedGlobals) {
            if (name == "A.counter") migratedCounter = true;
        }
        expectTrue(migratedCounter, "A.counter should be migrated by its qualified name");

        auto module = harness.engine->getModule("test");
        auto counter = module->globalsByName.find("A.counter");
        auto other = module->globalsByName.find("A.other");
        expectTrue(counter != module->globalsByName.end(), "A.counter exists");
        expectTrue(other != module->globalsByName.end(), "A.other exists");
        expectEqual(5, module->globals[counter->second].value.asI32());
        expectEqual(2, module->globals[other->second].value.asI32());
    });
}

void testStructValueTypes() {
    test("struct copy semantics", [] {
        expectOutput(R"(
struct Point {
    public int x;
    public int y;
    public Point(int x, int y) { this.x = x; this.y = y; }
    public void Shift(int d) { x += d; y += d; }
    public int Sum() { return x + y; }
}
Point global = new Point(1, 2);
Point Make(int x, int y) { return new Point(x, y); }
void main() {
    Point a = new Point(1, 2);
    Point b = a;
    b.x = 100;
    print(a.x);
    print(b.x);

    Point[] arr = new Point[2];
    arr[0] = a;
    arr[0].x = 9;
    print(a.x);
    print(arr[0].x);

    Point c = arr[0];
    c.x = 50;
    print(arr[0].x);

    a.Shift(5);
    print(a.x);

    arr[1] = Make(3, 4);
    print(arr[1].Sum());

    print(global.Sum());
    Point d = global;
    d.Shift(1);
    print(global.x);
    print(d.x);

    foreach (Point p in arr) { p.x = 0; }
    print(arr[0].x);

    Point e;
    print(e.x);
})",
                     {"1", "100", "1", "9", "9", "6", "7", "3", "1", "2", "9", "0"});
    });

    test("struct arrays, var copies and reference fields", [] {
        expectOutput(R"(
class C { public int v; }
struct S {
    public int value;
    public C link;
    public S(int v) { this.value = v; }
}
void main() {
    C c = new C();
    S a = new S(1);
    a.link = c;
    S b = a;
    b.link.v = 5;
    print(c.v);
    print(b.value);

    S[] list = new S[2];
    print(list[0].value);
    list[0] = a;
    list[0].value = 10;
    print(a.value);
    print(list[0].value);

    Array<S> mutating = new Array<S>();
    mutating.Add(list[0]);
    S popped = mutating.Get(0);
    popped.value = 77;
    print(popped.value);

    S probe = new S(10);
    probe.link = c;
    Array<S> boxed = new Array<S>();
    boxed.Add(list[0]);
    print(boxed.Contains(probe));

    var copy = a;
    copy.value = 42;
    print(a.value);
    print(copy.value);
})",
                     {"5", "1", "0", "1", "10", "77", "true", "1", "42"});
    });

    test("struct static fields work before the struct is declared", [] {
        expectOutput(R"(
class Holder {
    public static Pair pair;
    public static Pair other = new Pair(1, 2);
}
struct Pair {
    public int a;
    public int b;
    public Pair(int a, int b) { this.a = a; this.b = b; }
}
void main() {
    print(Holder.pair.a);
    Pair p = new Pair(3, 4);
    Holder.pair = p;
    p.a = 99;
    print(Holder.pair.a);
    print(Holder.other.b);
    Holder.pair.a = 7;
    print(Holder.pair.a);
})",
                     {"0", "3", "2", "7"});
    });

    test("host values are copied at the boundary", [] {
        TestEngine harness;
        harness.load(R"(
struct S {
    public int v;
    public S(int v) { this.v = v; }
}
S Make(int v) { return new S(v); }
int Mutate(S s) { s.v = 99; return s.v; }
)");
        Value held = harness.engine->invoke("test", "Make", {Value::i32(3)});
        auto value = held.asStruct();
        expectTrue(value != nullptr, "Make returns a struct");
        expectEqual(3, value->fields()[0].asI32());
        expectEqual(99, harness.engine->invoke("test", "Mutate", {held}).asI32());
        expectEqual(3, held.asStruct()->fields()[0].asI32());
    });

    test("nested structs and struct fields in classes", [] {
        expectOutput(R"(
struct Inner {
    public int v;
    public Inner(int v) { this.v = v; }
}
struct Outer {
    public Inner inner;
    public string tag;
}
class Holder {
    public Outer data;
    public int id = 3;
}
Inner MakeInner(int v) { return new Inner(v); }
int Read(Inner x) { x.v = 99; return x.v; }
void main() {
    Outer o = new Outer();
    o.inner = new Inner(5);
    Outer p = o;
    p.inner.v = 42;
    print(o.inner.v);
    print(p.inner.v);

    Holder h = new Holder();
    print(h.data.inner.v);
    h.data.inner = new Inner(7);
    print(h.data.inner.v);

    Outer q = h.data;
    q.inner.v = 8;
    print(h.data.inner.v);

    Inner m = MakeInner(11);
    print(m.v);
    int r = Read(m);
    print(r);
    print(m.v);
})",
                     {"5", "42", "0", "7", "7", "11", "99", "11"});
    });

    test("structs in namespaces with static members", [] {
        expectOutput(R"(
namespace N {
    struct S {
        public static int Total = 0;
        public int v;
        public S(int v) { this.v = v; S.Total += 1; }
        public int Value() { return v; }
    }
}
void main() {
    N.S a = new N.S(1);
    N.S b = a;
    print(N.S.Total);
    print(b.v);
    N.S c = new N.S(2);
    print(N.S.Total);
    print(c.Value());
    N.S[] items = new N.S[1];
    items[0] = c;
    items[0].v = 9;
    print(c.v);
    print(items[0].v);
})",
                     {"1", "1", "2", "2", "2", "9"});
    });

    test("struct restrictions are diagnosed", [] {
        auto expectDiagnostic = [](const std::string& source, const std::string& code) {
            TestEngine harness;
            std::vector<Diagnostic> diagnostics;
            auto module = harness.engine->tryLoadModule("bad", source, "bad.cs", diagnostics);
            expectTrue(module == nullptr, "expected compilation failure for " + code);
            bool found = false;
            std::string all;
            for (const auto& diagnostic : diagnostics) {
                if (diagnostic.code == code) found = true;
                all += diagnostic.toString() + "\n";
            }
            if (!found) throw std::runtime_error("expected diagnostic " + code + ", got:\n" + all);
        };

        expectDiagnostic("struct Node { public Node next; } void main() {}", "E0400");
        expectDiagnostic("struct S { public int v; } class D : S { } void main() {}", "E0203");
        expectDiagnostic("struct S { public int v; } void main() { S s = null; }", "E0374");
        expectDiagnostic("struct S { public int v; } void main() { S a = new S(); S b = new S(); print(a < b); }",
                         "E0357");
        expectDiagnostic("struct S { public int v; } void main() { S a = new S(); S b = (S)a; print(b.v); }",
                         "E0401");
        expectDiagnostic("struct S { public int v; } void main() { S a = new S(); S b = a as S; print(b.v); }",
                         "E0402");
        expectDiagnostic("struct S { public int v; } void main() { S a = new S(); print(a is S); }", "E0403");
    });

    test("struct values participate in garbage collection", [] {
        TestEngine harness;
        harness.engine->settings.autoGarbageCollect = false;
        harness.run(R"(
class Box { public Payload payload; }
struct Payload { public Box owner; }
void main() {
    Box box = new Box();
    box.payload = new Payload();
    box.payload.owner = box;
})");
        expectEqual(std::size_t{2}, harness.engine->gcStatistics().currentAllocated, "live objects");
        harness.engine->garbageCollect();
        auto stats = harness.engine->gcStatistics();
        expectEqual(std::size_t{2}, stats.lastCycleDetected, "class <-> struct cycle is collected");
        expectEqual(std::size_t{0}, stats.currentAllocated, "allocated after collection");
    });

    test("struct globals survive hot reload", [] {
        TestEngine harness;
        harness.run(R"(
struct S { public int a; public int b; }
S value;
void main() { value.a = 5; }
)");
        auto result = harness.engine->reloadModule(
            "test",
            "struct S { public int a; public int b; public int c; }\n"
            "S value;\n"
            "void main() {}\n",
            "test.cs");
        expectTrue(result.success, "reload should succeed");

        auto module = harness.engine->getModule("test");
        auto it = module->globalsByName.find("value");
        expectTrue(it != module->globalsByName.end(), "global 'value' exists");
        auto value = module->globals[it->second].value.asStruct();
        expectTrue(value != nullptr, "global is a struct value");
        auto* type = value->type();
        expectTrue(type != nullptr, "struct type");
        auto* a = type->findField("a");
        auto* b = type->findField("b");
        auto* c = type->findField("c");
        expectTrue(a != nullptr && b != nullptr && c != nullptr, "all fields present");
        expectEqual(5, value->fields()[a->index].asI32());
        expectEqual(0, value->fields()[b->index].asI32());
        expectEqual(0, value->fields()[c->index].asI32());
    });
}

void testDelegates() {
    test("delegate method groups, multicast and invocation", [] {
        expectOutput(R"(
delegate int Op(int a, int b);
delegate void Notify(string m);

class Calc {
    public int bias = 10;
    public int Add(int a, int b) { return a + b + bias; }
    public static int Mul(int a, int b) { return a * b; }
    public void Tell(string m) { print("calc:" + m); }
}

int Plain(int a, int b) { return a + b; }

void main() {
    Calc c = new Calc();
    Op f = c.Add;
    print(f(2, 3));
    Op g = Calc.Mul;
    print(g(2, 3));
    Op p = Plain;
    print(p(2, 3));

    Op both = f + g;
    print(both(2, 3));
    both -= g;
    print(both(2, 3));
    both += g;
    both += g;
    print(both(2, 3));

    Notify n = c.Tell;
    n("hi");
    n.Invoke("invoke");

    Op empty = null;
    print(empty == null);
    try { empty(1, 2); } catch (NullReferenceException e) { print("null-delegate"); }

    Op copy = f;
    print(copy == f);
    print(copy != g);
})",
                     {"15", "6", "5", "6", "15", "6", "calc:hi", "calc:invoke", "true", "null-delegate",
                      "true", "true"});
    });

    test("delegate fields, arguments and struct targets", [] {
        expectOutput(R"(
delegate void Handler(int v);
delegate int Getter();

namespace N {
    delegate int Op(int x);
    class Util { public static int Twice(int x) { return x * 2; } }
}

struct Counter {
    public int n;
    public int Bump() { n += 1; return n; }
}

class Sink {
    public int total = 0;
    public void On(int v) { total += v; }
    public int Zero() { return 0; }
}

class Emitter {
    public Handler onEvent;
    public void Fire(int v) { if (onEvent != null) { onEvent(v); } }
}

int Apply(Getter g) { return g(); }

void main() {
    Sink s = new Sink();
    Emitter e = new Emitter();
    e.onEvent += s.On;
    e.Fire(3);
    e.Fire(4);
    print(s.total);

    Counter c = new Counter();
    Getter g = c.Bump;
    print(g());
    print(g());
    print(c.n);

    Handler h = null;
    h += s.On;
    print(h != null);
    h(2);
    print(s.total);

    Getter z = s.Zero;
    print(Apply(z));

    N.Op op = N.Util.Twice;
    print(op(21));
})",
                     {"7", "1", "2", "0", "true", "9", "0", "42"});
    });

    test("delegate cycles participate in garbage collection", [] {
        TestEngine harness;
        harness.engine->settings.autoGarbageCollect = false;
        harness.run(R"(
delegate void Ping();

class Node {
    public Node next;
    public Ping self;
    public void Ping() {}
}

void main() {
    Node node = new Node();
    node.next = node;
    node.self = node.Ping;
})");
        expectEqual(std::size_t{2}, harness.engine->gcStatistics().currentAllocated, "live objects");
        harness.engine->garbageCollect();
        auto stats = harness.engine->gcStatistics();
        expectEqual(std::size_t{2}, stats.lastCycleDetected, "instance <-> delegate cycle is collected");
        expectEqual(std::size_t{0}, stats.currentAllocated, "allocated after collection");
    });

    test("delegate conversion is diagnosed", [] {
        auto expectDiagnostic = [](const std::string& source, const std::string& code) {
            TestEngine harness;
            std::vector<Diagnostic> diagnostics;
            auto module = harness.engine->tryLoadModule("bad", source, "bad.cs", diagnostics);
            expectTrue(module == nullptr, "expected compilation failure for " + code);
            bool found = false;
            std::string all;
            for (const auto& diagnostic : diagnostics) {
                if (diagnostic.code == code) found = true;
                all += diagnostic.toString() + "\n";
            }
            if (!found) throw std::runtime_error("expected diagnostic " + code + ", got:\n" + all);
        };

        expectDiagnostic(
            "delegate int Op(int a); int Bad(int a, int b) { return 0; } void main() { Op f = Bad; }",
            "E0409");
        expectDiagnostic("delegate int Op(int a); void main() { int x = 1; Op f = x; }", "E0382");
        expectDiagnostic("delegate int Op(int a); void main() { Op f = 3; }", "E0382");
    });
}

void testGenerics() {
    test("generic classes are monomorphized", [] {
        expectOutput(R"(
class Box<T> {
    public T value;
    public Box(T value) { this.value = value; }
    public T Get() { return value; }
    public void Set(T v) { value = v; }
}

void main() {
    Box<int> a = new Box<int>(5);
    print(a.Get());
    a.Set(7);
    print(a.value);

    Box<string> s = new Box<string>("hi");
    print(s.Get());
})",
                     {"5", "7", "hi"});
    });

    test("generic structs, namespaces and per-instantiation statics", [] {
        expectOutput(R"(
namespace Game {
    class Counter<T> {
        public static int total = 0;
        public T tag;
        public Counter(T tag) { this.tag = tag; total += 1; }
        public int Total() { return total; }
    }
    struct Wrapper<T> {
        public T value;
        public Wrapper(T value) { this.value = value; }
        public T Unwrap() { return value; }
    }
}

void main() {
    Game.Counter<int> a = new Game.Counter<int>(1);
    Game.Counter<int> b = new Game.Counter<int>(2);
    Game.Counter<string> c = new Game.Counter<string>("x");
    print(a.Total());
    print(c.Total());
    print(c.tag);

    Game.Wrapper<int> w = new Game.Wrapper<int>(9);
    print(w.Unwrap());
    Game.Wrapper<string> s = new Game.Wrapper<string>("ok");
    print(s.Unwrap());
})",
                     {"2", "1", "x", "9", "ok"});
    });

    test("recursive and nested generic types", [] {
        expectOutput(R"(
class Node<T> {
    public T value;
    public Node<T> next;
    public Node(T value) { this.value = value; }
}

class Box<T> {
    public T value;
    public Box(T value) { this.value = value; }
}

void main() {
    Node<int> head = new Node<int>(1);
    head.next = new Node<int>(2);
    print(head.next.value);

    Box<int>[] boxes = new Box<int>[2];
    boxes[0] = new Box<int>(3);
    print(boxes[0].value);

    Box<Box<int> > nested = new Box<Box<int> >(boxes[0]);
    print(nested.value.value);
})",
                     {"2", "3", "3"});
    });

    test("generic restrictions are diagnosed", [] {
        auto expectDiagnostic = [](const std::string& source, const std::string& code) {
            TestEngine harness;
            std::vector<Diagnostic> diagnostics;
            auto module = harness.engine->tryLoadModule("bad", source, "bad.cs", diagnostics);
            expectTrue(module == nullptr, "expected compilation failure for " + code);
            bool found = false;
            std::string all;
            for (const auto& diagnostic : diagnostics) {
                if (diagnostic.code == code) found = true;
                all += diagnostic.toString() + "\n";
            }
            if (!found) throw std::runtime_error("expected diagnostic " + code + ", got:\n" + all);
        };

        expectDiagnostic("class Box<T> { public T v; } void main() { Box b = null; }", "E0431");
        expectDiagnostic("class Box<T> { public T v; } void main() { Box<int, string> b; }", "E0432");
        expectDiagnostic("void main() { Missing<int> b; }", "E0430");
        expectDiagnostic("class Box<T> where T : class { public T v; } void main() { Box<int> b = null; }",
                         "E0435");
    });
}

void testAdvancedGenerics() {
    test("generic methods are monomorphized", [] {
        expectOutput(R"(
T Max<T>(T a, T b) {
    return a > b ? a : b;
}

T Identity<T>(T value) {
    return value;
}

int Twice(int value) { return value * 100; }

class Calc {
    public static T Twice<T>(T value) { return value + value; }
    public T[] Pair<T>(T value) {
        T[] result = new T[2];
        result[0] = value;
        result[1] = value;
        return result;
    }
}

void main() {
    print(Max(3, 7));
    print(Max<int>(11, 4));
    print(Identity<string>("hi"));
    print(Twice(2));
    print(Calc.Twice<int>(21));
    print(Calc.Twice<string>("ab"));
    Calc calc = new Calc();
    string[] pair = calc.Pair<string>("x");
    print(pair[0] + pair[1]);
})",
                     {"7", "11", "hi", "200", "42", "abab", "xx"});
    });

    test("generic methods honour constraints, recursion and ref parameters", [] {
        expectOutput(R"(
class Animal { public string name = "animal"; }
class Dog : Animal { public Dog() { name = "dog"; } }
class Widget { public int size = 3; }

T Create<T>() where T : new() {
    return new T();
}

string NameOf<T>(T item) where T : Animal {
    return item.name;
}

int Depth<T>(T value, int count) {
    if (count <= 0) return 0;
    return 1 + Depth(value, count - 1);
}

T Sum<T>(T[] items) where T : struct {
    T total = items[0];
    for (int i = 1; i < items.Length; i = i + 1) total = total + items[i];
    return total;
}

void Swap<T>(ref T first, ref T second) {
    T temporary = first;
    first = second;
    second = temporary;
}

void main() {
    Widget widget = Create<Widget>();
    print(widget.size);
    print(NameOf(new Dog()));
    print(Depth(1, 5));

    int[] numbers = new int[3];
    numbers[0] = 1;
    numbers[1] = 2;
    numbers[2] = 3;
    print(Sum(numbers));

    double[] decimals = new double[2];
    decimals[0] = 1.5;
    decimals[1] = 2.5;
    print(Sum(decimals));

    int a = 1;
    int b = 9;
    Swap(ref a, ref b);
    print(a);
    print(b);
})",
                     {"3", "dog", "5", "6", "4", "9", "1"});
    });

    test("generic methods of generic classes are monomorphized", [] {
        expectOutput(R"(
class Box<T> {
    public T value;
    public Box(T value) { this.value = value; }
    public U Map<U>(U input) { return input; }
    public Box<U> Repack<U>(U input) { return new Box<U>(input); }
}

Box<T> Wrap<T>(T value) {
    return new Box<T>(value);
}

void main() {
    Box<int> box = Wrap(5);
    print(box.value);

    Box<Box<int> > nested = box.Repack<Box<int> >(box);
    print(nested.value.value);

    Box<int> mapped = new Box<int>(1);
    print(mapped.Map<string>("mapped"));
})",
                     {"5", "5", "mapped"});
    });

    test("generic delegates are monomorphized", [] {
        expectOutput(R"(
delegate TResult Convert<in T, out TResult>(T value);
delegate void Action<in T>(T value);

class Box<T> {
    public T value;
    public Box(T value) { this.value = value; }
}

class Holder<T> {
    public Convert<T, string> toText;
    public Holder(Convert<T, string> toText) { this.toText = toText; }
    public string Apply(T value) { return toText(value); }
}

string Describe(Box<int> box) { return "box=" + box.value; }

void Run<T>(Action<T> action, T value) { action(value); }

void main() {
    Convert<Box<int>, string> describe = Describe;
    print(describe(new Box<int>(4)));

    Holder<int> holder = new Holder<int>((int value) => { return "n=" + value; });
    print(holder.Apply(5));
    print(holder.toText(6));

    Run<int>((int value) => { print("run:" + value); }, 7);
})",
                     {"box=4", "n=5", "n=6", "run:7"});
    });

    test("variance converts generic delegates", [] {
        expectOutput(R"(
class Animal { public string name = "animal"; }
class Dog : Animal { public Dog() { name = "dog"; } }
class Puppy : Dog { public Puppy() { name = "puppy"; } }

delegate T Producer<out T>();
delegate void Consumer<in T>(T value);
delegate TResult Map<in T, out TResult>(T value);

void main() {
    Producer<Puppy> puppyFactory = () => new Puppy();
    Producer<Dog> dogFactory = puppyFactory;
    Producer<Animal> animalFactory = dogFactory;
    print(animalFactory().name);

    Consumer<Animal> animalConsumer = (Animal item) => { print("consume:" + item.name); };
    Consumer<Dog> dogConsumer = animalConsumer;
    dogConsumer(new Puppy());

    Map<Animal, Puppy> toPuppy = (Animal item) => { return new Puppy(); };
    Map<Dog, Animal> widened = toPuppy;
    print(widened(new Dog()).name);
})",
                     {"puppy", "consume:puppy", "puppy"});
    });

    test("generic method and variance restrictions are diagnosed", [] {
        auto expectDiagnostic = [](const std::string& source, const std::string& code) {
            TestEngine harness;
            std::vector<Diagnostic> diagnostics;
            auto module = harness.engine->tryLoadModule("bad", source, "bad.cs", diagnostics);
            expectTrue(module == nullptr, "expected compilation failure for " + code);
            bool found = false;
            std::string all;
            for (const auto& diagnostic : diagnostics) {
                if (diagnostic.code == code) found = true;
                all += diagnostic.toString() + "\n";
            }
            if (!found) throw std::runtime_error("expected diagnostic " + code + ", got:\n" + all);
        };

        // Constraints.
        expectDiagnostic("class Box<T> where T : class { public T v; } void main() { Box<int> b = null; }",
                         "E0435");
        expectDiagnostic("class Box<T> where T : struct { public T v; } void main() { Box<string> b = null; }",
                         "E0435");
        expectDiagnostic("T Make<T>() where T : new() { return new T(); } void main() { int x = Make<string>(); }",
                         "E0435");
        // Inference and explicit type arguments.
        expectDiagnostic("T Make<T>() { return null; } void main() { int x = Make(); }", "E0436");
        expectDiagnostic("T Pick<T>(T value) { return value; } void main() { Pick<int, string>(1); }", "E0438");
        expectDiagnostic("int Plain(int value) { return value; } void main() { Plain<int>(1); }", "E0439");
        // Variance.
        expectDiagnostic("class Evil<out T> { public T value; } void main() { }", "E0440");
        expectDiagnostic("delegate void Bad<out T>(T value); void main() { }", "E0441");
        expectDiagnostic("delegate T Bad<in T>(); void main() { }", "E0442");
        expectDiagnostic("abstract class Base { public abstract T Get<T>(); } void main() { }", "E0444");
        expectDiagnostic(
            "class Animal { } class Dog : Animal { } delegate T Producer<out T>(); "
            "void main() { Producer<Animal> a = () => new Animal(); Producer<Dog> d = a; }",
            "E0381");
        expectDiagnostic(
            "class Animal { } class Dog : Animal { } delegate void Consumer<in T>(T value); "
            "void main() { Consumer<Dog> d = (Dog x) => { }; Consumer<Animal> a = d; }",
            "E0381");
    });
}

void testFuncAction() {
    test("built-in Func and Action bind lambdas and method groups", [] {
        expectOutput(R"(
using System;
Func<int, int> twice = x => x * 2;
int Square(int x) { return x * x; }
void Announce() { print("announced"); }
class Calculator {
    public int offset = 10;
    public int Add(int x) { return x + offset; }
    public static string Format(int x) { return "v=" + x; }
    public void Record(int x) { print("record:" + x); }
}
void main() {
    Action empty = () => { print("empty"); };
    empty();
    Action announce = new Action(Announce);
    announce.Invoke();
    Action<int> output = x => print(x);
    output(3);
    Func<int> answer = () => 42;
    print(answer.Invoke());
    Func<int, int> square = new Func<int, int>(Square);
    print(square(5));
    print(twice(4));
    Calculator calc = new Calculator();
    Func<int, int> add = calc.Add;
    Action<int> record = calc.Record;
    Func<int, string> format = Calculator.Format;
    print(add(2));
    record(7);
    print(format(8));
    Func<int, int, int> sum = (int a, int b) => a + b;
    print(sum(3, 4));
    int count = 0;
    Action increment = () => count++;
    increment();
    print(count);
})", {"empty", "announced", "3", "42", "25", "8", "12", "record:7", "v=8", "7", "1"});
    });

    test("Func and Action have canonical System identities and multicast semantics", [] {
        TestEngine harness;
        auto module = harness.load(R"(
Action first;
System.Action second;
Func<int> a;
System.Func<int> b;
Action<int> c;
System.Action<int> d;
void First() { print("first"); }
void Second() { print("second"); }
int One() { return 1; }
int Two() { return 2; }
void main() {
    first = First;
    second = Second;
    first += second;
    first();
    first -= second;
    first.Invoke();
    a = One;
    b = Two;
    a += b;
    print(a());
    a -= b;
    print(a.Invoke());
    c = x => print(x);
    d = c;
    d(9);
}
)");
        harness.engine->invokeEntryPoint("test");
        expectEqual(std::size_t{6}, harness.output.size(), "output count");
        const std::vector<std::string> expected{"first", "second", "first", "2", "1", "9"};
        for (std::size_t i = 0; i < expected.size(); ++i) expectEqual(expected[i], harness.output[i]);
        auto typeOf = [&](const std::string& name) {
            return module->globals[module->globalsByName.at(name)].type.object;
        };
        expectTrue(typeOf("first") == typeOf("second"), "Action aliases share a type");
        expectTrue(typeOf("a") == typeOf("b"), "Func aliases share a type");
        expectTrue(typeOf("c") == typeOf("d"), "generic Action aliases share a type");
    });

    test("Func higher-order calls, fields and results participate in inference", [] {
        expectOutput(R"(
class Holder {
    public Func<int, int> transform;
    public Holder(Func<int, int> transform) { this.transform = transform; }
}
Func<int, int> MakeAdder(int offset) { return x => x + offset; }
Func<Func<int, int>, int> Apply() { return f => f(5); }
Func<int> Counter() {
    int count = 0;
    return () => ++count;
}
T Identity<T>(T value) { return value; }
string Select(string x) { return "string:" + x; }
string Select(int x) { return "int:" + x; }
void Run(Action<int> action, int value) { action(value); }
void main() {
    Func<int, int> add = MakeAdder(10);
    Holder holder = new Holder(add);
    print(holder.transform(2));
    print(holder.transform.Invoke(3));
    print(Select(add(4)));
    print(Identity(add.Invoke(5)));
    print(Identity(holder.transform(6)));
    Func<Func<int, int>, int> apply = Apply();
    print(apply(add));
    Func<int> next = Counter();
    print(next());
    print(next());
    Func<int>[] functions = new Func<int>[1];
    functions[0] = next;
    print(Identity(functions[0]()));
    Run(x => print("run:" + x), 7);
})", {"12", "13", "int:14", "15", "16", "15", "1", "2", "3", "run:7"});
    });

    test("Func and Action support all CSharp arities", [] {
        std::string source = "void main() {\n";
        std::vector<std::string> expected;
        for (int count = 0; count <= 16; ++count) {
            std::string types;
            std::string parameters;
            std::string arguments;
            std::string sum = "0";
            for (int i = 0; i < count; ++i) {
                if (i > 0) { types += ", "; parameters += ", "; arguments += ", "; }
                types += "int";
                parameters += "p" + std::to_string(i);
                arguments += std::to_string(i + 1);
                sum += " + p" + std::to_string(i);
            }
            const std::string suffix = std::to_string(count);
            const std::string action = count == 0 ? "Action" : "Action<" + types + ">";
            const std::string func = "Func<" + types + (count == 0 ? "" : ", ") + "int>";
            source += action + " a" + suffix + " = (" + parameters + ") => print(" + sum + ");\n";
            source += func + " f" + suffix + " = (" + parameters + ") => " + sum + ";\n";
            source += "a" + suffix + "(" + arguments + "); print(f" + suffix + "(" + arguments + "));\n";
            expected.push_back(std::to_string(count * (count + 1) / 2));
            expected.push_back(expected.back());
        }
        source += "}";
        expectOutput(source, expected);
    });

    test("Func and Action variance and method-group reference conversions are safe", [] {
        expectOutput(R"(
class Animal { public string name = "animal"; }
class Dog : Animal { public Dog() { name = "dog"; } }
Dog MakeDog() { return new Dog(); }
Dog Echo(Animal value) { return new Dog(); }
void Consume(Animal value) { print("consume:" + value.name); }
string Which(Animal value) { return "animal overload"; }
string Which(Dog value) { return "dog overload"; }
class Dispatch {
    public static string Static(Animal value) { return "animal static"; }
    public static string Static(Dog value) { return "dog static"; }
    public string Instance(Animal value) { return "animal instance"; }
    public string Instance(Dog value) { return "dog instance"; }
}
void main() {
    Func<Dog> dog = MakeDog;
    Func<Animal> animal = dog;
    print(animal().name);
    Action<Animal> consume = Consume;
    Action<Dog> dogConsumer = consume;
    dogConsumer(new Dog());
    Func<Animal, Dog> echo = Echo;
    Func<Dog, Animal> widened = echo;
    print(widened(new Dog()).name);
    Func<Animal> direct = MakeDog;
    Action<Dog> directConsumer = Consume;
    Func<Dog, Animal> directEcho = Echo;
    print(direct().name);
    directConsumer(new Dog());
    print(directEcho(new Dog()).name);
    Func<Func<Dog> > nested = () => dog;
    Func<Func<Animal> > nestedWide = nested;
    print(nestedWide()().name);
    Func<Dog, string> which = Which;
    Func<Dog, string> staticWhich = Dispatch.Static;
    Dispatch dispatch = new Dispatch();
    Func<Dog, string> instanceWhich = dispatch.Instance;
    print(which(new Dog()));
    print(staticWhich(new Dog()));
    print(instanceWhich(new Dog()));
})", {"dog", "consume:dog", "dog", "dog", "consume:dog", "dog", "dog",
      "dog overload", "dog static", "dog instance"});
    });

    test("user delegate names can shadow built-in short names", [] {
        expectOutput(R"(
namespace Game {
    delegate int Func<T>(T value);
    delegate int Action();
    void Demo() {
        Func<int> custom = x => x + 1;
        Action customAction = () => 8;
        System.Func<int> standard = () => 4;
        System.Action standardAction = () => print("standard");
        print(custom(2));
        print(customAction());
        print(standard());
        standardAction();
    }
}
void main() { Game.Demo(); }
)", {"3", "8", "4", "standard"});
    });

    test("built-in Action closures participate in cycle collection", [] {
        TestEngine harness;
        harness.engine->settings.autoGarbageCollect = false;
        harness.run(R"(
class Node { public Action callback; public void Run() { } }
void main() {
    Node node = new Node();
    node.callback = () => node.Run();
}
)");
        expectEqual(std::size_t{3}, harness.engine->gcStatistics().currentAllocated, "live objects");
        harness.engine->garbageCollect();
        expectEqual(std::size_t{0}, harness.engine->gcStatistics().currentAllocated, "collected closure cycle");
    });

    test("Func and Action invalid signatures and conversions are diagnosed", [] {
        auto expectDiagnostic = [](const std::string& source, const std::string& code) {
            TestEngine harness;
            std::vector<Diagnostic> diagnostics;
            auto module = harness.engine->tryLoadModule("bad", source, "bad.cs", diagnostics);
            expectTrue(module == nullptr, "expected compilation failure for " + code);
            bool found = false;
            std::string all;
            for (const auto& diagnostic : diagnostics) {
                if (diagnostic.code == code) found = true;
                all += diagnostic.toString() + "\n";
            }
            if (!found) throw std::runtime_error("expected diagnostic " + code + ", got:\n" + all);
        };
        expectDiagnostic("void main() { Func f; }", "E0431");
        expectDiagnostic("void main() { Func<void> f; }", "E0445");
        expectDiagnostic("void main() { Action<void> a; }", "E0445");
        expectDiagnostic("void main() { Func<var> f; }", "E0445");
        for (const auto& family : {std::string("Func"), std::string("Action")}) {
            std::string types = "int";
            const int count = family == "Func" ? 18 : 17;
            for (int i = 1; i < count; ++i) types += ", int";
            expectDiagnostic("void main() { " + family + "<" + types + "> f; }", "E0432");
        }
        expectDiagnostic("void main() { Func<int, int> f = (x, y) => x; }", "E0412");
        expectDiagnostic("void main() { Func<int, int> f = (string x) => 1; }", "E0413");
        expectDiagnostic("void main() { Action a = () => 42; }", "E0414");
        expectDiagnostic("void main() { Action<int> a = x => x + 1; }", "E0414");
        expectDiagnostic("void main() { Action a = () => { return 1; }; }", "E0310");
        expectDiagnostic("int F() { return 1; } void main() { Action a = F; }", "E0409");
        expectDiagnostic("void F() { } void main() { Func<int> f = F; }", "E0409");
        expectDiagnostic("void main() { Action a = new Action(); }", "E0348");
        expectDiagnostic("void main() { Func<int> f = () => 1; Func<double> g = f; }", "E0381");
        expectDiagnostic("void main() { Action<double> a = x => { }; Action<int> b = a; }", "E0381");
        expectDiagnostic("double F(double x) { return x; } void main() { Func<int, double> f = F; }", "E0409");
        expectDiagnostic("namespace System { delegate int Func<T>(); } void main() { }", "E0408");
        expectDiagnostic("namespace System { class Action { } } void main() { }", "E0201");
        auto nullCall = expectRuntimeError("void main() { Func<int> f = null; f(); }");
        expectContains(nullCall.what(), "null");
    });
}

void testScriptStandardLibrary() {
    test("structs support value equality", [] {
        expectOutput(R"(
struct Point {
    public int x;
    public int y;
    public Point(int x, int y) { this.x = x; this.y = y; }
}

struct Label {
    public Point origin;
    public string text;
    public Label(Point origin, string text) { this.origin = origin; this.text = text; }
}

void main() {
    Point a = new Point(1, 2);
    Point b = new Point(1, 2);
    Point c = new Point(1, 3);
    print(a == b);
    print(a != b);
    print(a == c);
    print(a != c);

    Label first = new Label(a, "t");
    Label second = new Label(b, "t");
    Label other = new Label(a, "u");
    print(first == second);
    print(first == other);

    Point[] points = new Point[2];
    points[0] = c;
    points[1] = a;
    int found = -1;
    for (int i = 0; i < points.Length; i = i + 1) {
        if (points[i] == b) found = i;
    }
    print(found);
})",
                     {"true", "false", "false", "true", "true", "false", "1"});
    });

    test("Array<T> script library provides the collection algorithms", [] {
        expectOutput(R"(
class Player {
    public string name;
    public Player(string name) { this.name = name; }
}

struct Point {
    public int x;
    public int y;
    public Point(int x, int y) { this.x = x; this.y = y; }
}

void main() {
    Array<int> numbers = new Array<int>();
    numbers.Add(3);
    numbers.Add(5);
    numbers.Add(7);
    print(numbers.Count);
    print(numbers.IndexOf(7));
    print(numbers.Contains(4));

    numbers.Insert(1, 4);
    print(numbers.Get(1));
    numbers.RemoveAt(0);
    print(numbers.Get(0));
    numbers.Set(0, 9);
    numbers.ForEach(v => print("n:" + v));

    Array<string> names = new Array<string>();
    names.Reserve(8);
    names.Add("a");
    names.Add("b");
    print(names.Contains("b"));
    string[] raw = names.ToArray();
    print(raw.Length);

    Array<Player> players = new Array<Player>();
    players.Add(new Player("p1"));
    print(players.Get(0).name);

    Array<Point> points = new Array<Point>();
    points.Add(new Point(1, 2));
    points.Add(new Point(3, 4));
    print(points.IndexOf(new Point(3, 4)));
    print(points.Contains(new Point(9, 9)));
    Point stored = points.Get(0);
    stored.x = 99;
    print(points.Get(0).x);

    Array<Array<int> > grid = new Array<Array<int> >();
    Array<int> row = new Array<int>();
    row.Add(8);
    grid.Add(row);
    print(grid.Get(0).Get(0));

    numbers.Clear();
    print(numbers.Count);
})",
                     {"3", "2", "false", "4", "4", "n:9", "n:5", "n:7", "true", "2", "p1", "1",
                      "false", "1", "8", "0"});
    });

    test("collection algorithms left T[] and the library can be disabled", [] {
        auto expectDiagnostic = [](const std::string& source, const std::string& code) {
            TestEngine harness;
            std::vector<Diagnostic> diagnostics;
            auto module = harness.engine->tryLoadModule("bad", source, "bad.cs", diagnostics);
            expectTrue(module == nullptr, "expected compilation failure for " + code);
            bool found = false;
            std::string all;
            for (const auto& diagnostic : diagnostics) {
                if (diagnostic.code == code) found = true;
                all += diagnostic.toString() + "\n";
            }
            if (!found) throw std::runtime_error("expected diagnostic " + code + ", got:\n" + all);
        };

        expectDiagnostic("void main() { int[] a = new int[2]; a.insert(0, 1); }", "E0446");
        expectDiagnostic("void main() { int[] a = new int[2]; a.removeAt(0); }", "E0446");
        expectDiagnostic("void main() { int[] a = new int[2]; a.clear(); }", "E0446");
        expectDiagnostic("void main() { int[] a = new int[2]; a.IndexOf(1); }", "E0446");
        expectDiagnostic("void main() { int[] a = new int[2]; a.Contains(1); }", "E0446");

        TestEngine harness;
        harness.engine->settings.useScriptStandardLibrary = false;
        std::vector<Diagnostic> diagnostics;
        auto module = harness.engine->tryLoadModule("noLib", "void main() { Array<int> a = new Array<int>(); }",
                                                   "noLib.cs", diagnostics);
        expectTrue(module == nullptr, "Array<T> is unknown without the library");
        bool found = false;
        for (const auto& diagnostic : diagnostics) {
            if (diagnostic.code == "E0430") found = true;
        }
        expectTrue(found, "expected E0430 when the script library is disabled");
    });
}

void testSections() {
    test("sections share one global namespace", [] {
        TestEngine harness;
        auto module = harness.engine->loadModule(
            "multi",
            {ScriptSection("player.cs", R"(
namespace Game {
    class Player {
        public string name;
        public Player(string name) { this.name = name; }
        public string Describe() { return "player:" + name; }
    }
    int Created = 0;
}
)"),
             ScriptSection("util.cs", R"(
namespace Game {
    int Touch() {
        Created = Created + 1;
        return Created;
    }
}
)"),
             ScriptSection("main.cs", R"(
using Game;
void main() {
    Player p = new Player("hero");
    print(p.Describe());
    print(Touch());
    print(Game.Created);
}
)")},
            "multi.cs");
        expectEqual(std::size_t{4}, module->sections.size(), "3 user sections + library");
        expectEqual(std::string("main.cs"), module->sections[2].file, "last user section");
        expectEqual(std::string("lib/array.cs"), module->sections[3].file, "library section");
        harness.engine->invokeEntryPoint("multi");
        expectEqual(std::string("player:hero"), harness.output[0]);
        expectEqual(std::string("1"), harness.output[1]);
        expectEqual(std::string("1"), harness.output[2]);
    });

    test("diagnostics point at the section that failed", [] {
        TestEngine harness;
        std::vector<Diagnostic> diagnostics;
        auto module = harness.engine->tryLoadModule(
            "bad",
            {ScriptSection("good.cs", "int Add(int a, int b) { return a + b; }\n"),
             ScriptSection("broken.cs", "void main() { NoSuchType value = null; }\n")},
            "bad.cs", diagnostics);
        expectTrue(module == nullptr, "expected a compile failure");
        bool found = false;
        std::string all;
        for (const auto& diagnostic : diagnostics) {
            all += diagnostic.toString() + "\n";
            if (diagnostic.file == "broken.cs" && diagnostic.code == "E0215") found = true;
        }
        if (!found) throw std::runtime_error("expected E0215 in broken.cs, got:\n" + all);
    });

    test("section line offsets shift reported lines", [] {
        TestEngine harness;
        std::vector<Diagnostic> diagnostics;
        auto module = harness.engine->tryLoadModule(
            "offset", {ScriptSection("frag.cs", "// header\nvoid main() { NoSuchType value; }\n", 100)},
            "offset.cs", diagnostics);
        expectTrue(module == nullptr, "expected a compile failure");
        bool found = false;
        std::string all;
        for (const auto& diagnostic : diagnostics) {
            all += diagnostic.toString() + "\n";
            if (diagnostic.file == "frag.cs" && diagnostic.line == 102) found = true;
        }
        if (!found) throw std::runtime_error("expected line 102 in frag.cs, got:\n" + all);
    });

    test("sections reload and patch method bodies", [] {
        TestEngine harness;
        harness.engine->loadModule(
            "multi_reload", {ScriptSection("logic.cs", R"(
int Level() {
    return 1;
}
)"),
                             ScriptSection("main.cs", R"(
void main() {
    print(Level());
})")},
            "multi_reload.cs");
        harness.engine->invokeEntryPoint("multi_reload");
        expectEqual(std::string("1"), harness.output[0]);

        auto result = harness.engine->reloadModule(
            "multi_reload", {ScriptSection("logic.cs", R"(
int Level() {
    return 5;
}
)"),
                             ScriptSection("main.cs", R"(
void main() {
    print(Level());
    print(Level() + 1);
})")},
            "multi_reload.cs");
        expectTrue(result.success, result.summary());
        expectEqual(std::string("method-body"), result.strategy);
        expectEqual(2, result.module->generation);

        harness.engine->invokeEntryPoint("multi_reload");
        expectEqual(std::string("5"), harness.output[1]);
        expectEqual(std::string("6"), harness.output[2]);
    });

    test("the standard library is its own section", [] {
        TestEngine harness;
        auto module = harness.load("void main() { }", "stdlib_section");
        expectTrue(!module->sections.empty(), "expected sections");
        expectEqual(std::string("lib/array.cs"), module->sections.back().file, "library section name");
    });
}

void testInterfaces() {
    test("interfaces declare signatures and classes implement them", [] {
        expectOutput(R"(
using System;

interface IShape
{
    double Area();
    string Name { get; }
}

class Circle : IShape
{
    public double radius;
    public Circle(double radius) { this.radius = radius; }
    public double Area() { return 3.0 * radius * radius; }
    public string Name { get { return "circle"; } }
}

class Square : IShape
{
    public double side;
    public Square(double side) { this.side = side; }
    public double Area() { return side * side; }
    public string Name { get { return "square"; } }
}

class Program
{
    public static void Main()
    {
        IShape[] shapes = new IShape[2];
        shapes[0] = new Circle(2.0);
        shapes[1] = new Square(3.0);
        foreach (var shape in shapes)
        {
            Console.WriteLine(shape.Name + " " + shape.Area());
        }
        IShape first = shapes[0];
        Console.WriteLine(first is Circle);
        Console.WriteLine(first is Square);
        Circle cast = first as Circle;
        Console.WriteLine(cast.radius);
    }
})",
                     {"circle 12", "square 9", "True", "False", "2"});
    });

    test("interface inheritance and implementations are checked", [] {
        expectOutput(R"(
interface IShape { double Area(); }
interface INamed : IShape { string Describe(); }

class Circle : INamed
{
    public string Describe() { return "circle " + Area(); }
    public double Area() { return 5.0; }
}

void main() {
    INamed named = new Circle();
    print(named.Describe());
    IShape shape = named;
    print(shape.Area());
})",
                     {"circle 5", "5"});
    });

    test("interface misuse is diagnosed", [] {
        auto expectDiagnostic = [](const std::string& source, const std::string& code) {
            TestEngine harness;
            std::vector<Diagnostic> diagnostics;
            auto module = harness.engine->tryLoadModule("bad", source, "bad.cs", diagnostics);
            expectTrue(module == nullptr, "expected compilation failure for " + code);
            bool found = false;
            std::string all;
            for (const auto& diagnostic : diagnostics) {
                all += diagnostic.toString() + "\n";
                if (diagnostic.code == code) found = true;
            }
            if (!found) throw std::runtime_error("expected diagnostic " + code + ", got:\n" + all);
        };

        expectDiagnostic("interface IFoo { void Run(); } class A : IFoo { } void main() { }", "E0460");
        expectDiagnostic("class A : IMissing { } void main() { }", "E0203");
        expectDiagnostic(
            "interface IFoo { void Run(); } class A : IFoo { public void Run() { } } "
            "void main() { A a = new A(); IFoo f = a; A back = f; }",
            "E0381");
        expectDiagnostic("interface IFoo { void Run(); } void main() { IFoo f = new IFoo(); }", "E0465");
        expectDiagnostic("interface IFoo { void Run() { } } void main() { }", "E0100");
        expectDiagnostic("interface IFoo { public int x = 1; } void main() { }", "E0100");
    });

    test("using disposes the resource even when the body throws", [] {
        expectOutput(R"(
using System;

class Resource : IDisposable
{
    public string name;
    public Resource(string name) { this.name = name; Console.WriteLine("open " + name); }
    public void Dispose() { Console.WriteLine("close " + name); }
}

void main() {
    using (Resource a = new Resource("a"))
    {
        Console.WriteLine("work " + a.name);
    }
    try
    {
        using (Resource b = new Resource("b"))
        {
            throw new Exception("boom");
        }
    }
    catch (Exception e)
    {
        Console.WriteLine("caught " + e.Message);
    }
    using (Resource none = null)
    {
        Console.WriteLine("skipped");
    }
})",
                     {"open a", "work a", "close a", "open b", "close b", "caught boom", "skipped"});
    });

    test("foreach uses the GetEnumerator protocol", [] {
        expectOutput(R"(
using System;

void main() {
    Array<int> numbers = new Array<int>();
    numbers.Add(3);
    numbers.Add(5);
    numbers.Add(7);
    int total = 0;
    foreach (var n in numbers)
    {
        total = total + n;
    }
    print(total);

    Array<string> names = new Array<string>();
    names.Add("a");
    names.Add("b");
    foreach (var s in names) print(s);
})",
                     {"15", "a", "b"});
    });
}

void testProperties() {
    test("auto and explicit property accessors", [] {
        expectOutput(R"(
class Player {
    private int hp = 100;
    public int Health {
        get { return hp; }
        set { if (value < 0) hp = 0; else hp = value; }
    }
    public string Name { get; set; }
    public int Level { get; }

    public Player() {
        Level = 1;
        Name = "unnamed";
    }

    public void Damage(int amount) { Health = Health - amount; }
}

void main() {
    Player p = new Player();
    p.Name = "hero";
    print(p.Name);
    print(p.Level);
    print(p.Health);
    p.Damage(30);
    print(p.Health);
    p.Health = -5;
    print(p.Health);
    p.Health += 7;
    print(p.Health);
})",
                     {"hero", "1", "100", "70", "0", "7"});
    });

    test("static, inherited and generic properties", [] {
        expectOutput(R"(
struct Point { public int x; public int y; }

class Box<T> {
    public T Value { get; set; }
    public int Hits { get; private set; }
}

class Base {
    public int Health { get; set; }
    public static int Instances { get; set; }
}

class Derived : Base {
    public int Extra { get; set; }
}

void main() {
    Base.Instances = 3;
    Base.Instances += 1;
    print(Base.Instances);

    Derived d = new Derived();
    d.Health = 50;
    d.Extra = 2;
    print(d.Health + d.Extra);

    Box<int> numbers = new Box<int>();
    numbers.Value = 9;
    print(numbers.Value);

    Box<Point> points = new Box<Point>();
    Point p = new Point();
    p.x = 4;
    p.y = 5;
    points.Value = p;
    print(points.Value.x + points.Value.y);
})",
                     {"4", "52", "9", "9"});
    });

    test("property restrictions are diagnosed", [] {
        auto expectDiagnostic = [](const std::string& source, const std::string& code) {
            TestEngine harness;
            std::vector<Diagnostic> diagnostics;
            auto module = harness.engine->tryLoadModule("bad", source, "bad.cs", diagnostics);
            expectTrue(module == nullptr, "expected compilation failure for " + code);
            bool found = false;
            std::string all;
            for (const auto& diagnostic : diagnostics) {
                all += diagnostic.toString() + "\n";
                if (diagnostic.code == code) found = true;
            }
            if (!found) throw std::runtime_error("expected diagnostic " + code + ", got:\n" + all);
        };

        expectDiagnostic("class C { public int X { get; } } void main() { C c = new C(); c.X = 1; }", "E0455");
        expectDiagnostic("class C { public int X { set { } } } void main() { C c = new C(); print(c.X); }",
                         "E0454");
        expectDiagnostic("class C { public int X { get; set; } public int X { get; set; } } void main() { }",
                         "E0456");
    });
}

void testDiagnosticQuality() {
    auto collect = [](const std::string& source) {
        auto harness = std::make_unique<TestEngine>();
        std::vector<Diagnostic> diagnostics;
        auto module = harness->engine->tryLoadModule("bad", source, "bad.cs", diagnostics);
        expectTrue(module == nullptr, "expected a compile failure");
        return diagnostics;
    };

    test("a broken declaration header reports once", [&] {
        auto diagnostics = collect(R"(
void a()1
{
    print(1);
    if (true) { print(2); }
}
void b() { print(3); }
void main() { b(); }
)");
        expectEqual(std::size_t{1}, diagnostics.size(), "recovery must not reinterpret the body");
        expectEqual(std::string("E0100"), diagnostics[0].code, "code");
        expectEqual(2, diagnostics[0].line, "line");
    });

    test("a mistyped declaration type reports once", [&] {
        auto diagnostics = collect(R"(
void main() {
    string1 text = "hello";
    print(text.Length);
}
)");
        expectEqual(std::size_t{1}, diagnostics.size(), "no conversion/member cascade");
        expectEqual(std::string("E0215"), diagnostics[0].code, "code");
        expectEqual(3, diagnostics[0].line, "type line");
        expectEqual(5, diagnostics[0].column, "points at the type, not the name");
    });

    test("a broken statement reports once", [&] {
        auto diagnostics = collect(R"(
void main() {
    int total = 0;
    foreach (var n in2 numbers) {
        total += n;
    }
    print(total);
    for (int i = 0; i < 3; i++) {
        total += i;
    }
}
)");
        expectEqual(std::size_t{1}, diagnostics.size(), "recovery must resume after the block");
        expectEqual(4, diagnostics[0].line, "line of the broken foreach");
    });

    test("a broken for header reports once", [&] {
        auto diagnostics = collect(R"(
void main() {
    int total = 0;
    for (int i == 0; i < 3; i++) {
        total += i;
    }
    print(total);
}
)");
        expectEqual(std::size_t{1}, diagnostics.size(), "the header is recovered as a unit");
        expectEqual(4, diagnostics[0].line, "for line");
    });

    test("a statement outside a function is explained", [&] {
        auto diagnostics = collect("void main() { print(1); }\nprint(2);\n");
        expectEqual(std::size_t{1}, diagnostics.size(), "single diagnostic");
        expectContains(diagnostics[0].message, "looks like a function call");
    });
}

void testFinalizers() {
    test("finalizers run after the invocation with intact fields", [] {
        expectOutput(R"(
class Resource {
    public string name = "res";
    ~Resource() {
        print("finalize:" + name);
    }
}

void main() {
    Resource a = new Resource();
    a.name = "first";
    print("body");
})",
                     {"body", "finalize:first"});
    });

    test("derived finalizers run before base finalizers", [] {
        expectOutput(R"(
class Base {
    ~Base() { print("base"); }
}
class Middle : Base {
    ~Middle() { print("middle"); }
}
class Leaf : Middle {
    ~Leaf() { print("leaf"); }
}

void main() {
    Leaf leaf = new Leaf();
    leaf = null;
    print("dropped");
    Base plain = new Base();
    plain = null;
})",
                     {"dropped", "leaf", "middle", "base", "base"});
    });

    test("a throwing finalizer does not propagate", [] {
        expectOutput(R"(
class Boom {
    ~Boom() { throw new Exception("boom"); }
}

void main() {
    Boom value = new Boom();
    value = null;
    print("ok");
})",
                     {"ok"});
    });

    test("cycle-collected objects are finalized with intact fields", [] {
        TestEngine harness;
        harness.engine->settings.autoGarbageCollect = false;
        harness.run(R"(
class Node {
    public Node next;
    public string tag = "n";
    ~Node() {
        print("node:" + tag);
    }
}

void main() {
    Node a = new Node();
    Node b = new Node();
    a.tag = "a";
    b.tag = "b";
    a.next = b;
    b.next = a;
    a = null;
    b = null;
    print("dropped");
})");
        expectEqual(std::string("dropped"), harness.output.back());
        harness.engine->garbageCollect();
        std::size_t finalized = 0;
        for (const auto& line : harness.output) {
            if (line == "node:a" || line == "node:b") ++finalized;
        }
        expectEqual(std::size_t{2}, finalized, "both cycle members finalized with their fields");
        expectEqual(std::size_t{0}, harness.engine->gcStatistics().currentAllocated,
                    "cycle reclaimed after finalization");
    });

    test("finalizers are rejected where C# rejects them", [] {
        auto expectDiagnostic = [](const std::string& source, const std::string& code) {
            TestEngine harness;
            std::vector<Diagnostic> diagnostics;
            auto module = harness.engine->tryLoadModule("bad", source, "bad.cs", diagnostics);
            expectTrue(module == nullptr, "expected compilation failure for " + code);
            bool found = false;
            std::string all;
            for (const auto& diagnostic : diagnostics) {
                all += diagnostic.toString() + "\n";
                if (diagnostic.code == code) found = true;
            }
            if (!found) throw std::runtime_error("expected diagnostic " + code + ", got:\n" + all);
        };

        expectDiagnostic("struct S { ~S() { } } void main() { }", "E0451");
        expectDiagnostic("class C { ~C() { } ~C() { } } void main() { }", "E0453");
        expectDiagnostic("class C { ~Other() { } } void main() { }", "E0100");
        expectDiagnostic("class C { ~C(int x) { } } void main() { }", "E0100");
    });
}

void testFinally() {
    test("finally runs on every exit path", [] {
        expectOutput(R"(
int Make(int mode) {
    int value = 1;
    try {
        if (mode == 0) return value;
        if (mode == 1) { value = 5; return value; }
        throw new ArgumentException("inside");
    } catch (ArgumentException e) {
        print("caught:" + e.Message);
        value = 7;
        return value;
    } finally {
        print("finally:" + value);
        value = 100;
    }
}

void main() {
    print(Make(0));
    print(Make(1));
    print(Make(2));

    int i = 0;
    while (i < 4) {
        try {
            i += 1;
            if (i == 2) continue;
            if (i == 4) break;
            print("body:" + i);
        } finally {
            print("loop-finally:" + i);
        }
    }
    print("after:" + i);
})",
                     {"finally:1", "1",        "finally:5", "5",        "caught:inside", "finally:7",
                      "7",        "body:1",    "loop-finally:1", "loop-finally:2", "body:3",
                      "loop-finally:3", "loop-finally:4", "after:4"});
    });

    test("nested finally and try/finally without catch", [] {
        expectOutput(R"(
void main() {
    try {
        try { print("inner"); }
        finally { print("inner-finally"); }
    } finally {
        print("outer-finally");
    }

    try { print("normal"); }
    finally { print("only-finally"); }
})",
                     {"inner", "inner-finally", "outer-finally", "normal", "only-finally"});
    });

    test("exceptions run finally and can be replaced", [] {
        expectOutput(R"(
int Recover() {
    try {
        try { throw new ArgumentException("inner"); }
        finally {
            print("inner-finally");
            throw new InvalidOperationException("from-finally");
        }
    } catch (Exception e) {
        print("caught:" + e.Message);
        return 3;
    }
}

void Thrower() {
    try { throw new ArgumentException("boom"); }
    finally { print("thrower-finally"); }
}

void main() {
    try { Thrower(); }
    catch (ArgumentException e) { print("outer:" + e.Message); }

    print(Recover());
})",
                     {"thrower-finally", "outer:boom", "inner-finally", "caught:from-finally", "3"});
    });

    test("uncaught exceptions still run finally", [] {
        TestEngine harness;
        harness.load(R"(
void main() {
    try { throw new ArgumentException("uncaught"); }
    finally { print("uncaught-finally"); }
})");
        bool threw = false;
        try {
            harness.engine->invoke("test", "main");
        } catch (const ScriptRuntimeException& ex) {
            threw = true;
            expectContains(ex.what(), "uncaught");
        }
        expectTrue(threw, "exception should propagate to the host");
        expectEqual(std::size_t{1}, harness.output.size(), "finally output");
        expectEqual(std::string("uncaught-finally"), harness.output[0]);
    });

    test("finally restrictions are diagnosed", [] {
        auto expectDiagnostic = [](const std::string& source, const std::string& code) {
            TestEngine harness;
            std::vector<Diagnostic> diagnostics;
            auto module = harness.engine->tryLoadModule("bad", source, "bad.cs", diagnostics);
            expectTrue(module == nullptr, "expected compilation failure for " + code);
            bool found = false;
            std::string all;
            for (const auto& diagnostic : diagnostics) {
                if (diagnostic.code == code) found = true;
                all += diagnostic.toString() + "\n";
            }
            if (!found) throw std::runtime_error("expected diagnostic " + code + ", got:\n" + all);
        };

        expectDiagnostic("int F() { try { return 1; } finally { return 2; } } void main() { }", "E0426");
        expectDiagnostic("void main() { while (true) { try { break; } finally { break; } } }", "E0426");
    });
}

void testReferenceParameters() {
    test("ref/out/in parameters", [] {
        expectOutput(R"(
struct Point {
    public int x;
    public int y;
    public Point(int x, int y) { this.x = x; this.y = y; }
}

void Swap(ref int a, ref int b) { int t = a; a = b; b = t; }
void Bump(ref int value) { value += 1; }
void Shift(ref Point p) { p.x += 1; }
void Get(out int value) { value = 42; }
void GetPair(out int number, out string text) { number = 1; text = "x"; }
int Read(in int value) { return value + 1; }

void main() {
    int a = 1;
    int b = 2;
    Swap(ref a, ref b);
    print(a);
    print(b);

    Bump(ref a);
    print(a);

    int[] items = new int[2];
    items[0] = 5;
    Bump(ref items[0]);
    print(items[0]);

    Point p = new Point(1, 2);
    Shift(ref p);
    print(p.x);

    Get(out int value);
    print(value);
    Get(out var inferred);
    print(inferred);

    int number = 0;
    string text = "";
    GetPair(out number, out text);
    print(number);
    print(text);

    print(Read(4));
    int local = 10;
    print(Read(in local));
    print(local);
})",
                     {"2", "1", "3", "6", "2", "42", "42", "1", "x", "5", "11", "10"});
    });

    test("ref parameters can be forwarded", [] {
        expectOutput(R"(
void Inner(ref int value) { value *= 2; }
void Outer(ref int value) { Inner(ref value); value += 1; }
void Global(ref int value) { value = 9; }

int counter = 1;

void main() {
    int local = 5;
    Outer(ref local);
    print(local);

    Global(ref counter);
    print(counter);
})",
                     {"11", "9"});
    });

    test("ref/out/in restrictions are diagnosed", [] {
        auto expectDiagnostic = [](const std::string& source, const std::string& code) {
            TestEngine harness;
            std::vector<Diagnostic> diagnostics;
            auto module = harness.engine->tryLoadModule("bad", source, "bad.cs", diagnostics);
            expectTrue(module == nullptr, "expected compilation failure for " + code);
            bool found = false;
            std::string all;
            for (const auto& diagnostic : diagnostics) {
                if (diagnostic.code == code) found = true;
                all += diagnostic.toString() + "\n";
            }
            if (!found) throw std::runtime_error("expected diagnostic " + code + ", got:\n" + all);
        };

        expectDiagnostic("void F(ref int x) { } void main() { F(1); }", "E0421");
        expectDiagnostic("void F(ref int x) { } void main() { F(ref 1); }", "E0420");
        expectDiagnostic("delegate void D(ref int x); void main() { }", "E0424");
        expectDiagnostic("void main() { int x = 1; print(ref x); }", "E0424");
    });
}

void testLambdas() {
    test("lambda expressions bind to delegate types", [] {
        expectOutput(R"(
delegate int Op(int a, int b);
delegate void Notify(string m);
delegate int Zero();

Op globalAdd = (a, b) => a + b;

int Apply(Op f) { return f(3, 4); }

void main() {
    Op add = (int a, int b) => a + b;
    print(add(2, 3));

    Op mul = (a, b) => a * b;
    print(mul(2, 3));

    Op block = (a, b) => { int t = a - b; return t * 10; };
    print(block(7, 2));

    Zero zero = () => 42;
    print(zero());

    Notify n = m => print("n:" + m);
    n("hi");

    print(Apply((a, b) => a + b));
    print(globalAdd(10, 20));

    Op combined = add;
    combined += (a, b) => a * b;
    print(combined(2, 3));
})",
                     {"5", "6", "50", "42", "n:hi", "7", "30", "6"});
    });

    test("closures capture variables by reference", [] {
        expectOutput(R"(
delegate int Op(int x);

Op MakeAdder(int baseValue) {
    int offset = baseValue;
    return x => x + offset;
}

class Holder {
    public int stored = 100;
    public Op Make() { return x => x + stored; }
}

void main() {
    int total = 0;
    Op add = x => { total += x; return total; };
    print(add(5));
    print(add(2));
    print(total);

    Op adder = MakeAdder(10);
    print(adder(5));

    int shared = 1;
    Op a1 = x => shared + x;
    Op a2 = x => { shared += 1; return shared; };
    print(a1(0));
    print(a2(0));
    print(a1(0));
    print(shared);

    Op[] ops = new Op[3];
    for (int i = 0; i < 3; i++) {
        int copy = i;
        ops[i] = x => x + copy;
    }
    print(ops[0](10));
    print(ops[2](10));

    Holder holder = new Holder();
    Op fromField = holder.Make();
    holder.stored = 7;
    print(fromField(1));
})",
                     {"5", "7", "7", "15", "1", "2", "2", "2", "10", "12", "8"});
    });

    test("closures participate in garbage collection", [] {
        TestEngine harness;
        harness.engine->settings.autoGarbageCollect = false;
        harness.run(R"(
delegate void Ping();

class Node {
    public Ping self;
    public void P() {}
}

void main() {
    Node node = new Node();
    node.self = () => node.P();
})");
        expectEqual(std::size_t{3}, harness.engine->gcStatistics().currentAllocated, "live objects");
        harness.engine->garbageCollect();
        auto stats = harness.engine->gcStatistics();
        expectEqual(std::size_t{3}, stats.lastCycleDetected, "node <-> closure cycle is collected");
        expectEqual(std::size_t{0}, stats.currentAllocated, "allocated after collection");
    });

    test("lambda restrictions are diagnosed", [] {
        auto expectDiagnostic = [](const std::string& source, const std::string& code) {
            TestEngine harness;
            std::vector<Diagnostic> diagnostics;
            auto module = harness.engine->tryLoadModule("bad", source, "bad.cs", diagnostics);
            expectTrue(module == nullptr, "expected compilation failure for " + code);
            bool found = false;
            std::string all;
            for (const auto& diagnostic : diagnostics) {
                if (diagnostic.code == code) found = true;
                all += diagnostic.toString() + "\n";
            }
            if (!found) throw std::runtime_error("expected diagnostic " + code + ", got:\n" + all);
        };

        expectDiagnostic("void main() { var f = (a) => a; }", "E0411");
        expectDiagnostic("delegate int Op(int a); void main() { Op f = (a, b) => a; }", "E0412");
    });
}

void testExceptions() {
    test("try, catch and throw basics", [] {
        expectOutput(R"(
void main() {
    try { throw new Exception("boom"); }
    catch (Exception ex) { print(ex.Message); }

    try { throw new NullReferenceException("n"); }
    catch (ArgumentException e) { print("arg"); }
    catch (NullReferenceException e) { print("null:" + e.Message); }
    catch (Exception e) { print("general"); }

    try { throw new InvalidOperationException("x"); } catch { print("any"); }
    try { throw new Exception("y"); } catch (Exception) { print("typed"); }

    try { print("body"); } catch (Exception e) { print("no"); }
    print("end");
})",
                     {"boom", "null:n", "any", "typed", "body", "end"});
    });

    test("runtime errors are catchable", [] {
        expectOutput(R"(
void main() {
    try { int[] a = null; print(a[0]); }
    catch (NullReferenceException e) { print("null"); }

    try { int[] a = new int[2]; a[5] = 1; }
    catch (IndexOutOfRangeException e) { print("index"); }

    try { print(7 / 0); }
    catch (DivideByZeroException e) { print("div"); }

    try { string s = null; print(s.Length); }
    catch (NullReferenceException e) { print("string-null"); }

    try { int[] a = null; a[0] = 1; }
    catch (Exception e) { print("base"); }

    int x = 42;
    try { x = x + (new int[1])[3]; }
    catch (Exception e) { print(x); }
})",
                     {"null", "index", "div", "string-null", "base", "42"});
    });

    test("nested try and rethrow", [] {
        expectOutput(R"(
void main() {
    try {
        try { throw new InvalidOperationException("inner"); }
        catch (ArgumentException e) { print("wrong"); }
    } catch (Exception e) { print("outer:" + e.Message); }

    try {
        try { throw new NullReferenceException("orig"); }
        catch (Exception e) { e = new Exception("swapped"); throw; }
    } catch (NullReferenceException e) { print("rethrow:" + e.Message); }

    try {
        try { throw new Exception("a"); }
        catch (Exception e) { throw new ArgumentException("b"); }
    } catch (Exception e) { print("replace:" + e.Message); }

    print("done");
})",
                     {"outer:inner", "rethrow:orig", "replace:b", "done"});
    });

    test("exceptions propagate across calls with stack traces", [] {
        expectOutput(R"(
void Level2() { throw new ArgumentException("deep"); }
void Level1() { Level2(); }
void main() {
    try { Level1(); }
    catch (Exception e) {
        print(e.Message);
        print(e.StackTrace.Length > 0);
        print(e.StackTrace.Contains("Level2"));
    }
})",
                     {"deep", "true", "true"});
    });

    test("catch inside loops", [] {
        expectOutput(R"(
void main() {
    for (int i = 0; i < 5; i++) {
        try {
            if (i == 1) continue;
            if (i == 3) break;
            print("i" + i);
        } catch (Exception e) { print("no"); }
    }
    print("after");
})",
                     {"i0", "i2", "after"});
    });

    test("exception restrictions are diagnosed", [] {
        auto expectDiagnostic = [](const std::string& source, const std::string& code) {
            TestEngine harness;
            std::vector<Diagnostic> diagnostics;
            auto module = harness.engine->tryLoadModule("bad", source, "bad.cs", diagnostics);
            expectTrue(module == nullptr, "expected compilation failure for " + code);
            bool found = false;
            std::string all;
            for (const auto& diagnostic : diagnostics) {
                if (diagnostic.code == code) found = true;
                all += diagnostic.toString() + "\n";
            }
            if (!found) throw std::runtime_error("expected diagnostic " + code + ", got:\n" + all);
        };

        expectDiagnostic("void main() { throw; }", "E0406");
        expectDiagnostic("void main() { try { } catch (int e) { } }", "E0407");
        expectDiagnostic("void main() { try { print(1); } }", "E0100");
    });

    test("host code can throw script exceptions", [] {
        expectOutput(R"(
void main() {
    try { Host.Throw("fromhost"); }
    catch (ArgumentException e) { print(e.Message); }
    catch (Exception e) { print("wrong"); }
})",
                     {"fromhost"});
    });

    test("stack overflow guard is catchable", [] {
        expectOutput(R"(
int Recurse(int n) { return Recurse(n + 1); }
void main() {
    try { Recurse(0); }
    catch (StackOverflowException e) { print("overflow"); }
    print("alive");
})",
                     {"overflow", "alive"});
    });

    test("exception type checks and return analysis", [] {
        expectOutput(R"(
namespace N {
    int Run(bool fail) {
        try {
            if (fail) throw new ArgumentException("f");
            return 1;
        } catch (Exception e) {
            print(e is ArgumentException);
            print(e is NullReferenceException);
            ArgumentException specific = e as ArgumentException;
            print(specific != null);
            return 2;
        }
    }
}
void main() {
    print(N.Run(false));
    print(N.Run(true));
})",
                     {"1", "true", "false", "true", "2"});
    });

    test("uncaught exceptions reach the host", [] {
        auto ex = expectRuntimeError(R"(
void Thrower() { throw new ArgumentException("bad"); }
void main() { Thrower(); }
)");
        expectContains(ex.what(), "bad");
        expectContains(ex.format(), "Thrower");
    });
}

void testGarbageCollector() {
    test("acyclic objects are reference counted", [] {
        TestEngine harness;
        harness.engine->settings.autoGarbageCollect = false;
        harness.run(R"(
class Node { public Node next; }
void main() {
    Node a = new Node();
    Node b = new Node();
    a.next = b;
})");
        auto stats = harness.engine->gcStatistics();
        expectEqual(std::size_t{0}, stats.currentAllocated, "allocated before collection");
        expectEqual(std::size_t{2}, stats.totalDestroyed, "destroyed by reference counting");
    });

    test("cycles are collected by the garbage collector", [] {
        TestEngine harness;
        harness.engine->settings.autoGarbageCollect = false;
        harness.run(R"(
class Node { public Node next; }
void main() {
    Node a = new Node();
    Node b = new Node();
    a.next = b;
    b.next = a;
})");
        auto before = harness.engine->gcStatistics();
        expectEqual(std::size_t{2}, before.currentAllocated, "allocated before collection");
        expectTrue(harness.engine->garbageCollect(), "full cycle should finish in one call");

        auto stats = harness.engine->gcStatistics();
        expectEqual(std::size_t{0}, stats.currentAllocated, "allocated after collection");
        expectEqual(std::size_t{2}, stats.lastCycleDetected, "detected garbage objects");
        expectEqual(std::size_t{2}, stats.totalDestroyedByCollector, "destroyed by the collector");
        expectEqual(std::size_t{1}, stats.cycles, "completed cycles");
    });

    test("cycles reachable from globals are preserved", [] {
        TestEngine harness;
        harness.engine->settings.autoGarbageCollect = false;
        harness.run(R"(
class Node { public Node next; }
Node root;
void main() {
    root = new Node();
    Node b = new Node();
    root.next = b;
    b.next = root;
}
void check() { print(root.next.next == root); print(root != null); }
void drop() { root = null; }
)");
        harness.engine->garbageCollect();

        auto stats = harness.engine->gcStatistics();
        expectEqual(std::size_t{0}, stats.lastCycleDetected, "rooted cycle must survive");
        expectEqual(std::size_t{2}, stats.currentAllocated, "live objects");

        harness.engine->invoke("test", "check");
        expectEqual(std::size_t{2}, harness.output.size(), "check output");
        expectEqual(std::string("true"), harness.output[0]);
        expectEqual(std::string("true"), harness.output[1]);

        harness.engine->invoke("test", "drop");
        harness.engine->garbageCollect();
        auto after = harness.engine->gcStatistics();
        expectEqual(std::size_t{2}, after.lastCycleDetected, "cycle collected after the root is gone");
        expectEqual(std::size_t{0}, after.currentAllocated, "allocated after collection");
    });

    test("incremental cycle collection over arrays", [] {
        TestEngine harness;
        harness.engine->settings.autoGarbageCollect = false;
        harness.run(R"(
class Box { public Box[] items; }
void main() {
    Box a = new Box();
    Box b = new Box();
    a.items = new Box[1];
    b.items = new Box[1];
    a.items[0] = b;
    b.items[0] = a;
})");
        expectEqual(std::size_t{4}, harness.engine->gcStatistics().currentAllocated, "live objects");

        int steps = 0;
        while (!harness.engine->garbageCollect(GcFlag::OneStep, 1)) {
            if (++steps > 1000) throw std::runtime_error("incremental GC did not finish");
        }
        expectTrue(steps > 1, "incremental collection should span multiple steps");

        auto stats = harness.engine->gcStatistics();
        expectEqual(std::size_t{0}, stats.currentAllocated, "allocated after collection");
        expectEqual(std::size_t{4}, stats.lastCycleDetected, "detected garbage objects");
    });

    test("collector reclaims many cycles at once", [] {
        TestEngine harness;
        harness.engine->settings.autoGarbageCollect = false;
        harness.run(R"(
class Node { public Node next; }
void main() {
    for (int i = 0; i < 200; i++) {
        Node a = new Node();
        Node b = new Node();
        a.next = b;
        b.next = a;
        Node plain = new Node();
        plain.next = a;
    }
})");
        auto before = harness.engine->gcStatistics();
        expectEqual(std::size_t{400}, before.currentAllocated, "live cycle members");
        expectTrue(harness.engine->garbageCollect(), "full cycle should finish in one call");

        auto stats = harness.engine->gcStatistics();
        expectEqual(std::size_t{400}, stats.lastCycleDetected, "detected garbage objects");
        expectEqual(std::size_t{0}, stats.currentAllocated, "allocated after collection");
    });

    test("host references keep objects alive", [] {
        TestEngine harness;
        harness.engine->settings.autoGarbageCollect = false;
        harness.load(R"(
class Node { public Node next; }
Node make() {
    Node node = new Node();
    node.next = node;
    return node;
})");
        Value held = harness.engine->invoke("test", "make");
        expectTrue(!held.isNull(), "host received the object");

        harness.engine->garbageCollect();
        expectEqual(std::size_t{0}, harness.engine->gcStatistics().lastCycleDetected,
                    "host reference is a root");

        held = Value::null();
        harness.engine->garbageCollect();
        auto stats = harness.engine->gcStatistics();
        expectEqual(std::size_t{1}, stats.lastCycleDetected, "released cycle is collected");
        expectEqual(std::size_t{0}, stats.currentAllocated, "allocated after collection");
    });

    test("automatic collection honours the allocation threshold", [] {
        TestEngine harness;
        harness.engine->settings.autoGarbageCollect = true;
        harness.engine->settings.gcAutoThreshold = 1;
        harness.run(R"(
class Node { public Node next; }
void main() {
    Node a = new Node();
    Node b = new Node();
    a.next = b;
    b.next = a;
})");
        auto stats = harness.engine->gcStatistics();
        expectTrue(stats.cycles >= 1, "threshold should trigger a collection");
        expectEqual(std::size_t{0}, stats.currentAllocated, "allocated after automatic collection");
    });
}

void testCompileErrors() {
    test("syntax error is reported with location", [] {
        TestEngine harness;
        std::vector<Diagnostic> diagnostics;
        auto module = harness.engine->tryLoadModule("bad", "void main() { int x = ; }", "bad.cs", diagnostics);
        expectTrue(module == nullptr, "expected compilation failure");
        bool hasError = false;
        for (const auto& diagnostic : diagnostics) {
            if (diagnostic.severity == DiagnosticSeverity::Error) hasError = true;
        }
        expectTrue(hasError, "expected an error diagnostic");
    });

    test("unknown identifier and type mismatch", [] {
        TestEngine harness;
        std::vector<Diagnostic> diagnostics;
        auto module = harness.engine->tryLoadModule(
            "bad",
            R"(
void main() {
    int x = "text";
    y = 1;
})",
            "bad.cs", diagnostics);
        expectTrue(module == nullptr, "expected compilation failure");
        bool conversion = false;
        bool unknown = false;
        for (const auto& diagnostic : diagnostics) {
            if (diagnostic.code == "E0376" || diagnostic.code == "E0375" || diagnostic.code == "E0381")
                conversion = true;
            if (diagnostic.code == "E0313" || diagnostic.code == "E0359") unknown = true;
        }
        expectTrue(conversion, "expected conversion error");
        expectTrue(unknown, "expected unknown identifier error");
    });

    test("missing return value", [] {
        TestEngine harness;
        std::vector<Diagnostic> diagnostics;
        auto module = harness.engine->tryLoadModule(
            "bad", "int f(int x) { if (x > 0) { return 1; } }", "bad.cs", diagnostics);
        expectTrue(module == nullptr, "expected compilation failure");
        bool found = false;
        for (const auto& diagnostic : diagnostics) {
            if (diagnostic.code == "E0301") found = true;
        }
        expectTrue(found, "expected missing return diagnostic");
    });
}

void testRuntimeErrors() {
    test("division by zero with stack trace", [] {
        auto ex = expectRuntimeError(R"(
int divide(int a, int b) { return a / b; }
void main() { print(divide(1, 0)); }
)");
        expectContains(ex.what(), "division by zero");
        expectContains(ex.stackTrace(), "divide");
        expectContains(ex.stackTrace(), "main");
    });

    test("null reference", [] {
        auto ex = expectRuntimeError(R"(
class Box { public int v = 1; }
void main() { Box b = null; print(b.v); }
)");
        expectContains(ex.what(), "null reference");
    });

    test("array index out of range", [] {
        auto ex = expectRuntimeError(R"(
void main() { int[] a = new int[2]; a[5] = 1; }
)");
        expectContains(ex.what(), "out of range");
    });

    test("call depth limit", [] {
        TestEngine harness;
        harness.engine->settings.maxCallDepth = 64;
        harness.load(R"(
int f(int n) { return f(n + 1); }
void main() { f(0); }
)");
        try {
            harness.engine->invokeEntryPoint("test");
            throw std::runtime_error("expected a runtime error");
        } catch (const ScriptRuntimeException& ex) {
            expectContains(ex.what(), "call depth");
        }
    });
}

void testHotReload() {
    const std::string v1 = R"(
int health = 100;
string label = "v1";

class State {
    public int hits = 0;
    public string tag = "old";
}

State state = new State();

void main() {
    health -= 10;
    state.hits += 1;
    print(label, health, state.hits, state.tag);
}
)";

    const std::string v2 = R"(
int health = 100;
string label = "v2";
int bonus = 42;

class State {
    public int hits = 0;
    public string tag = "old";
    public int extra = 7;
}

State state = new State();

int square(int x) { return x * x; }

void main() {
    state.hits += 1;
    print(label, health, state.hits, state.tag, state.extra, bonus, square(5));
}
)";

    test("reload preserves globals and migrates class state", [v1, v2] {
        TestEngine harness;
        harness.load(v1, "game");
        harness.engine->invokeEntryPoint("game");
        expectEqual(std::string("v1 90 1 old"), harness.output[0]);

        auto result = harness.engine->reloadModule("game", v2, "game.cs");
        expectTrue(result.success, result.summary());
        expectEqual(2, result.module->generation);
        bool health = false;
        bool state = false;
        for (const auto& name : result.migratedGlobals) {
            if (name == "health") health = true;
            if (name == "state") state = true;
        }
        expectTrue(health, "health should be migrated");
        expectTrue(state, "state should be migrated");

        harness.engine->invokeEntryPoint("game");
        expectEqual(std::string("v2 90 2 old 7 42 25"), harness.output[1]);
    });

    test("failed reload keeps previous generation active", [v1] {
        TestEngine harness;
        harness.load(v1, "game");
        harness.engine->invokeEntryPoint("game");

        auto result = harness.engine->reloadModule("game", "void main() { this is not valid }", "game.cs");
        expectTrue(!result.success, "reload should fail");
        expectTrue(!result.diagnostics.empty(), "expected diagnostics");
        expectEqual(1, harness.engine->getModule("game")->generation);

        harness.engine->invokeEntryPoint("game");
        expectEqual(std::string("v1 80 2 old"), harness.output.back());
    });

    test("reload updates function bodies", [] {
        TestEngine harness;
        harness.load(R"(
int compute(int x) { return x + 1; }
void main() { print(compute(10)); }
)",
                     "calc");
        harness.engine->invokeEntryPoint("calc");
        expectEqual(std::string("11"), harness.output[0]);

        auto result = harness.engine->reloadModule("calc", R"(
int compute(int x) { return x * x; }
void main() { print(compute(10)); }
)",
                                                   "calc.cs");
        expectTrue(result.success, result.summary());
        harness.engine->invokeEntryPoint("calc");
        expectEqual(std::string("100"), harness.output[1]);
    });

    test("method bodies are patched in place", [] {
        TestEngine harness;
        harness.load(R"(
int hits = 0;
int Level() { return 1; }
void main() {
    hits = hits + 1;
    print(Level());
})",
                     "patch");
        harness.engine->invokeEntryPoint("patch");
        expectEqual(std::string("1"), harness.output[0]);

        auto result = harness.engine->reloadModule("patch", R"(
int hits = 0;
int Level() { return 7; }
void main() {
    hits = hits + 1;
    print(Level());
    print(hits);
})",
                                                   "patch.cs");
        expectTrue(result.success, result.summary());
        expectEqual(std::string("method-body"), result.strategy);
        expectTrue(result.patchedFunctions > 0, "expected bodies to be patched");
        expectEqual(2, result.module->generation);

        harness.engine->invokeEntryPoint("patch");
        expectEqual(std::string("7"), harness.output[1]);
        expectEqual(std::string("2"), harness.output[2]);   // global survived without migration
    });

    test("delegates created before the reload observe patched bodies", [] {
        TestEngine harness;
        harness.load(R"(
Func<int> probe;
int Level() { return 1; }
void main() {
    probe = Level;
    print(probe());
})",
                     "delegate_patch");
        harness.engine->invokeEntryPoint("delegate_patch");
        expectEqual(std::string("1"), harness.output[0]);

        auto result = harness.engine->reloadModule("delegate_patch", R"(
Func<int> probe;
int Level() { return 42; }
void main() {
    print(probe());
    print(Level());
})",
                                                   "delegate_patch.cs");
        expectTrue(result.success, result.summary());
        expectEqual(std::string("method-body"), result.strategy);

        harness.engine->invokeEntryPoint("delegate_patch");
        expectEqual(std::string("42"), harness.output[1]);   // old delegate, new body
        expectEqual(std::string("42"), harness.output[2]);
    });

    test("added functions are attached to the live module", [] {
        TestEngine harness;
        harness.load(R"(
int Value() { return 1; }
void main() { print(Value()); }
)",
                     "attach");
        harness.engine->invokeEntryPoint("attach");
        expectEqual(std::string("1"), harness.output[0]);

        auto result = harness.engine->reloadModule("attach", R"(
int Value() { return 1; }
int Extra() { return 5; }
void main() {
    print(Value());
    print(Extra());
})",
                                                   "attach.cs");
        expectTrue(result.success, result.summary());
        expectEqual(std::string("method-body"), result.strategy);

        harness.engine->invokeEntryPoint("attach");
        expectEqual(std::string("1"), harness.output[1]);
        expectEqual(std::string("5"), harness.output[2]);
    });

    test("patched bodies may create new closures of existing delegate types", [] {
        TestEngine harness;
        harness.load(R"(
int Apply(Func<int, int> f) { return f(2); }
int Run() { return Apply(x => x + 1); }
void main() { print(Run()); }
)",
                     "closure_patch");
        harness.engine->invokeEntryPoint("closure_patch");
        expectEqual(std::string("3"), harness.output[0]);

        auto result = harness.engine->reloadModule("closure_patch", R"(
int Apply(Func<int, int> f) { return f(2); }
int Run() { return Apply(x => x + 10); }
void main() {
    print(Run());
})",
                                                   "closure_patch.cs");
        expectTrue(result.success, result.summary());
        expectEqual(std::string("method-body"), result.strategy);

        harness.engine->invokeEntryPoint("closure_patch");
        expectEqual(std::string("12"), harness.output[1]);
    });

    test("structural changes fall back to a full module reload", [] {
        TestEngine harness;
        harness.load(R"(
int total = 0;
class Box {
    public int v = 1;
}
void main() {
    total = total + 5;
    Box box = new Box();
    print(box.v + total);
})",
                     "fallback");
        harness.engine->invokeEntryPoint("fallback");
        expectEqual(std::string("6"), harness.output[0]);

        auto result = harness.engine->reloadModule("fallback", R"(
int total = 0;
class Box {
    public int v = 1;
    public int extra = 2;
}
void main() {
    total = total + 5;
    Box box = new Box();
    print(box.v + box.extra + total);
})",
                                                   "fallback.cs");
        expectTrue(result.success, result.summary());
        expectEqual(std::string("module"), result.strategy);
        expectTrue(!result.fallbackReasons.empty(), "expected a fallback reason");
        expectEqual(2, result.module->generation);

        harness.engine->invokeEntryPoint("fallback");
        expectEqual(std::string("13"), harness.output[1]);   // total migrated (5+5), extra added
    });

    test("method-body strategy reports structure changes instead of reloading", [] {
        TestEngine harness;
        harness.engine->settings.reloadStrategy = ReloadStrategy::MethodBody;
        harness.load(R"(
class Box {
    public int v = 1;
}
void main() { print(new Box().v); }
)",
                     "strict");
        harness.engine->invokeEntryPoint("strict");
        expectEqual(std::string("1"), harness.output[0]);

        auto result = harness.engine->reloadModule("strict", R"(
class Box {
    public int v = 1;
    public int extra = 2;
}
void main() { print(new Box().v); }
)",
                                                   "strict.cs");
        expectTrue(!result.success, "structural change must not be patched in place");
        expectTrue(!result.fallbackReasons.empty(), "expected a reason");
        expectEqual(1, harness.engine->getModule("strict")->generation);
    });

    test("reload keeps the script standard library available", [] {
        TestEngine harness;
        harness.load(R"(
Array<int> values = new Array<int>();
void main() {
    values.Add(1);
    print(values.Count);
})",
                     "stdlib_reload");
        harness.engine->invokeEntryPoint("stdlib_reload");
        expectEqual(std::string("1"), harness.output[0]);

        auto result = harness.engine->reloadModule("stdlib_reload", R"(
Array<int> values = new Array<int>();
void main() {
    values.Add(2);
    values.Add(3);
    print(values.Count);
    print(values.IndexOf(3));
})",
                                                   "stdlib_reload.cs");
        expectTrue(result.success, result.summary());
        expectEqual(std::string("method-body"), result.strategy);

        harness.engine->invokeEntryPoint("stdlib_reload");
        expectEqual(std::string("3"), harness.output[1]);   // Add ran twice on the migrated list
        expectEqual(std::string("2"), harness.output[2]);
    });
}

void testBytecode() {
    test("instruction encoding roundtrip", [] {
        std::uint32_t word = encodeInstruction(OpCode::AddI32, 1234);
        auto decoded = decodeInstruction(word);
        expectEqual(static_cast<int>(OpCode::AddI32), static_cast<int>(decoded.op));
        expectEqual(1234, decoded.arg);
        expectEqual(-2, signExtend24(0xFFFFFE));
    });

    test("disassembler produces readable output", [] {
        TestEngine harness;
        auto module = harness.load(R"(
int value = 3;
void main() { print("x" + value); }
)",
                                   "d");
        std::string dump = Disassembler::disassembleModule(*module);
        expectContains(dump, "PushConst");
        expectContains(dump, "CallNativeVariadic");
        expectContains(dump, "Ret");
        expectContains(dump, "value");
    });

    test("compiled function has code, constants and line map", [] {
        TestEngine harness;
        auto module = harness.load(R"(
void main() {
    string s = "hello";
    print(s.Length);
}
)",
                                   "d");
        auto* mainFunction = module->entryPoint;
        expectTrue(mainFunction != nullptr, "expected entry point");
        expectTrue(!mainFunction->code.empty(), "expected bytecode");
        expectTrue(!mainFunction->constants.empty(), "expected constants");
        expectEqual(mainFunction->code.size(), mainFunction->lineMap.size(), "line map length");
        expectTrue(mainFunction->localSlotCount >= 1, "expected locals");
    });
}

}  // namespace

int main() {
    std::cout << "SharpScript C++17 test suite" << std::endl;
    testArithmetic();
    testConversions();
    testStrings();
    testCSharpStringSemantics();
    testControlFlow();
    testFunctions();
    testGlobals();
    testClasses();
    testArrays();
    testNativeBindings();
    testNamespaces();
    testStructValueTypes();
    testDelegates();
    testLambdas();
    testReferenceParameters();
    testGenerics();
    testAdvancedGenerics();
    testFuncAction();
    testScriptStandardLibrary();
    testSections();
    testInterfaces();
    testProperties();
    testDiagnosticQuality();
    testFinalizers();
    testFinally();
    testExceptions();
    testGarbageCollector();
    testCompileErrors();
    testRuntimeErrors();
    testHotReload();
    testBytecode();

    std::cout << "\n" << g_passed << " passed, " << g_failed << " failed" << std::endl;
    for (const auto& failure : g_failures) std::cout << "FAILED: " << failure << std::endl;
    return g_failed == 0 ? 0 : 1;
}
