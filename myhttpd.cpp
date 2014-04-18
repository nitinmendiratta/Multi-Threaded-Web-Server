// Multi Threaded Web Server
#define    BUF_LEN    1024
#define    BUFSIZE    1024
#include   <iostream>
#include   <vector>
#include   <map>
#include   <list>
#include   <stdio.h>
#include   <syslog.h>
#include   <unistd.h>
#include   <dirent.h>
#include   <stdlib.h>
#include   <string.h>
#include   <ctype.h>
#include   <termios.h>
#include   <fcntl.h>
#include   <assert.h>
#include   <sys/types.h>
#include   <sys/socket.h>
#include   <netdb.h>
#include   <netinet/in.h>
#include   <inttypes.h>
#include   <pthread.h>
#include   <semaphore.h>
#include   <time.h>
#include   <arpa/inet.h>
#include   <sys/stat.h>
#include   <fstream>
#include   <errno.h>
#include   <sys/wait.h>

#define BACKLOG 10 // how many pending connections queue will hold
using namespace std;
#define HTTP_OK     "HTTP/1.0 200 OK\n"
#define HTTP_NOTOK  "HTTP/1.0 404 Not Found\n"
#define IMAGE       "Content-Type:image/gif\n"
#define HTML        "Content-Type:text/html\n"
#define FNF_404     "<html><body><h1>SORRY......FILE NOT FOUND<h1><br><h3>Try Searching Another File</h3></body></html>"
#define DNF_404     "<html><body><h1>SORRY......DIRECTORY NOT FOUND<h1><h3><br>Try Searching Another File</h3></body></html>"
#define DFNF_404    "<html><body><h1>SORRY......NOT A EXISTING FILE OR DIRECTORY</h1><br><h3>Try Searching Another File</h3></body></html>"
pthread_cond_t ewait=PTHREAD_COND_INITIALIZER;
pthread_mutex_t llock=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t elock=PTHREAD_MUTEX_INITIALIZER;

int clsock,debug=0,tnumber=4,server;
char rootdir[256],updirectory[256];
char log_file[256];
int sch=1;
int sleeptime=60,flog=0;
char ch;
char *port = "8080";
void *schedule(void*);
void *execute(void*);
char *pvar;
void summary();
void requestprocessing(char[],char[],char[]);

class request{
    public:
        char request_time[100];
        string ip_address;
        int clientfd;        //Client File descriptor
        int rtype;
        int status;           //Request Type 0->Invalid,1->HEAD,2->GET
        int size;            //File size
        char fname[BUFSIZE];//File name
        char execution_time[100];
        string fline;
}r;

bool comparefn(const request& Left, const request& Right){
    return Left.size < Right.size;
}

bool headsort(const request& Left, const request& Right){
    return Left.rtype< Right.rtype;
}

void requestprocessing2(request);
list <request> r1;
list <request> r2;

