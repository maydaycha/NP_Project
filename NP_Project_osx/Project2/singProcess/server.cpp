#include <string.h>
#include <errno.h>
//#include <wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <fcntl.h>
using namespace std;

#define PORT 30001
#define MAX_LINE 10000  // number of char in one line
#define MAX_PIPE 10000   // maximum pipe
#define MAX_WORD 10000   // number of word in one line (cut by pipe)
#define MAX_USER 30

#define USERNAME 0
#define USERIP 1
#define USERENV 2

//project 1
void err_dump(const char *msg);
void welcome(int sockfd);
int readline(int fd, char *ptr, int maxlen);
int str_echo(int sockfd, char *addr, int port, int thisUserID);
void initial_output(int sockfd);
void initial_pipe();
void construct_depart_cmd(char *cutline[], int sockfd);
void do_command(char *argv,int sockfd, bool isfirst, bool islast, bool need_jump, int jump_num, bool show_error, char *addr, int port, char* fullCmdName);
string strLTrim(const string& str);
string strRTrim(const string& str); 
string strTrim(const string& str);
char *trim(char str[]);
void initial_cmd();

// project 2
int setIdIpPort(int sockfd, char *addr, int port);
void broadcastENTER(char* addr, int port);
void initial_userInfo();
void setUserName(char *name, int sockfd, char *addr, int port);
void broadcast(char *msg);
void yell(char* msg, int sockfd);
void who(int sockfd);
void tell(int userID, int receiverID, char *msg);
void client_exit(int userID);
const char* transName(const char* name);
void initialReceiverRecordSender();
void unsetIdIpPortPipe(int userID);
void broadcastLEAVE(int userID);

//for De_bug
void seeReceSendFdTable(int i, int j);
void seeFdTable();
void seeUserTable(int end, int type);

int pipe_input[MAX_USER+1][MAX_PIPE][3];  //  record the command's stdin needs to change to pipe(jump) or not , -1 = not
int pipe_fd[MAX_USER+1][MAX_PIPE][2];   // pipe file discriptor 
int cmd_count = 0;  // record the total line of command
int now_cmd = 0;
int now_cmd_line = 0;
int stdin_copy, stdout_copy;
//int record_jump_pipe_array[MAX_USER][MAX_PIPE];
int record_jump_pipe_num = 0;
char *cmd[100];
//project 2
int user_FDtable[MAX_USER+1];
char *user_InfoTable[MAX_USER+1][3];
int pipe_fd_ToUser[MAX_USER+1][MAX_USER+1][2];  //每位user對其他每位user都有一個 Pipe
int receiver_record_sender[MAX_USER+1][MAX_USER+1];
int thisUserID;
int receiverID, senderID;
char full_cmd_name[100];
int user_now_cmd[MAX_USER+1];
int user_now_cmd_line[MAX_USER+1];

