# SharpScript (C++17)

本项目代码全部由AI生成

一个**对标 AngelScript** 思路的脚本虚拟机，**完全使用 C++17 实现**：
类 C# 语法的脚本语言、**自定义 32 位字节码**、栈式解释执行、原生 C++ 绑定，以及支持**状态迁移的热重载**。

- 不依赖任何第三方库（只使用 C++17 标准库）
- 构建系统：CMake（Windows / MSVC，Linux / GCC、Clang 均可）
- 脚本语言：C# 风格（`class` / `struct` / `delegate` / lambda / `ref/out/in` / `namespace` / `using` / `foreach` / `as` / `is` / `string` / `T[]` ...）
- VM 实现语言：**C++17**（不是 C#）

```text
脚本源码 (.ss)
   │  Tokenizer            词法分析
   │  Parser → AST         递归下降语法分析
   │  ModuleCompiler       声明收集 / 类布局 / 虚表 / 类型检查
   │  ScriptBuilder        发射 32 位指令 + 常量池 + 重定位
   ▼
ScriptModule (generation N)
   │  ExecutionContext     栈式解释器（每个脚本调用一个 C++ 帧）
   ▼
宿主 C++（原生类型、全局函数、热重载事件）
```

---

## 快速开始

### Windows (MSVC)

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

build\Release\sharpscript_cli.exe run samples/hello.ss
build\Release\sharpscript_cli.exe run samples/oop.ss
build\Release\sharpscript_cli.exe run samples/native.ss
build\Release\sharpscript_cli.exe disasm samples/hello.ss
build\Release\sharpscript_cli.exe reload samples/hotreload.ss samples/hotreload.v2.ss
build\Release\sharpscript_cli.exe watch samples/hotreload.ss
build\Release\sharpscript_cli.exe repl

build\Release\sharpscript_tests.exe
```

### Linux / macOS

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/sharpscript_cli run samples/hello.ss
./build/sharpscript_tests
```

> `watch` 使用 `std::filesystem::last_write_time` 轮询检测文件变化，跨平台无需额外依赖。

---

## 目录结构

```text
include/sharpscript/
  common.hpp           诊断、异常、公共头
  value.hpp            Value 标签联合、ScriptInstance、ScriptArray、ValueOps
  gc.hpp               GcObject、GcCollector（引用计数 + 增量环检测）、GcStatistics
  exception.hpp        ScriptExceptionInfo（异常载荷）与 ScriptAbort（帧内展开信号）
  type.hpp             类型系统、脚本类/原生类、虚表、NativeFunction
  function.hpp         ScriptFunction、全局变量描述
  module.hpp           ScriptModule（generation 版本化）
  engine.hpp           ScriptEngine、ReloadResult、注册 API
  context.hpp          ExecutionContext
  tokenizer.hpp        Token / Tokenizer
  ast.hpp              AST 节点
  parser.hpp           递归下降解析器
  bytecode.hpp         32 位指令编码/解码
  builder.hpp          ScriptBuilder（常量池、标签回填、重定位）
  compiler.hpp         ModuleCompiler
  function_compiler.hpp FunctionCompiler
  disassembler.hpp     反汇编
  hot_reload.hpp       热重载状态迁移
src/
  value.cpp gc.cpp type.cpp tokenizer.cpp parser.cpp
  builder.cpp compiler.cpp function_compiler.cpp
  module.cpp engine.cpp context.cpp native.cpp
  stdlib.cpp method_reload.cpp hot_reload.cpp disassembler.cpp
lib/array.ss           脚本标准库（构建期嵌入，见 Array<T> 一节）
cli/main.cpp           run / watch / reload / disasm / repl
tests/main.cpp         控制台测试运行器（128 个用例）
samples/               示例脚本
```

---

## 脚本语言

### 类型

`bool` `byte` `sbyte` `short` `ushort` `int` `uint` `long` `ulong` `float` `double` `char` `string` `void`
`T[]`（数组），`var`（局部类型推断），脚本 `class`（引用类型），脚本 `struct`（值类型），
脚本 `delegate`（委托类型），宿主注册的原生类型。

> `char` 是一个 **UTF-16 code unit**（与 C# 一致，16 位宽），`string` 内部是 `std::u16string`。
> 长度、索引、`Substring`、`IndexOf` 等一律按 **UTF-16 code unit** 计算，
> 因此 `"你好A😀".Length == 5`（emoji 占两个 code unit）。
> 非 BMP 字符（如 emoji）不能作为 `char` 字面量，会报编译错误 `E0014`；`\uXXXX` 转义正常解码。
> 字符串进入/离开 C++ 宿主时通过 `sharpscript/unicode.hpp` 的 `utf16ToUtf8` / `Value::stringUtf8` 转换。

### 声明

```csharp
int counter = 0;
const int limit = 10;
string label = "count=";

class Shape
{
    public string name = "shape";          // 实例字段 + 字段初始化器
    public static int created = 0;         // 静态字段（编译为模块全局槽）

    public Shape() { created += 1; }       // 构造函数
    public virtual double area() { return 0.0; }
    public string describe() { return name + " area=" + area(); }  // 隐式 this
    public static int total() { return created; }
}

class Circle : Shape                    // 单继承，基类无参构造自动链式调用
{
    public double radius;
    public Circle(double r) { radius = r; name = "circle"; }
    public override double area() { return 3.14159 * radius * radius; }
}
```

脚本类的方法默认是虚方法：调用点使用 `CallVirt` + 虚表槽，派生类按签名覆盖同一槽位。

### 结构体（`struct`，值类型）

```csharp
struct Point
{
    public int x;
    public int y;

    public Point(int x, int y) { this.x = x; this.y = y; }
    public void Shift(int d) { x += d; y += d; }
    public int Sum() { return x + y; }
}

Point global = new Point(1, 2);

void main()
{
    Point a = new Point(1, 2);
    Point b = a;              // 拷贝：b 与 a 是两个独立的值
    b.x = 100;                // a.x 仍为 1

    Point[] list = new Point[2];
    list[0] = a;              // 存入副本
    list[0].Shift(1);         // 修改数组元素本身
    Point c = list[0];        // 取出副本
}
```

C# 的值语义：

- **复制边界**：局部/全局赋值、函数传参、返回值、字段与数组元素写入、`foreach` 迭代变量、`push` 都会复制；
- **共享的部分**：struct 内部的 `class` / 数组 / `string` 引用字段复制的是引用（浅层共享），与 C# 一致；
- **不可为 null**：`S s = null;` 是编译错误（`E0374`），`S` 与 `class` 之间不能互相转换；
- **没有继承/虚派发**：struct 不能有基类、不能被继承，方法不能 `abstract`/`virtual`/`override`，调用点总是静态派发；
- **不能包含自身**：直接或间接的值包含会在编译期报 `E0400`（`struct Node { Node next; }` 非法）；
- `new S()` 先把所有字段零初始化，再执行构造函数与字段初始化器；未赋值的字段保持零值；
- 支持静态字段/静态方法与命名空间（`namespace Game { struct Point { ... } }`）；
- `==` / `!=` 按**值语义逐字段比较**（嵌套 struct 递归比较），引用字段按引用身份比较；不支持 `<`/`>`，也没有 `is`、`as`、强制转换、`switch`（本语言暂不支持运算符重载）。

