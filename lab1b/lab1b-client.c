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

pthread_t *thread_id1;
pthread_t *thread_id2;


struct termios o;
struct pollfd pfd[2];

struct sockaddr_in serv_addr;
struct hostent *server;

int i = 0;
int logfd, keyfd;
int iflag = 0, eflag = 0;
MCRYPT encryptfd, decryptfd;
int sockfd;

char check1[2] = {0x0D, 0x0A};

void termin();

void read_check(){
    char buffer[1025];
    ssize_t count1 = read(STDIN_FILENO, buffer, 1025);
    for (int k = 0; k < count1; k++) {
        char t = buffer[k];
        
        if (t == 0x0D || t == 0x0A) {   
            write(STDOUT_FILENO, check1, 2);
        } else {                            
            write(STDOUT_FILENO, buffer+k,1);
        }
        if (eflag){
            mcrypt_generic (encryptfd, buffer+k, 1);
        }
        write(sockfd, buffer+k, 1);
        
        if (iflag) {
            char my_log[14] = "SENT 1 bytes: ";
            write(logfd, my_log, 14);
            write(logfd, buffer+k, 1);
            write(logfd, "\n",1);
        }
    }
}

int write_check(){
    char buffer[1025];
    ssize_t n;
    n = read(sockfd, buffer, 1024);
    if (n == 0) {
        return 1;
    }
    if (n < 0) {
        perror("ERROR reading from socket");
        termin();
        exit(1);
    }
    for (int k = 0; k < n; k++) {
        if (iflag)
        {
            char my_log[18] = "RECEIVED 1 bytes: ";
            write(logfd, my_log, 18);
            write(logfd,  buffer+k, 1);
            write(logfd, "\n",1);
        }
        
        if (eflag) mdecrypt_generic (decryptfd, buffer+k, 1);
    
        char t = buffer[k];
        if (t == 0x0D || t == 0x0A) write(STDOUT_FILENO, check1, 2);
         else {
            write(STDOUT_FILENO, buffer+k,1);
        }
    }
    return 0;
}

void termin (void) {
    tcsetattr (STDIN_FILENO, TCSANOW, &o);
    if (eflag) {
        thread_id1 = calloc(1, sizeof(pthread_t));
        thread_id2 = calloc(1, sizeof(pthread_t));
        mcrypt_generic_deinit(encryptfd);
        mcrypt_module_close(encryptfd);
        mcrypt_generic_deinit(decryptfd);
        mcrypt_module_close(decryptfd);
    }
    if (iflag) {
        close(logfd);
    }
    close(sockfd);
}


int main(int argc, char *argv[]) {
        
    static struct option long_options[] = {
        {"port",    required_argument,  0,  'p' },
        {"log",     required_argument,  0,  'l'  },
        {"encrypt", required_argument,  0,  'e'},
        {0,             0,                  0,  0 }  
    };
    
   
    int n_port = 0;
    int keysize = 16;
    int cursize;
    char *key;
    char *IV1;
    
    while (1) {
        int opt = getopt_long(argc, argv, "p:l:e", long_options, &i);
        if(opt==-1) break;
        switch(opt){
            case 'p':  
                n_port = atoi(optarg); 
                break;
            case 'l':
                logfd = creat(optarg, S_IRWXU); 
                if (logfd < 0){
                    fprintf(stderr, "Error: Fail to create the log file.\n");
                    exit(1);
                }
                iflag = 1;
                break;
            case 'e':
                keyfd = open(optarg, O_RDONLY);
                struct stat st;
                if (fstat(keyfd, &st) < 0) {
                    close(keyfd);
                    fprintf(stderr, "Error: fail to return information about the specified key file.\n");
                    exit(1);
                }
                cursize = (int) st.st_size;
                key = (char*) malloc(cursize * sizeof(char));
                read(keyfd, key, cursize);
                close(keyfd);
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
                    fprintf(stderr, "Error encryption.\n");
                    exit(1);
                }
                decryptfd = mcrypt_module_open("twofish", NULL, "cfb", NULL);
                if (decryptfd == MCRYPT_FAILED) {
                    fprintf(stderr, "Error: open\n");
                    exit(1);
                }

                if (mcrypt_generic_init(decryptfd, key, keysize, IV1) < 0) {
                    fprintf(stderr, "Error decrytion.\n");
                    exit(1);
                }
                eflag = 1;
                break;
            default:    
                fprintf(stderr, "Usage: ./lab1b-client --port=# [--log=logfile] [--encrypt=file]\n");
                exit(1);
                break;
        }
    }
    tcgetattr(STDIN_FILENO, &o);    
    struct termios n;
    tcgetattr(STDIN_FILENO, &n);
    n.c_iflag = ISTRIP;
    n.c_oflag = 0;
    n.c_lflag = 0;
    n.c_lflag &= ~(ICANON|ECHO); 
    n.c_cc[VMIN] = 1;
    n.c_cc[VTIME] = 0;
    
    if (tcsetattr(STDIN_FILENO, TCSANOW, &n) < 0)
    { 
        fprintf(stderr, "%s: Error Request.\n", strerror(errno));
        termin();
        exit(EXIT_FAILURE);
    }
    
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    
    if (sockfd < 0) {
        fprintf(stderr, "ERROR\n");
        termin();
        exit(1);
    }
    
    server = gethostbyname("localhost");
    
    if (server == NULL) {
        fprintf(stderr,"ERROR\n");
        termin();
        exit(0);
    }
        memset((char *) &serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy((char *)&serv_addr.sin_addr.s_addr, (char *)server->h_addr, server->h_length);
    serv_addr.sin_port = htons(n_port);    
    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR connecting");
        termin();
        exit(1);
    }
    
    pfd[0].fd = STDIN_FILENO;
    pfd[0].events = POLLIN | POLLERR | POLLHUP;
    pfd[0].revents = 0;
    pfd[1].fd = sockfd;
    pfd[1].events = POLLIN | POLLERR | POLLHUP;
    pfd[1].revents = 0;
    
 


    while(1) {
        int rv = poll(pfd, 2, 0);
        if (rv == -1) {
            fprintf(stderr, "error in poll().\n");
            termin();
            exit(1);
        }
        if (pfd[0].revents & POLLIN) {
            read_check();
        }
        
        if (pfd[0].revents & (POLLHUP+POLLERR)) {
            fprintf(stderr, "error in shell\n");
            break;
        }
        
        if (pfd[1].revents & POLLIN) {
            if(write_check()) break;
        }
        if (pfd[1].revents & (POLLHUP+POLLERR)) {
            fprintf(stderr, "error in shell\n");
            break;
        }
    }
    termin();
    exit(0);
}