void sigchld_handler(int s){
    while(waitpid(-1, NULL, WNOHANG) > 0);
}
// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa){
    if (sa->sa_family == AF_INET){
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char *argv[]){
    int sockfd, new_fd; // listen on sock_fd, new connection on new_fd
    char buf[1000];
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    struct sigaction sa;
    int yes=1;
    char s[INET6_ADDRSTRLEN];
    int rv;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP
    log_file[0] = '\0';
	if ((pvar = rindex(argv[0], '/')) == NULL)
		pvar = argv[0];
	else
		pvar++;
	//check various options
	server = 1;
	while ((ch = getopt(argc, argv, "dhr:s:n:p:l:t:")) != -1)
		switch (ch){
                case 'd': //debug mode
                    debug = 1;
                    break;
                case 'r': //set root directory
                    strcpy(rootdir, optarg);
                    break;
                case 's'://set scheduling policy
                    if (strcmp(optarg, "FCFS")==0)
                        sch = 1;
                    else if (strcmp(optarg, "SJF")==0)
                        sch = 2;
                    break;
                case 'n': //no of threads
                    tnumber = atoi(optarg);
                    if(tnumber<=0){
                        cout<<"Please enter number of threads greater than 0"<<endl;
                        exit(1);
                    }
                    break;
                case 'p': //set port no
                    port = optarg;
                    if(port<"1024"){
                        cout<<"Please enter port greater than 1024"<<endl;
                        exit(1);
                    }
                    break;
                case 'l': //address of log file
                    flog=1;
                    strncpy(log_file, optarg, sizeof(log_file));
                    if(optarg==NULL){
                        cout<<"PLEASE ENTER NAME FOR LOG FILE"<<endl;
                    }
                    break;
                case 't': //queuing time
                    sleeptime = atoi(optarg);
                    if(sleeptime<=0){
                        cout<<"Please enter sleeptime greater than 0"<<endl;
                        exit(1);
                    }
                    break;
                case 'h': summary(); //cout<<"in h"<<endl;
                default:
                    summary();//cout<<"in default"<<endl;
                    break;
		}
	argc -= optind;
	if (argc != 0)
		summary();
	if (!server && port == NULL)
		summary();
	//creation of daemon process
	if (debug == 0){
        if(fork() > 0 ){//Parent killed
			exit(1);
		}
		//Child Running
		setsid();
		chdir(rootdir);
		umask(0);
    }
    if ((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
    }
    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next){
        if ((sockfd = socket(p->ai_family, p->ai_socktype,p->ai_protocol)) == -1){
			perror("socket");
            continue;
        }
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,sizeof(int)) == -1){
            perror("Setsockopt");
            exit(1);
        }
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1){
            close(sockfd);
            perror("Server: bind");
            continue;
        }
            break;
    }
    if (p == NULL){
        fprintf(stderr, "Server: failed to bind\n");
        return 2;
    }
    freeaddrinfo(servinfo);
    if (listen(sockfd, BACKLOG) == -1){
        perror("listen error");
        exit(1);
     }
    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1){
        perror("sigaction");
        exit(1);
    }

    if(debug==0){
        int f=1;
        pthread_t sch_thread,exec_thread;
        pthread_t thr_id[tnumber];
        pthread_create(&sch_thread,NULL,&schedule,NULL);//scheduling thread
        for(int i=0;i<tnumber;i++){
            pthread_create(&thr_id[i],NULL,&  execute,NULL);//execution thread
        }
    }
    printf("Server: waiting for connections...\n");

    while(1){
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1){
            perror("Accept");
            continue;
        }
        r.ip_address=inet_ntop(their_addr.ss_family,get_in_addr((struct sockaddr *)&their_addr),s, sizeof s);
        r.clientfd=new_fd;
        char ibuf[BUFSIZE];
        char filename0[BUFSIZE];
        char action0[20],host0[20];
        int retcode = recv((int) r.clientfd, ibuf, sizeof(ibuf), 0); //HTTP request
        if (retcode < 0){
            printf("Error in receive!\n");
            exit(1);
        }
        printf("GOT UR REQUEST!     PROCESSING REQUEST....\n");
        string str(ibuf);
        string::size_type pos;
        int newlinepos=str.find('\n',0);
        str=str.substr(0,newlinepos);
        r.fline=str;
        pos=str.find(' ',0);
        string first=str.substr(0,pos);
        string newstr=str.substr(pos+1);
        pos=newstr.find(' ',0);
        string second=newstr.substr(0,pos);
        string third=newstr.substr(pos+1,newlinepos);
        first=first.substr(0,strlen(first.c_str()));
        second=second.substr(0,strlen(second.c_str()));
        third=third.substr(0,strlen(third.c_str())-1);
        strcpy(action0, first.c_str());
        strcpy(filename0, second.c_str());
        strcpy(host0, third.c_str());
        int i = 0;
        time_t now1 = time(0);
        struct tm  tstruct;
        char  buf[100];
        time_t now = time(0);
        tstruct = *localtime(&now);
        strftime(buf, sizeof buf, "%d/%b/%Y:%H:%M:%S -%z", &tstruct);
        strcpy(r.request_time,buf);
        if(debug==0){
            requestprocessing(action0,filename0,host0);
            pthread_mutex_lock(&llock);
            pthread_mutex_lock(&elock); // get mutex lock
            r1.push_back(r);
            pthread_mutex_unlock(&elock);
            pthread_mutex_unlock(&llock); //release mutex
        }
        else{
            requestprocessing(action0,filename0,host0);
            time_t now = time(0);
            tstruct = *localtime(&now);
            strftime(buf, sizeof buf, "%d/%b/%Y:%H:%M:%S -%z", &tstruct);
            strcpy(r.execution_time,buf);
            int fd = open(&r.fname[0], O_RDONLY, S_IREAD | S_IWRITE);
            if (fd == -1){r.status=404;}
            else{r.status=200;}
            close(fd);
            if(flog==1){
                std::ofstream log(log_file, std::ios_base::app | std::ios_base::out);
                log<<r.ip_address<<" - ["<<r.request_time<<"] ["<<r.execution_time<<"] "<<
                        r.fline<<" "<<r.status<<" "<<r.size<<endl;
            }
            cout<<r.ip_address<<" - ["<<r.request_time<<"] ["<<r.execution_time<<"] "<<
                        r.fline<<" "<<r.status<<" "<<r.size<<endl;
            requestprocessing2(r);
        }
    }
    return (0);
}

