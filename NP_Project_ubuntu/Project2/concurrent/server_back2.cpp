#include <stdlib.h>
#include <stdio.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <wait.h>
using namespace std;
#include <sys/ipc.h>
#include <sys/shm.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/stat.h>

#define PORT 30000
#define MAX_LINE 1000  // number of char in one line
#define MAX_PIPE 1000   // maximum pipe
#define MAX_WORD 1000   // number of word in one line (cut by pipe)
#define MAX_USER 31
#define MAXLEN_IPPORT 50
#define MAXLEN_NAME 50
#define FIFO_PATH "bin/fifo/fifo."

#define SHMKEY ((key_t)9292)
#define shmflg 0600

void err_dump(const char *msg);
void welcome(int sockfd);
int readline(int fd, char *ptr, int maxlen);
void str_echo(int sockfd);
void initial_output(int sockfd);
void initial_pipe();
void construct_depart_cmd(char *cutline[], int sockfd);
void do_command(char *argv,int sockfd, bool isfirst, bool islast, bool need_jump, int jump_num, bool show_error, char* fullCmdName);
string strLTrim(const string& str);
string strRTrim(const string& str); 
string strTrim(const string& str);
char *trim(char str[]);


int pipe_input[MAX_PIPE][3];  //  record the command's stdin needs to change to stdin or not , -1 = not
int pipe_fd[MAX_PIPE][2];   // pipe file discriptor 
int cmd_count = 0;  // record the total line of command
int now_cmd = 0;
int now_cmd_line = 0;
int stdin_copy, stdout_copy;
int record_jump_pipe_array[MAX_PIPE];
int record_jump_pipe_num = 0;
char line[MAX_LINE];
char *cutline[MAX_WORD];
bool show_error = false;

// project2
int client_own_fd=-1, client_own_pid=-1, client_own_id=-1;
int receiverID, senderID;
char full_cmd_name[100];
int FIFO_wait_for_read[MAX_USER];

void initial_Share();
void broadcastENTER(char *addr, int port);
int setIdIpPort(int pid, char *addr, int port);
void setUserName(char *name, int sockfd);
void who();
void tell(int sockfd, int receiverID, char *msg);
void yell(char* msg);
void broadcast(char *msg);
void broadcastLEAVE();
void initial_FIFO();

struct Share{
    char UserName[MAX_USER][MAXLEN_NAME];
    char UserIP[MAX_USER][MAXLEN_IPPORT];
    char Buffer[MAX_USER][MAX_WORD];
    int PidTable[MAX_USER];
    int FIFO_ForUser[MAX_USER][MAX_USER];
    int FIFO_Sender;
    int FIFO_Receiver;
    int FIFO_FD_TABLE[MAX_USER][MAX_USER];
};

struct Share *shareptr;
int shm;


// signal handler
void readFromBuffer(int signum){
    //fprintf(stderr, "readFromBuffer\n");
    write(client_own_fd, shareptr->Buffer[client_own_id], strlen(shareptr->Buffer[client_own_id]));
    memset(shareptr->Buffer[client_own_id], 0, sizeof(shareptr->Buffer[client_own_id]));     
}
void cleanSHM(int signum){
    if ( shmdt(shareptr) < 0  )
        err_dump("share mem close failed!");
    if ( shmctl( shm, IPC_RMID, 0  ) < 0  )
        err_dump("share mem close failed!");
    exit(0); 
}

void openFIFO_forRead(int signum){
    fprintf(stderr, "by signal , id: #%d\n",client_own_id);
    fprintf(stderr, "FIFO_Receiver: #%d, FIFO_Sender: #%d\n", shareptr->FIFO_Receiver, shareptr->FIFO_Sender);
    char path[20];
    sprintf(path, "%s%d-%d", FIFO_PATH, shareptr->FIFO_Receiver, shareptr->FIFO_Sender);
    fprintf(stderr, "fifo path:%s\n", path);
    int readfd = open(path, O_RDONLY);
    if(readfd == -1)
        err_dump("readfd fifo open fail in signal\n");

    FIFO_wait_for_read[shareptr->FIFO_Sender] = readfd; 
    fprintf(stderr, "~~~~fifo readfd: %d\n", FIFO_wait_for_read[shareptr->FIFO_Sender]);
}
// signal handler

