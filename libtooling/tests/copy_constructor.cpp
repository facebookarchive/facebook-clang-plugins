
struct A {
    int a, b, c, d;
};

const A& f() {
    static A a;
    return a;
}

int g() {
    auto x = f();
    return x.a + x.b;
}
