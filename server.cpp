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

const int CLIENT_FOUND_ANSWER = 1;
const int CLIENT_NOT_FOUND_ANSWER = 0;

int server_socket;
struct sockaddr_in my_addr;
bool flag_init = false;

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
        free(buf);
    }
};

// it contain socket descriptor and last time receive message from
std::map<Ip_Port, ClientInfo*> clients;

void * buf;
int buf_size;

char * answer_md5;
size_t answer_md5_len = 32 + 1;
char* answer_str; // will be answer on this task

std::map<char, long long> M;
std::map<long long, char> RM;

long long get_cur_time() {
    auto cur_time = std::chrono::high_resolution_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(cur_time).count();
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

    buf = malloc(BUFFER_SIZE);
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

std::vector<std::pair<long long, long long>> free_intervals;
std::pair<long long, long long> get_next_interval() {
    static long long left_bound = 0LL;

    std::pair<long long, long long> res;
    if (!free_intervals.empty()) {
        res = free_intervals.back();
        free_intervals.pop_back();
        return res;
    }
    else {
        res = {left_bound, left_bound + DEFAULT_STEP};
        left_bound += DEFAULT_STEP;
        return res;
    }
};

int handle_new_connection() {

}

int handle_client_message() {

}

int main(int argc, char* argv[]) {
    init_global();

    int opt;
    while ((opt = getopt(argc, argv, "p:")) != -1) {
        switch (opt) {
            case 'p':
                unsigned short port = (unsigned short)atoi(optarg);
                init_socket(port);
                break;
            case 'm':
                answer_md5_len = strlen(optarg);
                strncpy(answer_md5, optarg, answer_md5_len);
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
            int sock = client.second>socket;
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

        std::vector<Ip_Port> outdates;
        for (auto client : clients) {
            int sock = client.second->socket;
            if (-1 == sock) {
                continue;
            }
            if (FD_ISSET(sock, &fds)) {
                int res = handle_client_message();
                if (res == CLIENT_FOUND_ANSWER) {
                    fprintf(stderr, "Answer: %s\n", answer_str);
                    break;
                }
                else if (res == CLIENT_NOT_FOUND_ANSWER) {
                    client.second->last_time = get_cur_time();
                }
            }
            else {
                long long cur_time = get_cur_time();
                long long diff = cur_time - client.second->last_time;
                if (diff > TIME_WAIT_CLIENT) {
                    outdates.push_back(client.first);
                }
            }
        }
        for (auto id : outdates) {
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

    free(buf);
    return 0;
}
