#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <getopt.h>

#define MAX_CLIENTS 1000
#define BUFFER_SIZE 4096
#define ENGINE_POOL_SIZE 16
#define ENG_OUT_BUF_SIZE (64 * 1024)


#ifdef JIEQI
    #define ENGINE_PATH "ELEEYE_JIEQI.EXE"         // 揭棋
    #define LOG_FILE "./xiangyan_jieqi_server.log" // 日志文件
    #define PID_FILE  "./eleeye_jieqi_server.pid"  // PID 文件
    #define DEFAULT_PORT 6001
    #define ENGINE_NAME "揭棋引擎"
    #define SERVER_TITLE "揭棋引擎网络代理服务器"
#elif PIKAFISH
    #define ENGINE_PATH "PIKAFISH"                 // 皮卡鱼引擎路径
    #define LOG_FILE "./pikafish_server.log"       // 日志文件
    #define PID_FILE  "./pikafish_server.pid"     // PID 文件
    #define DEFAULT_PORT 6100
    #define ENGINE_NAME "皮卡鱼引擎"
    #define SERVER_TITLE "皮卡鱼引擎网络代理服务器"
#else
    #define ENGINE_PATH "ELEEYE.EXE"                // 象眼引擎路径
    #define LOG_FILE "./xiangyan_server.log"       // 日志文件
    #define PID_FILE  "./eleeye_server.pid"       // PID 文件
    #define DEFAULT_PORT 6000
    #define ENGINE_NAME "象眼引擎"
    #define SERVER_TITLE "象眼引擎网络代理服务器"
#endif


/* 引擎槽位：预启动的引擎，有数据时分配给客户端，输出完毕（eleeyeok 数 == 输入 \n 数）后解绑 */
typedef struct Client Client;
typedef struct EngineSlot
{
    int engine_stdin;
    int engine_stdout;
    pid_t engine_pid;
    Client *client;             /* NULL 表示槽位空闲 */
    int pending_responses;       /* 已写入引擎的输入行数（\n 数） */
    int eleeyeok_received;       /* 已从引擎输出中收到的 eleeyeok 行数 */
} EngineSlot;

struct Client
{
    int fd;
    int id;
    struct sockaddr_in addr;
    char buffer[BUFFER_SIZE];
    int buf_len;
    EngineSlot *engine_slot;    /* 分配到的引擎槽位，NULL 表示尚未分配 */
    int ready ;                 //就绪有数据但是没有空闲的引擎
};

typedef struct
{
    Client *clients[MAX_CLIENTS];
    int client_count;
    EngineSlot engine_slots[ENGINE_POOL_SIZE];
    FILE *log_file;
} ServerState;

ServerState server_state;

// 日志函数 - 只输出到文件
void log_msg(const char *format, ...)
{
    time_t now;
    struct tm *timeinfo;
    char timestamp[32];

    // 获取当前时间
    time(&now);
    timeinfo = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);

    // 格式化消息
    va_list args;
    char message[1024];

    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    // 输出到日志文件（如果已打开）
    if (server_state.log_file != NULL)
    {
        fprintf(server_state.log_file, "[%s] %s\n", timestamp, message);
        fflush(server_state.log_file); // 立即刷新，防止日志丢失
    }
}

// 获取当前时间字符串（用于日志）
char *get_current_time_str()
{
    static char time_str[64];
    time_t now;
    struct tm *timeinfo;

    time(&now);
    timeinfo = localtime(&now);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", timeinfo);

    return time_str;
}

// 初始化日志系统
void init_log_system()
{
    // 打开日志文件（追加模式）
    server_state.log_file = fopen(LOG_FILE, "a");
    if (server_state.log_file != NULL)
    {
        // 设置文件缓冲区为行缓冲
        setlinebuf(server_state.log_file);

        // 写入启动分隔符
        fprintf(server_state.log_file, "\n\n");
        fprintf(server_state.log_file, "============================================================\n");
        fprintf(server_state.log_file, "%s服务器启动 - %s\n",
                ENGINE_NAME, get_current_time_str());
        fprintf(server_state.log_file, "============================================================\n");
        fprintf(server_state.log_file, "\n");
        fflush(server_state.log_file);
    }
}

