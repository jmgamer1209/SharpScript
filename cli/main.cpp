#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "sharpscript/compiler.hpp"
#include "sharpscript/disassembler.hpp"
#include "sharpscript/engine.hpp"
#include "sharpscript/unicode.hpp"

using namespace sharpscript;

namespace {

std::string readFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open file: " + path);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::string moduleNameFor(const std::string& path) {
    return std::filesystem::path(path).stem().string();
}

std::string currentTimeText() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::ostringstream out;
    out << std::put_time(&local, "%H:%M:%S");
    return out.str();
}

void printDiagnostics(const std::vector<Diagnostic>& diagnostics) {
    for (const auto& diagnostic : diagnostics) std::cerr << diagnostic.toString() << "\n";
}

void registerMath(ScriptEngine& engine) {
    auto& math = engine.createNativeType("Math", true);
    engine.addMethod(math, "Abs", primI32(), {primI32()},
                     [](ExecutionContext&, const Value&, const std::vector<Value>& args) {
                         return Value::i32(std::abs(args[0].asI32()));
                     },
                     true);
    engine.addMethod(math, "Abs", primF64(), {primF64()},
                     [](ExecutionContext&, const Value&, const std::vector<Value>& args) {
                         return Value::f64(std::fabs(args[0].asF64()));
                     },
                     true);
    engine.addMethod(math, "Max", primI32(), {primI32(), primI32()},
                     [](ExecutionContext&, const Value&, const std::vector<Value>& args) {
                         return Value::i32(std::max(args[0].asI32(), args[1].asI32()));
                     },
                     true);
    engine.addMethod(math, "Max", primF64(), {primF64(), primF64()},
                     [](ExecutionContext&, const Value&, const std::vector<Value>& args) {
                         return Value::f64(std::max(args[0].asF64(), args[1].asF64()));
                     },
                     true);
    engine.addMethod(math, "Min", primI32(), {primI32(), primI32()},
                     [](ExecutionContext&, const Value&, const std::vector<Value>& args) {
                         return Value::i32(std::min(args[0].asI32(), args[1].asI32()));
                     },
                     true);
    engine.addMethod(math, "Min", primF64(), {primF64(), primF64()},
                     [](ExecutionContext&, const Value&, const std::vector<Value>& args) {
                         return Value::f64(std::min(args[0].asF64(), args[1].asF64()));
                     },
                     true);
    engine.addMethod(math, "Sqrt", primF64(), {primF64()},
                     [](ExecutionContext&, const Value&, const std::vector<Value>& args) {
                         return Value::f64(std::sqrt(args[0].asF64()));
                     },
                     true);
    engine.addMethod(math, "Floor", primF64(), {primF64()},
                     [](ExecutionContext&, const Value&, const std::vector<Value>& args) {
                         return Value::f64(std::floor(args[0].asF64()));
                     },
                     true);
    engine.addMethod(math, "Ceil", primF64(), {primF64()},
                     [](ExecutionContext&, const Value&, const std::vector<Value>& args) {
                         return Value::f64(std::ceil(args[0].asF64()));
                     },
                     true);
    engine.addMethod(math, "Round", primF64(), {primF64()},
                     [](ExecutionContext&, const Value&, const std::vector<Value>& args) {
                         return Value::f64(std::round(args[0].asF64()));
                     },
                     true);
    engine.addMethod(math, "Pow", primF64(), {primF64(), primF64()},
                     [](ExecutionContext&, const Value&, const std::vector<Value>& args) {
                         return Value::f64(std::pow(args[0].asF64(), args[1].asF64()));
                     },
                     true);
}