实现方式：struct 的存储是**装箱的 `ScriptStructValue`**（GC 对象，引用字段可被环检测遍历），
复制点由 `CloneStruct` 指令生成独立副本，嵌套 struct 递归复制；数组/字段读取返回存储中的 box，
因此 `arr[i].x = 1`、`obj.field.Method()` 会修改真实存储，而 `var copy = obj.field;` 得到副本。

> 暂未实现：`readonly struct`、`ref`/`in` 参数、`object` 装箱（`object` 类型本身尚未支持）、自定义运算符与 `ToString` 重载。

### 命名空间（C# 风格）

```csharp
namespace Game.Core
{
    int score = 10;                     // 命名空间内的全局变量
    int Add(int a, int b) { return a + b; }

    class Player
    {
        public string name = "p";
        public string Label() { return "player:" + name; }
    }

    namespace AI                        // 嵌套命名空间（等价于 Game.Core.AI）
    {
        int speed = 3;
        int Speed() { return speed + 1; }
    }
}

using Game.Core;                        // 导入命名空间成员
```

- 命名空间是**纯编译期概念**：函数、全局变量、类都登记为限定名（`Game.Core.Add`、`Game.Core.score`、`Game.Core.Player`），字节码、GC、解释器都没有额外开销。
- 非限定名按「当前命名空间 → 逐层外层命名空间 → `using` 导入 → 全局」查找，所以内层可以直接用外层的名字；同名的类/函数只要分属不同命名空间就不冲突。
- 限定访问用 C# 的点号：`Game.Core.Add(1, 2)`、`Game.Core.score`、`new Game.Core.Player()`、`x is Game.Core.Player`、`Game.Core.Player.StaticMethod()`。
- `using A.B;` 写在文件顶层时对整个文件生效；写在命名空间块内时只对该命名空间及其嵌套命名空间生效（不会泄漏到兄弟命名空间）。
- 同时支持 `namespace A.B.C { ... }` 点号声明、块嵌套，以及 C# 10 的 file-scoped 形式 `namespace A.B;`。
- 入口点 `main` 要求在全局命名空间；宿主按限定名访问脚本，例如 `engine.invoke("game", "Game.Core.Add", {...})`、`module->findClass("Game.Core.Player")`。
- 热重载按限定名为全局变量配对，`result.migratedGlobals` 里出现的是 `Game.Core.score` 这样的全名。
- 宿主注册的原生类型/全局函数仍处于全局命名空间，脚本可以写包装函数把它们纳入自己的命名空间；暂不支持 `using static` 与 `using X = A.B;` 别名。

### 多文件模块（section）

一个模块可以由多个源文件组成，它们**共享同一个全局命名空间**（AngelScript 的 `AddScriptSection` + `Build` 模型）：

```cpp
engine.loadModule("game", {
    {"player.ss", playerSource},
    {"main.ss",   mainSource},
});
```

```csharp
// player.ss
namespace Game { class Player { public string name = "hero"; } }

// main.ss —— 不需要 import，也不需要额外声明
using Game;
void main() { Player p = new Player(); print(p.name); }
```

- 声明顺序无关：编译是先收集全部声明（类、委托、函数）再解析、再编译函数体，所以 `main.ss` 可以引用 `player.ss` 里稍后出现的类型，反之亦然。
- `ScriptSection{file, source, lineOffset}`：`file` 用于诊断与异常定位，`lineOffset` 让“从大文件里截出来的片段”继续报告原始行号。
- 诊断按 section 反查：`error("player.ss(12,5): ...")`，不再是整个模块一个文件名。
- 重载接受同样形式的 section 列表：`engine.reloadModule("game", sections)`；方法体热更与 section 数量无关，改一个文件仍然只替换变化的函数体。
- CLI 支持多文件：`sharpscript_cli run main.ss player.ss util.ss`（第一个文件决定模块名）。
- 标准库本身也是一个 section（名为 `lib/array.ss`，见 `Array<T>` 一节），因此它的诊断不会污染用户文件的行号。
- CLI 的 `--no-stdlib` 可以关掉内置库：用来单独校验 `lib/array.ss`（`sharpscript_cli --no-stdlib disasm lib/array.ss`），
  或换成自己的实现。
- `ScriptModule::sections` / `sectionStartLines` 保留了来源信息，宿主可用 `module.fileOfLine(line)` 反查某行属于哪个文件。
- 限制：`using` 目前是**模块级**的（不区分 section），比 C# 的“文件级 `using`”略宽松；section 不提供独立编译或独立回退，结构变化仍然整模块重载。

### 异常处理（`try` / `catch` / `throw`）

```csharp
void main()
{
    try {
        int[] items = null;
        items[0] = 1;
    }
    catch (NullReferenceException e) {
        print("null: " + e.Message);
    }
    catch (Exception e) {
        print("other: " + e.Message);
    }

    try {
        try { throw new ArgumentException("bad"); }
        catch (ArgumentException e) { throw; }        // 重抛原始异常
    } catch (Exception e) {
        print(e.Message);                             // bad
        print(e.StackTrace.Length > 0);               // true
    }
}
```

- 语句形式：`try { } catch (T e) { }`、`catch (T)`、`catch`（兜底）、`finally { }`（可与 catch 组合，也可单独使用）、
  `throw expr;`、`throw;`。
- `finally` 在**所有出口**执行：正常结束、异常被本层 catch 捕获、异常未被捕获而向外传播、`return`、`break`、`continue`；
  嵌套时按“内层 → 外层”执行，`return` 的返回值在进入 finally 前求值并保存，finally 修改变量不会改变已保存的返回值。
- finally 中抛出的异常**取代**正在传播的异常或待执行的 return；finally 内的异常可以被 finally 里的 try/catch 捕获并继续完成。
- 实现方式对齐 C#/IL：`finally` 是独立的 EH 子句，离开保护区用 `Leave`（触发跨越的 finally），finally 末尾是 `EndFinally`
  （恢复被打断的控制转移）；`return` 由函数末尾的 epilogue 完成（先保存值 → `Leave` → 载入 → `Ret`）。
- `finally` 内不允许 `return`，也不允许跳出 finally 的 `break`/`continue`（`E0426`），与 C# `CS0157` 的约束一致。
- 异常类型是引擎内置的**原生类型层次**：`Exception` ← `NullReferenceException`、`IndexOutOfRangeException`、
  `DivideByZeroException`、`ArgumentException`、`InvalidCastException`、`InvalidOperationException`、`StackOverflowException`。
  用 `new Exception("msg")` / `new ArgumentException("msg")` 构造，提供 `Message`、`StackTrace` 属性和 `ToString()`。
- `catch` 按源码顺序匹配，依据原生类型的继承链；内层 try 的 catch 不匹配时继续向外层传播。
- `throw` 只接受 `Exception` 及其派生（`throw null;` 在运行时按 `NullReferenceException` 处理）；
  `throw;` 只能出现在 `catch` 块内，重抛**原始**异常，类型/消息/栈不受 catch 变量重新赋值影响。
- 解释器自身的运行时错误会映射为对应的异常，因此可以直接 catch：

| 运行时错误 | 异常类型 |
|---|---|
| 空引用（字段/方法/字符串/数组） | `NullReferenceException` |
| 数组越界、索引非法 | `IndexOutOfRangeException` |
| 整数/浮点除零、取模零 | `DivideByZeroException` |
| 类型转换失败 | `InvalidCastException` |
| 递归/指令/原生栈预算超限 | `StackOverflowException` |
| 其他（参数、非法调用等） | `Exception` 或 `ArgumentException` |