// 为槽位启动引擎（引擎池初始化时调用）
static int start_engine_for_slot(EngineSlot *slot, int slot_index)
{
    int stdin_pipe[2], stdout_pipe[2];
    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0)
    {
        log_msg("错误: 为引擎槽 %d 创建管道失败: %s", slot_index, strerror(errno));
        return -1;
    }
    slot->engine_stdin = stdin_pipe[1];
    slot->engine_stdout = stdout_pipe[0];
    slot->engine_pid = 0;
    slot->client = NULL;
    slot->pending_responses = 0;
    slot->eleeyeok_received = 0;

    slot->engine_pid = fork();
    if (slot->engine_pid < 0)
    {
        log_msg("错误: 为引擎槽 %d fork 失败: %s", slot_index, strerror(errno));
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        return -1;
    }
    if (slot->engine_pid == 0)
    {
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        {
            int max_fd = (int)sysconf(_SC_OPEN_MAX);
            if (max_fd <= 0) max_fd = 1024;
            for (int fd = 3; fd < max_fd; fd++)
                (void)close(fd);
        }
        execl(ENGINE_PATH, ENGINE_PATH, NULL);
        exit(1);
    }
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    int flags = fcntl(slot->engine_stdout, F_GETFL, 0);
    fcntl(slot->engine_stdout, F_SETFL, flags | O_NONBLOCK);
    log_msg("引擎槽 %d 启动成功，PID: %d", slot_index, slot->engine_pid);
    return 0;
}

static void stop_engine_for_slot(EngineSlot *slot)
{
    if (slot->engine_pid > 0)
    {
        kill(slot->engine_pid, SIGTERM);
        waitpid(slot->engine_pid, NULL, 0);
        slot->engine_pid = 0;
    }
    if (slot->engine_stdin > 0) { close(slot->engine_stdin); slot->engine_stdin = 0; }
    if (slot->engine_stdout > 0) { close(slot->engine_stdout); slot->engine_stdout = 0; }
}

static EngineSlot *pick_free_slot(void)
{
    for (int i = 0; i < ENGINE_POOL_SIZE; i++)
    {
        EngineSlot *s = &server_state.engine_slots[i];
        if (s->engine_pid > 0 && s->client == NULL)
            return s;
    }
    return NULL;
}

static EngineSlot *find_slot_by_engine_fd(int fd)
{
    for (int i = 0; i < ENGINE_POOL_SIZE; i++)
    {
        if (server_state.engine_slots[i].engine_stdout == fd)
            return &server_state.engine_slots[i];
    }
    return NULL;
}

/* 统计 buf 中以 "eleeyeok" 开头的行数（按 \n 分行） */
static int count_eleeyeok_lines(const char *buf, int len)
{
    int n = 0;
    const char *p = buf, *end = buf + len;
    while (p < end)
    {
        const char *line_start = p;
        while (p < end && *p != '\n') p++;
        if ((size_t)(p - line_start) >= 8 && strncmp(line_start, "eleeyeok", 8) == 0)
            n++;
        if (p < end) p++;
    }
    return n;
}

// 添加客户端（不分配引擎，有数据时再分配空闲槽位）
int add_client(int client_fd, struct sockaddr_in *addr, int epoll_fd)
{
    if (server_state.client_count >= MAX_CLIENTS)
    {
        log_msg("警告: 客户端连接数已达上限(%d)，拒绝新连接", MAX_CLIENTS);
        return -1;
    }
    int client_id = -1;
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (server_state.clients[i] == NULL) { client_id = i; break; }
    }
    if (client_id == -1) return -1;

    Client *client = malloc(sizeof(Client));
    memset(client, 0, sizeof(Client));
    client->fd = client_fd;
    client->id = client_id;
    client->addr = *addr;
    client->buf_len = 0;
    client->engine_slot = NULL;

    server_state.clients[client_id] = client;
    server_state.client_count++;

    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr->sin_addr, ip_str, sizeof(ip_str));
    log_msg("客户端 %d 连接: %s:%d (当前客户端数: %d)",
            client_id, ip_str, ntohs(addr->sin_port), server_state.client_count);
    return client_id;
}

