// Matrix operations demo

Matrix {
    rows: int;
    cols: int;
    value: int;
    
    init(r: int, c: int) {
        self.rows = r;
        self.cols = c;
        self.value = 0;
    }
    
    set(v: int) {
        self.value = v;
    }
}

Demo {
    a: int;
    b: int;
    
    run() {
        self.a = 10;
        self.b = 20;
        sum = self.a + self.b;
        prod = self.a * self.b;
        put("Sum: ");
        put(sum);
        put("Product: ");
        put(prod);
    }
}


d = Demo.new;
d.run;
