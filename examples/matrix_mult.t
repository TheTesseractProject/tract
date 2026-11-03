// A small 2x2 matrix product using scalar fields while arrays are being
// designed for the language.
Matrix2 {
    m00: int;
    m01: int;
    m10: int;
    m11: int;
}

let left = Matrix2.new;
left.m00 = 1;
left.m01 = 2;
left.m10 = 3;
left.m11 = 4;

let right = Matrix2.new;
right.m00 = 5;
right.m01 = 6;
right.m10 = 7;
right.m11 = 8;

let p00 = left.m00 * right.m00 + left.m01 * right.m10;
let p01 = left.m00 * right.m01 + left.m01 * right.m11;
let p10 = left.m10 * right.m00 + left.m11 * right.m10;
let p11 = left.m10 * right.m01 + left.m11 * right.m11;

put("Product:");
put(p00);
put(p01);
put(p10);
put(p11);
