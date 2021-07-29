1. 进入项目文件
   cd project
2. 修改 http_conn.cpp 文件 14 行对应的网站根目录
3. 安装 mysql 的 C/C++库函数
   sudo apt-get install libmysqlclient-dev
4. 创建 mysql 数据库和数据表

```mysql
//创建数据库
create database webServer;

// 创建user表
USE yourdb;
CREATE TABLE user(
    username char(50) NULL,
    passwd char(50) NULL
)ENGINE=InnoDB;

// 添加数据
INSERT INTO user(username, passwd) VALUES('admin', '123456');
```

5. 修改 /project/root/cgi/cgiMysql.cpp 第 23 行的数据库信息

6. xxx/project $ make server

7. xxx/project $ make cgiMysql

8. xxx/project $ ./server 127.0.0.1 8088

9. 浏览器访问 url 127.0.0.1:8088/log.html

#### 演示

##### 登录页

![](./pic/readme1.png)

##### cgiMysql 验证用户

![](./pic/readme2.png)

##### 主页

![](./pic/readme3.png)

##### 查看图片

![](./pic/readme4.png)
