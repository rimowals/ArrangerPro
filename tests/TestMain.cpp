#include "Test.h"

int main()
{
    int ran = 0;
    for (const auto& c : test::registry())
    {
        const int before = test::failures();
        try
        {
            c.body();
        }
        catch (const std::exception& e)
        {
            std::cerr << c.name << ": unexpected exception: " << e.what() << "\n";
            ++test::failures();
        }
        if (test::failures() != before)
            std::cerr << "  in test: " << c.name << "\n";
        ++ran;
    }
    std::cout << ran << " tests, " << test::failures() << " failed checks\n";
    return test::failures() == 0 ? 0 : 1;
}
