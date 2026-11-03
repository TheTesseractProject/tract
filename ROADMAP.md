# 🗺️ Terra (Tesseract Compiler) Roadmap

> **Terra** is a compiler for the Tesseract language — a prototype modern compilation system written in C23.  
> *This roadmap outlines the development path from version 0.1 to 1.0 and beyond.*

---

## 📌 Executive Summary

### Core Principles

Every release adheres to these principles:

1. **Nothing gets slower** — performance must match or exceed previous releases
2. **Measure against rivals** — benchmarked against GCC, Clang on identical hardware
3. **Backward compatibility** — old programs continue working without modifications
4. **Production-ready quality** — stability, documentation, and testing at every stage

---

## 🔍 Work Priorities

| Priority | Area | Description |
|----------|------|-------------|
| 🔥 **Critical** | Compilation Speed | Fast compilation for projects from 1K to 100K LOC |
| ⚡ **High** | Code Generation | Multiple backends: C99, LLVM IR, WebAssembly |
| 🌐 **High** | Cross-Platform Support | x86_64, ARM64, RISC-V with automated CI |
| 🛠️ **Medium** | IDE Integration | LSP server, debugger, profiler, package manager |

---

## 🗂️ Small Tasks Queue (`road.txt`)

`road.txt` is the backlog of small, already-scoped tasks: targeted fixes and
low-risk additions that do not need an RFC or a milestone of their own. Every
entry is a single actionable line written as `[] - <task>`, and each should be
small enough for one commit.

Rules of the queue:

- An item is added only when the problem is already understood and the fix is
  local (one file, or a few closely related ones).
- Items are taken from the top; mark a finished item by changing `[]` to `[x]`.
- The file is a queue, not a roadmap: anything that changes semantics, public
  syntax or the architecture still goes through the phases below.
- Keep the list short (roughly a dozen open items); promote larger entries into
  the appropriate phase instead of letting them pile up here.

---

## ✅ Completed Releases

| Version | Highlights | Status |
|---------|------------|--------|
| **0.0.1** | Lexer + Parser + Sema, CLI, demo examples | 🟢 Released |
| **0.0.2** | Error diagnostics with location info, test suite foundation | 🟢 Released |
| **0.1** | AST optimization passes, initial C99 codegen | 🟢 Released |
| **0.3.0** | C99 code generation, modular backend architecture, language syntax updates | ✅ Complete |
| **0.4.0 (Current)** | Native C headers, clearer declarations, application-oriented control flow and executable builds | 🏗️ In Progress |

---

## 📅 Development Phases (0.4 → 1.0)

Each phase builds upon the previous one while maintaining API/ABI stability.

### Native C and application support (active direction)

- [x] Import validated system headers with `import "header.h" as alias`.
- [x] Declare typed C functions with `extern fn`, including C-style variadic
  signatures and opaque C typedefs, then emit prototypes for the C compiler to
  validate.
- [x] Add explicit `let name = value;` declarations while retaining assignment
  based declarations for existing programs.
- [x] Add a typed Tesseract `fn main()` application entry point while retaining
  top-level script execution for examples and quick programs.
- [x] Add scoped C-style `for (init; condition; step)` loops.
- [x] Add loop-scoped `break;` and `continue;` with semantic validation.
- [x] Add top-level `link "library";` metadata and a shell-free executable
  build mode that passes library names to the C linker.
- [ ] Add typed `extern` declarations for C APIs that do not provide headers or
  need narrower language-level types.
- [ ] Expand application primitives (collections, iteration, modules, and a
  stable runtime API) based on small real programs and examples.

### Freestanding and operating-system work (required before a Linux 1.0 port)

The compiler now has a source-to-object freestanding path for runtime-free
programs. It still does not link a kernel or produce a bootable image.

- [x] Fixed-size arrays, typed slices, indexing, pointer arithmetic, and
  explicit address/dereference operations.
- [x] C ABI `struct`/`union` layout and `volatile` qualified types.
- [x] Freestanding object mode without hosted runtime includes or link steps;
  programs that require Terra's hosted helpers are rejected in this mode.
- [x] Target triple forwarding and GNU-style inline assembly templates.
- [ ] Complete function-pointer types, calling-convention attributes,
  assembly constraints/intrinsics, linker scripts, and reproducible boot-image
  builds.
- [ ] Architecture-specific integration tests and boot tests in an emulator.

---

### Phase X: Version 0.4 — Runtime Library & Executable Programs