void registerHost(ScriptEngine& engine) {
    auto& host = engine.createNativeType("Host", true);
    engine.addMethod(host, "Log", primVoid(), {primString()},
                     [](ExecutionContext&, const Value&, const std::vector<Value>& args) {
                         std::cout << "[host] " << utf16ToUtf8(args[0].asString()) << std::endl;
                         return Value::voidValue();
                     },
                     true);
    engine.addMethod(host, "Now", primString(), {},
                     [](ExecutionContext&, const Value&, const std::vector<Value>&) {
                         return Value::stringUtf8(currentTimeText());
                     },
                     true);
    engine.addMethod(host, "GetTickCount", primI32(), {},
                     [](ExecutionContext&, const Value&, const std::vector<Value>&) {
                         auto ticks = std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count();
                         return Value::i32(static_cast<std::int32_t>(ticks & 0x7FFFFFFF));
                     },
                     true);
}

std::unique_ptr<ScriptEngine> createEngine() {
    auto engine = std::make_unique<ScriptEngine>();
    engine->settings.output = [](const std::string& line) { std::cout << line << std::endl; };
    registerMath(*engine);
    registerHost(*engine);
    return engine;
}

void runEntryPoint(ScriptEngine& engine, ScriptModule& module) {
    if (module.entryPoint == nullptr) {
        std::cout << "(no entry point; define 'void main()')" << std::endl;
        return;
    }
    try {
        engine.invoke(module.entryPoint);
    } catch (const ScriptRuntimeException& ex) {
        std::cerr << ex.format() << std::endl;
    }
}

void printLoaded(const ScriptModule& module) {
    std::cout << "loaded " << module.toString() << " in " << module.compileTimeMs << " ms ("
              << module.functions.size() << " functions, " << module.classes.size() << " classes, "
              << module.globals.size() << " globals)" << std::endl;
}

int runFiles(ScriptEngine& engine, const std::vector<std::string>& paths) {
    for (const auto& path : paths) {
        if (!std::filesystem::exists(path)) {
            std::cerr << "file not found: " << path << std::endl;
            return 1;
        }
    }
    try {
        // Extra files become additional sections of the same module: they share
        // one global namespace, exactly like AngelScript's AddScriptSection.
        std::vector<ScriptSection> sections;
        for (const auto& path : paths) sections.emplace_back(path, readFile(path));
        auto module = engine.loadModule(moduleNameFor(paths.front()), sections, paths.front());
        printLoaded(*module);
        std::cout << std::endl;
        runEntryPoint(engine, *module);
        return 0;
    } catch (const ScriptCompileException& ex) {
        printDiagnostics(ex.diagnostics());
        return 2;
    }
}

int runFile(ScriptEngine& engine, const std::string& path) {
    return runFiles(engine, {path});
}

int reloadDemo(ScriptEngine& engine, const std::string& oldPath, const std::string& newPath) {
    if (!std::filesystem::exists(oldPath) || !std::filesystem::exists(newPath)) {
        std::cerr << "file not found" << std::endl;
        return 1;
    }
    try {
        std::string name = moduleNameFor(oldPath);
        auto module = engine.loadModule(name, readFile(oldPath), oldPath);
        printLoaded(*module);
        std::cout << std::endl;
        runEntryPoint(engine, *module);

        std::cout << "\n--- hot reloading with " << newPath << " ---" << std::endl;
        auto result = engine.reloadModule(name, readFile(newPath), newPath);
        if (!result.success) {
            std::cerr << "reload aborted, keeping generation "
                      << (result.previous ? result.previous->generation : 0) << ":" << std::endl;
            printDiagnostics(result.diagnostics);
            return 2;
        }
        std::cout << result.summary() << std::endl;
        if (!result.patchedInPlace()) {
            std::cout << "  preserved: ";
            for (std::size_t i = 0; i < result.migratedGlobals.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << result.migratedGlobals[i];
            }
            std::cout << "\n  reset:     ";
            for (std::size_t i = 0; i < result.resetGlobals.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << result.resetGlobals[i];
            }
            std::cout << "\n";
        }
        std::cout << std::endl;
        runEntryPoint(engine, *result.module);
        return 0;
    } catch (const ScriptCompileException& ex) {
        printDiagnostics(ex.diagnostics());
        return 2;
    }
}

