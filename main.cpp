#include <print>
#include <stdexcept>
#include "Source/Runtime/Core/Application.hpp"
#include "Source/Runtime/Core/StartupSplash.hpp"

int main() try
{
    engine::StartupSplash splash;
    splash.Show();

    engine::Application app([&splash](float progress, std::string_view stage)
        {
            splash.SetProgress(progress, stage);
		});
    splash.Close();
    app.ShowMainWindow();
    app.Run();
    return 0;
}
catch (std::exception const& e)
{
    std::print(stderr, "Error: {}\n", e.what());
    return 1;
}