#### 🎯 Objectives

Create a complete runtime library and ensure Terra programs compile to fully executable C99 code.

#### ✨ Features

##### 1. C99 Runtime Library (`terra_runtime.h`)

```c
// Memory management
void* terramalloc(size_t bytes);
void terrafree(void* ptr);

// Standard I/O
void put(int value);
void put_str(const char* str);
int input_int();
char* input_str();

// Type conversion
int to_int(double d);
double to_float(int i);
const char* to_string(int i);

// String manipulation (future)
const char* concat(const char* a, const char* b);
int len(const char* str);
```

##### 2. Object Lifecycle Management

**Rules:**
- Objects are allocated on heap at creation time
- No garbage collection during program execution  
- `let B = A` makes B reference the same object as A
- No manual deallocation in current version

**AST Representation:**
```tesseract
let obj = ClassName.init;      // Allocation happens in init()
let ref = obj;                  // Reference copy
obj.method(args);               // Method call via pointer
```

##### 3. Function Definitions

**New syntax for functions:**
```tesseract
fn main() {
    let x = 10;
    let y = 20;
    put(x + y);
}

fn multiply(a: int, b: int): int {
    return a * b;
}
```

Benefits:
- Clear distinction between function declarations and calls
- Consistent type notation (return type after parameters)
- Easier parsing and better readability

#### 📋 Deliverables

- [ ] `terra_runtime.h` - Core runtime functions
- [ ] Memory allocation/deallocation implementation
- [ ] Standard I/O integration
- [ ] Object creation semantics documentation
- [ ] Function syntax parser support
- [ ] Complete hello_world example compilation

#### 📊 Success Criteria

| Metric | Target | Measurement |
|--------|--------|-------------|
| Hello world compilation | 100% | Compiles to executable binary |
| Basic arithmetic ops | 100% | All examples run correctly |
| Runtime performance | ≤ 2x | vs native GCC O2 |
| Binary size overhead | ≤ 3x | vs equivalent C code |
| Memory safety | 0 leaks | Valgrind clean |

---

### Phase 3: Version 0.5 — Optimizations & Multi-B Backend Support

#### 🎯 Objectives

Apply optimization passes and enable multiple target backends.

#### ✨ Optimization Passes

| Optimization | Priority | Description | Status |
|--------------|----------|-------------|--------|
| Constant Folding | High | Evaluate constant expressions | 🔵 Planned |
| Dead Code Elimination | High | Remove unreachable code | 🔵 Planned |
| Inline Expansion | Medium | Embed small functions | 🟡 Next |
| Loop Unrolling | Medium | Expand loop iterations | 🔮 Future |

#### ✨ Multi-B Backend Architecture

**Supported Backends:**
- **C99 Backend** (✅ Implemented) - Production-ready
- **LLVM IR Backend** (🔵 Planned) - Max performance
- **WebAssembly Backend** (🔮 Future) - Web/edge deployment

**Backend Interface:**
```c
backend_codegen* backend_create_c99(config);
backend_codegen* backend_create_llvm(config);
backend_codegen* backend_create_wasm(config);

bool backend_generate(backend, ast, output_buffer);
```

Usage:
```bash
terra build --emit c99 program.t -o program.c
clang -O2 program.c -o program

terra build --emit llvm program.t -o program.ll
opt -O3 program.ll -o program_opt.ll
clang program_opt.ll -o program

terra build --emit wasm program.t -o program.wasm
node program.wasm
```

#### 📋 Deliverables

- [ ] AST optimization pipeline
- [ ] LLVM IR backend (via llvm-capi)
- [ ] WASM binary format writer
- [ ] Cross-compilation toolchain support
- [ ] Backend abstraction layer tests

#### 📊 Success Criteria

| Metric | Target | Platform |
|--------|--------|----------|
| LLVM IR validity | Passes `opt -O3` | All LLVM versions |
| WASM execution | Runs in browser | Chrome, Firefox, Edge |
| WASM execution | Runs in Node.js | v18+ |
| Performance (LLVM) | ≥ 1.5x better | vs C99 backend |

#### 🎯 Objectives

Enhance error reporting quality and establish comprehensive testing infrastructure.

#### ✨ Features

##### 1. Enhanced Diagnostic System

Multi-line error messages with context and suggestions:

```text
error[E001]: type mismatch in assignment
  ┌─ file.t:10:5
  │
10  │   x = "hello"
  │       ─────── expected int, found str
  │
help: did you mean str(x)?
  │
10  │   x = str("hello")
  │       ~~~~~        ~
```

##### 2. Test Infrastructure

- Unit tests for lexer, parser, sema (≥90% coverage)
- Integration test suite (examples that must compile)
- Snapshot testing for regression detection
- Benchmark harness for compiler performance metrics

#### 📋 Deliverables

- [ ] Advanced diagnostic system v2 (based on `core_diagnostics`)
- [ ] Unit tests for all frontend components
- [ ] Integration test suite covering all examples
- [ ] Benchmarking framework with CI integration
- [ ] Contributing guide and testing best practices docs

#### 📊 Success Criteria

| Metric | Target | Measurement |
|--------|--------|-------------|
| Time to first error | ≤ 10ms | Files up to 10K LOC |
| Code coverage | ≥ 85% | Critical paths only |
| Test execution time | ≤ 2min | Full suite on CI |
| Example pass rate | 100% | All examples compile |

---

### Phase 2: Version 0.3 — C99 Codegen Backend

#### 🎯 Objectives

Generate production-quality C99 code with runtime library support.

#### ✨ Features

##### 1. C99 Code Generation Engine

Translation from Tesseract to ANSI C99:

**Tesseract Source:**
```tesseract
Greeter {
    name: str;
    
    greet(target: str) {
        put("Hello, " + target + "!");
    }
}

g = Greeter.new;
g.name = "Alice";
g.greet("world");
```

**Generated C99:**
```c
typedef struct Greeter {
    char* name;
} Greeter;

void Greeter_greet(Greeter* self, char* target) {
    put("Hello, ");
    put(target);
    put("!");
}

int main(int argc, char** argv) {
    Greeter* g = (Greeter*)malloc(sizeof(Greeter));
    g->name = malloc(6);
    strcpy(g->name, "Alice");
    Greeter_greet(g, "world");
    return 0;
}
```

##### 2. Runtime Library

Memory management and standard functions:

- Arena allocator for efficient allocation
- Reference counting for objects
- Standard library: `std.put()`, `std.assert()`, `len()`, `input()`
- Linker scripts for executable creation

#### 📋 Deliverables

- [ ] C99 code generation engine
- [ ] Type representation in C (primitives, classes, arrays)
- [ ] Method dispatch implementation
- [ ] Memory management runtime (arena + refcount)
- [ ] Standard library `std.*` module
- [ ] Linker scripts for executable creation

#### 📊 Success Criteria

| Metric | Target | Notes |
|--------|--------|-------|
| Compilation success | 100% | All semantically valid programs |
| Performance penalty | ≤ 2x | Compared to native GCC O2 |
| Binary size overhead | ≤ 3x | vs equivalent manual C code |
| Memory safety | 0 leaks | Valgrind clean |

---

### Phase 3: Version 0.5 — Optimizations & Performance

#### 🎯 Objectives

Apply optimization passes to improve runtime performance and binary size.

#### ✨ Optimization Passes

| Optimization | Priority | Description | Status |
|--------------|----------|-------------|--------|
| Constant Folding | High | Evaluate constant expressions at compile time | 🔵 Planned |
| Dead Code Elimination | High | Remove unreachable code blocks | 🔵 Planned |
| Inline Expansion | Medium | Embed function bodies directly | 🟡 Next |
| Loop Unrolling | Medium | Expand loop iterations | 🔮 Future |
| Strength Reduction | Low | Replace expensive ops with cheaper ones | 🔮 Future |

#### ✨ Example: Automatic Optimizations

```tesseract
// Optimizations applied automatically
result = expensive_function(a, b) + 0;  // +0 eliminated
if (true) { /* dead branch removed */ }
return compute(x);  // inlined if small enough
```

#### 📋 Deliverables

- [ ] AST optimization pipeline
- [ ] Simple IR for cross-pass analysis
- [ ] Constant propagation engine
- [ ] Unused code detection and removal
- [ ] Function inlining heuristics
- [ ] Benchmark comparison vs unoptimized build

#### 📊 Success Criteria

| Metric | Target | Impact |
|--------|--------|--------|
| Runtime speedup | ≥ 1.2x | Optimized vs non-optimized |
| Binary size reduction | ≥ 15% | After optimization passes |
| Compilation overhead | ≤ 20% | Extra time for optimizations |
| Optimization coverage | ≥ 80% | Identifiable opportunities |