- 异常对象在**首次抛出**时记录脚本调用栈（`StackTrace`，跨函数的多帧信息）；`throw;` 会保留原栈。
- 未捕获的异常在宿主边界转换为 `ScriptRuntimeException`（`what()`/`format()` 带消息、行号与脚本调用栈），
  既有宿主代码无需改动。
- 宿主主动抛异常：`context.throwException(context.engine().createException("ArgumentException", "msg"))`；
  原生回调抛出的 `std::exception` 会被包装成 `Exception` 并可被脚本 catch。
- 限制：不支持异常过滤器 `when (...)`、以及 `class MyException : Exception`
  这类自定义异常类（脚本类不能继承原生类型）。

### 属性（`get` / `set`）

```csharp
class Player {
    private int hp = 100;

    public int Health {                       // 显式访问器
        get { return hp; }
        set { if (value < 0) hp = 0; else hp = value; }
    }
    public string Name { get; set; }          // 自动属性
    public int Level { get; }                 // 只读自动属性（可在构造函数里赋值）
    public static int Instances { get; set; } // 静态属性

    public Player() { Level = 1; Name = "unnamed"; }
}

void main() {
    Player p = new Player();
    p.Name = "hero";
    p.Health -= 30;          // 复合赋值：先 get 再 set
    print(p.Name + " " + p.Health + " " + p.Level);
    Player.Instances += 1;
}
```

- `set` 里的 `value` 是隐式参数名；`get`/`set` 都可省略其中一个（只写 `set` 即只写属性）。
- **实现方式**：属性在解析阶段被**降级为普通成员**——访问器编译成方法 `get_<Name>` / `set_<Name>`，自动属性额外生成后备字段 `__<Name>`。
  因此属性天然获得方法派发、继承、泛型单态化、热重载（方法体补丁）等已有能力，VM 与字节码层没有任何属性指令。
- 类内部可以直接写属性名（`Health = Health - 30;`），等价于 `this.Health`；静态属性用 `Cls.Name` 访问。
- 只读自动属性（`{ get; }`）允许在**构造函数**里赋值（写入后备字段），与 C# 一致；在其它地方赋值报 `E0455`。
- 诊断：只写属性被读取 `E0454`、只读属性被赋值 `E0455`、同名属性重复声明 `E0456`。
- 限制：不支持表达式体访问器（`=>`）、自动属性初始化器（`{ get; set; } = 5;`）、`init`、接口属性；
  访问器上的修饰符（如 `private set`）会被解析但**不做访问控制**——本语言目前对字段/方法也不强制执行 `private`。

### 泛型类 / 泛型 struct

```csharp
class Box<T> {
    public T value;
    public Box(T value) { this.value = value; }
    public T Get() { return value; }
}

struct Pair<T, U> {
    public T first;
    public U second;
    public Pair(T first, U second) { this.first = first; this.second = second; }
}

class Node<T> {
    public T value;
    public Node<T> next;
    public Node(T value) { this.value = value; }
}

void main() {
    Box<int> a = new Box<int>(5);
    Box<string> s = new Box<string>("hi");
    Pair<int, string> p = new Pair<int, string>(1, "x");
    Node<int> head = new Node<int>(1);
    head.next = new Node<int>(2);
    Box<Box<int> > nested = new Box<Box<int> >(a);
}
```

- 采用**单态化**：`Box<int>`、`Box<string>` 在编译期生成各自独立的类型、字段布局、方法、vtable 与静态字段（`Box<int>.total` 与 `Box<string>.total` 互不影响）。
- 支持泛型类、泛型 struct、命名空间内的泛型、递归泛型（`Node<T> next`）、嵌套泛型（`Box<Box<int> >`，嵌套时用 `> >` 以避开 `>>` 词法）。
- 嵌套泛型的类型名即规范名（`Box<int>`、`Pair<int, string>`），宿主/诊断/反汇编里看到的就是它。
- **运行时零改动**：实例化产物是普通 `ScriptClassType`，字节码没有泛型指令，GC/热重载沿用既有机制。

### 泛型方法

```csharp
T Max<T>(T a, T b) { return a > b ? a : b; }

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
    print(Max(3, 7));            // 类型实参自动推断：T = int
    print(Max<int>(11, 4));      // 也可以显式写出来
    print(Calc.Twice<string>("ab"));
}
```

- 泛型方法同样是**按调用点单态化**：每个 `T` 组合生成一份独立的普通函数，函数体内的一切（算术、`new`、字段访问）都按具体类型编译，因此 `Max<int>` 与 `Max<string>` 互不影响。
- 类型实参**从实参类型推断**（含 `ref`/`out` 实参与嵌套泛型 `Box<T>` 参数），推断不出来时用显式写法 `F<int>(...)`；两者都不满足会报 `E0436`。
- 非泛型重载优先：`int Twice(int)` 与 `T Twice<T>(T)` 同时存在时，`Twice(2)` 选中非泛型版本；显式写了类型实参则一定走泛型版本。
- 泛型方法**总是静态分派**（不占 vtable），因此不能声明为 `abstract`/`override`（`E0444`）。
- 实例化数量有上限（`E0437`），避免 `F<T>` 里再调用 `F<Box<T>>` 这类无限展开。

### 类型约束

```csharp
class Pool<T> where T : class, new() { }
class Cell<T> where T : struct { }
string NameOf<T>(T item) where T : Animal { return item.name; }
```

- 支持 `where T : class`（引用类型）、`struct`（不可空值类型）、`new()`（有无参构造函数）、以及基类/具体类型约束；多个约束用逗号分隔，多个类型参数各写一个 `where`。
- 约束在**实例化时**校验，违反报 `E0435`（例如 `Box<int>` 违反 `where T : class`）。

### 泛型委托与变型

```csharp
delegate TResult Convert<in T, out TResult>(T value);
delegate T Producer<out T>();
delegate void Consumer<in T>(T value);

class Animal { public string name = "animal"; }
class Dog : Animal { public Dog() { name = "dog"; } }

void main() {
    Producer<Dog> dogFactory = () => new Dog();
    Producer<Animal> animalFactory = dogFactory;   // 协变：out T

    Consumer<Animal> animalConsumer = (Animal item) => { print(item.name); };
    Consumer<Dog> dogConsumer = animalConsumer;    // 逆变：in T
    dogConsumer(new Dog());

    Convert<int, string> render = (int value) => { return "v=" + value; };
    print(render(3));
}
```

- 泛型委托与泛型类一样按实例单态化（`Convert<int, string>` 是独立的委托类型），可作为字段/参数/返回值/局部变量类型，lambda 与方法组都能转换过去。
- `out T` 只允许出现在输出位置（返回值），`in T` 只允许出现在输入位置（参数），否则报 `E0441`/`E0442`。变型标注**只允许出现在委托的类型参数上**，写在 class 上报 `E0440`（与 C# 一致，本语言没有 interface）。
- 有了这两条限制，`Producer<Dog> → Producer<Animal>`、`Consumer<Animal> → Consumer<Dog>` 在运行时是类型安全的，因此转换是**零开销**的（不生成适配器，字节码无额外指令）；方向写反（如 `Producer<Animal> → Producer<Dog>`）按普通不可转换处理，报 `E0381`。
- 限制：不支持泛型方法组（`Convert<int,string> c = SomeGeneric<int>`）、不支持泛型委托的自定义变型组合（如 `Box<in T>`）。
- 注意：变型转换后的委托值在运行时仍记着**原始**委托类型，`+=`/`-=` 组合要求两侧运行期类型完全一致，跨变型组合会报运行时错误。

