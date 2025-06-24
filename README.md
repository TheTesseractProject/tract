![GitHub Release](https://img.shields.io/github/v/release/pka-human/tract?style=plastic)
![GitHub Downloads (all assets, all releases)](https://img.shields.io/github/downloads/pka-human/tract/total?style=plastic)
![GitHub code size in bytes](https://img.shields.io/github/languages/code-size/pka-human/tract?style=plastic)

# TRACT

> [!NOTE]
> Make sure, that you have installed **clang** *(or gcc)*, **cmake** and **ninja**.

Build with CLANG (***recommended***):
```bash
export CC=clang && export CXX=clang++ && cmake -S . -B build -G Ninja && cmake --build build
```

Build with GCC:
```bash
export CC=gcc && export CXX=g++ && cmake -S . -B build -G Ninja && cmake --build build
```

Clean build:
```bash
rm -rf build bin
```

Run:
```bash
./bin/tract
```