---

### Phase 4: Version 0.5 — Multi-Target Support

#### 🎯 Objectives

Support multiple compilation targets for different use cases.

#### 🎯 Target Backends Comparison

| Backend | Use Case | Priority | Timeline |
|---------|----------|----------|----------|
| C99 | General purpose | High | ✅ Done |
| LLVM IR | Max performance | High | 🟡 Next |
| WebAssembly | Web/edge | Medium | 🏗️ Later |
| Native (x86) | Raw speed | Low | 🔮 Future |

#### ✨ Features

**LLVM IR Backend:**
```bash
terra compile --emit llvm-ir program.t -o program.ll
clang --target=x86_64-linux-gnu -O3 program.ll -o program
```

**WebAssembly Backend:**
```bash
terra compile --emit wasm program.t -o program.wasm
node --wasm program.wasm
```

**Cross-Compilation:**
```bash
# Linux → Windows
terra compile --target x86_64-pc-windows-gnu program.t

# Linux → macOS
terra compile --target x86_64-apple-darwin program.t

# Linux → ARM
terra compile --target aarch64-linux-gnu program.t
```

#### 📋 Deliverables

- [ ] LLVM IR code generation (via llvm-capi)
- [ ] WebAssembly binary format writer
- [ ] Cross-compilation toolchain support
- [ ] Runtime JIT for dynamic compilation
- [ ] Backend abstraction layer (common interface)

#### 📊 Success Criteria

| Metric | Target | Platform |
|--------|--------|----------|
| LLVM IR validity | Passes `opt -O3` | All LLVM versions |
| WASM execution | Runs in browser | Chrome, Firefox, Edge |
| WASM execution | Runs in Node.js | v18+ |
| Cross-compilation | Successful binaries | Linux→Win, Mac, ARM |
| Performance (LLVM) | ≥ 1.5x better | vs C99 backend |

---

### Phase 5: Version 0.6 — Standard Library & Package Manager

#### 🎯 Objectives

Provide comprehensive standard library and dependency management.

#### 📦 Standard Library Modules

| Module | Description | Status |
|--------|-------------|--------|
| `std.io` | Input/output, file operations | 🟡 Planned |
| `std.string` | String manipulation | 🟡 Planned |
| `std.collections` | Arrays, lists, maps | 🟡 Planned |
| `std.math` | Math functions | 🟡 Planned |
| `std.datetime` | Date/time handling | 🔮 Future |
| `std.net` | Network protocols | 🔮 Future |

#### 💼 Package Manager (`terrapkg`)

**Declaration (`terrapkg.toml`):**
```toml
[package]
name = "my-project"
version = "0.1.0"

[dependencies]
stdlib = ">=0.6.0"
http-client = "1.2.3"
json-parser = "^2.0"

[dev-dependencies]
test-framework = "latest"
```

**Module System:**
```tesseract
import std.io
import std.string
from std.math import sqrt, abs

use std.logging { level: Debug }

fn main() {
    let path = std.io.read_line();
    put("Hello, " + path + "!");
}
```

#### 📋 Deliverables

- [ ] `std.io` module (read, write, file I/O)
- [ ] `std.string` module (concat, split, replace)
- [ ] `std.collections` module (Array, HashMap, LinkedList)
- [ ] Module loader and resolver
- [ ] Package manager CLI (`terrapkg`)
- [ ] Public package registry MVP

#### 📊 Success Criteria

| Metric | Target | Measurement |
|--------|--------|-------------|
| Std lib coverage | 100% | Core functionality |
| Dependency resolution | ≤ 1s | Projects with 10 packages |
| Build time improvement | ≥ 30% | With caching enabled |
| Offline mode | Functional | Local cache only |

---

### Phase 6: Version 0.7 — Concurrency & Parallelism

#### 🎯 Objectives

Enable parallel compilation and concurrent program execution.

#### ⚡ Concurrency Model

**Parallel Execution:**
```tesseract
let results = async_map([1, 2, 3, 4], |x| {
    expensive_computation(x)
}, threads: 4);
```

**Async/Await Pattern:**
```tesseract
async fn fetch_data(url: str) -> str {
    let response = http_get(url).await;
    return parse(response);
}
```

**Channels for Communication:**
```tesseract
channel = make_channel[int](buffer: 10);
spawn send_to(channel);
let value = channel.recv().await;
```

#### 📋 Deliverables

