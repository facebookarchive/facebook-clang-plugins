  struct A {
    virtual void f() { }
  };
  struct B : public A { };
  struct C { };

  void f () {
    A a;
    B b;

    A* ap = &b;
    B* b1 = dynamic_cast<B*> (&a);
    B* b2 = dynamic_cast<B*> (ap);
    C* c = dynamic_cast<C*> (ap);

    A& ar = dynamic_cast<A&> (*ap);
    B& br = dynamic_cast<B&> (*ap);
    C& cr = dynamic_cast<C&> (*ap);
  }
