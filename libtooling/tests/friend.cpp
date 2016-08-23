
template <class T>
struct AnotherClass {};

template <class T>
struct Y {
  friend class AnotherClass<T *>;
  template <class Z>
  friend class AnotherClass;
};

Y<int> y1;