### 泛型的其它限制

- 在类外部写 `Counter<int>.total` 这类“泛型类型静态访问”暂未支持（实例内部可以直接写 `total`）。
- 词法限制：连续 `>>` 会被 tokenizer 当作右移，嵌套泛型请写 `Box<Box<int> >`。

### 委托与 Lambda（C# 风格）

```csharp
delegate int Op(int a, int b);
delegate void Notify(string message);

class Calc
{
    public int bias = 10;
    public int Add(int a, int b) { return a + b + bias; }
    public static int Mul(int a, int b) { return a * b; }
}

Calc c = new Calc();
Op f = (a, b) => a + b;          // lambda
Op g = Calc.Mul;                 // 静态方法组
Op h = c.Add;                    // 实例方法组
Notify log = m => print(m);      // void lambda

Op both = f + g;                 // 多播
both += (a, b) => a * b;
both -= g;
print(both(2, 3));               // 最后一个入口的返回值
both.Invoke(2, 3);
```

- `delegate R Name(params);` 声明委托类型，可放在模块或命名空间；委托是引用类型，可以是 `null`。
- 方法组：`obj.Method`、`Type.StaticMethod`、裸函数名 `Handler`，在期望类型是委托时自动转换，并按签名选择重载。
- 多播：`+`/`-`/`+=`/`-=` 按 C# 的不可变组合语义生成新委托；调用时按顺序执行，返回值取最后一个入口；`null` 委托被调用时抛 `NullReferenceException`（可被 catch）。
- `d(args)` 与 `d.Invoke(args)` 等价；`==`/`!=` 比较调用列表（函数 + 接收者），`d == null` 合法。
- lambda：`x => expr`、`(a, b) => expr`、`(int a, int b) => expr`、`() => expr`、块体 `(a) => { ... }`；参数类型可由目标委托推断。
- lambda 必须出现在委托类型的上下文中（`var f = (a) => a;` 报 `E0411`），参数个数不匹配报 `E0412`。
- **闭包按引用捕获**：lambda 引用外层局部变量/参数时，这些变量被提升为堆 cell，lambda 内外读写同一份存储；
  实例方法里的 lambda 还会捕获 `this`（通过委托的 receiver 传递）。
- `for` / `foreach` 的循环变量按迭代创建 cell，`ops[i] = x => x + i` 每次迭代捕获独立变量（C# 5+ 行为）。
- 实现：`ScriptDelegate` / `ScriptCell` 都是 `GcObject`，指令 `NewDelegate`、`CallDelegate`、`DelegateAdd`、`DelegateSub`、
  `NewCell`、`LoadCell`、`StoreCell`；GC 会遍历委托的 target 与 captures，因此 `class -> delegate -> cell -> class` 的环可被回收。
- 限制：不支持 `event`、`async/await`、表达式树、`ref/out` 捕获、匿名方法 `delegate(...) { }`、`GetInvocationList`；原生方法不能直接转成委托（需要脚本包装）。

### 内置 Func / Action

无需手写 `delegate` 声明，也无需导入库；短名和 `System.Func` / `System.Action` 指向同一委托类型，支持 `using System;`。

| 类型 | 输入参数 | 返回值 |
| --- | --- | --- |
| `Action` | 无 | `void` |
| `Action<T1, ..., T16>` | 1～16 个，各类型实参对应一个输入参数 | `void` |
| `Func<TResult>` | 无 | `TResult` |
| `Func<T1, ..., T16, TResult>` | 1～16 个，最后一个类型实参是返回类型 | `TResult` |

```csharp
int Square(int x) { return x * x; }
Func<int, int> MakeAdder(int offset) { return x => x + offset; }

void main() {
    Func<int> answer = () => 42;
    Func<int, int> square = new Func<int, int>(Square);
    Func<int, int, int> sum = (a, b) => a + b;
    Action log = () => print("ready");
    Action<string> output = message => print(message);
    System.Func<int, int> add = MakeAdder(10);

    log();
    output("hello");
    print(answer.Invoke());
    print(square(5));
    print(sum(3, 4));
    print(add(2));
}
```

- 支持隐式/显式参数 lambda、脚本静态/实例方法组、`new Func<...>(callable)` / `new Action(callable)`、字段、参数、返回值、数组和闭包；直接调用与 `.Invoke()` 等价，其结果类型也参与重载和泛型实参推断。
- `Action` 表达式体必须是调用、赋值、自增/自减或对象创建等语句表达式；`Action a = () => 42` 报 `E0414`。调用表达式可丢弃返回值，但不能把返回非 `void` 的方法组赋给 `Action`。
- 显式 lambda 参数必须与目标参数类型完全相同，不匹配报 `E0413`。方法组允许参数的引用逆变和返回值的引用协变，并优先匹配参数类型精确的重载。
- `Func` 的输入参数逆变（`in`），返回类型协变（`out`）；泛型 `Action` 的输入参数全部逆变。变型仅适用于引用类型，值类型实参必须相同：`Func<int>` 不能转换为 `Func<double>`，虽然普通数值表达式可以提升。
- 裸 `Func` 缺少返回类型报 `E0431`；超过参数上限报 `E0432`；`void`、`var`、`null` 不能作为类型实参（`E0445`）。自定义委托即使签名相同，也不会自动与 `Func` / `Action` 互转。
- 支持既有委托的 `+` / `-` / `+=` / `-=`、相等比较和空委托调用诊断；跨变型的多播组合仍要求运行期委托类型一致。
- 用户声明的同名类型可遮蔽短名；内置的 `System.Func` / `System.Action` 名称保留。泛型方法组和原生方法组仍需脚本包装；嵌套泛型仍需以 `> >` 分隔，未实现表达式树或 `async`。
- 实现仅在编译期创建普通 `ScriptDelegateType` 签名，复用既有委托字节码和 GC，没有新增运行时指令。

### ref / out / in 参数

```csharp
void Swap(ref int a, ref int b) { int t = a; a = b; b = t; }
void Get(out int value) { value = 42; }
int Read(in int value) { return value + 1; }

void Shift(ref Point p) { p.x += 1; }

void main() {
    int a = 1, b = 2;
    Swap(ref a, ref b);                 // 调用点必须写 ref
    Bump(ref a);
    Get(out int value);                 // out <type> name
    Get(out var inferred);              // out var
    print(Read(4));                     // in 实参可省略修饰符（右值会复制到临时 cell）
    Bump(ref items[0]);                 // 数组元素
    Shift(ref p);                       // struct 按引用传递，避免装箱复制
}
```

