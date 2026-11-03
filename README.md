![logo](logo.png)
# Terra - Tesseract Compiler
a fork based on https://github.com/TheTesseractProject/tract

> *Terra* is one of the implementations of the Tesseract language.
> At this time, this implementation may not comply with Tesseract standards.

> [!WARNING]
> This project is currently under active development. Features and documentation may change frequently. At the moment the language is not ready for full-fledged work, it is a prototype.

```tesseract
TesseractClass {
    a: int;
    b: str;
    hi {
         put("Hello there!");
    }
}

Class = TesseractClass.new;
Class.hi;
```

```txt
output:
Hello there!
```


## 🚀 Features

- **Fast Compilation**: Optimized for speed and efficiency
- **Cross-platform**: Works on Linux, macOS, and Windows
- **Modern C**: Written in C23 with best practices
- **Error Handling**: Comprehensive error reporting

## 🧩 Language Notes (current prototype)

- Classes declare fields (`name: type;`) and methods (`name { ... }` or
  `name(args: type) { ... }`).
- `Class.new` allocates a zeroed instance; `Class.init(args)` allocates one and
  calls `init`.
- A field whose type is a user-defined class is a pointer to an instance, so a
  class may reference another class declared later in the same file.
- Methods can be called with or without parentheses (`obj.run;` is `obj.run()`).
- Built-ins: `put`, `print`, `len`, `str`, `int`, `float`, `assert`, `input`.
- Native interop: `@c.printf(...)`, `@math.sqrt(...)`.

## 📋 Requirements

### Build Dependencies
- **CMake** 3.16 or higher
- **C Compiler**: Clang 18 or higher (recommended) or GCC 15 or higher
- **Build System**: Ninja (recommended) or Make

### Runtime Dependencies
- **POSIX-compliant terminal** (for animation feature)
- **Math library** (linked automatically)

## 🛠️ Building

### Quick Start (Recommended)
```bash
# Clone the repository
git clone https://github.com/nazar-ponomarenko-dots/Terra
cd terra
```

```bash
# Build with Clang and Ninja (recommended)
export CC=clang && export CXX=clang++
cmake -S . -B build -G Ninja
cmake --build build
```

```bash
# Build with GCC
export CC=gcc && export CXX=g++
cmake -S . -B build -G Ninja
cmake --build build
```

```bash
# Run the application
./bin/terra --help
```

### Clean Build
```bash
rm -rf bin/ build/
```

## 🎯 Usage

### Basic Commands

```bash
# Show help
./bin/terra --help

# Show version
./bin/terra --version

# Translate a file to C (writes ./output by default)
./bin/terra input.tt

# Translate with a custom output path
./bin/terra input.tt -o output.c

# Only parse and type-check
./bin/terra --check input.tt

# Write the generated C to stdout
./bin/terra input.tt --stdout

```

> Terra emits C99-style source that uses a few GNU extensions (`__auto_type`,
> statement expressions) for assign-to-declare and `Class.init(...)`. Build the
> result with GCC/Clang in GNU mode, e.g. `cc output.c -o program`.

### Command Line Options

| Option | Long Option | Description |
|--------|-------------|-------------|
| `-h` | `--help` | Show help information |
| `-v`, `-V` | `--version` | Show version and build information |
| `-b` | `--build` | Compile the specified file (path may also be positional) |
| `-o` | `--output` | Output path; `-` means stdout (`--output=path` also works) |
| `-c` | `--check` | Parse and type-check only, do not generate C |
| | `--stdout` | Write generated C to standard output |
| `-q` | `--quiet` | Suppress progress messages |
| | `--` | Treat all following arguments as paths |

### Supported File Extensions

- `.tt` - Tesseract source files
- `.tes` - Tesseract source files (alternative)
- `.t` - Tesseract source files (alternative)

## 🧪 Development

### Code Style

This project follows the following coding standards:
- **C23 Standard** compliance
- **4-space indentation** (no tabs)
- **Snake_case** for functions and variables
- **UPPER_CASE** for constants and macros
- **Comprehensive error handling**
- **Memory safety** practices

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

### Development Guidelines

- Follow the existing code style
- Add error handling for all new functions
- Include appropriate documentation
- Test on multiple platforms if possible

## 📄 License

This project is licensed under the terms specified in the [LICENSE](LICENSE) file.

## 🙏 Acknowledgments

- Made with ❤️ by Contributors
- Inspired by the mathematical beauty of 4D tesseracts
- Built with modern C development practices

## 📞 Support

If you encounter any issues or have questions:

1. Check the [Issues](https://github.com/nazar-ponomarenko-dots/Terra/issues) page
2. Create a new issue with detailed information
3. Include your system information and error messages

---

**Terra** - Where mathematics meets performance! 🚀
