//
// Created by mano on 19.11.16.
//

#include <iostream>
#include <fstream>
#include <map>
#include <set>
#include <algorithm>
#include <cassert>
#include <vector>
#include <string>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <netdb.h>
#include <libgen.h>
#include <chrono>

typedef std::pair<uint32_t, unsigned short> Ip_Port;

const int BUFFER_SIZE = 10000;
const int MAX_LENGTH = 7;
const int LITTLE_STRING_SIZE = 256;
const long long BASE = 5LL;
const int SELECT_TIMEOUT = 100000;
const long long TIME_WAIT_CLIENT = 10000000LL;
const long long DEFAULT_STEP = 100000LL;
const int ADDRESS_MAX_SIZE = 1000;

const int CLIENT_NOT_FOUND_ANSWER = 2;
const int CLIENT_FOUND_ANSWER = 1;
const int CLIENT_IN_PROCESS = 0;

int server_socket;
struct sockaddr_in my_addr;
//bool flag_init = false;

struct ClientInfo{
    long long l;
    long long r;
    long long last_time;
    int socket;
    void * buf;
    int pbuf;

    ClientInfo(int socket, long long l, long long r, long long last_time) {
        this->socket = socket;
        this->l = l;
        this->r = r;
        this->last_time = last_time;
        buf = malloc(BUFFER_SIZE);
        pbuf = 0;
    }

    ~ClientInfo() {
        if (-1 != socket) {
            close(socket);
        }
        free(buf);
    }
};

// it contain socket descriptor and last time receive message from
std::map<Ip_Port, ClientInfo*> clients;

void * buf;
int buf_size;

char answer_md5[LITTLE_STRING_SIZE];
size_t answer_md5_len = 32 + 1;
char correct_answer_str[LITTLE_STRING_SIZE]; // will be answer on this task
char received_answer_str[LITTLE_STRING_SIZE]; // if receive from client
//bool flag_found_message = false;

std::map<char, long long> M;
std::map<long long, char> RM;

long long get_cur_time() {
    auto cur_time = std::chrono::high_resolution_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(cur_time).count();
}

Ip_Port get_ip_port(struct sockaddr_in addr) {
    uint32_t ip = addr.sin_addr.s_addr;
    unsigned short port = addr.sin_port;
    return std::make_pair(ip, port);
}

long long str_to_ll(std::string str) {
    long long p5 = 1LL;
    long long base = 5LL;
    long long sum = 0;
    for (int i = 0; i < str.size(); ++i) {
        sum += M[str[i]] * p5;
        p5 *= base;
    }
    return sum;
}

std::string ll_to_str(long long num) {
    std::string str = "";
    while (num) {
        str += RM[num % BASE];
        num /= BASE;
    }
    return str;
}

void init_socket(unsigned short port) {
    socklen_t addr_size = sizeof(struct sockaddr_in);
    bzero(&my_addr, addr_size);
    my_addr.sin_family = PF_INET;
    my_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    my_addr.sin_port = htons(port);

    server_socket = socket(PF_INET, SOCK_STREAM, 0);
    int option = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&option, sizeof(option));

    bind(server_socket, (struct sockaddr *) &my_addr, sizeof(struct sockaddr_in));
    listen(server_socket, SOMAXCONN);
}

void init_global() {
    std::vector<char> v = {' ', 'A', 'G', 'T', 'C'};
    for (int i = 0; i < v.size(); ++i) {
        M[v[i]] = i;
        RM[i] = v[i];
    }
}

// so crutch...
std::string get_md5_sum(const char* str) {
    std::ofstream out("file_md5_sum_file_md5_sum");
    out << str;
    out.close();

    char command[LITTLE_STRING_SIZE];
    char result[LITTLE_STRING_SIZE];
    snprintf(command, LITTLE_STRING_SIZE, "md5sum file_md5_sum_file_md5_sum");
    FILE * md5 = popen(command, "r");
    fgets(result, LITTLE_STRING_SIZE, md5);

    remove("file_md5_sum_file_md5_sum");

    std::string md5sum = std::string(result);
    md5sum = md5sum.substr(0, md5sum.find(' '));
    return md5sum;
}

long long get_max_right_bound() {
    long long res = 1;
    for (int i = 0; i < MAX_LENGTH; ++i) {
        res *= BASE;
    }
    return res - 1LL;
}

std::vector<std::pair<long long, long long>> free_intervals;
std::pair<long long, long long> get_next_interval() {
    static long long left_bound = 0LL;
    static long long max_right_bound = get_max_right_bound();

    std::pair<long long, long long> res;
    if (!free_intervals.empty()) {
        res = free_intervals.back();
        free_intervals.pop_back();
        return res;
    }
    else {
        if (left_bound > max_right_bound) {
            return std::make_pair(-1LL, -1LL);
        }
        res = {left_bound, std::min(left_bound + DEFAULT_STEP, max_right_bound)};
        left_bound += DEFAULT_STEP;
        return res;
    }
};

