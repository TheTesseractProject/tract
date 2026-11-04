struct Device {
    value: volatile i32;
}

union Register {
    bits: u32;
    bytes: u8[4];
}

fn main() {
    let values: i32[4] = [1, 2, 3, 4];
    let view: slice[i32] = values[1:3];
    assert(len(view) == 2);

    let first: i32* = &values[0];
    *(first + 1) = 7;
    view[0] = 9;
    assert(values[1] == 9);

    let device = Device { value: values[0] };
    let register = Register { bits: 305419896 };
    asm volatile("nop");
    assert(register.bits == 305419896);
    put(device.value + len(view));
}