// 移除客户端（解绑槽位但不停止引擎）
void remove_client(int client_id, int epoll_fd)
{
    if (client_id < 0 || client_id >= MAX_CLIENTS || server_state.clients[client_id] == NULL)
        return;
    Client *client = server_state.clients[client_id];
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client->addr.sin_addr, ip_str, sizeof(ip_str));
    log_msg("客户端 %d 断开: %s:%d (剩余客户端数: %d)",
            client_id, ip_str, ntohs(client->addr.sin_port), server_state.client_count - 1);

    if (client->fd > 0)
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client->fd, NULL);

    if (client->engine_slot != NULL)
    {
        client->engine_slot->client = NULL;
        client->engine_slot = NULL;
    }
    close(client->fd);
    free(client);
    server_state.clients[client_id] = NULL;
    server_state.client_count--;
}

// 根据文件描述符查找客户端
Client *find_client_by_fd(int fd)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (server_state.clients[i] != NULL && server_state.clients[i]->fd == fd)
        {
            return server_state.clients[i];
        }
    }
    return NULL;
}


// 处理客户端数据：无槽位时分配空闲引擎；只把完整行（到最后一个 \n）发给引擎，统计 \n 数
// 返回值: 0 正常, -1 写引擎失败(EPIPE)，调用方应 remove_client
int handle_client_data(Client *client)
{
    if (client->buf_len == 0) return 0;

    if (client->engine_slot == NULL)
    {
        EngineSlot *slot = pick_free_slot();
        if (slot == NULL)
        {
            client->ready = 1;
            log_msg("客户端 %d 有数据但无可用引擎，数据留在 buffer", client->id);
            return 0;
        }
        slot->client = client;
        slot->pending_responses = 0;
        slot->eleeyeok_received = 0;
        client->engine_slot = slot;
        client->ready = 0;
        log_msg("客户端 %d 绑定引擎槽位 %d", client->id,slot->engine_pid);
    }

    /* 只发送完整行（到最后一个 \n），不完整句留在 buffer */
    char *last_nl = NULL;
    for (int i = 0; i < client->buf_len; i++)
        if (client->buffer[i] == '\n') {
            last_nl = &client->buffer[i];
            client->engine_slot->pending_responses++;
        }
    if (last_nl == NULL) return 0;

    size_t send_len = (size_t)(last_nl - client->buffer) + 1;
    int engine_stdin = client->engine_slot->engine_stdin;

    /* 统计本次写入的 \n 数，作为期待的 eleeyeok 数 */
    // for (size_t i = 0; i < send_len; i++)
    //     if (client->buffer[i] == '\n') client->engine_slot->pending_responses++;

    // if (send_len < 100)
    // if(send_len > 0)
    // {
    //     char log_buf[BUFFER_SIZE + 1];
    //     memcpy(log_buf, client->buffer, send_len);
    //     log_buf[send_len] = '\0';
    //     char *fn = strchr(log_buf, '\n'); if (fn) *fn = '\0';
    //     char ip_str[INET_ADDRSTRLEN];
    //     inet_ntop(AF_INET, &client->addr.sin_addr, ip_str, sizeof(ip_str));
    //     log_msg("客户端 %d(%s) -> 引擎: %s", client->id, ip_str, log_buf);
    // }
    log_msg("客户端 %d -> 引擎: %s", client->id, client->buffer);
    if (write(engine_stdin, client->buffer, send_len) < 0)
    {
        log_msg("错误: 写入客户端 %d 引擎失败: %s", client->id, strerror(errno));
        if (errno == EPIPE) return -1;
    }

    if (send_len < (size_t)client->buf_len)
    {
        memmove(client->buffer, client->buffer + send_len, client->buf_len - send_len);
        client->buf_len -= (int)send_len;
        client->buffer[client->buf_len] = '\0';
    }
    else
        client->buf_len = 0;
    return 0;
}

