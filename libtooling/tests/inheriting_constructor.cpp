struct A {
    A( int ) {}
  void foo() {};
};

struct B : A 
{
     using A::A;
};

int main() {
    B b(5);
}
