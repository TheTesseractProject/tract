import "stdio.h";
extern fn printf(format: str, ...): i32;

fn main() {
    let name = "Tesseract";
    printf("Hello from %s via native C interop!\n", name);
}
