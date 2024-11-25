#include "debugging_utils.hpp"

#include <sys/stat.h>

/*
Will output the function-name and its line to reconstruct the callstack
starting with offset addresses
 */
void resolveAddress(const std::string &input) {
    std::cout << input << std::endl;
    std::regex pattern(R"(\(([^()]*)\))");
    std::smatch match;

    if (std::regex_search(input, match, pattern)) {
        std::string address = match.str().substr(1, match.str().length() - 2); // Entfernt die Klammern

        std::string command = "addr2line -e ./prac " + address;
        std::cout << "Executing: " << command << std::endl;

        int ret = std::system(command.c_str());
        if (ret != 0) {
            std::cerr << "Error executing addr2line!" << std::endl;
        }
    } else {
        std::cerr << "Invalid input format!" << std::endl;
    }
}

void printBacktraceSelfImplemented() {
    std::cout << "==== Debugging ====\n";
    void *array[10];
    size_t size = backtrace(array, 10);

    std::cout << "Call Stack:\n";
    backtrace_symbols_fd(array, size, STDERR_FILENO);  // analyze the callstack with addr2line -e ./prac
    std::cout << "End Call Stack" << std::endl;
    std::cout << "========\n";
}