- `ref`：可读写别名；调用点必须写 `ref`，实参必须是**可寻址存储**（局部变量、全局、字段、数组元素），不能是常量/方法返回值。
- `out`：被调函数负责写入；调用点写 `out x`、`out var x` 或 `out Type x`（声明形式会新声明一个局部变量）。
- `in`：只读引用；被调函数内赋值报 `E0423`，调用点写 `in x` 或省略修饰符（右值会复制到临时 cell）。
- 支持在脚本函数/方法/构造函数之间转发：`void Outer(ref int v) { Inner(ref v); }` 会传递同一个引用。
- 实现：`ScriptRef`（Cell / Global / Field / Elem 四种存储）+ 字节码 `MakeRefCell`、`MakeRefGlobal`、`MakeRefField`、`MakeRefElem`、`LoadRef`、`StoreRef`；
  局部变量在含 `ref/out/in` 实参的函数中会提升为 cell，从而拥有稳定地址。
- 限制：不支持 `ref` 返回、`ref` 局部变量（`ref int r = ref x`）、`ref struct`/`Span`；
  委托、lambda 与原生函数的参数不能带 `ref/out/in`（`E0424`）；暂未做 C# 的确定赋值分析（`out` 参数未赋值时读到的是调用方旧值）。

### 语句

`{ }`、`if/else`、`while`、`do/while`、`for`、`foreach (var x in arrayOrString)`、
`switch/case/default`、`break/continue`、`return`、`try/catch/finally`、`throw`、局部变量声明、表达式语句。

### 表达式

算术 / 位运算 / 移位 / 比较 / 逻辑短路、三元 `?:`、`++` `--`（前缀/后缀）、
复合赋值（`+=` `-=` `*=` `/=` `%=` `&=` `|=` `^=` `<<=` `>>=`）、
`new T(...)`、`new T[n]`、`new T[] { ... }`、`obj.field`、`arr[i]`、方法调用、
原始类型显式转换 `(int)x`、引用类型 `as` / `is`、`null`。

### 字符串

`Length` 属性；`Substring` `IndexOf` `LastIndexOf` `Contains` `StartsWith` `EndsWith`
`ToUpper` `ToLower` `Trim` `Replace` `Split` `IsEmpty` `ToString`（C# 风格的 PascalCase 命名）。
`+` 遇到任意一边是 `string` 时自动把另一边转成字符串；`null` 参与拼接时视为空串。
`string.Empty`、`string.IsNullOrEmpty(s)` 也可用（后者在编译期内联）。
`null` 字符串的成员访问（如 `s.Length`）会抛出 `null reference` 运行时错误。

### 数组

`T[]` 只保留**存储层**能力：`new T[n]` / `new T[] { ... }`、`a[i]`、`Length` / `Count`（兼容小写 `length` / `count`）、
`push` / `pop`（就地增长/收缩，对应两条 `ArrayPush` / `ArrayPop` 指令）。
越界、空数组 `pop`、空引用都会抛出带脚本调用栈的运行时错误。

集合算法（`insert` / `removeAt` / `clear` / `indexOf` / `contains`）**不是数组的一部分**，
而是在脚本标准库的 `Array<T>` 里实现的——与 C# 一致（`T[]` 定长，`List<T>` 才有 `Add`/`Insert`）。
在数组上调用它们会报 `E0446` 并提示迁移。

### Array<T>（脚本标准库）

标准库是仓库里的**真实脚本文件** `lib/array.ss`：CMake 在配置期把它嵌入生成的
`stdlib_source.hpp`（二进制自包含），引擎再把它作为名为 `lib/array.ss` 的 section 追加到每个模块。
目前提供 `Array<T>`：

```csharp
void main() {
    Array<int> numbers = new Array<int>();
    numbers.Add(3);
    numbers.Add(5);
    numbers.Insert(1, 4);      // 3, 4, 5
    print(numbers.Count());    // 3
    print(numbers.Get(1));     // 4
    print(numbers.IndexOf(5)); // 2
    print(numbers.Contains(9));// false
    numbers.RemoveAt(0);
    numbers.Set(0, 9);
    numbers.ForEach(v => print(v));
    numbers.Clear();

    Array<string> names = new Array<string>();
    names.Reserve(8);          // 预分配容量
    names.Add("a");
    string[] raw = names.ToArray();
}
```

- 全部用 `T[]` 的存储原语 + 脚本循环实现，**没有专用字节码**；容量按 2 倍增长，`Reserve` 可预分配。
- `T` 可以是原始类型、`string`、脚本 `class`、`struct`、数组、委托，甚至是 `Array<U>`（嵌套泛型）。
  元素遵循值/引用语义：`struct` 元素取值即复制，`class` 元素取值即共享。
- 没有 `foreach`（`foreach` 只接受数组和 `string`），请用索引 `for` 或 `ForEach(Action<T>)`。
- 语言不支持构造函数重载，因此没有 `new Array<T>(capacity)`，用 `Reserve` 代替；`Count` 是方法不是属性。
- `Array<T>` 与其他泛型类一样是**恒定**（invariant）：`Array<Dog>` 不能赋给 `Array<Animal>`。
- 因为泛型类会**立即实例化全部成员**，`IndexOf`/`Contains` 依赖 `T` 的 `==`；
  原始类型、引用类型与同类型 `struct` 都可用，但如果将来给 `T` 加入不可比较的类型，需要单独处理。
- 不想注入这段库（例如宿主自带同名类型）时，设 `settings.useScriptStandardLibrary = false`：
  此时 `Array<T>` 变成未知类型（`E0430`），其余语言特性不受影响。
- 库文件可以单独校验：`sharpscript_cli --no-stdlib disasm lib/array.ss` 只编译这份脚本，
  库语法演进时能立刻发现；它也可以被宿主自己的实现替换。

---

## 自定义字节码

每条指令固定 **1 个 32 位字**：

```text
31        24 23                        0
+-----------+---------------------------+
|  opcode   |        operand (24 bit)   |
+-----------+---------------------------+
```

操作数是常量池下标、局部槽号、全局槽号、跳转目标（绝对指令下标）、虚表槽或链接表下标。
超出 24 位范围的立即数放进常量池（`PushConst`）。常量池按 `(ValueKind, 原始 64 位 bit, 字符串)` 去重，
因此 `2.5` 与 `2.0` 不会互相污染。

`ScriptFunction` 中保存的链接表：

| 表 | 用途 |
|---|---|
| `constants` | 字符串、long/double、char 等常量 |
| `functionRefs` | `Call` 的脚本函数目标 |
| `virtualRefs` | `CallVirt` 的（虚表槽, 参数个数） |
| `nativeRefs` | `CallNative` / `CallNativeVariadic` 的原生函数 |
| `typeRefs` | `NewObj` / `NewArray` / `Cast` / `IsType` 的目标类型 |
| `fieldRefs` | `LoadField` / `StoreField` 的字段描述 |
| `lineMap` | 指令下标 → 源码行，用于调用栈 |

主要指令分组：

```text
栈        PushNull PushTrue PushFalse PushI32Imm PushConst Pop Dup Dup2 Swap
局部/全局 LoadLocal StoreLocal LoadGlobal StoreGlobal LoadThis
对象      LoadField StoreField LoadElem StoreElem Length NewObj NewArray CloneStruct
算术      AddI32..ModF32 / AddI64..ModF64 / AddStr / Neg* / Not / BNot* / Inc* / Dec*
比较      CmpEQ CmpNE CmpLT CmpLE CmpGT CmpGE
跳转      Jmp Jz Jnz
转换      ConvI8 ConvI16 ConvI32 ConvI64 ConvU8 ConvU16 ConvChar ConvU32 ConvU64 ConvF32 ConvF64 ConvBool ConvStr
调用      Call CallVirt CallNative CallNativeVariadic CallDelegate NewDelegate DelegateAdd DelegateSub Throw Leave EndFinally Ret
闭包      NewCell LoadCell StoreCell
引用      MakeRefCell MakeRefGlobal MakeRefField MakeRefElem LoadRef StoreRef
类型      Cast IsType
数组      ArrayPush ArrayPop
```

