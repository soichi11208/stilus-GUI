// test/test_framework.hpp
#pragma once
#include <string>
#include <vector>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace test {

struct TestCase {
    std::string name;
    std::function<void()> func;
};

class TestRunner {
public:
    static TestRunner& instance() {
        static TestRunner runner;
        return runner;
    }

    void add_test(const std::string& name, std::function<void()> func) {
        tests_.push_back({name, func});
    }

    int run(const std::string& filter = "") {
        int passed = 0, failed = 0;
        for (const auto& test : tests_) {
            if (!filter.empty() && test.name.find(filter) == std::string::npos) {
                continue;
            }
            std::cout << "Running: " << test.name << "... " << std::flush;
            try {
                test.func();
                std::cout << "PASSED" << std::endl;
                ++passed;
            } catch (const std::exception& ex) {
                std::cout << "FAILED: " << ex.what() << std::endl;
                ++failed;
            } catch (...) {
                std::cout << "FAILED (unknown exception)" << std::endl;
                ++failed;
            }
        }
        std::cout << "\nResults: " << passed << " passed, " << failed << " failed" << std::endl;
        return failed > 0 ? 1 : 0;
    }

private:
    std::vector<TestCase> tests_;
};

class TestRegistrar {
public:
    TestRegistrar(const std::string& name, std::function<void()> func) {
        TestRunner::instance().add_test(name, func);
    }
};

} // namespace test

#define TEST(name) \
    void test_##name(); \
    static test::TestRegistrar registrar_##name(#name, test_##name); \
    void test_##name()
