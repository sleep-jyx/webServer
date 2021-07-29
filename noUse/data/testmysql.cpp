#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <mysql/mysql.h>
void testlink()
{
    MYSQL *ConnectPointer = mysql_init(NULL);
    ConnectPointer = mysql_real_connect(ConnectPointer, "127.0.0.1", "root", "muou123", "2021summer", 0, NULL, 0);
    if (ConnectPointer)
    {
        printf("connect database successfully\n");
    }
    else
    {
        printf("failed to connect database\n");
    }
    mysql_close(ConnectPointer);
    printf("database connection closed successfully\n");
    printf("\n");
}

void testSearch()
{
    char SelectAllExec[] = "select * from book";
    int ResultNum = 0;
    MYSQL_RES *Result;
    MYSQL_ROW Row;
    MYSQL ConnectPointer;
    mysql_init(&ConnectPointer);
    mysql_real_connect(&ConnectPointer, "127.0.0.1", "root", "muou123", "2021summer", 0, NULL, 0);
    if (&ConnectPointer)
    {
        printf("connect database successfully\n");
    }
    else
    {
        printf("failed to connect database\n");
        return;
    }
    //get exec result
    ResultNum = mysql_query(&ConnectPointer, SelectAllExec);
    if (ResultNum != 0)
    {
        printf("search data failed\n");
        return;
    }
    Result = mysql_store_result(&ConnectPointer);
    if (Result == NULL)
    {
        printf("get search data failed\n");
        return;
    }
    while ((Row = mysql_fetch_row(Result)) != NULL)
    {
        printf("%s\t%s\t%s\t%s\t\n", Row[0], Row[1], Row[2], Row[3]);
    }

    mysql_close(&ConnectPointer);
    printf("database connection closed successfully\n");
}

//插入
int insertMySQL(MYSQL *Pointer, char *Table, char *ID, int Age)
{
    char Exec[80] = "insert into ";
    char AgeStr[20] = "\0";
    int ResultNum, AffectRow = 0;
    sprintf(AgeStr, "%d", Age); //数字转字符串
    strcat(Exec, Table);
    strcat(Exec, " values('");
    strcat(Exec, ID);
    strcat(Exec, "',");
    strcat(Exec, AgeStr);
    strcat(Exec, ");");
    ResultNum = mysql_query(Pointer, Exec);
    AffectRow = (int)mysql_affected_rows(Pointer);
    if (!ResultNum)
    {
        printf("insert command [%s] exec successfully,affect row:%d row\n", Exec, AffectRow);
        return 0;
    }
    else
    {
        printf("an error occured while insert data\n");
        return -1;
    }
}

//删除
int deleteMySQL(MYSQL *Pointer, char *Table, char *ID)
{
    char Exec[80] = "delete from ";
    int ResultNum, AffectRow = 0;
    //make str
    strcat(Exec, Table);
    strcat(Exec, " where ID='");
    strcat(Exec, ID);
    strcat(Exec, "';");
    ResultNum = mysql_real_query(Pointer, Exec, (unsigned int)strlen(Exec));
    if (!ResultNum)
    {
        printf("delete data [ID=%s] successfully\n", ID);
        return 0;
    }
    else
    {
        printf("an error occured while delete data\n");
        return -1;
    }
}

int main()
{
    testSearch();
    return 0;
}
