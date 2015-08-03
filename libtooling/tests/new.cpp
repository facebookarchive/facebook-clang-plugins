struct pod {
  int a;
};

struct cpplike {
  int a;
  int b;
  pod c;
  cpplike() : a(10), b(2) {}
  cpplike(int a, int b) : a(a), b(b) {}
};

void test() {
  auto* i = new int(2);
  auto* i_a = new int[10];

  auto* p = new pod;
  auto* p_a = new pod[10];

  auto* c = new cpplike(1, 2);
  auto* c_a = new cpplike[10];

  delete i;
  delete[] i_a;
  delete p;
  delete[] p_a;
  delete c;
  delete[] c_a;
}

// This isn't exported quite right yet
void test_c11() {
  auto* i = new int[3] {1,2,3};
  auto* c = new cpplike {1,2};
  auto* c_a = new cpplike[4] {{1,2}, {3,4}, {5,6}};//initializer list is one too short
}
