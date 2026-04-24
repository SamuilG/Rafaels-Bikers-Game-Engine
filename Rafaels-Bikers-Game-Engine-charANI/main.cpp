#include <print>
#include <stdexcept>
#include "Source/Runtime/Core/Application.hpp"

int main() try
{
    engine::Application app;
    app.Run();
    return 0;
}
catch (std::exception const& e)
{
    std::print(stderr, "Error: {}\n", e.what());
    return 1;
}