- [ ] Thread pool implementation
- [ ] Task scheduler for work-stealing
- [ ] Async/await compiler support
- [ ] Channel-based communication primitives
- [ ] Concurrent data structures (ConcurrentQueue, AtomicRef)
- [ ] Race condition detector (thread sanitizer)

#### 📊 Success Criteria

| Metric | Target | Scaling |
|--------|--------|---------|
| Compile speedup | ≥ 2x | -jN flag for N cores |
| Runtime scaling | Linear | Up to 8 cores |
| Data races | 0 detected | Thread sanitizer |
| Async overhead | ≤ 5% | vs sync baseline |

---

### Phase 7: Version 0.8 — Advanced Type System

#### 🎯 Objectives

Add advanced type features for expressive and safe code.

#### 🔷 Type System Features

**Generics with Constraints:**
```tesseract
class List[T] {
    items: Array[T];
    
    fn append(item: T) {
        items.push(item);
    }
    
    fn get(index: int) -> T {
        return items[index];
    }
}

// Usage
let int_list = List[int].new();
let str_list = List[str].new();
```

**Type Inference:**
```tesseract
let inferred = 42;              // inferred as int
let string_val = "hello";       // inferred as str
let array = [1, 2, 3];          // inferred as Array[int]
```

**Union Types:**
```tesseract
type Result = success[str] | error[std.error.Error];

fn parse(text: str) -> Result {
    if text == "" {
        return error(ParseError::Empty);
    }
    return success(text.trim());
}
```

**Traits (Interfaces):**
```tesseract
trait Printable {
    fn print() -> str;
}

impl Printable for Int {
    fn print() -> str {
        return str(self.value);
    }
}
```

#### 📋 Deliverables

- [ ] Generic type parameter system
- [ ] Type inference engine (Hindley-Milner style)
- [ ] Union type representation and pattern matching
- [ ] Trait system with automatic impl resolution
- [ ] Higher-kinded types (optional)
- [ ] Type-level programming basics

#### 📊 Success Criteria

| Metric | Target | Notes |
|--------|--------|-------|
| Inference coverage | ≥ 80% | Code without annotations |
| Binary size increase | ≤ 20% | With generics |
| Trait resolution time | ≤ 100ms | For large projects |
| Type safety | 100% | No runtime type errors |

---

### Phase 8: Version 0.9 — Tooling & Developer Experience

#### 🎯 Objectives

Provide excellent developer experience with integrated tools.

#### 🛠️ Tooling Ecosystem

| Tool | Purpose | Status |
|------|---------|--------|
| `terra-lsp` | Language Server Protocol | 🟡 Planned |
| `terra-dbgr` | Debugger (GDB integration) | 🔮 Future |
| `terra-profil` | Runtime profiler | 🔮 Future |
| `terra-fmt` | Code formatter | 🟡 Planned |
| `terra-lint` | Static analysis | 🟡 Planned |

#### ✨ LSP Features

- Go to definition/references
- Auto-completion (IntelliSense)
- Hover documentation
- Find symbols (workspace-wide search)
- Rename symbol across files
- Diagnostics streaming on-save
- Workspace symbols

#### ✨ Formatter Rules

```tesseract
// terra fmt applies automatically
Class Example {
    field: int;  // single space after colon

    init(value: int) {
        self.field = value;
    }

    compute(other: int) -> int {
        return self.field + other;
    }
}
```

#### 📋 Deliverables

- [ ] LSP server implementation (language-server.org spec)
- [ ] Editor plugins (VS Code extension, Neovim plugin)
- [ ] Code formatter (deterministic, idempotent)
- [ ] Linter with style and security rules
- [ ] Debugger protocol (DAP integration)
- [ ] Profiler UI (flame graphs, hot spots)

#### 📊 Success Criteria

| Metric | Target | Measurement |
|--------|--------|-------------|
| LSP conformance | ≥ 90% | Official LSP test suite |
| Formatting speed | ≤ 500ms | File with 1K lines |
| Debugger features | Complete | Breakpoints, step, watch |
| Lint accuracy | ≥ 80% | Common errors caught |

---

### Phase 9: Version 1.0 — Stability & Production Readiness

#### 🎯 Objectives

Achieve production-ready status with stable API/ABI.

#### 🔒 Stability Commitments

1. **API Stability**: All public symbols have ABI-version scripts
2. **ABI Compatibility**: Old binaries work with new compiler versions
3. **Deprecation Policy**: 2 major release warning before removal
4. **Changelog**: Every change documented with migration guides