int watchFile(ScriptEngine& engine, const std::string& path) {
    if (!std::filesystem::exists(path)) {
        std::cerr << "file not found: " << path << std::endl;
        return 1;
    }

    std::string name = moduleNameFor(path);
    std::cout << "watching " << std::filesystem::absolute(path).string() << " (Ctrl+C to stop)"
              << std::endl;

    bool loaded = false;
    try {
        auto module = engine.loadModule(name, readFile(path), path);
        loaded = true;
        printLoaded(*module);
        std::cout << std::endl;
        runEntryPoint(engine, *module);
    } catch (const ScriptCompileException& ex) {
        printDiagnostics(ex.diagnostics());
    }

    auto lastWrite = std::filesystem::last_write_time(path);
    std::cout << "edit the script to trigger a hot reload" << std::endl;

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        std::error_code error;
        auto current = std::filesystem::last_write_time(path, error);
        if (error || current == lastWrite) continue;
        lastWrite = current;
        std::this_thread::sleep_for(std::chrono::milliseconds(80));

        std::cout << std::endl;
        if (!loaded) {
            try {
                auto module = engine.loadModule(name, readFile(path), path);
                loaded = true;
                printLoaded(*module);
                runEntryPoint(engine, *module);
            } catch (const ScriptCompileException& ex) {
                printDiagnostics(ex.diagnostics());
            }
            continue;
        }

        auto result = engine.reloadModule(name, readFile(path), path);
        if (!result.success) {
            std::cerr << "reload aborted, keeping generation "
                      << (result.previous ? result.previous->generation : 0) << ":" << std::endl;
            printDiagnostics(result.diagnostics);
            continue;
        }
        std::cout << result.summary() << std::endl;
        runEntryPoint(engine, *result.module);
    }
}

int disassembleFiles(ScriptEngine& engine, const std::vector<std::string>& paths,
                     const std::string& outputPath) {
    for (const auto& path : paths) {
        if (!std::filesystem::exists(path)) {
            std::cerr << "file not found: " << path << std::endl;
            return 1;
        }
    }
    try {
        // Extra files become sections, so a library split across several files can
        // be validated in one pass: `--no-stdlib disasm lib/*.cs`.
        std::vector<ScriptSection> sections;
        for (const auto& path : paths) sections.emplace_back(path, readFile(path));
        auto module = engine.loadModule(moduleNameFor(paths.front()), sections, paths.front());

        // Without --out the listing goes to stdout, exactly as before.
        if (outputPath.empty()) {
            Disassembler::writeModule(*module, std::cout);
            return 0;
        }
        std::ofstream file(outputPath, std::ios::binary | std::ios::trunc);
        if (!file) {
            std::cerr << "cannot write file: " << outputPath << std::endl;
            return 1;
        }
        Disassembler::writeModule(*module, file);
        file.close();
        if (!file) {
            std::cerr << "failed while writing: " << outputPath << std::endl;
            return 1;
        }
        std::cout << "wrote " << outputPath << " (" << module->functions.size() << " functions, "
                  << module->classes.size() << " classes)" << std::endl;
        return 0;
    } catch (const ScriptCompileException& ex) {
        printDiagnostics(ex.diagnostics());
        return 2;
    }
}

int repl(ScriptEngine& engine) {
    std::cout << "SharpScript REPL (C++17). Type an expression, ':globals' or ':quit'." << std::endl;
    std::string line;
    while (true) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        // Accept UTF-16-ish piped input from PowerShell by dropping NUL bytes and CR.
        line.erase(std::remove(line.begin(), line.end(), '\0'), line.end());
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == ":quit") break;
        if (line.empty()) continue;
        if (line == ":globals") {
            for (const auto& module : engine.modules()) {
                for (const auto& global : module->globals) {
                    std::cout << "  " << global.type.name << " " << global.name << " = "
                              << global.value.display() << std::endl;
                }
            }
            continue;
        }
        try {
            std::cout << engine.evaluate(line) << std::endl;
        } catch (const ScriptCompileException& ex) {
            printDiagnostics(ex.diagnostics());
        } catch (const ScriptRuntimeException& ex) {
            std::cerr << ex.format() << std::endl;
        } catch (const std::exception& ex) {
            std::cerr << ex.what() << std::endl;
        }
    }
    return 0;
}

