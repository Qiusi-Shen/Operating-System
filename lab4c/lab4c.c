#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <mraa.h>
#include <math.h>   
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <openssl/ssl.h>

pthread_mutex_t lockA;
pthread_mutex_t lockB;
pthread_mutex_t lockC;
pthread_mutex_t lockD;
pthread_t controller;
char bufferInput[512];
int sockNum;
int stop = 0;      
char scale = 'F';
int period = 1;
FILE *fd;
char buffer[50];
mraa_aio_context temperature;
mraa_gpio_context button;
char logFile[50];
float readValue;
float generateValue, generateDegree;
time_t nowTime;
time_t passTime;
struct tm realTime;
struct tm *timeInfo;
int off;
int portNum;
char id[10];
char hostName[56];
int delay = -1;
SSL* ssl;
SSL_CTX* ctx;

void pressBotton()
{
    if(fd!=NULL)
    {
        char tempBuffer[50];

        tzset();
        system("cp /usr/share/zoneinfo/PST8PDT /etc/localtime");
        setenv("TZ","PST8PDT",1);
        tzset();
        time(&nowTime);
        timeInfo = localtime(&nowTime);

        struct tm tempTime = *timeInfo;
        sprintf (tempBuffer, "%02d:%02d:%02d SHUTDOWN\n", tempTime.tm_hour, tempTime.tm_min, tempTime.tm_sec);

        if(fd)
        {
            fprintf(fd, tempBuffer);
            fflush(fd);
        }

        if(ssl)
        {
            if(SSL_write(ssl, tempBuffer, strlen(tempBuffer)) <= 0)
            {
                fprintf(stderr, "Catch error while try to write shutdown.\n");
            }
            if(ssl)
            {
                SSL_free(ssl);
            }
            if(ctx)
            {
                SSL_CTX_free(ctx);
            }
        }
        else if(sockNum > 0)
        {
            write(sockNum, tempBuffer, strlen(tempBuffer));
            shutdown(sockNum, 2);
        }
        fclose(fd);   
    }
    exit(0);
}

void* generateReport()
{ 
    while(1)
    {
        time(&nowTime);
        timeInfo = localtime(&nowTime);

        if(delay == -1)
        {
            sleep(period);
        }
        else
        {
            sleep(delay);
            delay=-1;
        }

        memset(buffer, 0, 50);

        readValue = mraa_aio_read(temperature);
        generateValue = 100000.0 * (1023.0 / ((float)readValue) - 1.0);
        
        tzset();
        system("cp /usr/share/zoneinfo/PST8PDT /etc/localtime");
        setenv("TZ","PST8PDT",1);
        tzset();
        time(&nowTime);
        timeInfo = localtime(&nowTime);

        struct tm sTime =* timeInfo;
        realTime = *timeInfo;

        if(scale == 'F')
        {
            generateDegree = (1.0/(log(generateValue/100000.0)/4275+1/298.15)-273.15)*1.8+32;
        }
        else
        {
            generateDegree = (1.0/(log(generateValue/100000.0)/4275+1/298.15)-273.15);
        }
        sprintf (buffer,"%02d:%02d:%02d %0.1f\n", sTime.tm_hour, sTime.tm_min, sTime.tm_sec, generateDegree);
    
        if(off == 0 && stop == 0)
        {
            if(ssl)
            {
                SSL_write(ssl, buffer, strlen(buffer));
            }
            else if (sockNum > 0)
            {
                write(sockNum, buffer, strlen(buffer)) ;
            }
            fflush(stdout);

            if(stop == 0 && fd != NULL)
            {
                fprintf(fd,buffer);
                fflush(fd);
            }
        }

        int threadReturn = 2;
        if(fd != NULL && threadReturn == 2)
        {
            if(off!=1)
            {
                ;
            }
            else
            {
                char tempBuffer[50];

                tzset();
                system("cp /usr/share/zoneinfo/PST8PDT /etc/localtime");
                setenv("TZ","PST8PDT",1);
                tzset();
                time(&nowTime);
                timeInfo = localtime(&nowTime);

                struct tm tempTime=*timeInfo;
                sprintf (tempBuffer,"OFF\n%02d:%02d:%02d SHUTDOWN", tempTime.tm_hour, tempTime.tm_min, tempTime.tm_sec);

                if(fd)
                {
                    fprintf(fd, tempBuffer);
                    fflush(fd);
                }

                char secondBuffer[56];
                sprintf(secondBuffer, "%02d:%02d:%02d SHUTDOWN\n", tempTime.tm_hour, tempTime.tm_min, tempTime.tm_sec);

                if(ssl)
                {
                    SSL_write(ssl, secondBuffer, strlen(secondBuffer));
                }
                else
                {
                    write(sockNum, secondBuffer, strlen(secondBuffer));
                }
                exit(0);
            }
        }    
    }
}

