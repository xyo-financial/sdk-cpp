#include <xyo/client.hpp>
#include <iostream>
#include <utility>

int main() {
    try {
        // Instantiate the Client with a dummy API key configuration
        xyo::ClientConfig config("RandomBase64EncodedStringApiKey");
        xyo::Client client(std::move(config));

        std::cout << "Successfully imported and instantiated the XYO Client (C++)\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
    }
    return 0;
}