int main(int argc, char *argv[]){
    int clientlen, childpid, sockfd, client_counter=0, socket_count=0;
    int newsockfd[30];	

    /*socket structure*/
    sockaddr_in client_addr, server_addr;

    /*file descriptor set*/
    fd_set afds, rfds;
    int nfds, fd;

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
    /*******************************************************************/


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

    listen(sockfd,5);
    fprintf(stderr, "---------server start----------\n");

    // 將指定的文件描述符集清空，在對file_descriptor set進行設置前，必須對其進行初始化，如果不清空，由於在系統分配內存空間後，通常並不作清空處理，所以結果是不可知的。
    FD_ZERO(&afds);
    FD_SET(sockfd,&afds);

    initial_userInfo();	

    initialReceiverRecordSender();

    //  initial the array which is used to record 
    initial_pipe();

    seeReceSendFdTable(10,10);

    nfds = getdtablesize();
    cout << "nfds: " << nfds << endl;

    while(true){
        fprintf(stderr, "The begging\n" );
        // copy rfds to afds
        memcpy(&rfds,&afds,sizeof(rfds));

        for(int i=0; i< nfds; i++)
            if(FD_ISSET(i, &rfds)!=0)
                fprintf(stderr, "*** %d\n",i );

        fprintf(stderr, "==========before select==========\n");
        if(select(nfds,&rfds,(fd_set*)0, (fd_set*)0, (struct timeval *)0)<0)
            err_dump("select error");

        fprintf(stderr, "==========after select==========\n");
        const char *msg = FD_ISSET(sockfd, &rfds) ? "true" : "false";
        fprintf(stderr,"master sockfd is set? %s\n",msg);

        if(FD_ISSET(sockfd, &rfds)){
            int ssockfd;

            // cout << "sockfd: " << sockfd << endl;
            /*return the size of this structure */
            clientlen = sizeof(client_addr);
            /* 連線成功時，參數addr所指的結構會被系統填入遠程主機的地址數據 */
            ssockfd = accept(sockfd, (sockaddr*)&client_addr, (socklen_t*)&clientlen); 
            fprintf(stderr, "#%d,Client \n", ssockfd);
            if(ssockfd < 0){
                perror("accept");   
                err_dump("Server: accept error");
            }
            else{
                // fprintf(stderr, "ssockfd:%d" , ssockfd);
                welcome(ssockfd);
                FD_SET(ssockfd, &afds);
                char msg1[100];
                sprintf(msg1,"*** User '(no name)' entered from %s/%d. ***\n",inet_ntoa(client_addr.sin_addr),client_addr.sin_port);
                write(ssockfd, msg1, strlen(msg1));
                broadcastENTER(inet_ntoa(client_addr.sin_addr),client_addr.sin_port);
                write(ssockfd, "% ", 2);
                if(setIdIpPort(ssockfd,inet_ntoa(client_addr.sin_addr),client_addr.sin_port)<0)
                    err_dump("over 30 users");
                fprintf(stderr, "---------client connect successfull---------\n");
            }
        }

        for(fd=0; fd<nfds; fd++){
            // fprintf(stderr,"fd:%d , %d\n",fd,FD_ISSET(fd,&rfds));
            if(fd != sockfd && FD_ISSET(fd,&rfds)){
                //當下對 fd 有（寫入）需求的fd(socket)
                fprintf(stderr, "fd: '%d' is selected \n", fd);
                fprintf(stderr, "Do str_echo\n");

                /* find current user ID */
                for(int i=1; i<MAX_USER+1; i++){
                    if(user_FDtable[i]==fd){
                        thisUserID = i;
                        break;
                    }
                }
                if(str_echo(fd, inet_ntoa(client_addr.sin_addr), client_addr.sin_port, thisUserID)==0){
                    fprintf(stderr, "~~~~~~~~~~~~~client #%d socket:%d leave~~~~~~~~~~~\n", thisUserID, fd);
                    broadcastLEAVE(thisUserID);
                    unsetIdIpPortPipe(thisUserID);
                    //FD_CLR(fd, &afds);
                    shutdown(fd, 2);
                    close(fd);
                    FD_CLR(fd, &afds);
                }
                else
                    write(fd,"% ",2);
            }
        }
    }
}


