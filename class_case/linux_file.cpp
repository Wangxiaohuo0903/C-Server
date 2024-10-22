#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <cstring>
#include <cerrno>
#include <iostream>

int main() {
    // 1. 使用open函数打开或创建一个文件，并获取文件描述符
    const char *filename = "example.txt";
    int flags = O_RDWR | O_CREAT;
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    int fd = open(filename, flags, mode);
    if (fd == -1) {
        std::cerr << "open error: " << strerror(errno) << std::endl;
        return errno;
    }

    // 2. 向文件中写入数据
    const char *message = "Hello, World!";
    ssize_t written = write(fd, message, strlen(message));
    if (written == -1) {
        std::cerr << "write error: " << strerror(errno) << std::endl;
        close(fd);
        return errno;
    }

    // 3. 重新定位文件偏移量到文件开头
    off_t offset = 0;
    off_t new_offset = lseek(fd, offset, SEEK_SET);
    if (new_offset == (off_t)-1) {
        std::cerr << "lseek error: " << strerror(errno) << std::endl;
        close(fd);
        return errno;
    }

    // 4. 从文件中读取数据
    char read_buffer[100];
    ssize_t read_bytes = read(fd, read_buffer, sizeof(read_buffer) - 1);
    if (read_bytes == -1) {
        std::cerr << "read error: " << strerror(errno) << std::endl;
        close(fd);
        return errno;
    }
    read_buffer[read_bytes] = '\0'; // 添加字符串结束符

    std::cout << "Read from file: " << read_buffer << std::endl;

    // 5. 获取文件状态信息
    struct stat st;
    if (stat(filename, &st) == -1) {
        std::cerr << "stat error: " << strerror(errno) << std::endl;
    } else {
        std::cout << "File size: " << st.st_size << " bytes" << std::endl;
    }

    // 6. 关闭文件描述符
    if (close(fd) == -1) {
        std::cerr << "close error: " << strerror(errno) << std::endl;
        return errno;
    }

    // 7. 示例目录操作（仅展示opendir和readdir）
    DIR *dirp = opendir(".");
    if (!dirp) {
        std::cerr << "opendir error: " << strerror(errno) << std::endl;
        return errno;
    }

    struct dirent *entry;
    while ((entry = readdir(dirp)) != NULL) {
        std::cout << entry->d_name << std::endl;
    }

    closedir(dirp);

    // 8. dup和dup2函数示例（假设我们有另一个有效的文件描述符如STDIN_FILENO）
    int old_fd = STDIN_FILENO;
    int new_fd1 = dup(old_fd); // 创建新的文件描述符复制
    if (new_fd1 == -1) {
        std::cerr << "dup error: " << strerror(errno) << std::endl;
    } else {
        std::cout << "New FD from dup: " << new_fd1 << std::endl;
    }

    // dup2示例，替换已有文件描述符为stdin
    int new_fd2 = STDOUT_FILENO; // 假设要替换stdout
    if (dup2(old_fd, new_fd2) == -1) {
        std::cerr << "dup2 error: " << strerror(errno) << std::endl;
    } else {
        std::cout << "Old FD (" << old_fd << ") duplicated to FD " << new_fd2 << " using dup2" << std::endl;
    }

    return 0;
}