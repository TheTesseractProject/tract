fn add(x: int, y: int): int {
    return x + y;
}

fn main() {
    let a = 12;
    let b = 5;

    put("add = " + str(add(a, b)));
    put("mul = " + str(a * b));
    put("div = " + str(a / b));
    put("mod = " + str(a % b));
    put("sqrt = " + str(@math.sqrt(25.0)));

    let sum = 0;
    for (let n = 1; n < 11; n = n + 1) {
        sum = sum + n;
    }
    put("sum1to10 = " + str(sum));

    if (a * b == 60) {
        put("check = ok");
    }

    put("hello = " + "world".upper());
}
