// C typedefs in extern signatures are opaque pointer types supplied by the
// included header. This keeps FILE's layout private to libc.
import "stdio.h" as stdio;

extern fn fopen(path: str, mode: str): FILE;
extern fn fputs(text: str, stream: FILE): i32;
extern fn fclose(stream: FILE): i32;

fn main() {
    let file = fopen("terra-output.txt", "w");
    if (file != null) {
        fputs("Written through the native C file API.\n", file);
        fclose(file);
    }
}
