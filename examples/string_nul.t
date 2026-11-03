/* Embedded NUL escapes in string literals must survive put()/print():
 * the helper receives the stored byte length instead of relying on a
 * C-style terminating NUL. */
put("a\0b");
put("x\x00y");
put("after");