int send_interval_to_new_client(Ip_Port ip_port) {
    if (!clients.count(ip_port)) {
        fprintf(stderr, "Error when send interval\n");
        return -1;
    }

    ClientInfo * cl = clients[ip_port];
    const long long d = 1000000000LL;
    uint32_t buf[4] = {htonl((uint32_t)(cl->l / d)),
                       htonl((uint32_t)(cl->l % d)),
                       htonl((uint32_t)(cl->r / d)),
                       htonl((uint32_t)(cl->r % d))};
    send(cl->socket, (void*)buf, 4 * sizeof(uint32_t), 0);
    close(cl->socket);
    cl->socket = -1;
}

int handle_new_connection() {
    struct sockaddr_in addr;
    socklen_t addr_size = sizeof(struct sockaddr_in);
    int new_socket = accept(server_socket, (struct sockaddr *)&addr, &addr_size);
    if (new_socket < 0) {
        perror("accept");
        return -1;
    }

    Ip_Port ip_port = get_ip_port(addr);
    if (!clients.count(ip_port)) {
        std::pair<long long, long long> interval = get_next_interval();
        clients[ip_port] = new ClientInfo(new_socket, interval.first, interval.second, get_cur_time());
        send_interval_to_new_client(ip_port);
    }
    else {
        ClientInfo * cl = clients[ip_port];
        cl->last_time = get_cur_time();
        cl->socket = new_socket;
    }
}

int handle_client_message(Ip_Port ip_port) {
    if (!clients.count(ip_port)) {
        fprintf(stderr, "Unknown ip_port in client message handler\n");
        return -1;
    }

    ClientInfo * cl = clients[ip_port];
    ssize_t res = recv(cl->socket, (void*)((char*)cl->buf + cl->pbuf), BUFFER_SIZE, 0);
    if (res < 0) {
        memcpy(received_answer_str, cl->buf, (size_t)cl->pbuf);
        received_answer_str[cl->pbuf] = '\0';
        if (strcmp(received_answer_str, correct_answer_str)) {
            return CLIENT_NOT_FOUND_ANSWER;
        }
        else {
            return CLIENT_FOUND_ANSWER;
        }
    }
    else {
        cl->pbuf += res;
        return CLIENT_IN_PROCESS;
    }
}

int main(int argc, char* argv[]) {
    init_global();

    int opt;
    unsigned short my_port;
    while ((opt = getopt(argc, argv, "p:m:")) != -1) {
        switch (opt) {
            case 'm':
                answer_md5_len = strlen(optarg);
                strncpy(answer_md5, optarg, answer_md5_len);
                break;
            case 'p':
                my_port = (unsigned short)atoi(optarg);
                init_socket(my_port);
                break;
            default:
                fprintf(stderr, "Unknown argument\n");
                exit(EXIT_FAILURE);
        }
    }

    bool flag_execute = true;
    while (flag_execute) {
        fd_set fds;
        int max_fd = 0;

        FD_ZERO(&fds);
        FD_SET(server_socket, &fds);
        max_fd = std::max(max_fd, server_socket);

        for (auto client : clients) {
            int sock = client.second->socket;
            if (-1 == sock) {
                continue;
            }
            FD_SET(sock, &fds);
            max_fd = std::max(max_fd, sock);
        }

        struct timeval tv = {0, SELECT_TIMEOUT};
        int activity = select(max_fd + 1, &fds, NULL, NULL, &tv);
        if (activity < 0) {
            continue;
        }

        std::vector<Ip_Port> to_delete;
        for (auto client : clients) {
            int sock = client.second->socket;
            if (-1 == sock) {
                continue;
            }
            if (FD_ISSET(sock, &fds)) {
                int res = handle_client_message(client.first);
                if (res == CLIENT_FOUND_ANSWER) {
                    fprintf(stderr, "Answer: %s\n", received_answer_str);
                    flag_execute = false;
                    break;
                }
                else if (res == CLIENT_IN_PROCESS) {
                    client.second->last_time = get_cur_time();
                }
                else if (res == CLIENT_NOT_FOUND_ANSWER){
                    to_delete.push_back(client.first);
                }
            }
            else {
                long long cur_time = get_cur_time();
                long long diff = cur_time - client.second->last_time;
                if (diff > TIME_WAIT_CLIENT) {
                    to_delete.push_back(client.first);
                }
            }
        }
        if (!flag_execute) {
            break;
        }

        for (auto id : to_delete) {
            long long l = clients[id]->l;
            long long r = clients[id]->r;
            free_intervals.push_back({l, r});

            delete clients[id];
            clients.erase(id);
        }

        if (FD_ISSET(server_socket, &fds)) {
            int res = handle_new_connection();
        }
    }

    for (auto client : clients) {
        delete client.second;
    }

    return 0;
}
