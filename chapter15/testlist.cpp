#include <list>
#include <stdio.h>
int main()
{
    std::list<int> a;
    a.push_back(2);
    printf("%d\n", a.front());
    return 0;
}