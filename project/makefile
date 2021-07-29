server:http_conn.cpp http_conn.h  threadpool.h webServer-v2.cpp locker.h
	g++ -o server webServer-v2.cpp http_conn.cpp  http_conn.h threadpool.h  locker.h -lpthread 

cgiMysql:./root/cgi/cgiMysql.cpp
	g++ -o ./root/cgi/cgiMysql.cgi ./root/cgi/cgiMysql.cpp  -lmysqlclient

clean:
	rm  server