#pragma once

#include "engine/frontend/ast.h"

#include <stdbool.h>

/*
 * Enhanced Semantic analyser for Tesseract with full type checking.
 *
 * Returns true if the program is well-formed; on the first error it prints a
 * diagnostic (same format as the parser) and returns false.  Analysis is done
 * in three passes:
 *
 *  1. Pre-declare all top-level class names so forward references work.
 *  2. Type inference pass: infer types for variables based on initializers.
 *  3. Validation pass: check type compatibility, member access, arity, etc.
 *
 * Type system features:
 *  - Built-in types: int, float, str, bool, null
 *  - Class types for object instances
 *  - Type compatibility checking for arithmetic operations
 *  - String concatenation and comparison
 *  - Boolean logic and comparison operations
 *  - Member access validation on class-typed objects
 *  - Method call signature checking
 *  - Ternary expression type consistency
 *
 * What it catches:
 *  - Use of an undeclared identifier.
 *  - Assignment to a non-lvalue expression.
 *  - Type mismatch in assignments.
 *  - Type incompatibility in binary operations (int + string, etc.)
 *  - Non-boolean condition in if/while statements.
 *  - Access to a member that is not declared on a known class.
 *  - Wrong argument count or types for built-in functions.
 *  - Wrong number of arguments in method calls.
 *  - `return` with wrong type outside a method body.
 *  - Duplicate class / field / method names.
 */
bool tess_sema_check(tess_program *program, const char *src, const char *path);

/* Get inferred type name for an expression (for diagnostics) */
const char* tess_sema_type_name(tess_expr_kind kind, const char *class_name);
