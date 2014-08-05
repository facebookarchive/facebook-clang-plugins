template <typename T> struct S {
     template <class U> static int foo(U *);
     static const unsigned int s = sizeof (S<T>::foo<T>(0));
};
