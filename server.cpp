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
#include <sstream>
#include <ctime>

typedef std::pair<uint32_t, unsigned short> Ip_Port;

const int BUFFER_SIZE = 10000;
const int MAX_LENGTH = 10;
const int LITTLE_STRING_SIZE = 256;
const long long BASE = 4LL;
const int SELECT_TIMEOUT = 100000;
const long long TIME_WAIT_CLIENT = 10000000LL;
const long long DEFAULT_STEP = 1000LL;
const int ADDRESS_MAX_SIZE = 1000;
const int MD5_SIZE = 32;

const int CLIENT_ASKED_INTERVAL = 3;
const int CLIENT_NOT_FOUND_ANSWER = 2;
const int CLIENT_FOUND_ANSWER = 1;
const int CLIENT_IN_PROCESS = 0;
const int CLIENT_ERROR = -1;

const uint32_t CLIENT_WANT_INTERVAL = 1;
const uint32_t CLIENT_HAVE_ANSWER = 2;

const int RET_ERROR = -1;
const int RET_GOOD = 1;

int server_socket;
struct sockaddr_in my_addr;

long long get_cur_time() {
    auto cur_time = std::chrono::high_resolution_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(cur_time).count();
}

struct ClientData {
    void * buf;
    int pbuf;
    long long last_time;

    ClientData() {
        buf = malloc(BUFFER_SIZE);
        pbuf = 0;
        last_time = get_cur_time();
    }

    ~ClientData() {
        free(buf);
    }
};
std::map<int, ClientData *> clients;

void * buf;
int pbuf;

char str_answer_md5[LITTLE_STRING_SIZE];
char answer_md5[MD5_SIZE + 1];
char received_answer_str[LITTLE_STRING_SIZE]; // if receive from client

std::map<char, long long> M;
std::map<long long, char> RM;

std::map<std::pair<long long, long long>, long long> calculating_intervals;

std::string pack_address(struct sockaddr_in addr) {
    char str_addr[ADDRESS_MAX_SIZE];
    uint32_t ip = ntohl(addr.sin_addr.s_addr);
    unsigned short port = ntohs(addr.sin_port);
    sprintf(str_addr, "%d.%d.%d.%d:%d",
            (ip & 0xff000000) >> 24,
            (ip & 0x00ff0000) >> 16,
            (ip & 0x0000ff00) >> 8,
            (ip & 0x000000ff), port);

    return std::string(str_addr);
}

Ip_Port get_ip_port(struct sockaddr_in addr) {
    uint32_t ip = addr.sin_addr.s_addr;
    unsigned short port = addr.sin_port;
    return std::make_pair(ip, port);
}

