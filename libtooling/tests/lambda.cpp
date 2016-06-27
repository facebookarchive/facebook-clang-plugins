int main (){
int m,n;

auto f = [](){return 1;};

    auto bar = [&m, n] (int a) { return m; };

    return 0;
}