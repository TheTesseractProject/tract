// A class may hold a reference to another class declared later, and methods
// may be called before their definition appears in the generated C.
Holder {
    box: Box;

    init(b: Box) {
        self.box = b;
    }

    reveal {
        put("Value: ");
        put(self.box.value);
        put("Doubled: ");
        put(self.box.doubled);
    }
}

Box {
    value: int;

    init(v: int) {
        self.value = v;
    }

    doubled {
        return self.value * 2;
    }
}

b = Box.init(21);
h = Holder.init(b);
h.reveal;
