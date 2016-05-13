class Foo {
public:
  void func() {} // a non-const member function
};

void someFunction( const Foo& f )  {
  Foo &fRef = const_cast<Foo&>(f);
  fRef.func();   // okay
}

constexpr int i = 1;