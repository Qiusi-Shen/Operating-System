// NAME: Qiusi Shen
// Email: steve.q.shen@gmail.com
// ID: 004749315

#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <netdb.h>
#include <netinet/in.h>
#include <mcrypt.h>
#include <string.h>

int sockfd, newsockfd;
int eflag = 0, n = 0;
int ikey;
MCRYPT encryptfd, decryptfd;

pid_t pid = -1;

char crlf[2] = {0x0D, 0x0A};
char lf[1] = {0x0A};

int pipe1[2], pipe2[2];

struct pollfd pfd[2];

void read_check(){
    char buf1[2049];
    ssize_t count1 = 0;
    count1 = read(newsockfd, buf1, 2048);
    if (count1 < 0) {
        fprintf(stderr, "Error in read.\n");
        kill(0, SIGTERM);
    } else if (count1 == 0) {
        kill(0, SIGTERM);
    }
    for (int k = 0; k < count1; k++) {
        if (eflag) {
            mdecrypt_generic (decryptfd, buf1+k, 1);
        }
        char cur = buf1[k];
        if (cur == 0x0D || cur == 0x0A) {   
            write(pipe1[1], lf, 1);
        } else if (cur == 0x04) {           
            close(pipe1[1]);
        } else if (cur == 0x03) {          
            kill(pid, SIGINT);
        } else {                           
            write(pipe1[1], buf1+k, 1);
        }
    }
}

void termin (int withmsg) {
    if (eflag) {  
        mcrypt_generic_deinit(encryptfd);
        mcrypt_module_close(encryptfd);
        mcrypt_generic_deinit(decryptfd);
        mcrypt_module_close(decryptfd);
    }
    if (withmsg) {  
        int status = 0;
        waitpid(0, &status, 0);
        fprintf(stderr, "SHELL EXIT SIGNAL=%d STATUS=%d\n", WTERMSIG(status), WEXITSTATUS(status));
    }
}

void handler(int signum) {
    if (signum == SIGINT || signum == SIGPIPE || signum == SIGTERM ) 
    {
        termin(1);
        exit(0);
    }
}

int main(int argc, char *argv[]) {
    static struct option long_options[] = {
        {"port",    required_argument,  0,  'p' },
        {"encrypt", required_argument,  0,  'e'},
        {0,             0,                  0,  0 }  
    };
    
    int l = 0;
    int keysize =16;

    char *key;
    char *IV1;
    while (1) {
        int temp = getopt_long(argc, argv, "p:e", long_options, &l);
        if (temp==-1) break;        
        switch(temp){
            case 'p':  
                n = atoi(optarg);  
                signal(SIGINT, handler);
                signal(SIGPIPE, handler);
                signal(SIGTERM, handler);
                break;
            case 'e':  
                ikey = open(optarg, O_RDONLY);
                struct stat st;
                if (fstat(ikey, &st) < 0) {
                    close(ikey);
                    fprintf(stderr, "Error return\n");
                    exit(1);
                }
                key = (char*) malloc((int) st.st_size * sizeof(char));
                read(ikey, key, (int) st.st_size);
                close(ikey);
                encryptfd = mcrypt_module_open("twofish", NULL, "cfb", NULL);
                if (encryptfd == MCRYPT_FAILED) {
                    fprintf(stderr, "Error open.\n");
                    exit(1);
                }
                IV1 = malloc(mcrypt_enc_get_iv_size(encryptfd));
                for (int i=0; i< mcrypt_enc_get_iv_size(encryptfd); i++) {
                    IV1[i]=rand();
                }
                if (mcrypt_generic_init(encryptfd, key, keysize, IV1) < 0) {
                    fprintf(stderr, "Error encrpt.\n");
                    exit(1);
                }
                decryptfd = mcrypt_module_open("twofish", NULL, "cfb", NULL);
                if (decryptfd == MCRYPT_FAILED) {
                    fprintf(stderr, "Error\n");
                    exit(1);
                }
                if (mcrypt_generic_init(decryptfd, key, keysize, IV1) < 0) {
                    fprintf(stderr, "Error open\n");
                    exit(1);
                }
                eflag = 1; 
                break;
            default:    
                fprintf(stderr, "Usage: ./lab1b-server --port=# [--encrypt=file]\n");
                exit(1);
                break;
        }
    }
    
    struct sockaddr_in serv_addr, cli_addr;
    
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    
    if (sockfd < 0) {
        perror("ERROR opening socket");
        termin(0);
        close(sockfd);
        exit(1);
    }
    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(n);
    
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR on binding");
        termin(0);
        close(sockfd);
        exit(1);
    }

    listen(sockfd,5);
    socklen_t clilen = sizeof(cli_addr);
    newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
    
    if (newsockfd < 0) {
        perror("ERROR on accept.\n");
        termin(0);
        close(newsockfd);
        close(sockfd);
        exit(1);
    }
    
    if (pipe(pipe1) == -1 || pipe(pipe2) == -1) {
        fprintf(stderr, "pipe() failed.\n");
        termin(0);
        close(newsockfd);
        close(sockfd);
        exit(1);
    }
    
    pid = fork();
    
    if (pid > 0) {
        close(pipe1[0]);
        close(pipe2[1]);
        
        pfd[0].fd = newsockfd; 
        pfd[0].events = POLLIN | POLLERR | POLLHUP;
        pfd[0].revents = 0;
        
        pfd[1].fd = pipe2[0];
        pfd[1].events = POLLIN | POLLERR | POLLHUP;
        pfd[1].revents = 0;

        while(1) {
            if (poll(pfd, 2, 0) == -1) {
                fprintf(stderr, "error in poll().\n");
                termin(1);
                close(newsockfd);
                close(sockfd);
                exit(1);
            }
            if (pfd[0].revents & POLLIN) {
                read_check();
            }
            if (pfd[0].revents & (POLLHUP+POLLERR)) {
                fprintf(stderr, "Error in shell.\n");
                break;
            }
            if (pfd[1].revents & POLLIN) {
                char buf2[2049];
                ssize_t count2 = 0;
                count2 = read(pipe2[0], buf2, 2048);
                for (int k = 0; k < count2; k++) {
                    if (eflag) {
                        mcrypt_generic (encryptfd, buf2+k, 1);
                    }
                    write(newsockfd, buf2+k, 1);
                }
            }
            if (pfd[1].revents & (POLLHUP+POLLERR)) {
                close(pipe2[0]);  
                termin(1);
                close(newsockfd);
                close(sockfd);
                exit(0);
            }
        }
    } else if (pid == 0) {
        close(pipe1[1]);
        close(pipe2[0]);

        dup2(pipe1[0], STDIN_FILENO);
        dup2(pipe2[1], STDOUT_FILENO);
        dup2(pipe2[1], STDERR_FILENO);
        
        close(pipe1[0]);
        close(pipe2[1]);
        
        char *execvp_argv[2];
        execvp_argv[0] = "/bin/bash";
        execvp_argv[1] = NULL;
        if (execvp("/bin/bash", execvp_argv) == -1) {
            fprintf(stderr, "execvp() failed.\n");
            termin(1);
            close(newsockfd);
            close(sockfd);
            exit(1);
        }
    } else {
        fprintf(stderr, "fork() failed.\n");
        termin(1);
        close(newsockfd);
        close(sockfd);
        exit(1);
    }
     
    close(newsockfd);
    close(sockfd);
    termin(1);
    exit(0);
}