int main(int argc, char *argv[]){
    int clientlen, childpid, sockfd, newsockfd; 
    sockaddr_in client_addr, server_addr;

    /* set server PATH environment */
    setenv("PATH","bin:.",1);

    /* change dir */
    int dir = chdir("./ras");
    if(dir<0)
        err_dump("change dir fail");

    /*  open a TCP socket (an Internet stream socket)  */
    sockfd  = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd<0)
        err_dump("server can't open stream socket");

    /* Tell the kernel that you are willing to re-use the port anyway */
    int yes=1;
    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1){
        //print the errorno
        perror("setsockopt");
        exit(1);

    }
    /*******************************************************************/

    /* Bind our local address so that the client can send to us  */
    bzero((char*)&server_addr,sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);
    if(bind(sockfd,(sockaddr*)&server_addr, sizeof(server_addr) )<0)
        err_dump("server: cant bind local address");
    /*******************************************************************/

    /* set initial the share memory */
    shm = shmget(SHMKEY, sizeof(Share), shmflg | IPC_CREAT );
    if(shm<0)
        err_dump("initial shm error");
    shareptr = (Share*)shmat(shm, (char*)0, 0);
    if(shareptr<0)
        err_dump("attach structure to shm error");
    /* -----------------------------------------------------*/

    /* initial Share*/
    initial_Share();
    /*--------------*/
    initial_FIFO();

    /* set signal*/
    signal(SIGUSR1, readFromBuffer); // signal for user define
    signal(SIGINT, cleanSHM); //signal when type ^C
    signal(SIGUSR2, openFIFO_forRead);
    /*----------------------------*/

    listen(sockfd,5);
    fprintf(stderr, "---------server start----------\n");
    for(;;){
        /*return the size of this srructure */
        clientlen = sizeof(client_addr);

        /* 連線成功時，參數addr所指的結構會被系統填入遠程主機的地址數據 */
        newsockfd = accept(sockfd, (sockaddr*)&client_addr, (socklen_t*)&clientlen); 
        fprintf(stderr, "---------client connect successfull---------\n");
        if(newsockfd<0){
            perror("accept");   
            err_dump("Server: accept error");
        }


        childpid = fork();
        if(childpid<0)
            err_dump("server: fork error");
        else if(childpid ==0){  /* child process*/
            /*close the mastersocket socket */
            close(sockfd);

            /* get pid*/
            client_own_pid = getpid();
            client_own_fd = newsockfd;
            fprintf(stderr, "client own pid: %d\n", client_own_pid);
            //write(newsockfd,"client own pid: %d\n", client_own_pid);

            /* set client user ID*/
            client_own_id = setIdIpPort(client_own_pid, inet_ntoa(client_addr.sin_addr), client_addr.sin_port);
            fprintf(stderr, "pidTable: \n", shareptr->PidTable[client_own_id]);
            fprintf(stderr,"client own id: %d\n", client_own_id);
            if(client_own_id < 0){
                err_dump("Over 30 users");
            }
            shareptr->PidTable[client_own_id] = client_own_pid;
            fprintf(stderr, "13123: %d", shareptr->PidTable[client_own_id]);

            welcome(newsockfd);

            //  initial the array which is used to record 
            initial_pipe();

            broadcastENTER(inet_ntoa(client_addr.sin_addr),client_addr.sin_port);
            str_echo(newsockfd);
            close(newsockfd);
            fprintf(stderr, "---------client close connention\n" );
            exit(0);
        }   
        /*parent process*/
        close(newsockfd);
    }
}   


