template <class T>
struct S {
  T field;
};

template<>
struct S<int> {
  int field;
};

void test(S<int> p) {
  S< S<int>   > val;
  val.field = p;
}