// 处理引擎输出（发送给对应的客户端）
// 返回值: 0 正常, -1 客户端已断开(EPIPE)，调用方应 remove_client
int handle_engine_output(Client *client, char *data, int len)
{
    // 记录引擎输出（为了调试）
    // if (len < 128)
    // if (len > 128)
    // { // 只记录较短的输出
    //     char log_buffer[BUFFER_SIZE + 1];
    //     memcpy(log_buffer, data, len);
    //     log_buffer[len] = '\0';

    //     char ip_str[INET_ADDRSTRLEN];
    //     inet_ntop(AF_INET, &client->addr.sin_addr, ip_str, sizeof(ip_str));
    //     log_msg("引擎 -> 客户端 %d(%s): %s",
    //             client->id, ip_str, log_buffer);
    // }
    log_msg("引擎 -> 客户端 %d: %s",client->id,  data);
    // 发送给对应的客户端（客户端已断开时 write 返回 EPIPE）
    if (write(client->fd, data, len) < 0)
    {
        log_msg("错误: 写入客户端 %d 失败: %s",
                client->id, strerror(errno));
        if (errno == EPIPE)
            return -1;
    }
    return 0;
}

// 设置socket非阻塞
void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// 检查 PID 文件中记录的进程是否仍在运行
// 返回 1 表示已存在且进程在运行（不应启动），0 表示可启动
static int check_existing_process(const char *pidfile)
{
    FILE *f = fopen(pidfile, "r");
    if (!f)
        return 0; /* 文件不存在或不可读，允许启动 */
    int pid_val = 0;
    if (fscanf(f, "%d", &pid_val) != 1)
    {
        fclose(f);
        return 0; /* 内容无效，允许启动并覆盖 */
    }
    fclose(f);
    if (pid_val <= 0)
        return 0;
    pid_t old_pid = (pid_t)pid_val;
    /* kill(pid, 0) 不发送信号，仅检查进程是否存在；存在返回 0，不存在返回 -1 且 errno==ESRCH */
    if (kill(old_pid, 0) == 0)
        return 1; /* 进程仍在运行，不要启动 */
    return 0;
}

// 将当前进程 ID 写入 PID 文件（覆盖已有内容）
static void write_pid_file(const char *pidfile)
{
    FILE *f = fopen(pidfile, "w");
    if (f)
    {
        fprintf(f, "%d\n", (int)getpid());
        fclose(f);
    }
}

// 清理函数
void cleanup()
{
    log_msg("正在清理资源...");
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (server_state.clients[i] != NULL)
        {
            close(server_state.clients[i]->fd);
            free(server_state.clients[i]);
            server_state.clients[i] = NULL;
        }
    }
    for (int i = 0; i < ENGINE_POOL_SIZE; i++)
        stop_engine_for_slot(&server_state.engine_slots[i]);

    // 关闭日志文件
    if (server_state.log_file != NULL)
    {
        log_msg("关闭日志文件");
        fclose(server_state.log_file);
        server_state.log_file = NULL;
    }

    // 删除 PID 文件
    unlink(PID_FILE);

    log_msg("清理完成");
}

// 创建守护进程
int daemonize()
{
    pid_t pid;
    char current_dir[1024];

    // 保存当前工作目录
    if (getcwd(current_dir, sizeof(current_dir)) == NULL)
    {
        return -1;
    }

    // 第一次fork
    pid = fork();
    if (pid < 0)
    {
        return -1; // fork失败
    }
    if (pid > 0)
    {
        exit(0); // 父进程退出
    }

    // 子进程继续
    // 创建新的会话
    if (setsid() < 0)
    {
        return -1;
    }

    // 忽略SIGHUP信号
    signal(SIGHUP, SIG_IGN);

    // 第二次fork
    pid = fork();
    if (pid < 0)
    {
        return -1;
    }
    if (pid > 0)
    {
        exit(0); // 第一个子进程退出
    }

    // 第二个子进程继续
    // 保持在原工作目录，不切换到根目录
    // 这样日志文件可以正常创建
    if (chdir(current_dir) < 0)
    {
        return -1;
    }

    // 设置文件权限掩码
    umask(0);

    // 只关闭标准输入、输出、错误，保留其他文件描述符
    // 这样不会影响后续的socket和文件操作
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // 重定向标准输入、输出、错误到/dev/null
    int fd = open("/dev/null", O_RDWR);
    if (fd != -1)
    {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO)
        {
            close(fd);
        }
    }

    return 0;
}