`disasm` 输出示例：

```text
// void main()
// code=99 words, locals=11, stack=5, constants=9
  00000  PushConst             ; "Hello from SharpScript!"
  00001  PushI32Imm            ; 1
  00002  CallNativeVariadic    ; print(...)
  00003  PushI32Imm            ; 7
  00004  StoreLocal            ; local #0
  ...
  00010  MulI32
  00011  PushI32Imm            ; 2
  00012  CallNativeVariadic    ; print(...)
```

---

## 运行时模型

- `Value` 是标签联合：`ValueKind`（Bool/Int32/Float64/String/Object/Array/Null/...）+ 64 位数值载荷 +
  `std::shared_ptr<void>` 引用载荷。字符串、脚本实例、数组、原生对象都用 `shared_ptr` 持有。
- 每个脚本调用拥有独立的**局部槽数组**和**私有操作数栈**；栈是 `std::vector<Value>`，按需增长。
- 调用采用 C++ 递归实现，受 `settings.maxCallDepth` 限制；`settings.maxStackBytes`
  （默认 512 KB）用栈地址差检测宿主原生栈用量，在栈耗尽前抛出脚本运行时错误，
  避免直接崩溃；`settings.maxInstructionsPerExecution` 可设置指令预算，防止不可信脚本死循环。
- 运行时错误会被包装成脚本异常（可被 `catch` 捕获），未捕获时在宿主边界构造脚本调用栈：

```text
samples/hello.ss(20): runtime error: array index 5 is out of range (length 2)
  at divide (samples/hello.ss:3)
  at main (samples/hello.ss:5)
```

- 整数除零、空引用解引用、越界、负数数组长度、抽象方法调用、调用深度超限等均有明确诊断。
- **内存管理**：`ScriptInstance` / `ScriptArray` / `ScriptStructValue` 继承 `GcObject`，引用计数由
  `std::shared_ptr` 提供（无手动 `AddRef`/`Release`），无环对象在最后一个引用消失时立即释放；
  环由增量垃圾回收器检测并销毁，见下一节。struct 的 box 也参与引用枚举，因此
  `class -> struct -> class` 形成的环同样能被回收。原生对象由宿主提供 `shared_ptr`，通过 `Value::native` 交给 VM。
- 脚本函数/类/字段由 `ScriptModule` 持有；正在执行的帧持有模块的 `shared_ptr`，因此热重载后旧调用仍可安全返回。

### 垃圾回收（引用计数 + 增量环检测）

实现思路对标 AngelScript：**引用计数负责即时释放，收集器只负责找环**。

- `GcObject` 是脚本堆对象的基类，暴露两个钩子：
  `gcEnumerateReferences(GcCollector&)` 遍历自身持有的每个引用（对应 `EnumReferences`），
  `gcReleaseReferences()` 丢弃全部引用以打破环（对应 `ReleaseAllReferences`）。
- 每次回收周期：
  1. 所有登记对象成为候选（`gcFlag = true`），内部引用计数清零；
  2. 每个候选枚举自己的引用，指向其他候选的引用使其内部计数 +1（对应 `gcCount`）；
  3. 对象的 `shared_ptr` 强引用数大于内部计数，说明还有环外的引用（全局变量、栈、宿主），
     视为根，并从根沿引用边传播可达性；
  4. 始终未被标记可达的候选只被环引用，断开它们的出边后随之销毁。
- 没有环的对象根本不会进入第 4 步：引用计数归零时 `shared_ptr` 已经把它销毁了，
  所以 `totalDetected` 只统计真正的环。
- 增量：`GarbageCollect(GcFlag::OneStep, stepSize)` 每次最多处理 `stepSize` 个对象，
  返回 `false` 表示还有剩余工作，状态保留在收集器里；`GcFlag::FullCycle` 一次跑完。
  分步期间收集器用强引用固定候选集合，宿主/栈/全局中的引用都算根，因此不会误回收。
  由于检测结果跨调用保存，脚本不能在两步之间执行；`invoke` 会先跑完进行中的周期再执行脚本。
- 自动回收由 `settings.autoGarbageCollect` 与 `settings.gcAutoThreshold`（默认 1024 次分配）控制，
  只在脚本执行返回、模块加载/卸载/重载等安全点触发。

```cpp
auto stats = engine.gcStatistics();          // currentAllocated / totalDetected / cycles / ...
engine.garbageCollect(GcFlag::FullCycle);    // 一次跑完
while (!engine.garbageCollect(GcFlag::OneStep, 128)) {
    // 分步推进，避免长停顿
}
```

---

## 许可证

本项目以 [MIT License](LICENSE) 开源。

与 AngelScript 的对应关系：

| AngelScript | 本项目 |
|---|---|
| `asCScriptObject` 内嵌 `refCount` + `AddRef/Release` | `GcObject` + `std::shared_ptr` 控制块 |
| `EnumReferences` / `ReleaseAllReferences` | `gcEnumerateReferences` / `gcReleaseReferences` |
| `gcFlag` / `gcCount` | `gcFlag_` / `gcInternalReferences_` |
| `GarbageCollect(asGC_FULL_CYCLE / asGC_ONE_STEP)` | `garbageCollect(GcFlag::FullCycle / OneStep)` |
| `GetGCStatistics` | `gcStatistics()` |
| `asEP_AUTO_GARBAGE_COLLECT` | `settings.autoGarbageCollect` + `gcAutoThreshold` |

> 原生对象不参与环检测：宿主若让原生对象反过来持有脚本对象，形成的环需要宿主自己打破
> （脚本对象看到的是"来自外部的引用"，收集器会保守地保留它）。

---

## 原生绑定（C++ 宿主）

没有运行时反射（C++ 也不适合强行做），采用显式注册，思路与 AngelScript 的
`RegisterObjectType` / `RegisterObjectMethod` 一致：

```cpp
#include "sharpscript/engine.hpp"
using namespace sharpscript;

ScriptEngine engine;

// 静态类型：Math.Abs(x) / Math.Sqrt(x) ...
auto& math = engine.createNativeType("Math", /*isStatic=*/true);
engine.addMethod(math, "Sqrt", primF64(), {primF64()},
    [](ExecutionContext&, const Value&, const std::vector<Value>& args) {
        return Value::f64(std::sqrt(args[0].asF64()));
    },
    /*isStatic=*/true);

// 可构造类型 + 实例方法 + 属性
struct Counter { int value = 0; };

auto& counter = engine.createNativeType("Counter", /*isStatic=*/false);
engine.addConstructor(counter, {primI32()},
    [](ExecutionContext&, const Value&, const std::vector<Value>& args) {
        auto instance = std::make_shared<Counter>();
        instance->value = args[0].asI32();
        return Value::native(instance);
    });
engine.addMethod(counter, "Add", primVoid(), {primI32()},
    [](ExecutionContext&, const Value& self, const std::vector<Value>& args) {
        self.asNative<Counter>()->value += args[0].asI32();
        return Value::voidValue();
    });
engine.addProperty(counter, "Value", primI32(),
    [](const Value& self) { return Value::i32(self.asNative<Counter>()->value); },
    [](const Value& self, const Value& value) { self.asNative<Counter>()->value = value.asI32(); });

// 全局函数（可变参数）
engine.addGlobalFunction("print", primVoid(), {}, myPrintImplementation, /*isVariadic=*/true);

// 使用
engine.loadModule("game", scriptSource, "game.ss");
engine.invokeEntryPoint("game");
```

