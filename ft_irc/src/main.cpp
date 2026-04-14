#include <iostream>
#include <cstdlib>   // atoi
#include <csignal>   // signal
#include "Server.hpp"

// 全局标志：收到 SIGINT/SIGTERM 时设为 true，让主循环优雅退出
// （声明为 volatile sig_atomic_t 是信号处理函数的安全写法）
volatile sig_atomic_t g_stop = 0;

static void signalHandler(int /*sig*/)
{
    g_stop = 1;
}

int main(int argc, char* argv[])
{
    if (argc != 3) {
        std::cerr << "Usage: ./ircserv <port> <password>" << std::endl;
        return 1;
    }

    // 参数解析：port 必须是正整数
    int port = std::atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        std::cerr << "Error: invalid port number" << std::endl;
        return 1;
    }

    std::string password = argv[2];
    if (password.empty()) {
        std::cerr << "Error: password cannot be empty" << std::endl;
        return 1;
    }

    // 注册信号处理（Ctrl+C 时优雅停止）
    signal(SIGINT,  signalHandler);
    signal(SIGTERM, signalHandler);
    // SIGPIPE: 当对端已关闭连接时 send() 会触发此信号，我们忽略它
    // 让 send() 返回 -1 并通过 errno 处理错误
    signal(SIGPIPE, SIG_IGN);

    try {
        Server server(port, password);
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "Server shut down cleanly." << std::endl;
    return 0;
}
