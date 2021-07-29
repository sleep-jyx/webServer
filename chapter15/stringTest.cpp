/* strpbrk example */
#include <stdio.h>
#include <string.h>
void TestStrpbrk()
{
    /*char str[] = "This is a sample string";
    char key[] = "aeiou";
    char *pch;
    printf("Vowels in '%s': ", str);
    pch = strpbrk(str, key);

    while (pch != NULL)
    {
        printf("--info--  %s \n", pch);
        pch = strpbrk(pch + 1, key);
    }
    printf("\n");*/

    char str[] = "GET   / HTTP/1.1";
    char key[] = " \t";
    char *m_version;
    char *pch;
    printf("原字符串:%s \n", str);
    pch = strpbrk(str, key); //搜索到第一次出现空格
    printf("匹配后:%s \n", pch);
    *pch++ = '\0'; //最开始的斜杠去掉
    printf("截断后:%s \n", pch);
    pch += strspn(pch, " \t"); //匹配所有的空格
    printf("匹配所有的空格:%s \n", pch);
    m_version = strpbrk(pch, " \t"); //搜索到第一次出现空格
    printf("匹配出版本:%s\n", m_version);

    if (strcasecmp(str, "GET") == 0)
    {
        printf("匹配到GET方法\n");
    }
}

void testStrspn()
{
    int i;
    int j;
    char strtext[] = "15s122232h";
    char cset[] = "1234567890";

    //匹配所有在集合的符号，直到第一次遇到不在集合中的符号
    i = strspn(strtext, cset);
    //匹配所有不在集合的符号，直到第一次遇到在集合中的符号
    j = strcspn(strtext, cset);
    printf("The initial number has %d digits.\n", i);
    printf("j = %d\n", i);
}
int main()
{
    TestStrpbrk();
    //testStrspn();
    return 0;
}