#include <stdio.h>
int main()
{
    union
    { //共用体的所有成员占用同一段内存
        //共用体内存等于最长的成员占用的内存
        short value;
        char a[sizeof(short)]; //a[2],因为共用内存，所以数组a代表了字节数组
    } test;

    test.value = 0x0102;
    printf("%ld\n", sizeof(test));
    if (test.a[0] == 1 && test.a[1] == 2)
        printf("大端模式\n");
    else if (test.a[0] == 2 && test.a[1] == 1)
        printf("小端模式\n");
    else
        printf("未知\n");
    return 0;
}