int str_echo(int sockfd, char *addr, int port, int thisUserID){
    int n, word;
    char line[MAX_LINE];
    char *cutline[MAX_WORD];
    bool show_error = false;
    
    fprintf(stderr,"thisUserID: %d\n", thisUserID);
    // set user's environment
    setenv("PATH", user_InfoTable[thisUserID][USERENV], 1);
    fprintf(stderr, "user ENV: %s\n", user_InfoTable[thisUserID][USERENV]);

    // redirect the output of this connection to sockfd
    initial_output(sockfd);

    // //  initial the array which is used to record 
    // 	initial_pipe();

    
    // read client input
    n = readline(sockfd, line, MAX_LINE);
    strcpy(full_cmd_name,line);
    // write(sockfd, "% ",2);
    fprintf(stderr, "read %d bytes\n", n);
    if(n<0)
        err_dump("str_echo: readline error");
    else if(n==0)
        return 0;    //terminate
    // else if(n==2)
    //   continue;   // 防止按enter導致斷線

    fprintf(stderr, "===========#%d user input: %s", thisUserID, line);


    //指令行 ++
    //now_cmd_line++;
    user_now_cmd_line[thisUserID]++;
    fprintf(stderr, "241 user now cmd line: %d\n", user_now_cmd_line[thisUserID]);
    now_cmd_line = user_now_cmd_line[thisUserID];

    // 判斷有沒有 '!'
    for(int i =0; i<n; i++){
        if(line[i]=='!'){
            show_error = true;
            // dup2(sockfd,STDERR_FILENO);
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
    for(int cmd_num=0; cmd_num<count; cmd_num++){
        //cout << "==============Start==================" << cmd_num << endl;
        fprintf(stderr, "====%d, cmd: %s\n", cmd_num, cutline[cmd_num]);
        jump_num = atoi(cutline[cmd_num+1]);
        if(jump_num!=0)
            need_jump =true;
        else if(cmd_num==count-1)
            islast = true;
        if(strcmp(cutline[cmd_num],"exit")==0){
            return 0;
        }

        if(!notdo_nextcmd)
            do_command(cutline[cmd_num], sockfd, isfirst, islast, need_jump, jump_num, show_error, addr, port, full_cmd_name);
        if(need_jump)
            notdo_nextcmd = true;
        isfirst = false;
        //cout << "==============End==================" << endl;
    }
    return n;
    fprintf(stderr, "end of str_echo\n");
}



void do_command(char *argv,int sockfd, bool isfirst, bool islast, bool need_jump, int jump_num, bool show_error, char *addr, int port, char* fullCmdName){
    /*
    fprintf(stderr, "------------------------------------------\n");
    fprintf(stderr, "argv: %s\n", argv);
    fprintf(stderr, "sockfd: %d\n", sockfd);
    fprintf(stderr, "isfirst: %d\n", isfirst);
    fprintf(stderr, "islast: %d\n", islast);
    fprintf(stderr, "need_jump: %d\n", need_jump);
    fprintf(stderr, "jump_num: %d\n", jump_num);
    fprintf(stderr, "------------------------------------------\n");
    */
    int childpid;
    bool PIPE = false;
    bool tofile = false;
    bool to_other_pipe = false;
    char *filename;
    int to_other_cmd_pipe;
    // int thisUserID;
    bool pipe_to_user = false, pipe_from_user = false;
    // int receiverID, senderID;
    char nouse;
    char completeCmd[50];

    strcpy(completeCmd, argv);
    /* find current user ID */
    // for(int i=1; i<MAX_USER+1; i++){
    // 	if(user_FDtable[i]==sockfd){
    // 		thisUserID = i;
    // 		break;
    // 	}
    // }

    /* parsing the command*/
    initial_cmd();
    cmd[0]  = strtok(argv," ");
    int word = 0;
    while(cmd[word]!=NULL){
        word++;
        cmd[word] = strtok(NULL," ");
    }
    fprintf(stderr, "!!!~~~argv: %s\n", completeCmd);

    //	判斷是不是要pipe給user
    int np = 0;
    while(cmd[np]!=NULL){
        for(int i=0; i<strlen(cmd[np]); i++){
            if(cmd[np][i]=='>' ){
                char msg[100];
                /* 如果是 ">" 而不是 ">n" 則是輸出給file而不是user */
                if(strlen(cmd[np])==1)
                    break;
                // char to int
                sscanf(cmd[np],"%c%d",&nouse,&receiverID);
                fprintf(stderr, "pipe to user\n");
                fprintf(stderr, "sender ID: %d\n", thisUserID);
                fprintf(stderr, "receiver ID: %d\n", receiverID);
                if(user_FDtable[receiverID]==-1){
                    // char msg[100];
                    sprintf(msg, "*** Error: user #%d does not exist yet. ***\n", receiverID);
                    write(sockfd, msg, strlen(msg));
                   // write(sockfd,"% ", 2);
                    return;
                }
                if(receiver_record_sender[receiverID][thisUserID]==1){
                    // char msg[100];
                    sprintf(msg, " *** Error: the pipe #%d->#%d already exists. ***\n", thisUserID, receiverID);
                    write(sockfd, msg, strlen(msg));
                    //write(sockfd,"% ", 2);
                    return;
                }
                pipe_to_user = true;
                cmd[np] = NULL;
                break;
            }
        }
        np++;
    }
    np = 0;

    while(cmd[np]!=NULL){
        for(int i=0; i<strlen(cmd[np]); i++){
            if(cmd[np][i]=='<'){
                sscanf(cmd[np],"%c%d", &nouse, &senderID);
                fprintf(stderr, "=====this is receive cmd====\n");
                fprintf(stderr, "receiver ID: %d\n", thisUserID);
                fprintf(stderr, "sender ID: %d\n", senderID);

                if(receiver_record_sender[thisUserID][senderID]!=1){
                    char msg[100];
                    sprintf(msg, "*** Error: the pipe #%d->#%d does not exist yet. ***\n",senderID, thisUserID);
                    write(sockfd, msg, strlen(msg));
                    return;
                }
                pipe_from_user = true;
                cmd[np] = NULL;
                break;
            }
        }
        np++;
    }

    fullCmdName[strlen(fullCmdName)-1] = ' ';
    fullCmdName[strlen(fullCmdName)-2] = ' ';
    if(pipe_from_user){
        char msg[100];
        const char *senderName = transName(user_InfoTable[thisUserID][USERNAME]);
        const char *receiverName = transName(user_InfoTable[receiverID][USERNAME]);
        sprintf(msg, "*** %s (#%d) just received from %s (#%d) by ' %s'***\n", senderName, thisUserID, receiverName, receiverID, fullCmdName);
        broadcast(msg);
    }
    if(pipe_to_user){
        char msg[100];
        const char *senderName = transName(user_InfoTable[thisUserID][USERNAME]);
        const char *receiverName = transName(user_InfoTable[receiverID][USERNAME]);
        sprintf(msg, "*** %s (#%d) just piped ' %s' to %s (#%d) ***\n", senderName, thisUserID, fullCmdName, receiverName, receiverID);
        broadcast(msg);
    }

    fprintf(stderr, "cmd0:%s\n", cmd[0]);
    fprintf(stderr, "cmd1:%s\n", cmd[1]);
    fprintf(stderr, "cmd2:%s\n", cmd[2]);
    fprintf(stderr, "cmd3:%s\n", cmd[3]);

    fprintf(stderr, "word: %d\n", word);

    // fprintf(stderr, "pipe_to_user: %d\n", pipe_to_user);
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
    //現在指令 +1 
    //now_cmd++;
    user_now_cmd[thisUserID]++;
    now_cmd = user_now_cmd[thisUserID];

    //cout << "now_cmd:" << now_cmd << endl;

    // setenv
    if(strcmp(cmd[0],"setenv") == 0 ){
        fprintf(stderr,"~~~~~~env: %s\n", cmd[2]);
        strcpy(user_InfoTable[thisUserID][USERENV], cmd[2]);
        return;
    }

    /* cmd: printenv*/
    if(strcmp(cmd[0],"printenv") == 0){
        fprintf(stderr, "test printPATH~~~~: userID: %d, %s\n", thisUserID, user_InfoTable[thisUserID][USERENV]);
        cout << "PATH=" << user_InfoTable[thisUserID][USERENV] << endl;
        return;
    }

    /* cmd: exit*/
    if(strcmp(cmd[0],"exit") == 0){
        // seeFdTable();
        // close connection
        // close(sockfd);
        // restore the stdout
        // dup2(stdout_copy,1);
        // close(stdout_copy);
        // *** User '(name)' left. ***
        // exit(0);
        return;

    }

    /* cmd: name*/
    if(strcmp(cmd[0], "name") == 0){
        fprintf(stderr, "comand: name\n");
        fprintf(stderr, "name is : %s\n", cmd[1]);
        setUserName(cmd[1], sockfd, addr, port);
        // write(sockfd,"% ", 2);
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
        yell(message, sockfd);
        // write(sockfd,"% ", 2);
        return;
    }

    /* cmd: who */
    if(strcmp(cmd[0], "who")==0){
        who(sockfd);
        return;
    }

    /* cmd: tell */
    if(strcmp(cmd[0], "tell")==0){
        int i=2;
        char message[100]="";
        // fprintf(stderr, "cmd_size: %lu\n", sizeof(cmd)/sizeof(*cmd));
        //防止輸入參數數量不夠
        if(cmd[1]==NULL || cmd[2]==NULL){
            const char* errmsg = "error: plz give enough arguments\n";
            write(sockfd, errmsg, strlen(errmsg));
            return;
        }
        while(*(cmd+i)!=NULL){
            strcat(message, *(cmd+i));
            strcat(message, " ");
            i++;
        }
        tell(thisUserID, atoi(cmd[1]), message);
        return;
    }

    fprintf(stderr, "user_now_cmd_line[%d]: %d\n",thisUserID, user_now_cmd_line[thisUserID]);
    fprintf(stderr, "now_cmd_line: %d\n",now_cmd_line);
    fprintf(stderr, "user_now_cmd[%d]: %d\n", thisUserID, user_now_cmd[thisUserID]);
    fprintf(stderr, "now_cmd: %d\n",now_cmd);
    fprintf(stderr, "from privious pipe? %d\n", pipe_input[thisUserID][now_cmd_line][2]);

    // set pipe
    if(!islast){ 
        /*不是最後的指令才要開pipe */
        // fprintf(stderr, "!!!!OPEN PIPEs\n");
        if(pipe(pipe_fd[thisUserID][now_cmd])<0){
            err_dump("pipe error");
        }
        PIPE = true;
        if(need_jump){ 
            /* pipe到另一個pipe */
            if(pipe_input[thisUserID][now_cmd_line+jump_num][2] == 1){
                to_other_pipe = true;
                to_other_cmd_pipe = pipe_input[thisUserID][now_cmd_line+jump_num][0];
                close(pipe_fd[thisUserID][now_cmd][0]);
                close(pipe_fd[thisUserID][now_cmd][1]);
            }
            /*把當下這個pipe記錄下來*/
            else{ 
                pipe_input[thisUserID][now_cmd_line+jump_num][2] = 1;
                fprintf(stderr, "pipe_input[%d][%d][2]: %d\n", thisUserID, now_cmd_line+jump_num, pipe_input[thisUserID][now_cmd_line+jump_num][2]);
                pipe_input[thisUserID][now_cmd_line+jump_num][0] = now_cmd;
                fprintf(stderr, "pipe_input[%d][%d][0]: %d\n", thisUserID, now_cmd_line+jump_num, pipe_input[thisUserID][now_cmd_line+jump_num][0]);
                pipe_input[thisUserID][now_cmd_line+jump_num][1] = now_cmd;
                fprintf(stderr, "pipe_input[%d][%d][1]: %d\n", thisUserID, now_cmd_line+jump_num, pipe_input[thisUserID][now_cmd_line+jump_num][1]);
            }
        }
    }

    /*islast == true, but need to "pipe to" other user*/
    if(pipe_to_user){
        if(pipe(pipe_fd_ToUser[thisUserID][receiverID]) < 0){
            err_dump("pipe(to user) error");
        }
        receiver_record_sender[receiverID][thisUserID] = 1;
        fprintf(stderr, "pipe num1: %d\n", pipe_fd_ToUser[thisUserID][receiverID][0]);
        fprintf(stderr, "pipe num2: %d\n", pipe_fd_ToUser[thisUserID][receiverID][1]);
    }


    if((childpid = fork())<0)
        err_dump("fork error");
    else if(childpid>0){   //parent process
        if(!isfirst){
            close(pipe_fd[thisUserID][now_cmd-1][0]);
            close(pipe_fd[thisUserID][now_cmd-1][1]);

        }
        /*之前沒關掉的pipe， 其read end 在這行指令要給stdin讀，以後不會再用到，全部關掉*/
        if(pipe_input[thisUserID][now_cmd_line][2] == 1){
            fprintf(stderr, "-----------parent close previous pipe-----------\n");
            int from_previous = pipe_input[thisUserID][now_cmd_line][0];
            close(pipe_fd[thisUserID][from_previous][1]);
            close(pipe_fd[thisUserID][from_previous][0]);
            fprintf(stderr, "-----------parent close previous pipe-----------\n");
        }
        if(pipe_from_user){
            receiver_record_sender[thisUserID][senderID] = 0;
            fprintf(stderr, "parent close pipe_fd_ToUser[%d][%d]%d\n", senderID, thisUserID, pipe_fd_ToUser[senderID][thisUserID][0]);
            close(pipe_fd_ToUser[senderID][thisUserID][0]);
        }
        /*parent close the write end of the pipe that is to user*/
        if(pipe_to_user){
            fprintf(stderr, "parent close write end(to user): %d\n",pipe_fd_ToUser[thisUserID][receiverID][1]);
            close(pipe_fd_ToUser[thisUserID][receiverID][1]);
        }
        int status;
        wait(&status);
        if(pipe_input[thisUserID][now_cmd_line][2] == 1)
            pipe_input[thisUserID][now_cmd_line][2] = 0;
        //cout << "parent" << endl;
        return;
    }
    else{ //child process
        fprintf(stderr, "~~~Child process~~~~\n");
        if(isfirst){    //第一個指令
            // 設定 stdin
            //前面有跳行指令， output到這個指令的stdin
            if(pipe_input[thisUserID][now_cmd_line][2] == 1){    
                fprintf(stderr, "前面有跳行指令， output到這個指令的stdin\n");
                int from_previous = pipe_input[thisUserID][now_cmd_line][0];   //將要input到這行指令的data從 pipe read端取出
                //cout << "from_previous: " << from_previous << endl;  
                fprintf(stderr,"pipe_fd[%d][%d][0])\n", thisUserID, from_previous, pipe_fd[thisUserID][from_previous][0]);
                dup2(pipe_fd[thisUserID][from_previous][0],STDIN_FILENO);
                close(pipe_fd[thisUserID][from_previous][1]);
                close(pipe_fd[thisUserID][from_previous][0]);
                pipe_input[thisUserID][now_cmd_line][2] = 0;
                //cout << "-----------child close previous pipe-----------" <<endl ;

            }
            else if(pipe_from_user){
                fprintf(stderr, "thisUserID: %d\n", thisUserID);
                fprintf(stderr, "senderID: %d\n", senderID);
                fprintf(stderr, "pipe_fd_ToUser[%d][%d]: %d\n", senderID, thisUserID, pipe_fd_ToUser[senderID][thisUserID][0]);
                dup2(pipe_fd_ToUser[senderID][thisUserID][0], STDIN_FILENO);
                close(pipe_fd_ToUser[senderID][thisUserID][0]);
                // close(pipe_fd_ToUser[thisUserID][senderID][0]);
                // if(n<0)
                // 	err_dump("636, read error");
                // write(STDIN_FILENO, buffer, n);
            }

            int j=0,n;
            /*第一個指令有開pipe才要關*/
            if(PIPE){  
                close(pipe_fd[thisUserID][now_cmd][0]);
            }
        }
        /*非第一行指令， stdin 設定成pipe的read端 （0）*/
        else{
            // 設定stdin
            close(pipe_fd[thisUserID][now_cmd-1][1]);
            dup2(pipe_fd[thisUserID][now_cmd-1][0],STDIN_FILENO);
            close(pipe_fd[thisUserID][now_cmd-1][0]);

        }

        /*有用到pipe 來存output*/
        if(PIPE){ 
            fprintf(stderr, "有用到pipe 來存output\n");

            /* 設定 stdout */
            if(to_other_pipe){
                 fprintf(stderr, "to other pipe\n");
                if(show_error)
                    dup2(pipe_fd[thisUserID][to_other_cmd_pipe][1],STDERR_FILENO);

                dup2(pipe_fd[thisUserID][to_other_cmd_pipe][1],STDOUT_FILENO);
                close(pipe_fd[thisUserID][to_other_cmd_pipe][1]);

            }
            else{
                 fprintf(stderr, "not to other pipe\n");
                if(show_error)
                    dup2(pipe_fd[thisUserID][now_cmd][1],STDERR_FILENO);

                dup2(pipe_fd[thisUserID][now_cmd][1],STDOUT_FILENO);
                //cout << "376 close pipe_fd[" << now_cmd << "][1]:"  << pipe_fd[now_cmd][1] << endl;
                close(pipe_fd[thisUserID][now_cmd][1]);
            }

        }

        /* close unused pipe */
        for(int j=1; j<=MAX_USER; j++){
            for(int i=0; i<MAX_PIPE; i++){
                if(pipe_input[j][i][2]==1){
                    int n = pipe_input[j][i][0];
                    close(pipe_fd[j][n][0]);
                    close(pipe_fd[j][n][1]); 
                }
            }
        }

        // redirect to file
        if(tofile){
            int fd = open(filename,O_RDWR| O_CREAT | O_TRUNC,0666);
            dup2(fd,STDOUT_FILENO);
            close(fd);
        }

        /* redirect to user */
        if(pipe_to_user){
            dup2(pipe_fd_ToUser[thisUserID][receiverID][1],STDOUT_FILENO);
            fprintf(stderr, "child close: %d\n", pipe_fd_ToUser[thisUserID][receiverID][1] );
            fprintf(stderr, "child close: %d\n", pipe_fd_ToUser[thisUserID][receiverID][0]);
            close(pipe_fd_ToUser[thisUserID][receiverID][1]);
            close(pipe_fd_ToUser[thisUserID][receiverID][0]);
        }
        /* close unused pipe */
        for(int i=1; i<=MAX_USER; i++){
            for(int j=1; j<=MAX_USER; j++){
                if(receiver_record_sender[i][j]==1){
                    close(pipe_fd_ToUser[j][i][0]);
                    close(pipe_fd_ToUser[j][i][1]);
                    receiver_record_sender[i][j]=0;
                }
            }
        }

        /* exexute the command */
        if(execvp(cmd[0],cmd)==-1){
            cout << "Unknown command: [" << cmd[0] << "]" <<endl;
            exit(0);
        }
    }
}




// 去除句首句尾空白
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
    close(fileno(stdout));
    dup2(sockfd,STDOUT_FILENO);
    // dup2(sockfd,STDERR_FILENO);
    // close(sockfd);
}

// initial all pipe which is used to record the cmd need to change the stdin to pipe or not (-1 == not)
void initial_pipe(){
    for(int j=0; j<=MAX_USER; j++){
        for(int i=0; i < MAX_PIPE; i++){
            pipe_input[j][i][0] = 0;
            pipe_input[j][i][1] = 0;
            pipe_input[j][i][2] = 0;
           // record_jump_pipe_array[i] = -1;
        }

    }
}

void initial_cmdNumber(){
    for(int i=0; i<=MAX_USER; i++){
        user_now_cmd[i] = 0;
        user_now_cmd_line[i] = 0;
    }
}

int readline(int fd, char *ptr, int maxlen){

    int n, rc;
    char c;
    for(n=1; n<maxlen; n++){
        rc = read(fd, &c, 1);
        if(rc==1){
            *ptr++=c;
            if(c=='\n') break;
        }
        else if(rc ==0){
            if(n==1) return 0;   /*END of File*/
            else break;
        }
        else return -1;
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
    // //cout << msg << ", errno = " << errno << endl;
    exit(1);
}


void initial_userInfo(){
    for(int i=1; i<=MAX_USER; i++){
        user_FDtable[i] = -1;
        user_InfoTable[i][USERENV] = (char*)malloc(10*sizeof(char));
       strcpy(user_InfoTable[i][USERENV] , "bin:.");
    }
    user_FDtable[0] = -1000;
}

int setIdIpPort(int sockfd, char *addr, int port){
    int i;
    char addr_port[25];
    for(i=1; i<=MAX_USER ; i++){
        if(user_FDtable[i]==-1){
            user_FDtable[i] = sockfd;
            sprintf(addr_port,"%s/%d", addr, port);
            user_InfoTable[i][USERIP] = (char*)malloc((strlen(addr_port)+1)*sizeof(char));
            strcpy(user_InfoTable[i][USERIP], addr_port);
            return i; //represent ID
        }
    }
    return -1;
}

void broadcast(char *msg){
    for(int i=1; i<=MAX_USER; i++){
        if(user_FDtable[i] != -1){
            write(user_FDtable[i],msg,strlen(msg));
        }
        fflush(NULL);
    }
}


void broadcastENTER(char *addr, int port){
    // *** User '(no name)' entered from 140.113.215.63/1013. ***

    fprintf(stderr, "addr: %s\n", addr);
    char msg1[80] = "*** User '(no name)' entered from ";
    sprintf(msg1,"*** User '(no name)' entered from %s/%d. ***\n",addr,port);

    for(int i=1; i<=MAX_USER; i++){
        if(user_FDtable[i] != -1){
            write(user_FDtable[i],msg1,sizeof(msg1));
        }
    }
}

void broadcastLEAVE(int userID){
    char msg[100]="";
    const char *name = transName(user_InfoTable[userID][USERNAME]);
    sprintf(msg, "*** User '%s' left. ***\n", name);
    broadcast(msg);
}

void setUserName(char *name, int sockfd, char *addr, int port){
    int id;
    char msg[100];
    bool dupName = false;
    for(int iid=1; iid<=MAX_USER; iid++){
        if(user_FDtable[iid] == sockfd){
            id = iid;
        }
    }
    fprintf(stderr, "fd :%d\n", sockfd);
    fprintf(stderr, "id :%d\n", id);

    // user_InfoTable[id][USERNAME] = (char*)malloc((strlen(name)+1)*sizeof(char));

    for(int i=0; i<5; i++)
        fprintf(stderr, "user_InfoTable[%d][USERNAME]: %s\n", i, user_InfoTable[i][USERNAME]);

    /*檢查重複name*/
    for(int iid=1; iid<=MAX_USER; iid++){
        if(user_InfoTable[iid][USERNAME]!=NULL){
            fprintf(stderr, "user_InfoTable:%s, name:%s\n", user_InfoTable[iid][USERNAME], name);
            if(iid!=id && strcmp(user_InfoTable[iid][USERNAME],name)==0){
                sprintf(msg, "*** User '%s' already exists. ***\n", name);
                fprintf(stderr, "setUserName: %s\n", msg);
                write(sockfd, msg, strlen(msg));
                return;
            }
        }
    }
    fprintf(stderr, "set userNametable[%d]=%s\n", id, name);
    user_InfoTable[id][USERNAME] = (char*)malloc((strlen(name)+1)*sizeof(char));
    strcpy(user_InfoTable[id][USERNAME],name);

    sprintf(msg, "*** User from %s/%d is named '%s'. ***\n", addr, port,name);
    fprintf(stderr, "setUserName %s\n", msg);
    broadcast(msg);
    return;
}

void unsetIdIpPortPipe(int userID){
    //free socket
    user_FDtable[userID] = -1;
    //free username
    free(user_InfoTable[userID][USERNAME]);
    user_InfoTable[userID][USERNAME] = NULL;
    //free userip
    free(user_InfoTable[userID][USERIP]);
    user_InfoTable[userID][USERIP] = NULL;
    for(int i=1; i<=MAX_USER; i++){
        //關掉我給別人pipe的 write end
        close(pipe_fd_ToUser[userID][i][0]);
        close(pipe_fd_ToUser[userID][i][1]);
        receiver_record_sender[i][userID] = 0;
    }

    for(int i=1; i<=MAX_USER; i++){
        //關掉別人給我的pipe
        close(pipe_fd_ToUser[i][userID][0]);
        close(pipe_fd_ToUser[i][userID][1]);
        receiver_record_sender[userID][i] = 0;
    }
}
void yell(char* msg, int sockfd){
    int id;
    char msg1[100];
    const char *name;
    for(int i=1; i<=MAX_USER; i++){
        if(user_FDtable[i] == sockfd){
            id = i;
            break;
        }
    }
    name = (user_InfoTable[id][USERNAME]==NULL) ? "(no name)" : user_InfoTable[id][USERNAME];
    sprintf(msg1, "*** %s yelled ***: %s\n", name, msg);
    broadcast(msg1);
}

void who(int sockfd){
    char msg[100]="";
    const char *meta = "<ID> \t <nickname> \t <IP/port> \t <indicate me>\n";
    write(sockfd,meta, strlen(meta));
    for(int i=1; i<=MAX_USER; i++){
        if(user_FDtable[i] != -1){
            const char *name = (user_InfoTable[i][USERNAME]==NULL)? "(no name)" : user_InfoTable[i][USERNAME];
            if(user_FDtable[i]==sockfd)
                sprintf(msg, "%d \t %s \t %s \t <-me\n", i, name, user_InfoTable[i][USERIP]);	
            else
                sprintf(msg, "%d \t %s \t %s\n", i, name, user_InfoTable[i][USERIP]);
            write(sockfd, msg, strlen(msg));
        }
    }
    // seeUserTable(5,0);
}

void tell(int userID, int receiverID, char *msg){
    seeFdTable();
    // check if receiver is online
    char errormsg[50];
    char msg1[100];
    if(user_FDtable[receiverID]==-1){
        sprintf(errormsg, "*** Error: user #%d does not exist yet. ***\n", receiverID);
        write(user_FDtable[userID], errormsg, strlen(errormsg));
        return;
    }
    const char *name = transName(user_InfoTable[userID][USERNAME]);
    // % *** IamUser told you ***: Hello World.
    sprintf(msg1, "*** %s told you ***: %s.\n", name, msg);
    write(user_FDtable[receiverID], msg1, strlen(msg1));
}

void client_exit(int userID){
    char msg[100];
    const char* name = transName(user_InfoTable[userID][USERNAME]);
    sprintf(msg, "*** User '%s' left. ***\n", name);
    broadcast(msg);
}


const char* transName(const char* name){
    return (name==NULL)? "(no name)" : name;
}

void initialReceiverRecordSender(){
    for(int i=0; i<=MAX_USER; i++){
        for(int j=0; j<=MAX_USER; j++){
            if(i==0 || j==0)
                receiver_record_sender[i][j]=-1;
            else	
                receiver_record_sender[i][j]=0;
        }
    }
}
void initial_cmd(){
    for(int i=0; i<sizeof(cmd)/sizeof(*cmd); i++){
        cmd[i] = NULL;
    }

}

/* for De_Bug */
void seeUserTable(int end, int type){
    for(int i=1; i<=end; i++)
        fprintf(stderr, "userInfo[%d]: %s\n", i, user_InfoTable[i][type]);
}

void seeFdTable(){
    for(int i=0; i<=MAX_USER; i++)
        fprintf(stderr, "user_FDtable[%d]: %d\n", i, user_FDtable[i]);
}

void seeReceSendFdTable(int i, int j){
    for(int x=0; x<=i; x++)
        for(int y=0; y<=j; y++)
            fprintf(stderr, "receiver_record_sender[%d][%d]:%d\n",x, y, receiver_record_sender[x][y]);
}



