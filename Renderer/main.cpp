#include "Application.h"
#include <iostream>
#include <stdexcept>

int main() {
    try {
        Application app;
        app.Run();
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal Exception intercepted: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}