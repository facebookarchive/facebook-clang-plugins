#define test(x) _Generic((x), _Bool : 1, char : 2, int : 3, default : 4)

int test_typename(void) {
    char s;
    int y;
    int x = test(s);
    int z = test(y);
    return x + z;
};