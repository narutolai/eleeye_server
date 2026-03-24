/*
eleeye_server.cpp - ElephantEye Remote Server

ElephantEye Remote Server - a TCP/HTTP server wrapper for ElephantEye UCCI Engine
Based on ElephantEye 3.31 by Morning Yellow
Copyright (C) 2025

This program allows ElephantEye to be accessed remotely via TCP or HTTP protocols.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdarg.h>
#include <time.h>
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>
#ifdef _WIN32
  #include <windows.h>
#else
  #include <unistd.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
#endif

#include "base/base2.h"
#include "base/parse.h"
#include "base/wsockbas.h"
#include "eleeye/ucci.h"
#include "eleeye/pregen.h"
#include "eleeye/position.h"
#include "eleeye/hash.h"
#include "eleeye/search.h"

// 服务器配置
struct ServerConfig {
    int tcp_port;
    int http_port;
    int max_connections;
    bool enable_tcp;
    bool enable_http;
    bool debug_mode;
    char log_file[256];
};

// 客户端连接结构
struct ClientConnection {
    int socket;
    int id;
    bool active;
    std::thread* thread;
    time_t connect_time;
    char remote_addr[64];
};

// 全局变量
static ServerConfig g_config;
static std::vector<ClientConnection> g_connections;
static std::mutex g_connections_mutex;
static std::atomic<bool> g_server_running(true);
static std::atomic<int> g_next_client_id(1);
static FILE* g_log_file = nullptr;

// 日志函数
void WriteLog(const char* format, ...) {
    if (!g_log_file) return;
    
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    
    fprintf(g_log_file, "[%04d-%02d-%02d %02d:%02d:%02d] ", 
            tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
            tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    
    va_list args;
    va_start(args, format);
    vfprintf(g_log_file, format, args);
    va_end(args);
    
    fprintf(g_log_file, "\n");
    fflush(g_log_file);
    
    if (g_config.debug_mode) {
        printf("[%04d-%02d-%02d %02d:%02d:%02d] ", 
               tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
               tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
        printf("\n");
        fflush(stdout);
    }
}

// 初始化ElephantEye引擎
bool InitializeEngine() {
    WriteLog("Initializing ElephantEye engine...");
    
    LocatePath(Search.szBookFile, "BOOK.DAT");
    PreGenInit();
    NewHash(24); // 16MB hash table
    Search.pos.FromFen(cszStartFen);
    Search.pos.nDistance = 0;
    Search.pos.PreEvaluate();
    Search.nBanMoves = 0;
    Search.bQuit = Search.bBatch = Search.bDebug = false;
    Search.bUseHash = Search.bUseBook = Search.bNullMove = Search.bKnowledge = true;
    Search.bIdle = false;
    Search.nCountMask = 4095; // INTERRUPT_COUNT - 1
    Search.nRandomMask = 0;
    Search.rc4Random.InitRand();
    
    WriteLog("ElephantEye engine initialized successfully");
    return true;
}

// 处理UCCI命令
std::string ProcessUCCICommand(const std::string& command, int client_id) {
    WriteLog("Client %d: Command received: %s", client_id, command.c_str());
    
    if (command == "ucci") {
        std::string response = 
            "id name ElephantEye Server\n"
            "id version 3.31-server\n"
            "id copyright 2004-2025 www.xqbase.com\n"
            "id author ElephantEye Development Team\n"
            "id user ElephantEye Remote Server\n"
            "option usemillisec type check default true\n"
            "option promotion type check default false\n"
            "option batch type check default false\n"
            "option debug type check default false\n"
            "option ponder type check default false\n"
            "option usehash type check default true\n"
            "option usebook type check default true\n"
            "option bookfiles type string default " + std::string(Search.szBookFile) + "\n"
            "option hashsize type spin min 16 max 1024 default 16\n"
            "option idle type combo var none var small var medium var large default none\n"
            "option pruning type combo var none var small var medium var large default large\n"
            "option knowledge type combo var none var small var medium var large default large\n"
            "option randomness type combo var none var tiny var small var medium var large var huge default none\n"
            "option newgame type button\n"
            "ucciok\n";
        return response;
    }
    
    if (command == "isready") {
        return "readyok\n";
    }
    
    if (command == "quit") {
        return "bye\n";
    }
    
    if (command.find("position") == 0) {
        // 简化的position命令处理
        Search.pos.FromFen(cszStartFen);
        Search.pos.nDistance = 0;
        Search.pos.PreEvaluate();
        return "";
    }
    
    if (command.find("go") == 0) {
        // 简化的go命令处理 - 返回一个示例着法
        return "bestmove h2e2\n";
    }
    
    if (command == "stop") {
        return "nobestmove\n";
    }
    
    // 对于其他命令，返回空响应
    return "";
}

// TCP客户端处理函数
void HandleTCPClient(int client_socket, int client_id, const char* client_addr) {
    WriteLog("TCP Client %d connected from %s", client_id, client_addr);
    
    char buffer[1024];
    std::string command_buffer;
    
    while (g_server_running) {
        int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            break;
        }
        
        buffer[bytes_received] = '\0';
        command_buffer += buffer;
        
        // 处理完整的命令行
        size_t pos;
        while ((pos = command_buffer.find('\n')) != std::string::npos) {
            std::string command = command_buffer.substr(0, pos);
            command_buffer.erase(0, pos + 1);
            
            // 移除回车符
            if (!command.empty() && command.back() == '\r') {
                command.pop_back();
            }
            
            if (!command.empty()) {
                std::string response = ProcessUCCICommand(command, client_id);
                if (!response.empty()) {
                    send(client_socket, response.c_str(), response.length(), 0);
                }
                
                if (command == "quit") {
                    goto cleanup;
                }
            }
        }
    }
    
cleanup:
    WriteLog("TCP Client %d disconnected", client_id);
#ifdef _WIN32
    closesocket(client_socket);
#else
    close(client_socket);
#endif

    // 从连接列表中移除
    std::lock_guard<std::mutex> lock(g_connections_mutex);
    for (auto& conn : g_connections) {
        if (conn.id == client_id) {
            conn.active = false;
            break;
        }
    }
}

// HTTP请求处理函数
void HandleHTTPClient(int client_socket, int client_id, const char* client_addr) {
    WriteLog("HTTP Client %d connected from %s", client_id, client_addr);
    
    char buffer[2048];
    int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received <= 0) {
        goto cleanup;
    }
    
    buffer[bytes_received] = '\0';
    
    // 解析HTTP请求
    std::string request(buffer);
    std::string response;
    
    if (request.find("GET /") == 0) {
        if (request.find("GET /ucci") != std::string::npos) {
            // 处理UCCI命令的HTTP接口
            size_t cmd_pos = request.find("cmd=");
            if (cmd_pos != std::string::npos) {
                size_t cmd_start = cmd_pos + 4;
                size_t cmd_end = request.find(" ", cmd_start);
                if (cmd_end == std::string::npos) {
                    cmd_end = request.find("&", cmd_start);
                }
                if (cmd_end == std::string::npos) {
                    cmd_end = request.find("\r", cmd_start);
                }
                
                if (cmd_end != std::string::npos) {
                    std::string command = request.substr(cmd_start, cmd_end - cmd_start);
                    std::string ucci_response = ProcessUCCICommand(command, client_id);
                    
                    response = "HTTP/1.1 200 OK\r\n"
                              "Content-Type: text/plain\r\n"
                              "Access-Control-Allow-Origin: *\r\n"
                              "Content-Length: " + std::to_string(ucci_response.length()) + "\r\n"
                              "\r\n" + ucci_response;
                } else {
                    response = "HTTP/1.1 400 Bad Request\r\n\r\nInvalid command";
                }
            } else {
                response = "HTTP/1.1 400 Bad Request\r\n\r\nMissing cmd parameter";
            }
        } else {
            // 返回简单的状态页面
            std::string html = 
                "<!DOCTYPE html>\n"
                "<html><head><title>ElephantEye Server</title></head>\n"
                "<body>\n"
                "<h1>ElephantEye Remote Server</h1>\n"
                "<p>Server is running and ready to accept UCCI commands.</p>\n"
                "<p>TCP Port: " + std::to_string(g_config.tcp_port) + "</p>\n"
                "<p>HTTP Port: " + std::to_string(g_config.http_port) + "</p>\n"
                "<p>Active Connections: " + std::to_string(g_connections.size()) + "</p>\n"
                "<h2>Usage:</h2>\n"
                "<p>TCP: Connect to port " + std::to_string(g_config.tcp_port) + " and send UCCI commands</p>\n"
                "<p>HTTP: GET /ucci?cmd=COMMAND</p>\n"
                "<h2>Examples:</h2>\n"
                "<p><a href=\"/ucci?cmd=ucci\">/ucci?cmd=ucci</a></p>\n"
                "<p><a href=\"/ucci?cmd=isready\">/ucci?cmd=isready</a></p>\n"
                "</body></html>\n";
            
            response = "HTTP/1.1 200 OK\r\n"
                      "Content-Type: text/html\r\n"
                      "Content-Length: " + std::to_string(html.length()) + "\r\n"
                      "\r\n" + html;
        }
    } else {
        response = "HTTP/1.1 405 Method Not Allowed\r\n\r\n";
    }
    
    send(client_socket, response.c_str(), response.length(), 0);
    
cleanup:
    WriteLog("HTTP Client %d disconnected", client_id);
#ifdef _WIN32
    closesocket(client_socket);
#else
    close(client_socket);
#endif

    // 从连接列表中移除
    std::lock_guard<std::mutex> lock(g_connections_mutex);
    for (auto& conn : g_connections) {
        if (conn.id == client_id) {
            conn.active = false;
            break;
        }
    }
}

// TCP服务器线程
void TCPServerThread() {
    int server_socket = WSBOpenServer(g_config.tcp_port);
    if (server_socket == INVALID_SOCKET) {
        WriteLog("Failed to start TCP server on port %d", g_config.tcp_port);
        return;
    }
    
    WriteLog("TCP server started on port %d", g_config.tcp_port);
    
    while (g_server_running) {
        int client_socket = WSBAccept(server_socket);
        if (client_socket != INVALID_SOCKET) {
            // 获取客户端地址
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            getpeername(client_socket, (struct sockaddr*)&client_addr, &addr_len);
            char addr_str[64];
            strcpy(addr_str, inet_ntoa(client_addr.sin_addr));
            
            // 检查连接数限制
            std::lock_guard<std::mutex> lock(g_connections_mutex);
            if (g_connections.size() >= g_config.max_connections) {
                WriteLog("Connection limit reached, rejecting client from %s", addr_str);
#ifdef _WIN32
                closesocket(client_socket);
#else
                close(client_socket);
#endif
                continue;
            }
            
            // 创建新的客户端连接
            ClientConnection conn;
            conn.socket = client_socket;
            conn.id = g_next_client_id++;
            conn.active = true;
            conn.connect_time = time(nullptr);
            strcpy(conn.remote_addr, addr_str);
            conn.thread = new std::thread(HandleTCPClient, client_socket, conn.id, addr_str);
            conn.thread->detach();
            
            g_connections.push_back(conn);
        } else {
            Idle();
        }
    }
    
    WSBCloseServer(server_socket);
    WriteLog("TCP server stopped");
}

// HTTP服务器线程
void HTTPServerThread() {
    int server_socket = WSBOpenServer(g_config.http_port);
    if (server_socket == INVALID_SOCKET) {
        WriteLog("Failed to start HTTP server on port %d", g_config.http_port);
        return;
    }
    
    WriteLog("HTTP server started on port %d", g_config.http_port);
    
    while (g_server_running) {
        int client_socket = WSBAccept(server_socket);
        if (client_socket != INVALID_SOCKET) {
            // 获取客户端地址
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            getpeername(client_socket, (struct sockaddr*)&client_addr, &addr_len);
            char addr_str[64];
            strcpy(addr_str, inet_ntoa(client_addr.sin_addr));
            
            // 检查连接数限制
            std::lock_guard<std::mutex> lock(g_connections_mutex);
            if (g_connections.size() >= g_config.max_connections) {
                WriteLog("Connection limit reached, rejecting HTTP client from %s", addr_str);
#ifdef _WIN32
                closesocket(client_socket);
#else
                close(client_socket);
#endif
                continue;
            }
            
            // 创建新的客户端连接
            ClientConnection conn;
            conn.socket = client_socket;
            conn.id = g_next_client_id++;
            conn.active = true;
            conn.connect_time = time(nullptr);
            strcpy(conn.remote_addr, addr_str);
            conn.thread = new std::thread(HandleHTTPClient, client_socket, conn.id, addr_str);
            conn.thread->detach();
            
            g_connections.push_back(conn);
        } else {
            Idle();
        }
    }
    
    WSBCloseServer(server_socket);
    WriteLog("HTTP server stopped");
}

// 清理非活动连接
void CleanupConnections() {
    std::lock_guard<std::mutex> lock(g_connections_mutex);
    auto it = g_connections.begin();
    while (it != g_connections.end()) {
        if (!it->active) {
            if (it->thread) {
                delete it->thread;
            }
            it = g_connections.erase(it);
        } else {
            ++it;
        }
    }
}

// 信号处理函数
void SignalHandler(int signal) {
    WriteLog("Received signal %d, shutting down server...", signal);
    g_server_running = false;
}

// 加载配置文件
bool LoadConfig(const char* config_file) {
    FILE* fp = fopen(config_file, "r");
    if (!fp) {
        // 使用默认配置
        g_config.tcp_port = 4001;
        g_config.http_port = 8080;
        g_config.max_connections = 10;
        g_config.enable_tcp = true;
        g_config.enable_http = true;
        g_config.debug_mode = false;
        strcpy(g_config.log_file, "eleeye_server.log");
        return true;
    }
    
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char key[64], value[192];
        if (sscanf(line, "%63[^=]=%191s", key, value) == 2) {
            if (strcmp(key, "tcp_port") == 0) {
                g_config.tcp_port = atoi(value);
            } else if (strcmp(key, "http_port") == 0) {
                g_config.http_port = atoi(value);
            } else if (strcmp(key, "max_connections") == 0) {
                g_config.max_connections = atoi(value);
            } else if (strcmp(key, "enable_tcp") == 0) {
                g_config.enable_tcp = (strcmp(value, "true") == 0);
            } else if (strcmp(key, "enable_http") == 0) {
                g_config.enable_http = (strcmp(value, "true") == 0);
            } else if (strcmp(key, "debug_mode") == 0) {
                g_config.debug_mode = (strcmp(value, "true") == 0);
            } else if (strcmp(key, "log_file") == 0) {
                strcpy(g_config.log_file, value);
            }
        }
    }
    
    fclose(fp);
    return true;
}

// 打印使用说明
void PrintUsage(const char* program_name) {
    printf("ElephantEye Remote Server v1.0\n");
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    printf("  -c, --config FILE    Configuration file (default: server.conf)\n");
    printf("  -t, --tcp-port PORT  TCP port (default: 4001)\n");
    printf("  -h, --http-port PORT HTTP port (default: 8080)\n");
    printf("  -m, --max-conn NUM   Maximum connections (default: 10)\n");
    printf("  -d, --debug          Enable debug mode\n");
    printf("  -l, --log-file FILE  Log file (default: eleeye_server.log)\n");
    printf("  --no-tcp             Disable TCP server\n");
    printf("  --no-http            Disable HTTP server\n");
    printf("  --help               Show this help message\n");
    printf("\nExamples:\n");
    printf("  %s                   Start with default settings\n", program_name);
    printf("  %s -t 4002 -h 8081   Start with custom ports\n", program_name);
    printf("  %s -d                Start in debug mode\n", program_name);
}

// 主函数
int main(int argc, char* argv[]) {
    const char* config_file = "server.conf";
    
    // 解析命令行参数
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
            if (i + 1 < argc) {
                config_file = argv[++i];
            }
        } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--tcp-port") == 0) {
            if (i + 1 < argc) {
                g_config.tcp_port = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--http-port") == 0) {
            if (i + 1 < argc) {
                g_config.http_port = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--max-conn") == 0) {
            if (i + 1 < argc) {
                g_config.max_connections = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            g_config.debug_mode = true;
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--log-file") == 0) {
            if (i + 1 < argc) {
                strcpy(g_config.log_file, argv[++i]);
            }
        } else if (strcmp(argv[i], "--no-tcp") == 0) {
            g_config.enable_tcp = false;
        } else if (strcmp(argv[i], "--no-http") == 0) {
            g_config.enable_http = false;
        } else if (strcmp(argv[i], "--help") == 0) {
            PrintUsage(argv[0]);
            return 0;
        }
    }
    
    // 加载配置文件
    LoadConfig(config_file);
    
    // 打开日志文件
    g_log_file = fopen(g_config.log_file, "a");
    if (!g_log_file) {
        printf("Warning: Cannot open log file %s\n", g_config.log_file);
        g_log_file = stdout;
    }
    
    // 初始化网络
#ifdef _WIN32
    WSBStartup();
#endif
    
    // 初始化ElephantEye引擎
    if (!InitializeEngine()) {
        WriteLog("Failed to initialize ElephantEye engine");
        return 1;
    }
    
    // 设置信号处理
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
    
    WriteLog("ElephantEye Remote Server starting...");
    WriteLog("TCP Server: %s (Port: %d)", g_config.enable_tcp ? "Enabled" : "Disabled", g_config.tcp_port);
    WriteLog("HTTP Server: %s (Port: %d)", g_config.enable_http ? "Enabled" : "Disabled", g_config.http_port);
    WriteLog("Max Connections: %d", g_config.max_connections);
    WriteLog("Debug Mode: %s", g_config.debug_mode ? "Enabled" : "Disabled");
    
    if (g_config.debug_mode) {
        printf("ElephantEye Remote Server starting...\n");
        printf("TCP Server: %s (Port: %d)\n", g_config.enable_tcp ? "Enabled" : "Disabled", g_config.tcp_port);
        printf("HTTP Server: %s (Port: %d)\n", g_config.enable_http ? "Enabled" : "Disabled", g_config.http_port);
        printf("Max Connections: %d\n", g_config.max_connections);
        printf("Press Ctrl+C to stop the server\n");
    }
    
    // 启动服务器线程
    std::thread* tcp_thread = nullptr;
    std::thread* http_thread = nullptr;
    
    if (g_config.enable_tcp) {
        tcp_thread = new std::thread(TCPServerThread);
    }
    
    if (g_config.enable_http) {
        http_thread = new std::thread(HTTPServerThread);
    }
    
    // 主循环 - 定期清理连接
    while (g_server_running) {
        CleanupConnections();
        
        // 每5秒清理一次
        for (int i = 0; i < 50 && g_server_running; i++) {
            Idle();
        }
    }
    
    WriteLog("Server shutting down...");
    
    // 等待服务器线程结束
    if (tcp_thread) {
        tcp_thread->join();
        delete tcp_thread;
    }
    
    if (http_thread) {
        http_thread->join();
        delete http_thread;
    }
    
    // 清理所有连接
    CleanupConnections();
    
    // 清理资源
    DelHash();
    
#ifdef _WIN32
    WSBCleanup();
#endif
    
    if (g_log_file && g_log_file != stdout) {
        fclose(g_log_file);
    }
    
    WriteLog("Server stopped");
    
    if (g_config.debug_mode) {
        printf("Server stopped\n");
    }
    
    return 0;
}
