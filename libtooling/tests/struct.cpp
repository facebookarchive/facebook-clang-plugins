struct Point {
  double x, y;
};

void fun() {
  struct X {};
  X x;
}

Point blank = {3.0, 4.0};

struct A {
  static int a;
};

int A::a = 32;

struct B {
  static const int b = 52;
};
