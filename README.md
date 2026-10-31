![logo](logo.png)
# Terra - Tesseract Compiler
a fork based on https://github.com/TheTesseractProject/tract

> *Terra* is one of the implementations of the Tesseract language.

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

Class = TesseractClass.init;
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

# Build a file
./bin/terra input.tt

# Build with custom output
./bin/terra input.tt -o output

# Show tesseract animation
./bin/terra --tesseract
```

### Command Line Options

| Option | Long Option | Description |
|--------|-------------|-------------|
| `-h` | `--help` | Show help information |
| `-v` | `--version` | Show version information |
| `-b` | `--build` | Build the specified file |
| `-o` | `--output` | Specify output file path |
| | `--tesseract` | Show ASCII tesseract animation |

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
