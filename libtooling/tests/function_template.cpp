
template <class T>
T get(T x) { return x; }

// specialization
template <>
int get(int x) { return 2 * x; }

// explicit instantiacion
template double get(double x);

void test() {
  char c;
  float f;
  c = get(c);
  f = get(f);
}
