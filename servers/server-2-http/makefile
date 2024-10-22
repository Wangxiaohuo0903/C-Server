CC = g++ # 指定C++编译器

# 定义目标
all: myserver

myserver: myserver.o
	$(CC) myserver.o -o $@

myserver.o: myserver.cpp
	$(CC) -c myserver.cpp -o $@

# 清理规则
clean:
	rm -f myserver myserver.o