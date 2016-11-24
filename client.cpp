//
// Created by mano on 20.11.16.
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

const int BUFFER_SIZE = 1000000;
const int ADDRESS_MAX_SIZE = 1000;
const long long CONNECTION_TIMEOUT = 3000000LL; // millisecnds
const int TIME_SLEEP = 3; // seconds
const int MAX_ATTEMPTS_CONNECT = 10;
const int MD5_SIZE = 32;
const long long BASE = 4LL;
const int LITTLE_STRING_SIZE = 1000;

const uint32_t CLIENT_WANT_INTERVAL = 1;
const uint32_t CLIENT_HAVE_ANSWER = 2;

char str_server_addr[ADDRESS_MAX_SIZE];
struct sockaddr_in server_addr;
unsigned short my_port;
struct sockaddr_in my_addr;
int client_socket;

void * buf;
int pbuf;

std::pair<long long, long long> interval;
char md5_answer[MD5_SIZE + 1];
std::string found_answer = "AGTC";

std::map<char, long long> M;
std::map<long long, char> RM;

long long str_to_ll(std::string str) {
    long long p4 = 1LL;
    long long base = 4LL;
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

struct sockaddr_in get_address(char *strip, char *strport) {
    struct sockaddr_in addr;
    bzero(&addr, sizeof(struct sockaddr_in));
    addr.sin_family = PF_INET;
    addr.sin_port = htons((unsigned short)atoi(strport));
    struct hostent *host_info = gethostbyname(strip);
    memcpy(&(addr.sin_addr), host_info->h_addr, (size_t)host_info->h_length);
    return addr;
}

bool parse_address(char* cstr_addr, struct sockaddr_in &addr) {
    fprintf(stderr, "Addr: %s\n", cstr_addr);
    std::string str_addr(cstr_addr);
    size_t pos = str_addr.find(':');
    size_t len = str_addr.size();
    if (pos == std::string::npos) {
        fprintf(stderr, "Bad string\n");
        return false;
    }

    char strip[1000];
    char strport[1000];
    strncpy(strip, cstr_addr, pos);
    strip[pos] = '\0';
    strncpy(strport, cstr_addr + pos + 1, len - pos - 1);
    strport[len - pos - 1] = '\0';

    addr = get_address(strip, strport);
    return true;
}

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

void init_global() {
    std::vector<char> v = {'A', 'G', 'T', 'C'};
    for (int i = 0; i < v.size(); ++i) {
        M[v[i]] = i;
        RM[i] = v[i];
    }

    buf = malloc(BUFFER_SIZE);
    pbuf = 0;

    bzero(&my_addr, sizeof(struct sockaddr_in));
    my_addr.sin_family = PF_INET;
    my_addr.sin_port = htons(my_port);
    my_addr.sin_addr.s_addr = htonl(INADDR_ANY);
}

void parse_buffer() {
    std::vector<long long> v(4);
    for (int i = 0; i < 4; ++i) {
        v[i] = (long long)ntohl(((uint32_t*)(buf))[i]);
    }
    const long long d = 1000000000LL;
    long long l = v[0] * d + v[1];
    long long r = v[2] * d + v[3];
    interval = {l, r};
    strncpy(md5_answer, (char*)buf + 4 * sizeof(uint32_t), MD5_SIZE);
    md5_answer[MD5_SIZE] = '\0';
}

void endless_try_to_connect() {
    client_socket = socket(PF_INET, SOCK_STREAM, 0);
    int res = connect(client_socket, (struct sockaddr *)&server_addr, sizeof(struct sockaddr_in));
    while (res < 0) {
        perror("connect");
        close(client_socket);
        client_socket = socket(PF_INET, SOCK_STREAM, 0);
        fprintf(stderr, "%d\n", client_socket);
        res = connect(client_socket, (struct sockaddr *) & server_addr, sizeof(struct sockaddr_in));
    }
}

int do_receive() {
    pbuf = 0;
    ssize_t size = recv(client_socket, (void*)((char*)buf + pbuf), BUFFER_SIZE, 0);
    while (size > 0){
        pbuf += size;
        size = recv(client_socket, (void*)((char*)buf + pbuf), BUFFER_SIZE, 0);
    }
    if (size < 0) {
        fprintf(stderr, "Error when receive\n");
        return -1;
    }
    if (pbuf < 4 * sizeof(uint32_t) + MD5_SIZE) {
        fprintf(stderr, "Received is not correct\n");
        return -1;
    }
    fprintf(stderr, "Good receive\n");
    return 1;
}


bool check_str(std::string str) {
    std::string md5res = get_md5_sum(str.c_str());
    return (!strcmp(md5_answer, md5res.c_str()));
}

bool try_to_find_answer() {
    if (interval.second < interval.first) {
        return false;
    }

    for (long long cur = interval.first; cur <= interval.second; ++cur) {
        std::string str = ll_to_str(cur);
        if (cur % 1000LL == 0) {
            fprintf(stderr, "Step: {L(%d) CUR(%d) R(%d)}\n", interval.first, cur, interval.second);
            fprintf(stderr, "Try string: \"%s\"\n", str.c_str());
        }
        if (check_str(str)) {
            found_answer = str;
            return true;
        }
    }
    fprintf(stderr, "Not found :-(\n");
    return false;
}

void send_answer(bool res) {
    char answer[LITTLE_STRING_SIZE];
    strncpy(answer, found_answer.c_str(), found_answer.size());
    if (res) {
        fprintf(stderr, "Found answer: %s\n", answer);
    }
    else {
        fprintf(stderr, "Try answer: %s\n", answer);
    }
    answer[found_answer.size()] = '\0';
    ((uint32_t*)(buf))[0] = htonl(CLIENT_HAVE_ANSWER);
    memcpy((void*)((char*)buf + sizeof(uint32_t)), answer, strlen(answer));
    send(client_socket, buf, sizeof(uint32_t) + strlen(answer), 0);
    fprintf(stderr, "!!! Answer has been sent !!!\n");
}

void send_ask_interval() {
    ((uint32_t*)(buf))[0] = htonl(CLIENT_WANT_INTERVAL);
    send(client_socket, buf, sizeof(uint32_t), 0);
}

int main(int argc, char * argv[]) {
    init_global();

    int opt;
    while ((opt = getopt(argc, argv, "s:")) != -1) {
        switch (opt) {
            case 's':
                strncpy(str_server_addr, optarg, strlen(optarg));
                parse_address(str_server_addr, server_addr);
                break;
            default:
                fprintf(stderr, "Unknown argument\n");
                exit(EXIT_FAILURE);
        }
    }

    while (1) {
        endless_try_to_connect();
        fprintf(stderr, "Connected to receive interval\n");

        send_ask_interval();
        int res = do_receive();
        close(client_socket);

        if (res <= 0) {
            fprintf(stderr, "Error when receive interval");
            sleep(TIME_SLEEP);
            continue;
        }

        fprintf(stderr, "Received interval\n");

        parse_buffer();
        fprintf(stderr, "Buffer parsed\n");
        bool result_find = try_to_find_answer();
        fprintf(stderr, "Tried %d\n", (int)result_find);

        endless_try_to_connect();
        fprintf(stderr, "Connected to send answer\n");
        send_answer(result_find);
        close(client_socket);

        fprintf(stderr, "After close\n");

        if (result_find) {
            fprintf(stderr, "Answer has been found\n");
            break;
        }
    }

    free(buf);
    return 0;
}

