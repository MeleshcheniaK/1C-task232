#include <arpa/inet.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wait.h>

volatile sig_atomic_t must_exit = 0;
volatile sig_atomic_t client_socket = -1;
pid_t client_pid = -1;
volatile sig_atomic_t socket_fd = -1;

const int LISTEN_BACKLOG = 128;
const int STRING_SIZE = 4096;

const char STR_NOT_FOUND[] = "HTTP/1.1 404 Not Found\r\n";
const char STR_FORBIDDEN[] = "HTTP/1.1 403 Forbidden\r\n";
const char STR_OK[] = "HTTP/1.1 200 OK\r\n";

void handle_sigstop(int signum)
{
  if (client_socket > -1) {
    shutdown(client_socket, SHUT_RDWR);
    close(client_socket);
    client_socket = -1;
  }
  if (client_pid != -1) {
    kill(client_pid, SIGKILL);
  }
  shutdown(socket_fd, SHUT_RDWR);
  close(socket_fd);
  socket_fd = -1;

  must_exit = 1;
}

int signal_handler()
{
  struct sigaction action_stop;
  memset(&action_stop, 0, sizeof(action_stop));
  action_stop.sa_handler = handle_sigstop;
  action_stop.sa_flags = SA_RESTART;

  sigaction(SIGINT, &action_stop, NULL);
  sigaction(SIGTERM, &action_stop, NULL);

  return 0;
}

void normalize_path(char* path, char* new_path)
{
  new_path = strcpy(new_path, path);

  if (new_path[strlen(path) - 1] != '/') {
    new_path[strlen(path)] = '/';
    new_path[strlen(path) + 1] = '\0';
  }
}

int server_main(char* port_num, char* catalog_way)
{
  socket_fd = socket(AF_INET, SOCK_STREAM, 0);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(atoi(port_num));
  struct hostent* hosts = gethostbyname("localhost");
  memcpy(&addr.sin_addr, hosts->h_addr_list[0], sizeof(addr.sin_addr));

  int bind_ret = bind(socket_fd, (struct sockaddr*)&addr, sizeof(addr));
  int listen_ret = listen(socket_fd, LISTEN_BACKLOG);

  char* message = malloc(STRING_SIZE * sizeof(char));
  char* buffer = malloc(STRING_SIZE * sizeof(char));

  while (!must_exit) {
    int client_fd = accept(socket_fd, NULL, NULL);

    int message_len = recv(client_fd, message, STRING_SIZE, 0);
    message[message_len] = '\0';

    char* filename = malloc(STRING_SIZE * sizeof(char));
    strcpy(filename, message);
    filename += 4;
    *strstr(filename, " ") = '\0';

    char* filepath = malloc(STRING_SIZE * sizeof(char));
    strcpy(filepath, catalog_way);
    strcpy(filepath + strlen(catalog_way), filename);

    if (access(filepath, F_OK) == -1) {
      dprintf(client_fd, STR_NOT_FOUND);
    } else if (access(filepath, R_OK) == -1) {
      dprintf(client_fd, STR_FORBIDDEN);
    } else if (access(filepath, X_OK) == -1) {
      FILE* file = fopen(filepath, "r");
      fseek(file, 0L, SEEK_END);
      long int size = ftell(file);
      fseek(file, 0, SEEK_SET);

      fgets(buffer, size, file);

      dprintf(
          client_fd,
          "%s%d\r\n\r\n%s",
          "HTTP/1.1 200 OK\r\nContent-Length: ",
          size,
          buffer);
    } else {
      dprintf(client_fd, "%s\r\n", STR_OK);

      client_pid = fork();

      if (client_pid != 0) {
        waitpid(client_pid, 0, 0);
      } else {
        dup2(client_fd, 1);
        execlp(filepath, filepath, NULL);
      }
    }

    shutdown(client_fd, SHUT_RDWR);
    close(client_fd);
  }

  shutdown(socket_fd, SHUT_RDWR);
  close(socket_fd);

  return 0;
}

int main(int argc, char** argv)
{
  if (signal_handler() != 0) {
    return 0;
  }

  char* catalog_way = malloc(STRING_SIZE * sizeof(char));
  normalize_path(argv[2], catalog_way);

  server_main(argv[1], catalog_way);

  return 0;
}