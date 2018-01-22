#include <cstdio>

#include <vector>
#include <string>
#include <functional>

template<typename T>
using Vector = std::vector<T>;
using String = std::string;

struct {
    struct {
        std::function<void(const String&)> println;
    } out;
} System = {{
    [](const String &s) { printf("%s\n", s.c_str()); }
}};

class Foo {
    public: static void Main(String args[]) {
        System.out.println("Hello");
    }
};

int main(int argc, char **argv) {
    return Foo::Main(Vector<String>(argv, argv+argc).data()), 0;
}