#### ✅ Production Checklist

- [ ] API surface frozen and fully documented
- [ ] All benchmarks meet targets vs competitors
- [ ] Coverage tests ≥ 95% for critical paths
- [ ] Security audit completed
- [ ] Performance regression tests in CI
- [ ] Migration guides for every breaking change
- [ ] Production-ready documentation website

#### 📊 Benchmarks Against Competitors

| Metric | Terra | GCC | Clang | Notes |
|--------|-------|-----|-------|-------|
| Compile speed | Baseline | +X% | +Y% | Measured on 100K LOC |
| Binary size | Baseline | ±Z% | ±W% | Optimized (-O3) |
| Runtime perf | Baseline | ±A% | ±B% | Same optimization level |

#### 📋 Deliverables

- [ ] Version 1.0 release with stable API/ABI
- [ ] Comprehensive user guide (100+ pages)
- [ ] API reference documentation
- [ ] Tutorial series (beginner to advanced)
- [ ] Cookbook of common patterns
- [ ] Community contribution guidelines
- [ ] Official website with download mirrors

#### 📊 Success Criteria

| Criterion | Target | Verification |
|-----------|--------|--------------|
| Semantic versioning | Upheld | All 1.x releases |
| Performance parity | Within 10% | Of best alternative |
| Documentation | Complete | All public APIs |
| Community adoption | ≥ 100 stars | GitHub metrics |
| Active contributors | ≥ 10 | PRs/issues monthly |

---

## 🚀 Beyond 1.0: Long-term Vision

### 1.1 — Cloud & Distributed Compilation
- Cloud-based compilation service
- Global distributed caching
- Continuous integration platform

### 1.2 — Machine Learning Integration
- ML-powered code optimization suggestions
- Auto-tuning based on workload profiles
- Neural code generation assistance

### 1.3 — Edge Computing
- Microcontroller support (Cortex-M, ESP32)
- Embedded systems toolchain
- Real-time OS targets

### 1.4 — Quantum Computing
- Quantum circuit compilation
- QASM backend generation
- Hybrid classical-quantum programs

---

## 📈 Key Performance Indicators

Tracked per release:

### 1. Compiler Performance
- ✓ Compilation speed (LOC/sec)
- ✓ Memory usage during compile
- ✓ Binary size ratio vs manual C

### 2. Runtime Performance
- ✓ Execution speed vs reference implementations
- ✓ Memory efficiency (heap usage)
- ✓ Startup time

### 3. Developer Experience
- ✓ Time to first compile (new user)
- ✓ Error message clarity score
- ✓ Documentation coverage

### 4. Ecosystem Health
- ✓ Number of published packages
- ✓ Active contributors
- ✓ Community engagement (issues, PRs)

---

## 🔄 Change Process

Every version follows this workflow:

```
Proposal RFC → Implementation → Testing Beta → Community Feedback → Release → Maintenance
```

### Phases

1. **Proposal Phase**: RFC (Request for Comments) for major changes
2. **Implementation Phase**: Develop in feature branches
3. **Testing Phase**: Beta releases, community feedback
4. **Release Phase**: Tagged release with changelog
5. **Maintenance Phase**: Bug fixes for minor versions

### Changelog Format

```markdown
## Version 0.2.0

### Added
- Enhanced diagnostics with context-aware suggestions
- Test suite framework with snapshot testing

### Changed
- Error messages now include hint suggestions
- Lexer uses single-pass tokenization

### Fixed
- Parser crash on malformed class definitions
- Memory leak in arena allocator edge case
```

---

## 🤝 Contribution Guide

### How You Can Help

1. **Report Issues**: Bugs, feature requests, documentation gaps
2. **Submit PRs**: Fixes, new features, improvements
3. **Write Tests**: Increase coverage, add edge cases
4. **Improve Docs**: Tutorials, API reference, examples
5. **Spread the Word**: Blog posts, talks, workshops
6. **Pick a Small Task**: Take the next `[]` item from [`road.txt`](road.txt)

### Getting Started

```bash
git clone https://github.com/nazar-ponomarenko-dots/Terra
cd terra
cmake -S . -B build -G Ninja
ninja -C build
ninja -C build test
```

---

## 📝 License & Attribution

This project is licensed under the terms specified in the [LICENSE](LICENSE) file.

**Copyright © 2026 Terra Contributors**