void printHelp() {
    std::cout << "SharpScript - a C#-like scripting VM with custom bytecode and hot reload (C++17)\n\n"
              << "  run <file.cs>...   compile and run a script (extra files become sections)\n"
              << "  watch <file.cs>    run and hot reload whenever the file changes\n"
              << "  reload <a> <b>     load a, run it, hot reload with b, run again\n"
              << "  disasm <file.cs>... [--out <file>]  dump the generated bytecode\n"
              << "                     (extra files become sections; --out writes it to a file)\n"
              << "  repl               interactive expression evaluator\n\n"
              << "Host bindings: print/assert, Math.*, Host.*\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printHelp();
        return 0;
    }

    // `--no-stdlib` drops the embedded library sections, so the same files can be
    // supplied explicitly (validating lib/*.cs, or shipping custom ones).
    // `--out <file>` redirects the bytecode dump of `disasm` into a file.
    std::vector<std::string> args(argv + 1, argv + argc);
    bool useStdlib = true;
    std::string outputPath;
    for (auto it = args.begin(); it != args.end();) {
        if (*it == "--no-stdlib") {
            useStdlib = false;
            it = args.erase(it);
        } else if (*it == "--out" || *it == "-o") {
            if (it + 1 == args.end()) {
                std::cerr << "--out needs a file path" << std::endl;
                return 1;
            }
            outputPath = *(it + 1);
            it = args.erase(it, it + 2);
        } else {
            ++it;
        }
    }
    if (args.empty()) {
        printHelp();
        return 0;
    }

    std::string command = args[0];
    if (command == "-h" || command == "--help" || command == "help") {
        printHelp();
        return 0;
    }
    if (!outputPath.empty() && command != "disasm") {
        std::cerr << "--out is only supported by 'disasm'" << std::endl;
        return 1;
    }

    auto engine = createEngine();
    engine->settings.useScriptStandardLibrary = useStdlib;
    try {
        if (command == "run") {
            if (args.size() < 2) {
                std::cerr << "usage: sharpscript_cli run <file.cs> [more.cs ...]" << std::endl;
                return 1;
            }
            std::vector<std::string> paths(args.begin() + 1, args.end());
            return runFiles(*engine, paths);
        }
        if (command == "watch") {
            if (args.size() < 2) {
                std::cerr << "usage: sharpscript_cli watch <file.cs>" << std::endl;
                return 1;
            }
            return watchFile(*engine, args[1]);
        }
        if (command == "reload") {
            if (args.size() < 3) {
                std::cerr << "usage: sharpscript_cli reload <old.cs> <new.cs>" << std::endl;
                return 1;
            }
            return reloadDemo(*engine, args[1], args[2]);
        }
        if (command == "disasm") {
            if (args.size() < 2) {
                std::cerr << "usage: sharpscript_cli disasm <file.cs> [more.cs ...] [--out <file>]"
                          << std::endl;
                return 1;
            }
            const std::vector<std::string> files(args.begin() + 1, args.end());
            return disassembleFiles(*engine, files, outputPath);
        }
        if (command == "repl") return repl(*engine);

        return runFile(*engine, command);
    } catch (const ScriptCompileException& ex) {
        printDiagnostics(ex.diagnostics());
        return 2;
    } catch (const ScriptRuntimeException& ex) {
        std::cerr << ex.format() << std::endl;
        return 3;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << std::endl;
        return 1;
    }
}
