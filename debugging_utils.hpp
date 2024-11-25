#include <string>
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <iostream>
#include <regex>

#ifndef DEBUGGING_UTILS_H
#define DEBUGGING_UTILS_H

void printBacktraceSelfImplemented();
void resolveAddress(const std::string &input);

#endif //DEBUGGING_UTILS_H