> 原生对象的运行时类型检查只在编译期完成；`Value` 不携带原生类型标签，
> 因此脚本里把 A 类型赋给 B 类型不会被运行时拦截。宿主 lambda 中可用 `asNative<T>()` 取回对象。

### 终结器（`~Type()`）

```csharp
class FileHandle {
    private string path;
    public FileHandle(string path) { this.path = path; }
    ~FileHandle() {
        print("closing " + path);   // 字段在终结阶段仍然可读
    }
}

void main() {
    FileHandle handle = new FileHandle("data.txt");
    handle = null;
    print("dropped");
}
// 输出：dropped，然后 closing data.txt
```

- 语义对齐 C#：`~Type()` 无参数、无返回值、不能有修饰符；同一个类只能有一个（`E0453`），`struct` 不能声明（`E0451`）。
- **延迟执行**：最后一个引用消失时不立刻执行，而是进入引擎的终结队列，在本次 `invoke` / `invokeEntryPoint` 结束时统一执行；
  宿主也可以显式调用 `engine.runFinalizers()`（返回本轮执行的个数）来做确定性收尾。
- **顺序**：先派生类、再逐层基类（`Leaf → Middle → Base`），与 C# 编译出的 `try { ... } finally { base.Finalize(); }` 等价。
- **字段可用**：终结时对象尚未销毁，字段完整可读。环回收路径也保证这一点——`GcCollector` 在**断链之前**把需要终结的对象交给引擎，
  终结完成后由引擎断开其引用，所以环不会泄漏。
- **异常被吞掉**：终结器里抛出的异常不会传播到宿主，也不影响同批其他终结器。
- 终结器不参与虚派发、不进入方法表，`obj.~Type()` 无法解析（不能显式调用）。
- 实现要点：`GcCollector` 给脚本对象装了自定义删除器，引用归零时把对象**复活**成新控制块交给 `finalizerSink`（C# 的“可终结对象存活一轮”），
  因此终结器拿到的是有效引用；`ScriptInstance` 用 `finalized_` 标记保证只跑一次。
- 限制：没有 `GC.SuppressFinalize`/`Dispose` 模式；终结期间把 `this` 存到全局会复活对象，但字段已被断开，不要依赖这种写法。

---

## 热重载

### API

```cpp
auto engine = std::make_unique<ScriptEngine>();
auto module = engine->loadModule("game", sourceV1, "game.ss");

auto result = engine->reloadModule("game", sourceV2, "game.ss");
if (!result.success) {
    for (const auto& diagnostic : result.diagnostics) {
        std::cerr << diagnostic.toString() << "\n";
    }
    // 编译失败：旧模块继续工作
} else {
    std::cout << result.summary() << "\n";   // gen 1->2, preserved N, reset M
    engine->invokeEntryPoint("game");
}
```

### 方法体热更（`ReloadStrategy`）

默认 `ReloadStrategy::Auto`：先尝试**就地替换函数体**，结构变不了才回退到整模块重载。

```cpp
engine->settings.reloadStrategy = ReloadStrategy::Auto;       // 默认
engine->settings.reloadStrategy = ReloadStrategy::MethodBody; // 结构变化时直接失败
engine->settings.reloadStrategy = ReloadStrategy::FullModule; // always 整模块

auto result = engine->reloadModule("game", v2, "game.ss");
result.patchedInPlace();     // true = 走的是方法体补丁
result.patchedFunctions;     // 被替换的函数数量
result.fallbackReasons;      // 未能就地替换的原因（例如 "class 'Box' changed layout: field count changed (1 -> 2)"）
```

判定器（`planMethodReload`）在**不修改任何状态**的前提下比较两代，检查：

| 检查项 | 不通过时的原因 |
| --- | --- |
| 类布局（字段名/类型/顺序、`struct` 标志、基类） | `fieldRefs`、`LoadField` 的槽号会错位 |
| vtable 每个槽的函数 | `CallVirt` 的槽号会指向别的实现 |
| 委托类型签名 | 已有的委托值签名失配 |
| 全局变量表（名字/类型/顺序） | `LoadGlobal`/`StoreGlobal` 用索引寻址 |
| 全局初始化器字节码 | 补丁不执行初始化器，改了会静默不生效 |
| 新一代新增的类/委托类型 | 类型对象归属即将丢弃的模块 |
| 每个函数的签名与虚拟槽位 | 签名变化视为“新函数”，旧对象保留 |

全部通过时 `applyMethodReload` 只替换 `code`/`constants`/`lineMap`/`localSlotCount`/`maxStack`/`handlers`，
并把 `functionRefs`、`typeRefs`、`fieldRefs`、`delegateInfos`、`catchType` **按名字重映射回旧代对象**。
因此：

- 函数对象身份不变 → **重载前创建的委托会自动看到新代码**（整模块重载做不到）；
- 全局与实例状态**完全不动**，不需要迁移；
- 新增的函数会被挂到旧模块（由 `ScriptModule::patchedDependency` 持有新一代的存储）。

限制：新增/删除字段、改签名、改虚表、改全局表、新增类型、修改全局初始化器都会回退整模块；
被替换的函数不能正处于执行中；lambda 不参与配对（按序号命名，容易错位）。

### 语义

1. 新版本先完整编译成**新的 `ScriptModule`（generation + 1）**；旧模块保持完整可用。
2. 新旧模块的全局变量按名字配对：
   - 新模块先执行自己的全局初始化器；
   - 装载时记录每个全局的**初始值快照**；
   - 如果旧模块中的值仍等于其初始快照（说明脚本没有改过它）→ 采用**新版本的初始化值**
     （改源码里的字面量会立即生效）；
   - 如果已被脚本修改 → **保留运行时值**；
   - 脚本对象/数组会**按字段名递归合并**：新增字段取新初始化值，已修改字段保留，
     未修改字段采用新初始化值，字段顺序变化不影响迁移。
3. 原子替换模块表并派发 `onModuleReloaded` 事件；旧模块标记为 obsolete。
   正在执行的调用帧通过 `shared_ptr<ScriptModule>` 持有旧模块，继续使用旧函数、旧虚表、旧全局数组直到返回。
4. 编译失败时不替换模块，`result.success == false`，旧的 generation 继续服务。
5. `watch` 模式轮询文件修改时间，保存即重载并重新执行 `main()`。
6. 宿主如果缓存了 `ScriptFunction*` / `ScriptClassType*`，重载后应重新查询；旧指针可能随旧模块释放而失效。

### 演示

```text
$ sharpscript_cli reload samples/hotreload.ss samples/hotreload.v2.ss
loaded module 'hotreload' gen 1 ...
v1 hello, world (#1) | views = 1

--- hot reloading with samples/hotreload.v2.ss ---
reloaded 'hotreload' gen 1->2 in 0.66 ms (preserved 2 globals, reset 1)
  preserved: views, greeter
  reset:     revision

v2 hello, world (#2)! | views = 2 | revision = 2
```

