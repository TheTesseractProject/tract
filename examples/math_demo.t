// C headers and typed externs make native functions ordinary Tesseract calls.
import "math.h";
import "stdio.h";

extern fn printf(format: str, ...): i32;
extern fn sqrt(value: f64): f64;
extern fn pow(base: f64, exponent: f64): f64;
extern fn sin(value: f64): f64;

fn main() {
    printf("sqrt(9) = %f\n", sqrt(9.0));
    printf("pow(2,8) = %f\n", pow(2.0, 8.0));
    printf("sin(pi/2) = %f\n", sin(3.14159265 / 2.0));
}