void controlProcessor()
{
    pthread_attr_t attr;
    pthread_attr_init(&attr);

    fd_set resetFds;
    struct timeval timeNewValue;
    int newReturnValue;
    
    memset(bufferInput, 0, 512);
    
    FD_ZERO(&resetFds);
    FD_SET(sockNum, &resetFds);
    
    timeNewValue.tv_sec = 0;
    timeNewValue.tv_usec = 0;

    while(1)
    {
        FD_ZERO(&resetFds);
        FD_SET(sockNum, &resetFds);

        timeNewValue.tv_sec = 0;
        timeNewValue.tv_usec = 0;
        
        newReturnValue = select(sockNum+1, &resetFds, NULL, NULL, &timeNewValue);

        if (newReturnValue == 0)
        {
            continue;
            
        }

        if(newReturnValue < 0)
        {
            return;
			
        }
        
        fflush(fd);

        int length = 0;
        char lengthBuffer;
		int byteNum = 0;

        memset(bufferInput, 0, 512);

        if(ssl)
        {
            byteNum = SSL_read(ssl, &lengthBuffer, 1);
        }
        else if (sockNum > 0)
        {
            byteNum = read(sockNum, &lengthBuffer, 1);
        }

        while(byteNum > 0)
        {
            if (lengthBuffer != '\n')
            {
                bufferInput[length] = lengthBuffer;
                
            }
            else
            {
                bufferInput[length] = lengthBuffer;
                bufferInput[length + 1] = '\0';
                break;
            }

            memset(&lengthBuffer, 0, 1);
			length++;

            if(ssl)
            {
                byteNum = SSL_read(ssl, &lengthBuffer, 1);
            }
            else if (sockNum > 0)
            {
                byteNum = read(sockNum, &lengthBuffer, 1);
            }
        }

        fflush(stdin);

        if(strlen(bufferInput) < 2)
        {
            continue;
        }
        
        if(strstr(bufferInput, "PERIOD"))
        {
            pthread_cancel(controller);

            tzset();
            system("cp /usr/share/zoneinfo/PST8PDT /etc/localtime");
            setenv("TZ","PST8PDT",1);
            tzset();
            time_t resetTime;
            time(&resetTime);
            struct tm *newTimeInfo;
            newTimeInfo = localtime(&resetTime);
            struct tm newTempTime = *newTimeInfo;

            fflush(fd);
    
            char *intruString = (char *) malloc(strlen(bufferInput) - strlen("PERIOD"));
            unsigned int intIndex;
            int m=0;

            for(intIndex = strlen("PERIOD="); intIndex < strlen(bufferInput); intIndex++)
            {
                intruString[m] = bufferInput[intIndex];
                m++;
            }

            period = atoi(intruString);
            long int newSeconds = 0;
            newSeconds = 3600 * newTempTime.tm_hour + newTempTime.tm_min * 60 + newTempTime.tm_sec - realTime.tm_sec - 60 * realTime.tm_min - 3600 * realTime.tm_hour;

            if(newSeconds >= period)
            {
                delay=0;
            }
            else
            {
                delay = period - newSeconds;
            }

            free(intruString);

            if(fd)
            {
                fprintf(fd, bufferInput);
                fflush(fd);
            }

             pthread_create(&controller,&attr,generateReport,NULL);
        }
        else if (strcmp(bufferInput, "OFF\n") == 0)
        {
            pthread_cancel(controller);
            
            fflush(fd);

			char tempBuffer[50];

            off = 1;

            tzset();
            system("cp /usr/share/zoneinfo/PST8PDT /etc/localtime");
            setenv("TZ","PST8PDT",1);
            tzset();
            time(&nowTime);
            timeInfo = localtime(&nowTime);

            struct tm tempTime = *timeInfo;
            sprintf (tempBuffer, "OFF\n%02d:%02d:%02d SHUTDOWN", tempTime.tm_hour, tempTime.tm_min, tempTime.tm_sec);

            if(fd)
            {
                fprintf(fd,tempBuffer);
                fflush(fd);
            }

            char secondBuffer[56];
            sprintf (secondBuffer, "%02d:%02d:%02d SHUTDOWN\n", tempTime.tm_hour, tempTime.tm_min, tempTime.tm_sec);

            if(ssl)
            {
                SSL_write(ssl, secondBuffer, strlen(secondBuffer));
            }
            else if (sockNum > 0)
            {
            
                write(sockNum, secondBuffer, strlen(secondBuffer));
            }
            return;
        }
        else if(strcmp(bufferInput, "SCALE=F\n") == 0)
        {
            pthread_cancel(controller);

            fflush(fd);

            tzset();
            system("cp /usr/share/zoneinfo/PST8PDT /etc/localtime");
            setenv("TZ", "PST8PDT", 1);
            tzset();
            time_t resetTime;
            time(&resetTime);
            struct tm *newTimeInfo;
            newTimeInfo = localtime(&resetTime);
            struct tm newTempTime=*newTimeInfo;

            scale='F';

            if(fd)
            {
                fprintf(fd,bufferInput);
                fflush(fd);
            }

            long int newSeconds = 0;
            newSeconds = 3600 * newTempTime.tm_hour - 3600 * realTime.tm_hour + newTempTime.tm_min * 60 + newTempTime.tm_sec - realTime.tm_sec - 60 * realTime.tm_min ;

            if(newSeconds >= period)
            {
                delay=0;
            }
            else
            {
                delay = period - newSeconds;
            }
            pthread_create(&controller, &attr, generateReport, NULL);
        }
        else if(strcmp(bufferInput, "SCALE=C\n") == 0)
        {
            pthread_cancel(controller);

            tzset();
            system("cp /usr/share/zoneinfo/PST8PDT /etc/localtime");
            setenv("TZ","PST8PDT",1);
            tzset();
            time_t resetTime;
            time(&resetTime);
            struct tm *newTimeInfo;
            newTimeInfo = localtime(&resetTime);
            struct tm newTempTime = *newTimeInfo;
     
            fflush(fd);
    
            scale='C';

            if(fd)
            {
                fprintf(fd,bufferInput);
                fflush(fd);
            }

            long int newSeconds = 0;
            newSeconds = 3600 * newTempTime.tm_hour - 3600 * realTime.tm_hour + newTempTime.tm_min * 60 + newTempTime.tm_sec - realTime.tm_sec - 60 * realTime.tm_min ;
    
            if(newSeconds >= period)
            {
                delay=0;
            }
            else
            {
                delay = period - newSeconds;
            }
            pthread_create(&controller,&attr,generateReport,NULL);
        }
		else if(strcmp(bufferInput, "START\n") == 0)
        {
            if(fd)
            {
                fflush(fd);
                
                fprintf(fd,bufferInput);
                fflush(fd);
            }
            if(stop!=0)
            {
                pthread_create(&controller,&attr,generateReport,NULL);
                stop=0;
            }
        }
        else if(strcmp(bufferInput, "STOP\n") == 0)
        {
            pthread_cancel(controller);

            if(fd)
            {
                fprintf(fd,bufferInput);
                fflush(fd);
            }
            
            fflush(fd);
    
            stop=1;
        }
    }
}