void *schedule(void*){
  while(1){
    pthread_mutex_lock(&llock);
    pthread_mutex_lock(&elock);
        while (!r1.empty()){
           r2.push_back(r1.front());
                if(!r1.empty())r1.pop_front();
                pthread_cond_signal(&ewait);
        }
    pthread_mutex_unlock(&elock);
    pthread_mutex_unlock(&llock);
  }

}
void *execute(void*){
    time_t now2 = time(0);
    struct tm  tstruct;
    char buf[100];
    sleep(sleeptime);
    while(1){
        request r_temp;
        pthread_mutex_lock(&elock); // get mutex lock
        if(r2.empty()){
            pthread_cond_wait(&ewait,&elock);
        }
        if(!r2.empty()){
           if(sch==2){
              r2.sort(comparefn);
              r2.sort(headsort);
           }
          r_temp=r2.front();
          r2.pop_front();
        }
        if(flog==1){
            std::ofstream log(log_file, std::ios_base::app | std::ios_base::out);
            time_t now = time(0);
            tstruct = *localtime(&now);
            strftime(buf, sizeof buf, "%d/%b/%Y:%H:%M:%S -%z", &tstruct);
            strcpy(r_temp.execution_time,buf);
            int fd = open(&r_temp.fname[0], O_RDONLY, S_IREAD | S_IWRITE);
            if (fd == -1){r_temp.status=404;}
            else{r_temp.status=200;}
            close(fd);
            log<<r_temp.ip_address<<" - ["<<r_temp.request_time<<"] ["<<r_temp.execution_time<<"] "<<
                    r_temp.fline<<" "<<r_temp.status<<" "<<r_temp.size<<endl;
        }
        pthread_mutex_unlock(&elock); //release mutex
        requestprocessing2(r_temp);
        printf("-----REQUEST PROCESSED-----\n");
    }
}

