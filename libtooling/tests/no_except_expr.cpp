void no_throw() noexcept {};

int main() {
  return noexcept(no_throw());
}
