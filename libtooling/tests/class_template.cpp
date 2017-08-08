namespace N {
template <class T>
struct S {
  T field;
};

template <class T>
struct S<T*> {
  T field; /* not T* !!! */
};

template<>
struct S<int> {
  int field;
};

void test(S<int> p) {
  S<S<int>*> val;
  val.field = p;
}
}
