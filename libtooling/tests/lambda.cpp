int main (){
int m,n;

auto f = [](){return 1;};

    auto bar = [&m, n] (int a) { return m; };

    auto init_capture = [i = 0]() { return i; };

    return 0;
}
