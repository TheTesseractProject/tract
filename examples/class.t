TestClass {
    a: int;
    b: str;

    c {
        put("Hello!");
    }

    show(prefix: str) {
        put(prefix);
        put(b);
    }
}

t = TestClass.new;
t.b = "world";
t.c;
t.show("> ");
