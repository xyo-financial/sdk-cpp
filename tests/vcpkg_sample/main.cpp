#include <xyo/xyo_client.hpp>
#include <iostream>

int main() {
    xyo::ClientConfig config("vcpkg_test_token");
    xyo::XyoClient client(config);
    std::cout << "vcpkg consumer built and linked XYO::SDK successfully!\n";
    return 0;
}
