
struct X {
  virtual void f() {}
};

template<class T>
bool is_pod() { return __is_pod(T); }

bool a = is_pod<int>();
bool b = is_pod<X>();
