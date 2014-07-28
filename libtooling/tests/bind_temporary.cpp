 struct S {
   public:
     S() { }  // User defined constructor makes S non-POD.
     ~S() { } // User defined destructor makes it non-trivial.
 };
 void test() {
   const S &s_ref = S(); // Requires a CXXBindTemporaryExpr.
 }
