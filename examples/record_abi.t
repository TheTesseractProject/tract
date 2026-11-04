struct Inner { value: i32; }
struct Outer { inner: Inner; }

fn bump(p: Outer): Outer {
    p.inner.value = p.inner.value + 1;
    return p;
}

fn make_inner(): Inner {
    return Inner { value: 41 };
}

fn main() {
    let value = Outer { inner: Inner { value: 20 } };
    let changed = bump(value);
    assert(changed.inner.value == 21);
    assert(make_inner().value == 41);
    put(changed.inner.value + make_inner().value);
}