void requestprocessing2(request r_temp2){
    int csock = r_temp2.clientfd; //copy socket
    char obuf[BUF_LEN];
    char dname[BUFSIZE];
    char *fname = r_temp2.fname;
    int fd;
    int buffile;
    int status;
    struct stat st_buf;
    time_t timevar = time(NULL);
    char *SERVER = "myhttpd";
    int flag=1;
    status = stat(r_temp2.fname, &st_buf);
    int dirstatus=S_ISDIR (st_buf.st_mode);
    char timeStr[100];
    time_t ltime;
    char datebuf[9];
    char timebuf[9];//modified time of file
    ltime=st_buf.st_mtime;
    struct tm *newBuff=gmtime(&ltime);
    strftime(timeStr, 100, "%d-%b-%Y %H:%M:%S", newBuff);
    if (status != 0){  //not a file or directory
        flag=0;
        printf("SORRY------ Not an existing file or directory!\n");//header creation
        sprintf(obuf,"%s Date:%s SERVER:%s\n Last Modified:%s\n %s Content-Length:%d\n\n",
                    HTTP_NOTOK, asctime(gmtime(&timevar)), SERVER, timeStr, HTML,r_temp2.size);
        send(csock, obuf, strlen(obuf), 0);//send header
        strcpy(obuf, DFNF_404);
        send(csock, obuf, strlen(obuf), 0);//send webpage
    }
    else if (r_temp2.rtype == 0){
        flag=0;//wrong request type
        printf("Error in Request type or Protocol not supported!\n");
    }
    else if (r_temp2.rtype == 1 && dirstatus==0){
        flag=0;//HEAD request
        fd = open(&r_temp2.fname[0], O_RDONLY, S_IREAD | S_IWRITE);
        if (fd == -1){//File not found
            if ((strstr(r_temp2.fname, ".jpg") != NULL)|| (strstr(r_temp2.fname, ".gif") != NULL)||
                    (strstr(r_temp2.fname, ".png") != NULL)|| (strstr(r_temp2.fname, ".html") != NULL)){
                sprintf(obuf,"%s Date:%s SERVER:%s\n Last Modified:%s\n %s Content-Length:%d\n\n",
                        HTTP_NOTOK, asctime(gmtime(&timevar)), SERVER, timeStr,HTML, r_temp2.size);
                send(csock, obuf, strlen(obuf), 0);
            }
        }
        else{
            // File found and we will header sent according to image or html
            if ((strstr(r_temp2.fname, ".jpg") != NULL)|| (strstr(r_temp2.fname, ".gif") != NULL)||
                    (strstr(r_temp2.fname, ".png") != NULL))
            sprintf(obuf,"%s Date:%s SERVER:%s\n Last Modified:%s\n %s Content-Length:%d\n\n",
                        HTTP_OK, asctime(gmtime(&timevar)), SERVER, timeStr, IMAGE,r_temp2.size);
            else
            sprintf(obuf,"%s Date:%s SERVER:%s\n Last Modified:%s\n %s Content-Length:%d\n\n",
                        HTTP_OK, asctime(gmtime(&timevar)), SERVER, timeStr, HTML,r_temp2.size);
            send(csock, obuf, strlen(obuf), 0);
        }
        close(fd);
    }
    else if (r_temp2.rtype == 2 && dirstatus==0){
        flag=0;//GET request,Send the requested file to client
        if (S_ISREG (st_buf.st_mode)){
            fd = open(&r_temp2.fname[0], O_RDONLY, S_IREAD | S_IWRITE);
            if (fd == -1){ //File not found
                if ((strstr(r_temp2.fname, ".jpg") != NULL)|| (strstr(r_temp2.fname, ".gif") != NULL)||
                        (strstr(r_temp2.fname, ".png") != NULL)|| (strstr(r_temp2.fname, ".html") != NULL)){
                    printf("Sorry couldn't find the File %s\n please try another\n", r_temp2.fname);
                    r_temp2.status=404;
                    sprintf(obuf,"%s Date:%s SERVER:%s\n Last Modified:%s\n %s Content-Length:%d\n\n",
                            HTTP_NOTOK, asctime(gmtime(&timevar)), SERVER, timeStr,HTML,r_temp2.size);
                    send(csock, obuf, strlen(obuf), 0);
                    strcpy(obuf, FNF_404);
                    send(csock, obuf, strlen(obuf), 0);
                }
            }
            else{// File found, file sent according to image or html
                if ((strstr(r_temp2.fname, ".jpg") != NULL)|| (strstr(r_temp2.fname, ".gif") != NULL)||
                            (strstr(r_temp2.fname, ".png") != NULL))
                    sprintf(obuf,"%s Date:%s SERVER:%s\n Last Modified:%s\n %s Content-Length:%d\n\n",
                            HTTP_OK, asctime(gmtime(&timevar)), SERVER, timeStr,IMAGE,r_temp2.size);
                else
                    sprintf(obuf,"%s Date:%s SERVER:%s\n Last Modified:%s\n %s Content-Length:%d\n\n",
                            HTTP_OK, asctime(gmtime(&timevar)), SERVER, timeStr,HTML,r_temp2.size);
                send(csock, obuf, strlen(obuf), 0);
                buffile = 1;
                while (buffile > 0){
                    buffile = read(fd, obuf, BUFSIZE);
                    if (buffile > 0){
                        send(csock, obuf, buffile, 0);
                    }
                }
            }
        }
    }
    if(S_ISDIR (st_buf.st_mode) && flag==1){
    //Is Directory
            DIR *dp;
            int flag = 0;
            struct dirent *dirp;
            if ((dp = opendir(r_temp2.fname)) == NULL){
                printf("Error In Opening File %s\n", r_temp2.fname);
                sprintf(obuf,"%s Date:%s SERVER:%s\n %s Content-Length:%d\n\n",
                            HTTP_NOTOK, SERVER, timeStr,HTML, r_temp2.size);
                send(csock, obuf, strlen(obuf), 0);
                strcpy(obuf, DNF_404);
                send(csock, obuf, strlen(obuf), 0);
            }
            while ((dirp = readdir(dp)) != NULL){ //check for index.html
                if (strcasecmp(dirp->d_name, "index.html") == 0||strcasecmp(dirp->d_name, "index.htm") == 0){
                    flag = 1;
                    sprintf(obuf, "%s/%s", r_temp2.fname, dirp->d_name);
                    fd = open(&obuf[0], O_RDONLY, S_IREAD | S_IWRITE);
                    sprintf(obuf,"%s Date:%s SERVER:%s\n %s Content-Length:%d\n\n",HTTP_OK, SERVER, timeStr,HTML, r_temp2.size);
                    send(csock, obuf, strlen(obuf), 0);
                    buffile = 1;
                    while (buffile > 0) {
                        buffile = read(fd, obuf, BUFSIZE);
                        if (buffile > 0) {
                            send(csock, obuf, buffile, 0);
                        }
                    }
                    break;
                }
            }
            if (flag != 1){
                //display directory contents
                if ((dp = opendir(r_temp2.fname)) == NULL){
                    printf("Error In Opening File %s", r_temp2.fname);
                    sprintf(obuf,"%s Date:%s SERVER:%s\n %s Content-Length:%d\n\n",
                                HTTP_NOTOK, SERVER, timeStr,HTML,r_temp2.size);
                    send(csock, obuf, strlen(obuf), 0);
                    strcpy(obuf, DNF_404);
                    send(csock, obuf, strlen(obuf), 0);
                }
                sprintf(obuf,"%s Date:%s SERVER:%s\n %s Content-Length:%d\n\n",
                            HTTP_OK, SERVER, timeStr, HTML,r_temp2.size);
                send(csock, obuf, strlen(obuf), 0);
                strcpy(obuf,
                    "<html><body> <h1><u><b>Required Directory Contents</b></u></h1><br><br><br><h3><font color='red'>We Found Below Files</font></h3><br><br>");
                send(csock, obuf, strlen(obuf), 0);
                obuf[0] = '\0';
                while ((dirp = readdir(dp)) != NULL){
                    if (dirp->d_name[0] != '.'){
                        sprintf(dname,"%s%s",r.fname,dirp->d_name);
                        sprintf(obuf, "<a href=%s%s>%s</a>&emsp;&emsp;&emsp;<br>",r.fname,dirp->d_name, dirp->d_name);
                        send(csock, obuf, strlen(obuf), 0);
                        obuf[0] = '\0';
                    }
                }
                sprintf(obuf, "</body></html>");
                send(csock, obuf, strlen(obuf), 0);
                closedir(dp);
            }
        }
        else{
            if(flag==1){
                printf("Error In Opening File %s\n", r_temp2.fname);
                sprintf(obuf,"%s Date:%s SERVER:%s\n %s Content-Length:%d\n\n",
                        HTTP_NOTOK, SERVER, timeStr, HTML,r_temp2.size);
                send(csock, obuf, strlen(obuf), 0);
                strcpy(obuf, DNF_404);
                send(csock, obuf, strlen(obuf), 0);
            }
        }
    close(csock);
}

