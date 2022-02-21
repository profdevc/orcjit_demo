#include <iostream>
#include "mul_func.h"
using namespace std;

extern "C"
{
    void *__dso_handle = 0;
}

int main()
{
    int a = 5;
    int c = 8;
    return mul(a, c);
}