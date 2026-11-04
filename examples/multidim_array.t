struct Matrix { values: i32[2][2]; }

fn main() {
    let matrix: i32[2][2] = [[1, 2], [3, 4]];
    let wrapped = Matrix { values: [[5, 6], [7, 8]] };
    assert(len(matrix) == 2);
    assert(matrix[1][0] == 3);
    assert(wrapped.values[1][1] == 8);
    put(matrix[1][1] + wrapped.values[0][0]);
}