可以看到：`prefix` 的源码修改生效（v1 → v2）、新字段 `suffix` 生效、
`greeter.greeted` 与 `views` 的运行时状态被保留、新全局 `revision` 正常初始化。

---

## 与 AngelScript 的对应关系

| AngelScript | 本项目（C++17） |
|---|---|
| `asCTokenizer` | `Tokenizer` |
| `asCParser` / `asCScriptNode` | `Parser` / `ast.hpp` |
| `asCCompiler` | `ModuleCompiler` + `FunctionCompiler` |
| `asCScriptFunction` | `ScriptFunction`（含字节码与链接表） |
| `asCContext` | `ExecutionContext` |
| `asCScriptEngine` | `ScriptEngine` |
| `asIScriptModule` | `ScriptModule`（generation 版本化） |
| `RegisterObjectType/Method` | `createNativeType` + `addMethod` / `addProperty` / `addConstructor` |
| 引用计数 + 增量 GC | `std::shared_ptr` + `GcObject` / `GcCollector`（环检测、增量、统计） |
| 值类型 `asOBJ_VALUE` | `struct` + `ScriptStructValue` box + `CloneStruct` |
| `try` / `catch` / `finally` | EH 子句表 + `Leave`/`EndFinally` + 内置 `Exception` 原生层次 |
| `delegate` / lambda / 闭包 | `ScriptDelegate` + `ScriptCell`（按引用捕获）+ 多播 |
| `ref` / `out` / `in` | `ScriptRef`（cell/global/field/elem）+ `MakeRef*` / `LoadRef` / `StoreRef` |
| `SaveByteCode` / `LoadByteCode` | 暂未实现（字节码结构已按可序列化方式组织） |

---

## 测试

```powershell
build\Release\sharpscript_tests.exe
# 或
ctest --test-dir build -C Release --output-on-failure
```

当前：**141 passed, 0 failed**。

覆盖：算术/位运算/浮点、类型转换、字符串（UTF-16 长度/索引、PascalCase 方法、`null` 语义）、控制流、递归、全局初始化、
类/继承/虚派发/静态成员、数组、命名空间（嵌套、using、静态成员、同名类型、file-scoped、热重载配对）、
多文件 section（共享命名空间、诊断定位、行偏移、重载与函数体热更）、
属性（自动/显式访问器、只读与构造函数初始化、静态与继承、泛型类、复合赋值、限制诊断）、
`struct` 值类型（拷贝语义、嵌套、数组/引用字段、静态成员、限制诊断、GC、宿主边界、热重载）、
委托与 lambda（方法组、多播、Invoke、null、类型检查、lambda 推断、闭包按引用捕获、循环变量捕获、GC 环）、
ref/out/in（引用传递、字段/数组元素/struct、out var、in 只读、引用转发、限制诊断）、
异常（try/catch/throw、运行时错误映射、嵌套与重抛、跨调用栈、循环内 catch、宿主抛出、栈溢出、未捕获上报）、
finally（所有出口、return 值保存、break/continue、嵌套顺序、异常替代、finally 内捕获、限制诊断）、
原生绑定（静态类型、单例方法、原生构造 + 属性）、
垃圾回收（引用计数、环检测、全局/宿主根、增量分步、自动阈值）、
编译诊断、运行时错误与调用栈、热重载（状态迁移、失败回退、方法体就地替换、委托看到新代码、结构变更回退）、
指令编解码与反汇编。

---

## 设计取舍与当前限制

- **实现语言**：C++17；脚本语言保持 C# 风格（这是产品定位，不是实现语言）。
- **内存管理**：`shared_ptr` 引用计数 + 增量环检测；脚本类可声明 `~Type()` 终结器（见“终结器”一节），
  由引用计数与环回收共同触发；原生对象之间的环不参与检测。
- **赋值不是表达式**：`x = y` 只作为语句使用（不产生栈上的值）；`a = b = 1` 不支持。
- **方法默认虚调用**：脚本类实例方法都会进入虚表；没有 `sealed`/非虚方法优化。
- **单继承**：基类必须与派生类在同一模块；暂不支持 `base(...)` 显式构造参数，
  派生类只自动调用基类的无参构造。
- **类型转换语法**：`(int)x` 等原始类型转换、`x as T`、`x is T`；用户类型暂不支持 C 风格 `(T)x`。
- **字符/字符串**：对齐原生C#字符串，`char` 是单个 UTF-16 code unit；字符串按 UTF-16 code unit 处理，长度和索引不按"字符"（grapheme）计算，
  非 BMP 字符占两个 code unit 且不能作为 `char` 字面量。
- **struct 值类型**：用“装箱 + 显式复制”（`ScriptStructValue` + `CloneStruct`）实现值语义，而不是内联栈存储；
  语义与 C# 对齐，代价是每次复制多一次堆分配（嵌套 struct 递归复制）。struct 不可为 null，也不支持 `==`/`is`/`as`。
- **闭包**：包含 lambda 的函数会把局部变量与参数提升为 `ScriptCell`（按引用捕获，不做精确逃逸分析），
  代价是少量额外装箱；`for`/`foreach` 每次迭代新建 cell，以匹配 C# 5+ 的捕获语义。
- **原生栈保护**：递归解释器用 `maxStackBytes` 估算宿主栈用量，超限抛出脚本错误而不是让进程崩溃。
- **原生绑定**：无反射；必须显式注册方法/属性/构造函数。原生类型仅在编译期检查。
- **命名空间**：C# 风格（点号限定、`using`、嵌套、file-scoped），但只在模块内生效；
  跨模块引用命名空间成员暂不支持，`using static` 与别名 `using X = A.B;` 也未实现。
- **异常**：`try/catch/finally/throw/throw;` 与内置 `Exception` 层次已实现（`finally` 走 IL 风格的
  `Leave` / `EndFinally` 展开）；暂不支持异常过滤器 `when (...)`、自定义异常类（脚本类不能继承原生类型）。
- **委托/lambda**：类型化 delegate、方法组、多播、lambda 与按引用捕获闭包已实现；
  暂不支持泛型 `Func<>`/`Action<>`、`event`、`async/await`、表达式树、`ref/out` 捕获、协变/逆变、匿名方法。
- **ref/out/in**：脚本函数/方法/构造函数之间已支持按引用传递；不支持 `ref` 返回、`ref` 局部变量、
  `ref struct`，委托/lambda/原生函数参数也不能带修饰符。
- 暂不支持：运算符重载、多维数组、跨模块继承、字节码持久化。
- 未做 JIT；当前是纯解释执行。性能目标是“足够快 + 结构清晰”，不是生产级峰值性能。

---

## 后续可扩展方向

1. 字节码序列化/反序列化（对应 AngelScript 的 `SaveByteCode`）。
2. 异常增强：异常过滤器 `when (...)`、脚本自定义异常类、异常序列化。
3. `IDisposable` 风格的确定性释放（`using` 语句），与终结器互补。
4. 运算符重载。
5. 编译期常量折叠、局部变量槽复用、superinstruction 优化。
6. 多模块类型系统（跨模块继承、全局类型注册表）。
7. 调试器协议：断点、单步、局部变量检视（`lineMap` 与帧信息已具备基础）。
8. 可选的 C API / C# P/Invoke 包装，把 C++ VM 暴露给其他宿主。
