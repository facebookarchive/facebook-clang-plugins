class X {
public:
  X(int);
  X(const X&);
  X& operator=(const X&);
  ~X();
};
class Y {
public:
  Y(int);
  ~Y();
};
X f(X);
Y g(Y);
void h() {
  X a(1);
  X b = f(X(2));
  Y c = g(Y(3));
  a = f(a);
}