int main(int argc, char **argv)
{
    mraa_init();
    if (pthread_mutex_init(&lockA, NULL) != 0)
    {
        printf("\n mutex init failed\n");
        return 1;
    }
    if (pthread_mutex_init(&lockB, NULL) != 0)
    {
        printf("\n mutex init failed\n");
        return 1;
    }
    if (pthread_mutex_init(&lockC, NULL) != 0)
    {
        printf("\n mutex init failed\n");
        return 1;
    }

    if (pthread_mutex_init(&lockD, NULL) != 0)
    {
        printf("\n mutex init failed\n");
        return 1;
    }

    temperature = mraa_aio_init(1);
    button = mraa_gpio_init(60);

    mraa_gpio_dir(button,MRAA_GPIO_IN);
    mraa_gpio_isr(button,MRAA_GPIO_EDGE_RISING,&pressBotton,NULL);

    static struct option long_options[] =
    {
        {"period", required_argument, 0, 'p'},
        {"scale",  required_argument, 0, 's'},
        {"log",    required_argument, 0, 'l'},
        {"id",     required_argument, 0, 'i'},
        {"host",   required_argument, 0, 'h'},
		{0,        0,                  0, 0 }
    };
    
	struct sockaddr_in serverName;
    int opt = 1;
	int n;
    int t = 1;

    for (n = 1; n < argc; n++)
    {
        if(argv[n][0] != '-' && argv[n][1] != '-')
        {
            unsigned int r;

            for(r=0; r < strlen(argv[n]); r++)
            {
                if(!isdigit(argv[n][r]))
                {
                    t=0;
                }
            }

            if(t == 1)
            {
                portNum = atoi(argv[n]);
            }
            else
            {
                fprintf (stderr,"Non-option argument %s\n", argv[n]);
            }
            t = 1;
        }
    }

    while (1)
    {
        opt = getopt_long(argc, argv, "p:s:l:i:h:", long_options, NULL);
        
        if(opt == -1)
            break;

        switch(opt)
        {
            case 'p':
			{
				if (optarg[0] == '-')
				{
					fprintf(stderr, "period requires argument\n");
					exit(1);
				}

				period = atoi(optarg);
				if (period < 0 || period > 100)
				{
					fprintf(stderr, "Period not valid\n");
					exit(1);
				}
				break;
			}
            case 's':
			{
				if (optarg[0] == '-')
				{
					fprintf(stderr, "--scale requires argument\n");
					exit(1);
				}

				if (strlen(optarg) > 1)
				{
					fprintf(stderr, "usage --scale=F or scale=C\n");
					exit(1);
				}

				scale = optarg[0];
				break;
			}
            case 'l':
			{
				if (optarg[0] == '-')
				{
					fprintf(stderr, "--log requires argument\n");
					exit(1);
				}

				fd = fopen(optarg, "w");
				strcpy(logFile, optarg);

				if (fd == NULL)
				{
					fprintf(stderr, "unable to create log file\n");
					exit(1);
				}
				break;
			}
            case 'i':
			{
				if (strlen(optarg) != 9)
				{
					fprintf(stderr, "id not 9-digit number\n");
					break;
				}

				int fd = 0;
				int r2;

				for (r2 = 0; r2 < 9; r2++)
				{
					if (!isdigit(optarg[r2]))
					{
						fprintf(stderr, "id not 9-digit number\n");
						fd = 1;
						break;
					}
				}

				if (fd == 0)
				{
					strcpy(id, optarg);
				}
				break;
			}
            case 'h':
			{
				strcpy(hostName, optarg);
				break;
			}
			default:
			{
				exit(1);
			}
        }
    }

    struct hostent *ht;
    if ( (ht = gethostbyname(hostName) ) == NULL )
    {
        exit(1);
    }
    
    sockNum = socket(AF_INET, SOCK_STREAM, 0);

    if(sockNum == -1)
    {
        fprintf(stderr, "could not create socket: %s\n", strerror(errno));
    }
 
    memcpy(&serverName.sin_addr, ht->h_addr_list[0], ht->h_length);
 
    serverName.sin_family = AF_INET;
    serverName.sin_port = htons(portNum);

    if (connect(sockNum , (struct sockaddr *)&serverName, sizeof(serverName)) < 0)
    {
        perror("connect failed. Error");
        exit(1);
    }

    char thirdBuffer[56];

    sprintf(thirdBuffer, "ID=%s\n", id);

    if(strcmp(argv[0], "./lab4c_tls") == 0)
    {
        SSL_load_error_strings();           
        SSL_library_init();               
        ctx = SSL_CTX_new(SSLv23_client_method());

        if(ctx)
        {
            ssl = SSL_new(ctx);
            if(ssl)
            {
                int returnValue2 =  SSL_set_fd(ssl, sockNum);
                if( returnValue2 == 0)
                {
                    fprintf(stderr, "SSL_set_fd failed: %s\n", strerror(errno));
                    exit(1);
                }
                returnValue2 = SSL_connect(ssl);
                if( returnValue2 != 1)
                {
                    fprintf(stderr, "SSL_connect failed: %s\n", strerror(errno));
                    exit(1);
                }
                if(SSL_write(ssl, thirdBuffer, strlen(thirdBuffer)) <= 0)
                {
                    fprintf(stderr, "write failed");
                }
                if(fd)
                {
                    fprintf(fd, thirdBuffer);
                }

            }
        }
    }
    else
    {
     
        if(write(sockNum, thirdBuffer, strlen(thirdBuffer)) <= 0)
        {
            fprintf(stderr, "write failed");
        }
        if(fd)
        {
            fprintf(fd, thirdBuffer);
        }
    }

    off=0;

    pthread_attr_t attr;
    pthread_attr_init(&attr);

    tzset();
    system("cp /usr/share/zoneinfo/PST8PDT /etc/localtime");
    setenv("TZ","PST8PDT",1);
    tzset();
    time( &passTime);

    pthread_create(&controller,&attr,generateReport,NULL);

    controlProcessor();
        
    if(fd != NULL)
    {
        fclose(fd);
    }

    if(sockNum != 0)
    {
        shutdown(sockNum, 2);
        if(ssl)
        {
            SSL_free(ssl);
        }
        if(ctx)
        {
            SSL_CTX_free(ctx);
        }
    }
    return 0;
}
