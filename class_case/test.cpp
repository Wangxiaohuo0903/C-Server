int accept(int listening_socket_fd, struct sockaddr *client_address, socklen_t *address_length) {
    // 等待并接受来自listening_socket_fd上新的连接请求
    while (true) {
        if (有新的连接请求到达) {
            // 创建一个新的套接字以与客户端通信
            int new_socket_fd = create_new_socket();

            // 将新连接的信息填充到client_address指向的结构体中，并更新address_length
            fill_client_address_info(new_socket_fd, client_address, address_length);

            // 关联新的套接字与客户端连接
            associate_new_socket_with_client_connection(new_socket_fd);

            return new_socket_fd; // 返回新的套接字描述符
        } else {
            // 没有新连接时，可能阻塞等待或者非阻塞模式下返回错误（如EAGAIN或EWOULDBLOCK）
            handle_no_new_connection();
        }
    }

    // 实际上这个函数不会像上面这样循环，而是内部实现会处理这些逻辑
}