string exec(char*command){
	FILE* p = popen(command, "r");
	if (!p) return "Error in exec";
	char temp_buffer[128];
	std::string res = "";
	while(!feof(p)) {
		if(fgets(temp_buffer, 128, p) != NULL)
			res += temp_buffer;
	}
	pclose(p);
	return res;
}

void requestprocessing(char action[],char fname[], char host[]){
    int buffile;
    char temp[30];
    string dir = exec("pwd");
	string ret = "\n";
	dir.erase(dir.find(ret));
    char* home = "home";
    char* found;
    int size = 0;
    FILE *fd;
    int status;
    struct stat st_buf;
    char *til,username[50];
    int temp_pos, i = 0;
    til = strchr(fname, '~');
    if (til != NULL){
        temp_pos = til - fname + 1;
        for (i = 0; fname[temp_pos] != '/'; i++, temp_pos++)
        username[i] = fname[temp_pos];
        username[i] = '\0';
        fname[0] = '\0';
        sprintf(updirectory,"/home/%s/",username);
        sprintf(fname, "/home/%s/myhttpd/", username);
    }
    else{ //get file or directory to open
        found = strstr(fname, home);
        if (!found){
            strcpy(temp, fname);
            fname[0] = '\0';
            if (*rootdir == '\0')sprintf(rootdir, "%s", (char*) dir.c_str());
            sprintf(fname, "%s%s", rootdir, temp);
        }
    }//file size
    status = stat(fname, &st_buf);
    if (status != 0){
        cout<<"SORRY------ Not an existing file or directory!"<<endl;
        size = 0;
    }
    else{
        if (S_ISREG (st_buf.st_mode)){
            fd = fopen(fname, "rb");
            fseek(fd, 0, SEEK_END);
            size = ftell(fd);
            fclose(fd);
        }
        else if (S_ISDIR (st_buf.st_mode))
                size = 0;
            else
                size = 0;
    }
    strcpy(r.fname, fname);
    r.size=size;
    if (strcasecmp(host, "HTTP/1.0") == 0 || strcasecmp(host, "HTTP/1.1") == 0){
        if (strcasecmp(action, "HEAD") == 0)r.rtype = 1;
        else if (strcasecmp(action, "GET") == 0)r.rtype = 2;
             else r.rtype = 0;
    }
    else r.rtype = 0;
}

void summary() {
	fprintf(stderr,"usage:myhttpd [−d] [−h] [−l file] [−p port] [−r dir] [−t time] [−n threadnum] [−s sched]\n",pvar);
	fprintf(stderr,
			"−d             : Enter debugging mode. \n"
            "−h             : Print a usage summary.\n"
            "−l file        : Log all requests to the given file.\n"
            "−p port        : Listen on the given port.\n"
            "−r dir         : Set the root directory for the http server to dir.\n"
            "−t time        : Set the queuing time to time seconds.\n"
            "−n threadnum   : Set number of threads.\n"
            "−s sched       : Set the scheduling policy.\n",
			pvar);
	exit(1);
}


