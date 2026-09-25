#include "TestHarness.h"

#include <iostream>

namespace avtest {

std::vector<TestCase>& AllTests()
{
    static std::vector<TestCase> tests;
    return tests;
}

void AddTest(std::string name, std::function<void()> body)
{
    AllTests().push_back(TestCase{ std::move(name), std::move(body) });
}

void Fail(const char* file, int line, const std::string& message)
{
    std::ostringstream oss;
    oss << file << ":" << line << "  " << message;
    throw Failure(oss.str());
}

} // namespace avtest

int main(int argc, char** argv)
{
    const std::string filter = argc > 1 ? argv[1] : std::string();

    int passed = 0;
    int failed = 0;
    for (const avtest::TestCase& test : avtest::AllTests())
    {
        if (!filter.empty() && test.name.find(filter) == std::string::npos) continue;
        try
        {
            test.body();
            ++passed;
            std::cout << "[ PASS ] " << test.name << "\n";
            std::cout.flush();
        }
        catch (const avtest::Failure& failure)
        {
            ++failed;
            std::cout << "[ FAIL ] " << test.name << "\n         " << failure.what() << "\n";
        }
        catch (const std::exception& error)
        {
            ++failed;
            std::cout << "[ FAIL ] " << test.name << "\n         未预期异常: " << error.what() << "\n";
        }
    }

    std::cout << "\n" << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
