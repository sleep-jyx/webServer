#include <iostream>
using namespace std;

int main(int argc, char *argv[])
{
    printf("HTTP/1.1 200 OK\r\nContent-type:hello\r\n\r\n");
    printf("<html>\n");
    printf("<head>\n");
    printf("<title>Hello World - First CGI Program</title>\n");
    printf("</head>\n");
    printf("<body>\n");
    printf("<h2>Hello World! This is my first CGI program</h2>\n");
    printf("</body>\n");
    printf("</html>\n");

    return 0;
}