// 显示使用帮助
void show_usage(const char *program_name)
{
    printf("%s v2.0\n", SERVER_TITLE);
    printf("用法: %s [选项]\n", program_name);
    printf("选项:\n");
    printf("  -p <端口>     指定服务器端口 (默认: %d)\n", DEFAULT_PORT);
    printf("  -d            以守护进程模式运行\n");
    printf("  -h            显示此帮助信息\n");
    printf("\n");
    printf("示例:\n");
    printf("  %s                    # 前台运行，默认端口%d\n", program_name, DEFAULT_PORT);
    printf("  %s -p 8080            # 前台运行，端口8080\n", program_name);
    printf("  %s -d                 # 守护进程运行，默认端口%d\n", program_name, DEFAULT_PORT);
    printf("  %s -d -p 8080         # 守护进程运行，端口8080\n", program_name);
    printf("\n");
    printf("日志文件: %s\n", LOG_FILE);
    printf("PID 文件: %s（存在且进程在运行时不重复启动）\n", PID_FILE);
    printf("引擎路径: %s\n", ENGINE_PATH);
}

// 信号处理函数
void signal_handler(int sig)
{
    log_msg("收到信号 %d，准备退出...", sig);
    cleanup();
    exit(0);
}

int main(int argc, char *argv[])
{
    int server_port = DEFAULT_PORT; // 默认端口
    int daemon_mode = 0;    // 守护进程模式标志
    int opt;

    // 解析命令行参数
    while ((opt = getopt(argc, argv, "p:dh")) != -1)
    {
        switch (opt)
        {
        case 'p':
            server_port = atoi(optarg);
            if (server_port <= 0 || server_port > 65535)
            {
                fprintf(stderr, "错误: 端口号必须在1-65535之间\n");
                return 1;
            }
            break;
        case 'd':
            daemon_mode = 1;
            break;
        case 'h':
            show_usage(argv[0]);
            return 0;
        default:
            fprintf(stderr, "使用 %s -h 查看帮助信息\n", argv[0]);
            return 1;
        }
    }

    // 启动前检查 PID 文件：若记录的进程仍在运行则不再启动
    if (check_existing_process(PID_FILE))
    {
        fprintf(stderr, "错误: 服务已在运行（见 %s），请勿重复启动\n", PID_FILE);
        return 1;
    }

    // 如果启用守护进程模式，先创建守护进程
    if (daemon_mode)
    {
        printf("启动守护进程模式...\n");
        if (daemonize() < 0)
        {
            fprintf(stderr, "错误: 创建守护进程失败\n");
            return 1;
        }
    }

    // 初始化服务器状态
    memset(&server_state, 0, sizeof(server_state));
    init_log_system();

    // 写入当前进程 ID 到 PID 文件（覆盖旧文件）
    write_pid_file(PID_FILE);

    // 忽略 SIGPIPE：向已关闭的管道/套接字写时不再导致进程被杀死，write 会返回 EPIPE
    signal(SIGPIPE, SIG_IGN);

    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    log_msg("%s v2.0 (引擎池 %d)", SERVER_TITLE, ENGINE_POOL_SIZE);
    log_msg("运行模式: %s", daemon_mode ? "守护进程" : "前台");
    log_msg("端口: %d", server_port);
    log_msg("日志文件: %s", LOG_FILE);
    log_msg("最大客户端数: %d", MAX_CLIENTS);

    // 创建服务器socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        log_msg("错误: 创建socket失败: %s", strerror(errno));
        cleanup();
        return 1;
    }

    // 设置socket选项
    int sock_opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &sock_opt, sizeof(sock_opt)) < 0)
    {
        log_msg("警告: 设置socket选项失败: %s", strerror(errno));
        // 继续运行，这不是致命错误
    }

    // 绑定地址
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(server_port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        log_msg("错误: 绑定地址失败: %s", strerror(errno));
        cleanup();
        return 1;
    }

    // 监听
    if (listen(server_fd, 1000) < 0)
    {
        log_msg("错误: 监听失败: %s", strerror(errno));
        cleanup();
        return 1;
    }

    log_msg("服务器启动成功，等待客户端连接...");

    // 设置服务器socket为非阻塞
    set_nonblocking(server_fd);

    // 创建 epoll 实例
    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0)
    {
        log_msg("错误: 创建epoll失败: %s", strerror(errno));
        cleanup();
        return 1;
    }

    // 添加服务器socket到epoll
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) < 0)
    {
        log_msg("错误: 添加服务器socket到epoll失败: %s", strerror(errno));
        cleanup();
        return 1;
    }

    /* 启动引擎池并加入 epoll */
    memset(server_state.engine_slots, 0, sizeof(server_state.engine_slots));
    for (int i = 0; i < ENGINE_POOL_SIZE; i++)
    {
        if (start_engine_for_slot(&server_state.engine_slots[i], i) < 0)
        {
            log_msg("错误: 启动引擎池失败，已启动 %d/%d", i, ENGINE_POOL_SIZE);
            for (int j = 0; j < i; j++) stop_engine_for_slot(&server_state.engine_slots[j]);
            close(epoll_fd);
            cleanup();
            return 1;
        }
        struct epoll_event ev_eng;
        ev_eng.events = EPOLLIN;
        ev_eng.data.fd = server_state.engine_slots[i].engine_stdout;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_state.engine_slots[i].engine_stdout, &ev_eng) < 0)
        {
            log_msg("错误: 添加引擎槽 %d 到 epoll 失败: %s", i, strerror(errno));
            for (int j = 0; j <= i; j++) stop_engine_for_slot(&server_state.engine_slots[j]);
            close(epoll_fd);
            cleanup();
            return 1;
        }
    }
    log_msg("引擎池已启动 %d 个引擎", ENGINE_POOL_SIZE);

    log_msg("使用epoll模式，等待事件...");

