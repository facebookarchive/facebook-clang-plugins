struct A {
  A(){};
  ~A(){};
};

struct B : virtual A {
  B(){};
  ~B(){};
};

struct C {
  C(){};
  ~C(){};
};

struct E : B, virtual C {
  E(){};
  ~E(){};
};
