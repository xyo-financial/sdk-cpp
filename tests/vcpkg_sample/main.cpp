#include <xyo/client.hpp>
#include <iostream>
#include <utility>

int main() {
    xyo::ClientConfig config("vcpkg_test_token");
    xyo::Client client(std::move(config));
    std::cout << "vcpkg consumer built and linked XYO::SDK successfully!\n";
    return 0;
}
