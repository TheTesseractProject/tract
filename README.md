# TRACT - Tesseract Compiler

[![GitHub Release](https://img.shields.io/github/v/release/pka-human/tract?style=plastic)](https://github.com/pka-human/tract/releases)
[![GitHub Downloads](https://img.shields.io/github/downloads/pka-human/tract/total?style=plastic)](https://github.com/pka-human/tract/releases)
[![GitHub code size](https://img.shields.io/github/languages/code-size/pka-human/tract?style=plastic)](https://github.com/pka-human/tract)
[![GitHub lines of code](https://tokei.rs/b1/github/pka-human/tract)](https://github.com/pka-human/tract)
[![CMake](https://img.shields.io/badge/CMake-3.16+-blue.svg?style=plastic)](https://cmake.org/)
[![C Standard](https://img.shields.io/badge/C-11-blue.svg?style=plastic)](https://en.wikipedia.org/wiki/C11_(C_standard_revision))

> **TRACT** - Lightning-fast Tesseract language compiler with aggressive optimization for peak performance. Tesseract is simple but with great power.

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
- **Modern C**: Written in C11 with best practices
- **Error Handling**: Comprehensive error reporting

## 📋 Requirements

### Build Dependencies
- **CMake** 3.16 or higher
- **C Compiler**: GCC 4.9+ or Clang 3.5+
- **Ninja** (recommended) or Make

### Runtime Dependencies
- **POSIX-compliant terminal** (for animation feature)
- **Math library** (linked automatically)

## 🛠️ Building

### Quick Start (Recommended)
```bash
# Clone the repository
git clone https://github.com/pka-human/tract.git
cd tract
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
./bin/tract --help
```

### Clean Build
```bash
rm -rf bin/ build/
```

## 🎯 Usage

### Basic Commands

```bash
# Show help
./bin/tract --help

# Show version
./bin/tract --version

# Build a file
./bin/tract input.tt

# Build with custom output
./bin/tract input.tt -o output

# Show tesseract animation
./bin/tract --tesseract
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
- **C11 Standard** compliance
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

- Made with ❤️ by [@pka_human](https://github.com/pka-human)
- Inspired by the mathematical beauty of 4D tesseracts
- Built with modern C development practices

## 📞 Support

If you encounter any issues or have questions:

1. Check the [Issues](https://github.com/pka-human/tract/issues) page
2. Create a new issue with detailed information
3. Include your system information and error messages

---

**TRACT** - Where mathematics meets performance! 🚀 