void str_echo(int sockfd){
    int n, word;
    // redirect the output of this connection to sockfd
    initial_output(sockfd);
    // //  initial the array which is used to record 
    // initial_pipe();

    // infinity loop until connection terminate
    for(;;){
        write(sockfd, "% ",2);
        // read client input
        n = readline(sockfd, line, MAX_LINE);
        strcpy(full_cmd_name,line);
        if(n<0)
            err_dump("str_echo: readline error");
        else if(n==0)
            return;    //terminate
        else if(n==2)
            continue;   // 防止按enter導致斷線

        fprintf(stderr, "client input: %s",line);

        //指令行 ++
        now_cmd_line++;

        // 判斷有沒有 '!'
        for(int i =0; i<n; i++){
            if(line[i]=='!'){
                show_error = true;
            }
        }

        // parse client input to indivisual word
        cutline[0] = strtok(line, "|!\n\r\t");
        word = 0;
        while(cutline[word]!=NULL){
            word++;
            cutline[word] = strtok(NULL, "|!\n\r\t");
        }

        // 去除指令頭尾空白
        int count = 0;
        while(cutline[count]!=NULL){
            cutline[count] = trim(cutline[count]); 
            count++;
        }
        //cout << "count" << count << endl;
        //塞一個null 到command的最後
        cutline[count] = (char*)"NULL";
        // //cout << "last: " << atoi(cutline[count+1])<< endl;

        // need pipe 
        bool isfirst = true, need_jump = false, islast=false, notdo_nextcmd=false;
        int jump_num = 0;
        // if(count>1){
        for(int cmd_num=0; cmd_num<count; cmd_num++){
            //cout << "==============Start==================" << cmd_num << endl;
            jump_num = atoi(cutline[cmd_num+1]);
            if(jump_num!=0)
                need_jump =true;
            else if(cmd_num==count-1)
                islast = true;

            //cout << "notdo_cmd" << notdo_nextcmd << endl;
            if(!notdo_nextcmd)
                do_command(cutline[cmd_num], sockfd, isfirst, islast, need_jump, jump_num, show_error, full_cmd_name);
            if(need_jump)
                notdo_nextcmd = true;
            isfirst = false;
            //cout << "==============End==================" << endl;
        }

    }
    }



    void do_command(char *argv,int sockfd, bool isfirst, bool islast, bool need_jump, int jump_num, bool show_error, char* fullCmdName){
        //cout << "------------------------------------------"<< endl;
        //cout << "argv: " << argv << endl;
        // //cout << "sockfd: " << sockfd << endl;
        //cout << "isfirst: " << isfirst << endl;
        //cout << "islast: " << islast << endl;
        //cout << "need_jump: " << need_jump << endl;
        //cout << "jump_num: " << jump_num << endl;
        //cout << "------------------------------------------"<< endl;

        int childpid;
        bool PIPE = false;
        bool tofile = false;
        bool to_other_pipe = false;
        char *filename;
        int to_other_cmd_pipe;
        char nouse;
        bool pipe_to_user = false, pipe_from_user = false;
        char path[20];
        char path2[20];
        char *full[10];

         full[0] = strtok(fullCmdName, "\n\r\t");
         int word = 0;
         while(full[word]!=NULL){
             word++;
             full[word] = strtok(NULL, "\n\r\t");
         }

        /* parsing the command*/
        fprintf(stderr, ">>>>>>>>>>>>#%d  now cmd:", client_own_id);
        char *cmd[100];
        cmd[0]  = strtok(argv," ");
        word = 0;
        while(cmd[word]!=NULL){
            fprintf(stderr, "%s, ", cmd[word]);
            word++;
            cmd[word] = strtok(NULL," ");
        }

        //判斷是不是要pipe給user
        int skipNULL = 0;
        fprintf(stderr, "=====================test pipe to user==============\n");
        for(int np=1; np<word; np++){
            if(cmd[np]==NULL) continue;
            for(int i=0; i<strlen(cmd[np]); i++){
                if(cmd[np][i]=='>' ){
                    char msg[100];
                    /*如果是 ">" 而不是 ">n" 則是輸出給file而不是user  */
                    if(strlen(cmd[np])==1)
                        break;
                    //char to int
                    sscanf(cmd[np],"%c%d",&nouse,&receiverID);
                    fprintf(stderr, "#%d, pipe to user\n", client_own_id);
                    fprintf(stderr, "#%d, sender ID: %d\n",client_own_id, client_own_id);
                    fprintf(stderr, "#%d, receiver ID: %d\n",client_own_id, receiverID);
                    if(shareptr->PidTable[receiverID] == -1){
                        sprintf(msg, "*** Error: user #%d does not exist yet. ***\n", receiverID);
                        write(sockfd, msg, strlen(msg));
                        return;
                    }
                    // receiver 的 mail box 有 已經有信了
                    if(shareptr->FIFO_ForUser[receiverID][client_own_id] == 1){
                        sprintf(msg, " *** Error: the pipe #%d->#%d already exists. ***", client_own_id, receiverID);
                        cout << msg << endl;
                        return;
                    }
                    pipe_to_user = true;
                    fprintf(stderr, "!!, cmd: %s\n",cmd[np] );
                    cmd[np] = NULL;
                    break;
                }
            }
        }
        fprintf(stderr, "#%d, pipe to user ? %d ,0=file, 1=user\n",client_own_id,pipe_to_user);
        fprintf(stderr, "=========================test pipe to user==============\n");

        fprintf(stderr, "=========================test receive from  user==============\n");
        //判斷是不是要receive form user
        for(int np=1; np<word; np++){
            if(cmd[np]==NULL) continue;
            fprintf(stderr, "cmd[%d]: %s\n", np, cmd[np]);
            for(int i=0; i<strlen(cmd[np]); i++){
                if(cmd[np][i]=='<'){
                    sscanf(cmd[np],"%c%d", &nouse, &senderID);
                    fprintf(stderr, "=====this is receive cmd====\n");
                    fprintf(stderr, "#%d, receiver ID: %d\n",client_own_id, client_own_id);
                    fprintf(stderr, "#%d, sender ID: %d\n",client_own_id, senderID);

                    if(shareptr->FIFO_ForUser[client_own_id][senderID]!=1){
                        char msg[100];
                        sprintf(msg, "*** Error: the pipe #%d->#%d does not exist yet. ***",senderID, client_own_id);
                        cout << msg << endl;
                        return;
                    }
                    pipe_from_user = true;
                    cmd[np] = NULL;
                    break;
                }
            }
        }
        fprintf(stderr, "#%d ,receive user ? %d ,0=No, 1=user\n",client_own_id ,pipe_from_user);
        fprintf(stderr, "=========================test receive from  user==============\n");
        //現在指令 +1 
        now_cmd++;

        // setenv
        if(strcmp(cmd[0],"setenv") == 0 ){
            setenv(cmd[1], cmd[2], 1);
            return;
        }
        // printenv
        if(strcmp(cmd[0],"printenv") == 0){
            char *env;
            env = getenv(cmd[1]);
            if(env != NULL){
                cout << "PATH=" << env << endl;
            }
            else
                err_dump("get env error");
            return;
        }

        // exit
        if(strcmp(cmd[0],"exit") == 0){
            // close connection
            broadcastLEAVE();
            close(sockfd);
            fprintf(stderr, "~~~~~~~~~~client #%d leave ~~~~~~~~~~\n", client_own_id);

            exit(0);

        }
        /* cmd: name*/
        if(strcmp(cmd[0], "name") == 0){
            fprintf(stderr, "comand: name\n");
            fprintf(stderr, "name is : %s\n", cmd[1]);
            setUserName(cmd[1], sockfd);
            return;
        }
        /* cmd: who*/
        if(strcmp(cmd[0], "who")==0){
            who();
            return;
        }
        /* cmd : tell*/
        if(strcmp(cmd[0], "tell")==0){
            int i=2;
            char message[100]="";
            //防止輸入參數數量不夠
            if(cmd[1]==NULL || cmd[2]==NULL){
                const char* errmsg = "error: plz give enough arguments\n";
                cout << errmsg;
                return;
            }
            while(*(cmd+i)!=NULL){
                strcat(message, *(cmd+i));
                strcat(message, " ");
                i++;
            }
            tell(sockfd, atoi(cmd[1]), message);
            return;

        }

        /* cmd: yell */
        if(strcmp(cmd[0], "yell")==0){
            char message[100]="";
            int i=1;
            fprintf(stderr, "command: yell\n");
            while(*(cmd+i)!=NULL){
                strcat(message, *(cmd+i));
                strcat(message, " ");
                i++;
            }
            yell(message);
            return;             
        }


        // 判斷有沒有輸出到檔案 ">"
        if(!pipe_to_user && !pipe_from_user){
            for(int i=0; i< word; i++){
                if(strcmp(cmd[i],">")==0) {
                    tofile = true;
                    filename = cmd[i+1];
                    cmd[i] = NULL;
                    cmd[i+1] = NULL;
                    break;
                }
            }
        }

        // set pipe
        if(!islast){     //不是最後的指令才要開pipe
            if(pipe(pipe_fd[now_cmd])<0){
                err_dump("pipe error");
            }
            PIPE = true;
            if(need_jump){   
                if(pipe_input[now_cmd_line+jump_num][2] == 1){
                    to_other_pipe = true;
                    to_other_cmd_pipe = pipe_input[now_cmd_line+jump_num][0];
                    close(pipe_fd[now_cmd][0]);
                    close(pipe_fd[now_cmd][1]);
                }
                else{
                    pipe_input[now_cmd_line+jump_num][2] = 1;
                    pipe_input[now_cmd_line+jump_num][0] = now_cmd;
                    pipe_input[now_cmd_line+jump_num][1] = now_cmd;
                }
            }
        }

        /*islast == true, but need to "pipe to" other user*/
        if(pipe_to_user){
            sprintf(path, "%s%d-%d", FIFO_PATH, receiverID, client_own_id);
           shareptr -> FIFO_Sender = client_own_id;
            shareptr ->FIFO_Receiver = receiverID;
            fprintf(stderr, "===================create FIFO, path: %s\n", path);
            shareptr->FIFO_ForUser[receiverID][client_own_id] = 1;
            fprintf(stderr, "send FIFO_ForUser[%d][%d]: %d\n", receiverID, client_own_id, shareptr->FIFO_ForUser[receiverID][client_own_id]);
        }

        if(pipe_from_user){
            sprintf(path2, "%s%d-%d", FIFO_PATH, client_own_id, senderID);
            fprintf(stderr, "===================read FIFO, path: %s\n", path2);
            shareptr->FIFO_ForUser[client_own_id][senderID] = -1;
            fprintf(stderr, "receive FIFO_ForUser[%d][%d]: %d\n", client_own_id, senderID, shareptr->FIFO_ForUser[client_own_id][senderID]);

        }
        if((childpid = fork())<0){
            err_dump("fork error");
        }
        else if(childpid>0){   //parent process

            if(!isfirst){
                close(pipe_fd[now_cmd-1][0]);
                close(pipe_fd[now_cmd-1][1]);

            }
            if(pipe_input[now_cmd_line][2] == 1){   //之前沒關掉的pipe， 其read end 在這行指令要給stdin讀，以後不會再用到，全部關掉
                fprintf(stderr, "-----------parent close previous pipe-----------\n");
                int from_previous = pipe_input[now_cmd_line][0];
                close(pipe_fd[from_previous][1]);
                close(pipe_fd[from_previous][0]);
                fprintf(stderr, "-----------parent close previous pipe-----------\n");
            }
            int status;
            if(!pipe_to_user){
                wait(&status);
                if(pipe_input[now_cmd_line][2] == 1)
                    pipe_input[now_cmd_line][2] = 0;
           }
            if(pipe_from_user){
                char msg[100];
                char *senderName = shareptr->UserName[senderID];
                char *receiverName = shareptr->UserName[client_own_id];
                sprintf(msg, "*** %s (#%d) just received from %s (#%d) by '%s' ***\n", receiverName, client_own_id, senderName, senderID, full[0]);
                broadcast(msg);
                if(unlink(path2)<0){
                    char maymsg[50];
                    sprintf(maymsg, "error! unlink fifo : %s\n", path2); 
                    err_dump(maymsg);
                }
                fprintf(stderr, "$$$$$$$$$$$$$$$$unlink fifo success:%s$$$$$$$$$$$$$$$\n",path2);
            }


            if(pipe_to_user){
                char msg[100];
                char *senderName = shareptr->UserName[client_own_id];
                char *receiverName = shareptr->UserName[receiverID];
                sprintf(msg, "*** %s (#%d) just piped '%s' to %s (#%d) ***\n", senderName, client_own_id, full[0], receiverName, receiverID);
                broadcast(msg);
            }
            return;
        }
        else{ //child process

            if(isfirst){    //第一個指令
                // 設定 stdin
                if(pipe_input[now_cmd_line][2] == 1){    //前面有跳行指令， output到這個指令的stdin
                    //cout << "-----------child close previous pipe-----------" <<endl ;
                    int from_previous = pipe_input[now_cmd_line][0];   //將要input到這行指令的data從 pipe read端取出
                    //cout << "from_previous: " << from_previous << endl;  
                    dup2(pipe_fd[from_previous][0],STDIN_FILENO);
                    fprintf(stderr, "#%d, this stdin is :%d\n",client_own_id, pipe_fd[from_previous][0]);
                    close(pipe_fd[from_previous][1]);
                    close(pipe_fd[from_previous][0]);
                    pipe_input[now_cmd_line][2] = 0;
                    //cout << "-----------child close previous pipe-----------" <<endl ;

                }
                else{
                    //cout << "stdin is stdin" << endl;
                }

                // int j=0,n;
                // while((n=record_jump_pipe_array[j++])!=-1){
                //     close(pipe_fd[n][0]);
                //     close(pipe_fd[n][1]);
                // }
                if(PIPE){  /*第一個指令有開pipe才要關*/
                    close(pipe_fd[now_cmd][0]);
                }
            }
            else{   /*非第一行指令， stdin 設定成pipe的read端 （0）*/
                // 設定stdin
                // close(STDIN_FILENO);
                close(pipe_fd[now_cmd-1][1]);
                dup2(pipe_fd[now_cmd-1][0],STDIN_FILENO);
                close(pipe_fd[now_cmd-1][0]);
                fprintf(stderr, "#%d,this stdin is %d\n",client_own_id ,pipe_fd[now_cmd-1][0]);
            }

            //cout << "PIPE: " << PIPE << endl;

            if(PIPE){ //有用到pipe 來存output
                /* 設定 stdout */
                if(to_other_pipe){
                   // if(show_error)
                     //   dup2(pipe_fd[to_other_cmd_pipe][1],STDERR_FILENO);

                    dup2(pipe_fd[to_other_cmd_pipe][1],STDOUT_FILENO);

                    fprintf( stderr ,"#%d, stdout is pipe : %d\n", client_own_id,pipe_fd[to_other_cmd_pipe][1]);
                    close(pipe_fd[to_other_cmd_pipe][1]);
                }
                else{
                   // if(show_error)
                     //   dup2(pipe_fd[now_cmd][1],STDERR_FILENO);
                    dup2(pipe_fd[now_cmd][1],STDOUT_FILENO);
                    fprintf(stderr, "#%d, stdout is pipe : %d\n",client_own_id ,pipe_fd[now_cmd][1]);
                    close(pipe_fd[now_cmd][1]);
                }
            }
            for(int i=0; i<MAX_PIPE; i++){
                if(pipe_input[i][2]==1){
                    int n = pipe_input[i][0];
                    close(pipe_fd[n][0]);
                    close(pipe_fd[n][1]); 
                }
            }
            // redirect to file
            if(tofile){
                //int fd = open(filename,O_WRONLY|O_CREAT|O_TRUNC,S_IREAD|S_IWRITE);
                int fd = open(filename, O_RDWR| O_CREAT | O_TRUNC,0666);
                dup2(fd,STDOUT_FILENO);
                close(fd);
                fprintf(stderr, "#%d, stdout is file: %d\n",client_own_id, fd);
            }
            // redirect to FIFO
            if(pipe_to_user){
                //unlink(path);
                if(mkfifo( path, S_IFIFO | shmflg  )==-1){
                        char maymsg[50];
                        sprintf(maymsg, "error! make fifo fault : %s\n", path); 
                        err_dump(maymsg);
                }
                fprintf(stderr, "mkfifo success!: path: %s\n",path);
                kill(shareptr->PidTable[receiverID], SIGUSR2);
                int writefd = open(path, O_WRONLY );
                if(writefd == -1){
                    char maymsg[50];
                    sprintf(maymsg, "#%d error!, open fifo falust: %s\n ",client_own_id,  path);
                    err_dump(maymsg);
                }
                dup2(writefd, STDOUT_FILENO);
                close(writefd);
                fprintf(stderr, "#%d, stdout is FIFO: %d\n", client_own_id ,writefd);
            }
            if(pipe_from_user){
                //int readfd = open(path2, O_RDONLY);
                //if(readfd == -1)
                  //  err_dump("open file error, at line 674\n");
                fprintf(stderr, "path2: %s\n", path2);
                fprintf(stderr, "sender is : #%d\n", senderID);

                dup2(FIFO_wait_for_read[senderID], STDIN_FILENO);
                close(FIFO_wait_for_read[senderID]);
                fprintf(stderr, "#%d, this stdin is from FIFO: %d\n",client_own_id ,FIFO_wait_for_read[senderID]);
                FIFO_wait_for_read[senderID] = -1;
            }
            fprintf(stderr, "\n~~~child leave~~~%d\n", client_own_id);
            /* exexute the command */
            dup2(sockfd, STDERR_FILENO);
            if(execvp(cmd[0],cmd)==-1){
                cout << "Unknown command: [" << cmd[0] << "]" <<endl;
                exit(0);
            }
        }
    }

    char *trim(char str[])  {
        char last = ' '; //去除句首空白的配套 
        int p1 , p2;
        for ( p1 = p2 = 0 ; str[p2] ; last = str[p2++] )  {
            if ( last == ' ' ) { //若前一字是空白 
                if ( str[p2] == ' ' ) 
                    continue; //本字是空白，略過（即刪除之） 
                // if ( str[p2] == '.' && p1 > 0 ) 
                // --p1; //去除句尾句點前的空白 
            }
            str[p1++] = str[p2];
        }
        str[p1] = '\0'; //設定字串結尾標記 
        return str;
    }



    void initial_output(int sockfd){
        // clone the stdin & stdout
        // stdin_copy = dup(0);
        // stdout_copy = dup(2);
        // close(fileno(stdout));
        dup2(sockfd,STDOUT_FILENO);
        // dup2(sockfd,STDERR_FILENO);
        //close(sockfd);
    }

    // initial all pipe which is used to record the cmd need to change the stdin to pipe or not (-1 == not)
    void initial_pipe(){
        for(int i=0; i < MAX_PIPE; i++){
            pipe_input[i][0] = 0;
            pipe_input[i][1] = 0;
            pipe_input[i][2] = 0;
            record_jump_pipe_array[i] = -1;
        }
    }



    int readline(int fd, char *ptr, int maxlen){
        int n, rc;
        char c;
        for(n=1; n<maxlen; n++){
            rc = read(fd, &c, 1);
            if(rc==1){
                *ptr++=c;
                if(c=='\n')
                    break;
            }
            else if(rc ==0){
                if(n==1)
                    return 0;   /*END of File*/
                else
                    break;
            }
            else{
                return -1;
            }
        }
        *ptr = 0;
        return n;
    }

    string strLTrim(const string& str){ 
        return str.substr(str.find_first_not_of(" \n\r\t")); 
    } 

    string strRTrim(const string& str){ 
        return str.substr(0,str.find_last_not_of(" \n\r\t")+1); 
    } 

    string strTrim(const string& str){ 
        return strLTrim(strRTrim(str)); 
    }


    void welcome(int sockfd)
    {
        char wel[] = "****************************************\n** Welcome to the information server. **\n****************************************\n";
        write(sockfd,wel,strlen(wel));

    }


    void err_dump(const char *msg)
    {
        fprintf(stderr, "%s, errno = %d\n", msg,errno);
        exit(1);
    }



    void initial_Share(){
        memset(shareptr -> FIFO_ForUser, -1, sizeof(shareptr -> FIFO_ForUser));
        memset(shareptr -> Buffer, 0, sizeof(shareptr -> Buffer));
        for(int i=1; i<MAX_USER; i++){
            strcpy(shareptr -> UserName[i], "(no name)");
            strcpy(shareptr -> UserIP[i], "");
            shareptr -> PidTable[i] = -1;
        }

    }

    void broadcastENTER(char *addr, int port){
        fprintf(stderr, "addr: %s\n", addr);
        char msg1[80] = "*** User '(no name)' entered from ";
        //sprintf(msg1, "*** User '(no name)' entered from %s/%d. ***\n",addr,port);
        sprintf(msg1, "*** User '(no name)' entered from %s/%d. ***\n","CGILAB",511);
        broadcast(msg1);
    }

    void broadcastLEAVE(){
        char msg[100]="";
        char path[100]="";
        char path2[100]="";
        sprintf(msg, "*** User '%s' left. ***\n", shareptr->UserName[client_own_id]);
        //broadcast(msg);
        // unset some info
        //shareptr->PidTable[client_own_id] = -1;
        //strcpy(shareptr->UserName[client_own_id], "(no name)");
        //strcpy(shareptr->UserIP[client_own_id], "");

        for(int i=1; i<MAX_USER; i++){
            //fprintf(stderr, "cleitn: %d, other :%d, = %d\n", client_own_id, i , shareptr->FIFO_ForUser[client_own_id][i]);
         //   if(shareptr->FIFO_ForUser[client_own_id][i] != -1){
            //    close(FIFO_wait_for_read[i]);
             //   FIFO_wait_for_read[i] = -1;
             //   sprintf(path, "%s%d-%d", FIFO_PATH, client_own_id, i);
             //   char buf[MAX_WORD];
             //   read(FIFO_wait_for_read[i], buf, strlen(buf));
             //   unlink(path);
             //   close(FIFO_wait_for_read[i]);
               //FIFO_wait_for_read[i] = -1;
                // if not open and read FIFO , the FIFO creater(writer) will be block
               // int readfd = open(path, 0);
                //char buf[MAX_WORD]="";
               // if(read(readfd,buf,strlen(buf))!=strlen(buf));
               // unlink(path);
                //readfd = open(path2,0);
                //read(readfd,buf,strlen(buf));
                //unlink(path2);
                //fprintf(stderr, "unlink success FIFO : %s\n", path);
            //}
            if(FIFO_wait_for_read[i] != -1){
                char buf[MAX_WORD];
                read(FIFO_wait_for_read[i],buf, strlen(buf));
                close(FIFO_wait_for_read[i]);
                sprintf(path, "%s%d-%d", FIFO_PATH, client_own_id, i);
                unlink(path);
                FIFO_wait_for_read[i] = -1;
            }
                shareptr->FIFO_ForUser[client_own_id][i] = -1;
        }
        broadcast(msg);
        shareptr->PidTable[client_own_id] = -1;
        strcpy(shareptr->UserName[client_own_id], "(no name)");
        strcpy(shareptr->UserIP[client_own_id], "");
        memset(shareptr->Buffer[client_own_id], 0, sizeof(shareptr->Buffer[client_own_id]));
        fprintf(stderr, "#%d process leave\n", client_own_id);

    }

    void broadcast(char *msg){
        fprintf(stderr,"in broadcast: %s\n", msg);
        for(int i=1; i<MAX_USER; i++){
            if(shareptr-> PidTable[i]!= -1){
                strcat(shareptr -> Buffer[i], msg);
                kill(shareptr -> PidTable[i], SIGUSR1);
                fflush(NULL);
            }  
        }
    }


    int setIdIpPort(int pid, char *addr, int port){
        int i;
        char addr_port[25];
        for(i=1; i<MAX_USER ; i++){
            if(shareptr -> PidTable[i] == -1){
                fprintf(stderr, "set Pidtable[%d] = %d\n",i, pid);
                shareptr -> PidTable[i] = pid;
                fprintf(stderr, "after set PidTable:%d\n",shareptr->PidTable[i]);
                sprintf(addr_port,"%s/%d", addr, port);
                //strcpy(shareptr -> UserIP[i], addr_port);
                strcpy(shareptr-> UserIP[i], "CGILAB/511");
                return i; // represent ID
            }
        }
        return -1;

    }

    void setUserName(char *name, int sockfd){
        int id;
        char msg[100]="";
        bool dupName = false;

        for(int i=0; i<5; i++)
            fprintf(stderr, "user[%d] Name: %s\n", i, shareptr -> UserName[i]);

        /* 檢查重複name */
        for(int i=1; i<MAX_USER; i++){
            if(strcmp(shareptr -> UserName[i],"(no name)") != 0){
                if(i != client_own_id && strcmp(shareptr -> UserName[i],name)==0){
                    sprintf(msg, "*** User '%s' already exists. ***\n", name);
                    fprintf(stderr, "setUserName: %s\n", msg);
                    cout << msg << endl;
                    return;
                }
            }
        }
        fprintf(stderr, "set User #%d = %s\n", client_own_id, name);
        strcpy(shareptr->UserName[client_own_id], name);
        sprintf(msg, "*** User from %s is named '%s'. ***\n", shareptr->UserIP[client_own_id], name);
        fprintf(stderr, "setUserName %s\n", msg);
        broadcast(msg);
        return;
    }

    void tell(int sockfd, int receiverID, char *msg){
        // check if receiver is online
        fprintf(stderr, "tell #:%d\n",receiverID);
        char errormsg[50];
        char msg1[100];
        if(shareptr->PidTable[receiverID] == -1){
            sprintf(errormsg, "*** Error: user #%d does not exist yet. ***\n", receiverID);
            write(sockfd, errormsg, strlen(errormsg));
            return;
        }
        //const char *name = transName(user_InfoTable[receiverID][USERNAME]);
        // % *** IamUser told you ***: Hello World.
        sprintf(msg1, "*** %s told you ***: %s.\n", shareptr->UserName[client_own_id], msg);
        strcat(shareptr->Buffer[receiverID], msg1);
        kill(shareptr->PidTable[receiverID], SIGUSR1);
        fflush(NULL);
    }

    void who(){
        char msg[100]="";
        const char *meta = "<ID>\t<nickname>\t<IP/port>\t<indicate me>\n";
        write(STDOUT_FILENO,meta, strlen(meta));
        for(int i=1; i<6; i++)
            fprintf(stderr, "%d PID:%d \n", i, shareptr -> PidTable[i]);
        for(int i=1; i<MAX_USER; i++){
            if(shareptr->PidTable[i] != -1){
                //const char *name = (user_InfoTable[i][USERNAME]==NULL)? "(no name)" : user_InfoTable[i][USERNAME];
                if(client_own_id == i){
                    fprintf(stderr, "who, client own %d\n", i);
                    sprintf(msg, "%d\t%s\t%s\t<-me", i, shareptr->UserName[i], shareptr -> UserIP[i]);            
                }
                else{
                    fprintf(stderr, "who else, client other %d\n",i);
                    sprintf(msg, "%d\t%s\t%s", i, shareptr->UserName[i], shareptr -> UserIP[i]);
                }
                cout << msg<< endl;
            }
        }
    }


    void yell(char* msg){
        int id;
        char msg1[100];
        const char *name;
        sprintf(msg1, "*** %s yelled ***: %s\n", shareptr->UserName[client_own_id], msg);
        broadcast(msg1);
    }

    void initial_FIFO(){
        for(int i=1; i<MAX_USER; i++){
            FIFO_wait_for_read[i] = -1;
        }
    }