#define MAX_EVENTS 2048
    struct epoll_event events[MAX_EVENTS];

    while (1)
    {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000); // 1秒超时

        if (nfds < 0)
        {
            if (errno == EINTR)
            {
                continue; // 被信号中断，继续
            }
            log_msg("错误: epoll_wait失败: %s", strerror(errno));
            break;
        }

        for (int i = 0; i < nfds; i++)
        {
            int fd = events[i].data.fd;

            if (fd == server_fd)
            {
                // 新连接
                struct sockaddr_in client_addr;
                socklen_t addr_len = sizeof(client_addr);

                int client_fd = accept(server_fd,
                                       (struct sockaddr *)&client_addr,
                                       &addr_len);

                if (client_fd >= 0)
                {
                    set_nonblocking(client_fd);
                    int client_id = add_client(client_fd, &client_addr, epoll_fd);

                    if (client_id >= 0)
                    {
                        // 添加客户端socket到epoll
                        struct epoll_event client_ev;
                        client_ev.events = EPOLLIN | EPOLLET; // 边缘触发
                        client_ev.data.fd = client_fd;
                        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_ev) < 0)
                        {
                            log_msg("错误: 添加客户端socket到epoll失败: %s", strerror(errno));
                            remove_client(client_id, epoll_fd);
                        }
                    }
                    else
                    {
                        close(client_fd);
                    }
                }
            }
            else
            {
                Client *client = find_client_by_fd(fd);
                if (client != NULL)
                {
                    /* 边缘触发：必须在 while 里读尽当前可读数据 */
                    ssize_t bytes_read;
                    while ((bytes_read = read(client->fd,
                                             client->buffer + client->buf_len,
                                             sizeof(client->buffer) - client->buf_len - 1)) > 0)
                    {
                        client->buf_len += bytes_read;
                        client->buffer[client->buf_len] = '\0';
                        /* buffer 满时先处理完整行腾出空间（一次即可，处理完最多剩半行） */
                        if (client->buf_len >= (int)(sizeof(client->buffer) - 1))
                        {
                            if (strchr(client->buffer, '\n') != NULL)
                            {
                                if (handle_client_data(client) < 0)
                                {
                                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                                    remove_client(client->id, epoll_fd);
                                    goto next_ev;
                                }
                            }
                            else
                            {
                                log_msg("客户端 %d 单行超过 %zu 字节，断开", client->id, sizeof(client->buffer) - 1);
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                                remove_client(client->id, epoll_fd);
                                goto next_ev;
                            }
                        }
                    }
                    if (bytes_read == 0)
                    {
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                        remove_client(client->id, epoll_fd);
                    }
                    else if (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
                    {
                        log_msg("错误: 读取客户端 %d 失败: %s", client->id, strerror(errno));
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                        remove_client(client->id, epoll_fd);
                    }
                    else if (client->buf_len > 0 && strchr(client->buffer, '\n') != NULL)
                    {
                        if (handle_client_data(client) < 0)
                        {
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                            remove_client(client->id, epoll_fd);
                        }
                    }
                next_ev: ;
                }
                else
                {
                    EngineSlot *slot = find_slot_by_engine_fd(fd);
                    if (slot != NULL)
                    {
                        char eng_buf[ENG_OUT_BUF_SIZE];
                        ssize_t bytes_read = read(fd, eng_buf, sizeof(eng_buf) - 1);
                        if (bytes_read > 0)
                        {
                            eng_buf[bytes_read] = '\0';
                            if (slot->client != NULL)
                            {
                                slot->eleeyeok_received += count_eleeyeok_lines(eng_buf, (int)bytes_read);
                                if (handle_engine_output(slot->client, eng_buf, (int)bytes_read) < 0)
                                    remove_client(slot->client->id, epoll_fd);
                            }
                        }
                        else if (bytes_read == 0)
                        {
                            log_msg("引擎槽 stdout EOF，引擎进程已退出");
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                            stop_engine_for_slot(slot);
                            if (slot->client != NULL)
                                remove_client(slot->client->id, epoll_fd);
                        }
                        else if (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
                        {
                            log_msg("错误: 读取引擎输出失败 fd=%d: %s", fd, strerror(errno));
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                            stop_engine_for_slot(slot);
                            if (slot->client != NULL)
                                remove_client(slot->client->id, epoll_fd);
                        }
                        /* 仅当 eleeyeok 数 >= 输入 \n 数时解绑 */
                        if (slot->client != NULL
                            && slot->eleeyeok_received >= slot->pending_responses)
                        {
                            log_msg("客户端 %d 解除绑定引擎槽位 %d", slot->client->id,slot->engine_pid);
                            slot->client->engine_slot = NULL;
                            slot->client = NULL;
                            slot->pending_responses = 0;
                            slot->eleeyeok_received = 0;
                        }
                    }
                }
            }
        }

       for (int i = 0; i < MAX_CLIENTS; i++)
        {   
            Client *client = server_state.clients[i];
            if (  client != NULL && client->ready == 1)
            {
               if (handle_client_data(client) < 0)
                        {
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client->fd, NULL);
                            remove_client(client->id, epoll_fd);
                        }
            }
        }
    }

    // 关闭epoll
    close(epoll_fd);

    // 正常退出
    log_msg("服务器正在关闭...");
    cleanup();
    log_msg("服务器已关闭");

    return 0;
}

/*
# 编译
gcc -o eleeye_server eleeye_server.c
gcc -DPIKAFISH -o eleeye_server eleeye_server.c
gcc -DJIEQI -o eleeye_jieqiserver eleeye_server.c
# 运行示例

## 前台模式
./eleeye_server                    # 默认端口6000，前台运行
./eleeye_server -p 8080            # 指定端口8080，前台运行
./eleeye_server -h                 # 显示帮助信息

## 守护进程模式
./eleeye_server -d                 # 默认端口6000，守护进程运行
./eleeye_server -d -p 8080         # 指定端口8080，守护进程运行

# 查看日志（路径随编译宏不同：默认象眼 xiangyan_server.log，皮卡鱼 pikafish_server.log，揭棋 xiangyan_jieqi_server.log）
tail -f <LOG_FILE>

# 停止守护进程
ps aux | grep eleeye_server        # 查找进程ID
kill <PID>                         # 终止进程

# 测试连接
telnet localhost 6000
ucci
position fen rnbakabnr/9/1c5c1/p1p1p1p1p/9/9/P1P1P1P1P/1C5C1/9/RNBAKABNR w - - 0 1
go depth 3
quit
*/