long long str_to_ll(std::string str) {
    long long p4 = 1LL;
    long long base = BASE;
    long long sum = 0;
    for (int i = 0; i < str.size(); ++i) {
        sum += M[str[i]] * p4;
        p4 *= base;
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
    std::vector<char> v = {'A', 'G', 'T', 'C'};
    for (int i = 0; i < v.size(); ++i) {
        M[v[i]] = i;
        RM[i] = v[i];
    }

    buf = malloc(BUFFER_SIZE);
    pbuf = 0;
}

// so crutch...
std::string get_md5_sum(const char* str) {
    std::stringstream ss;
    ss << "file_md5_sum_file_md5_sum_";
    ss << (int)getpid();
    std::string file_md5_name;
    ss >> file_md5_name;

    std::ofstream out(file_md5_name.c_str());
    out << str;
    out.close();

    char command[LITTLE_STRING_SIZE];
    char result[LITTLE_STRING_SIZE];

    snprintf(command, LITTLE_STRING_SIZE, "md5sum %s", file_md5_name.c_str());
    FILE * md5 = popen(command, "r");
    fgets(result, LITTLE_STRING_SIZE, md5);

    fclose(md5);
    remove(file_md5_name.c_str());

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

int handle_new_connection() {
    struct sockaddr_in addr;
    socklen_t addr_size = sizeof(struct sockaddr_in);
    int new_socket = accept(server_socket, (struct sockaddr *)&addr, &addr_size);
    if (new_socket < 0) {
        perror("accept");
        return RET_ERROR;
    }

    fprintf(stderr, "Accepted new client connection: %s\n", pack_address(addr).c_str());

    clients[new_socket] = new ClientData();
}

int delete_client_connection(int client_socket) {
    if (!clients.count(client_socket)) {
        fprintf(stderr, "No this client socket in delete\n");
        return RET_ERROR;
    }
    close(client_socket);
    delete clients[client_socket];
    clients.erase(client_socket);
    return RET_GOOD;
}

int send_interval_to_client(int client_socket) {
    fprintf(stderr, "Send interval to new cliend\n");

    if (!clients.count(client_socket)) {
        fprintf(stderr, "No this client socket in send interval\n");
        return RET_ERROR;
    }
    ClientData * cl = clients[client_socket];

    std::pair<long long, long long> interval = get_next_interval();
    long long l = interval.first;
    long long r = interval.second;

    const long long d = 1000000000LL;
    uint32_t num_buf[4];
    if (l == -1 && r == -1) {
        // Send not correct interval
        num_buf[0] = htonl((uint32_t)(1));
        num_buf[1] = htonl((uint32_t)(1));
        num_buf[2] = htonl((uint32_t)(0));
        num_buf[3] = htonl((uint32_t)(0));
    }
    else {
        num_buf[0] = htonl((uint32_t)(l / d));
        num_buf[1] = htonl((uint32_t)(l % d));
        num_buf[2] = htonl((uint32_t)(r / d));
        num_buf[3] = htonl((uint32_t)(r % d));
    }
    memcpy(cl->buf, (void*)num_buf, 4 * sizeof(uint32_t));
    memcpy((void*)((char*)cl->buf + 4 * sizeof(uint32_t)), answer_md5, MD5_SIZE);
    send(client_socket, cl->buf, 4 * sizeof(uint32_t)+ MD5_SIZE , 0);

    return RET_GOOD;
}

int receive_answer_from_client(ssize_t res, int client_socket) {
    fprintf(stderr, "Receive answer form client\n");

    if (!clients.count(client_socket)) {
        fprintf(stderr, "No this client\n");
        return RET_ERROR;
    }

    ClientData * cl = clients[client_socket];

    if (res > 0) {
        res = recv(client_socket, (void*)((char*)cl->buf + cl->pbuf), BUFFER_SIZE, 0);
        pbuf += res;
    }
    if (res > 0) {
        return CLIENT_IN_PROCESS;
    }

    if (res < 0) {
        perror("recv");
        return CLIENT_ERROR;
    }

    memcpy(received_answer_str, (void*)((char*)cl->buf + sizeof(uint32_t)), (size_t)cl->pbuf - sizeof(uint32_t));
    received_answer_str[cl->pbuf - sizeof(uint32_t)] = '\0';
    fprintf(stderr, "Received: %s\n", received_answer_str);
    fprintf(stderr, "Strs: %s %s\n", get_md5_sum(received_answer_str).c_str(), answer_md5);
    if (strcmp(get_md5_sum(received_answer_str).c_str(), answer_md5)) {
        return CLIENT_NOT_FOUND_ANSWER;
    }
    else {
        return CLIENT_FOUND_ANSWER;
    }
}

int handle_client_message(int client_socket) {
    if (!clients.count(client_socket)) {
        fprintf(stderr, "No this client\n");
        return RET_ERROR;
    }

    ClientData * cl = clients[client_socket];
    ssize_t res = recv(client_socket, (void*)((char*)cl->buf + cl->pbuf), BUFFER_SIZE, 0);
    cl->pbuf += res;
    if (res < 0) {
        perror("recv");
        return CLIENT_ERROR;
    }
    if (res == 0 || cl->pbuf >= sizeof(uint32_t)) {
        if (cl->pbuf < sizeof(uint32_t)) {
            fprintf(stderr, "Client send bad message\n");
            return CLIENT_ERROR;
        }

        uint32_t type = ntohl(((uint32_t*)cl->buf)[0]);
        if (type == CLIENT_WANT_INTERVAL) {
            send_interval_to_client(client_socket);
            return CLIENT_ASKED_INTERVAL;
        }
        else if (type == CLIENT_HAVE_ANSWER){
            int res_answer = receive_answer_from_client(res, client_socket);
            return res_answer;
        }
        else {
            fprintf(stderr, "Unknown type client message\n");
            return CLIENT_ERROR;
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
    std::string res_md5sum;
    bool flag_answer_set = false;
    bool flag_port_set = false;
    while ((opt = getopt(argc, argv, "p:a:")) != -1) {
        switch (opt) {
            case 'a':
                strncpy(str_answer_md5, optarg, strlen(optarg));
                str_answer_md5[strlen(optarg)] = '\0';
                res_md5sum = get_md5_sum(str_answer_md5);
                strncpy(answer_md5, res_md5sum.c_str(), res_md5sum.size());
                answer_md5[MD5_SIZE] = '\0';
                flag_answer_set = true;
                break;
            case 'p':
                my_port = (unsigned short)atoi(optarg);
                init_socket(my_port);
                flag_port_set = true;
                break;
            default:
                fprintf(stderr, "Unknown argument\n");
                exit(EXIT_FAILURE);
        }
    }
    if (!flag_answer_set || !flag_port_set) {
        fprintf(stderr, "Not all arguments\n");
        exit(EXIT_FAILURE);
    }

    long long start_time = get_cur_time();
    bool flag_execute = true;
    while (flag_execute) {
        fd_set fds;
        int max_fd = 0;

        FD_ZERO(&fds);
        FD_SET(server_socket, &fds);
        max_fd = std::max(max_fd, server_socket);

        for (auto client : clients) {
            FD_SET(client.first, &fds);
            max_fd = std::max(max_fd, client.first);
        }

        struct timeval tv = {0, SELECT_TIMEOUT};
        int activity = select(max_fd + 1, &fds, NULL, NULL, &tv);
        if (activity < 0) {
            continue;
        }
        if (activity) {
            fprintf(stderr, "Activity: %d\n", activity);
        }

        std::vector<int> to_delete;
        for (auto client : clients) {
            if (FD_ISSET(client.first, &fds)) {
                fprintf(stderr, "Client: %d\n", client.first);
                int res = handle_client_message(client.first);
                if (res == CLIENT_FOUND_ANSWER) {
                    flag_execute = false;
                    break;
                }
                else if (res == CLIENT_IN_PROCESS) {
                    client.second->last_time = get_cur_time();
                }
                else if (res == CLIENT_NOT_FOUND_ANSWER){
                    to_delete.push_back(client.first);
                }
                else if (res == CLIENT_ASKED_INTERVAL) {
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

        std::vector<std::pair<long long, long long>> intervals_to_delete;
        long long cur_time = get_cur_time();
        for (auto interval : calculating_intervals) {
            if (cur_time - interval.second > TIME_WAIT_CLIENT) {
                intervals_to_delete.push_back(interval.first);
            }
        }
        for (auto interval : intervals_to_delete) {
            calculating_intervals.erase(interval);
            free_intervals.push_back(interval);
        }

        for (auto id : to_delete) {
            fprintf(stderr, "Have to delete\n");
            delete_client_connection(id);
        }

        if (FD_ISSET(server_socket, &fds)) {
            int res = handle_new_connection();
        }
    }

    for (auto client : clients) {
        delete client.second;
    }

    fprintf(stderr, "Answer: %s\n", received_answer_str);

    long long end_time = get_cur_time();
    std::cerr << double(end_time - start_time) / 1000000. << " sec.\n";
    free(buf);
    return 0;
}
