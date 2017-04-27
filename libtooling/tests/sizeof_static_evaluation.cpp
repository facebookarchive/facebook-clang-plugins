template<typename T, bool> class __foo;

template<typename T>
class __foo<T, true> {
  int x = sizeof(T);
  public: __foo() {};
};

int y = sizeof(char);
