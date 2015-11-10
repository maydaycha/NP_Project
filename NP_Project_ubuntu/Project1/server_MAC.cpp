#include <wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
//using namespace std;
using std::cout;
using std::cerr;
using std::string;
using std::endl;

#define DEFALUT_PORT 30001
#define MAX_LINE 10000  // number of char in one line
#define MAX_PIPE 10000   // maximum pipe
#define MAX_WORD 10000   // number of word in one line (cut by pipe)

void err_dump(const char *msg);
void welcome(int sockfd);
int readline(int fd, char *ptr, int maxlen);
void str_echo(int sockfd);
void initial_output(int sockfd);
void initial_pipe();
void construct_depart_cmd(char *cutline[], int sockfd);
void do_command(char *argv,int sockfd, bool isfirst, bool islast, bool need_jump, int jump_num, bool show_error);
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


    /* Bind our local address so that the client can send to us  */
    bzero((char*)&server_addr,sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(DEFALUT_PORT);

    /* Tell the kernel that you are willing to re-use the port anyway */
    int yes=1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        perror("setsockopt");
        exit(1);
    }
	
	if(bind(sockfd,(struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
        err_dump("server: cant bind local address");
    /*******************************************************************/
	
    listen(sockfd,5);
	cerr << "server start, with port: " << DEFALUT_PORT << endl;

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
            /*close the progine socket */
        close(sockfd);
            /* process the request */
        welcome(newsockfd);
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
    char line[MAX_LINE];
    char *cutline[MAX_WORD];
    bool show_error = false;

    // redirect the output of this connection to sockfd
    initial_output(sockfd);
    //  initial the array which is used to record 
    initial_pipe();

    // infinity loop until connection terminate
    for(;;){
        write(sockfd, "% ",2);
        // read client input
        n = readline(sockfd, line, MAX_LINE);

        if(n<0)
            err_dump("str_echo: readline error");
        else if(n==0)
              return;    //terminate
          else if(n==2)
            continue;   // 防止按enter導致斷線

        //fprintf(stderr, "client input: %s",line);

        //指令行 ++
        now_cmd_line++;

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
                do_command(cutline[cmd_num], sockfd, isfirst, islast, need_jump, jump_num, show_error);
            if(need_jump)
                notdo_nextcmd = true;
            isfirst = false;
            //cout << "==============End==================" << endl;
        }

    }
}



void do_command(char *argv,int sockfd, bool isfirst, bool islast, bool need_jump, int jump_num, bool show_error){
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
    bool appendfile = false;
    bool to_other_pipe = false;
    char *filename;
    int to_other_cmd_pipe;
     /* parsing the command*/
    char *cmd[100];
    cmd[0]  = strtok(argv," ");
    int word = 0;
    while(cmd[word]!=NULL){
        word++;
        cmd[word] = strtok(NULL," ");
    }
    //cout << "word: " << word << endl;

   // 判斷有沒有輸出到檔案 ">"
   // or >>
    int count_append = 0;
    for(int i=0; i< word; i++){
        if(strcmp(cmd[i],">")==0){
            tofile = true;
            filename = cmd[i+1];
            cmd[i] = NULL;
            cmd[i+1] = NULL;
            break;
        }
        else if(strcmp(cmd[i], ">>")==0){
            appendfile = true;
            filename = cmd[i+1];
            cmd[i] = NULL;
            cmd[i+1]= NULL;
            break;
        }
    }


    //現在指令 +1 
    now_cmd++;

    //cout << "now_cmd:" << now_cmd << endl;

    // setenv
    if(strcmp(cmd[0],"setenv") == 0 ){
        //cout << "setenv: " << "param1 = " <<cmd[1] << endl;
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
        close(sockfd);
        // restore the stdout
        dup2(stdout_copy,1);
        close(stdout_copy);
        exit(0);

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
            //cout << "set pipe_input["<< now_cmd_line+jump_num << "][2] = " << pipe_input[now_cmd_line+jump_num][2] << endl;
                pipe_input[now_cmd_line+jump_num][0] = now_cmd;
            //cout << "set pipe_input["<< now_cmd_line+jump_num << "][0] = " << pipe_fd[now_cmd][0] << endl;
                pipe_input[now_cmd_line+jump_num][1] = now_cmd;
            //cout << "set pipe_input["<< now_cmd_line+jump_num << "][1] = " << pipe_fd[now_cmd][1] << endl;
            // bool parentHold_childNotUse = true;
            // record_jump_pipe_array[record_jump_pipe_num++] = now_cmd_line+jump_num ;
            }
        }
    }

    if((childpid = fork())<0){
        err_dump("fork error");
    }
    else if(childpid>0){   //parent process

        if(!isfirst){
            // //cout << "parent close pipe's read end: " <<pipe_fd[now_cmd-1][0]<< endl;
            // //cout << "parent close pipe's write end: " <<pipe_fd[now_cmd-1][1]<< endl;
            // //cout << "Parent close pipe_fd[" << now_cmd-1 << "][0]:" << pipe_fd[now_cmd-1][0] << endl;
            // //cout << "Parent close pipe_fd[" << now_cmd-1 << "][1]:" << pipe_fd[now_cmd-1][1] << endl;
            close(pipe_fd[now_cmd-1][0]);
            close(pipe_fd[now_cmd-1][1]);

        }
        if(pipe_input[now_cmd_line][2] == 1){   //之前沒關掉的pipe， 其read end 在這行指令要給stdin讀，以後不會再用到，全部關掉
            //cout << "-----------parent close previous pipe-----------" <<endl ;
            int from_previous = pipe_input[now_cmd_line][0];
            close(pipe_fd[from_previous][1]);
            close(pipe_fd[from_previous][0]);
            //cout << "pipe_fd[" << from_previous << "][2]" << pipe_input[now_cmd_line][2] <<endl ;
            //cout << "pipe_fd[" << from_previous << "][1]" << pipe_fd[from_previous][1] <<endl ;
            //cout << "pipe_fd[" << from_previous << "][0]" << pipe_fd[from_previous][0] <<endl ;
            //cout << "-----------parent close previous pipe-----------" <<endl ;
        }
         // if(pipe_input[now_cmd_line][2] == 1)
            // pipe_input[now_cmd_line][2] = 0;
        int status;
        wait(&status);
        if(pipe_input[now_cmd_line][2] == 1)
            pipe_input[now_cmd_line][2] = 0;
        //cout << "parent" << endl;
        return;
    }
    else{ //child process

         if(isfirst){    //第一個指令
        // 設定 stdin
            //cout << "now cmd line: " << now_cmd_line << endl;
            //cout << "pipe_input[" << now_cmd_line << "][2]=" << pipe_input[now_cmd_line][2] << endl;
                if(pipe_input[now_cmd_line][2] == 1){    //前面有跳行指令， output到這個指令的stdin
                   //cout << "-----------child close previous pipe-----------" <<endl ;
                    int from_previous = pipe_input[now_cmd_line][0];   //將要input到這行指令的data從 pipe read端取出
                    //cout << "from_previous: " << from_previous << endl;  
                    dup2(pipe_fd[from_previous][0],STDIN_FILENO);
                    close(pipe_fd[from_previous][1]);
                    close(pipe_fd[from_previous][0]);
                    pipe_input[now_cmd_line][2] = 0;
                    //cout << "pipe_fd[" << from_previous << "][2]" << pipe_input[now_cmd_line][2] <<endl ;
                    //cout << "pipe_fd[" << from_previous << "][1]" << pipe_fd[from_previous][1] <<endl ;
                    //cout << "pipe_fd[" << from_previous << "][0]" << pipe_fd[from_previous][0] <<endl ;
                    //cout << "325 stdin is :" << pipe_fd[from_previous][0] << endl;
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
                    //cout << "347 close pipe_fd[" << now_cmd<< "][0]:"  << pipe_fd[now_cmd][0] << endl;
                close(pipe_fd[now_cmd][0]);
            }
                 // //cout << "255 this pro stdin is stdin" << endl;
        }
        else{   /*非第一行指令， stdin 設定成pipe的read端 （0）*/
            // 設定stdin
            //cout << "355 close stdin(else)" << endl;
            // close(STDIN_FILENO);
        close(pipe_fd[now_cmd-1][1]);
        dup2(pipe_fd[now_cmd-1][0],STDIN_FILENO);
            //cout << "367 close pipe_fd[" << now_cmd-1 << "][0]:"  << pipe_fd[now_cmd-1][0] << endl;

            // //cout << "264 sidin is from pipe " << pipe_fd[now_cmd-1][0] << endl;

        close(pipe_fd[now_cmd-1][0]);
            //cout << "364 sidin is from pipe " << pipe_fd[now_cmd-1][0] << endl;
            //cout << "365 stdin is(else) : " << pipe_fd[now_cmd-1][0] << endl;

    }

        //cout << "PIPE: " << PIPE << endl;

        if(PIPE){ //有用到pipe 來存output
            //cout << "370 pipe write end: " << pipe_fd[now_cmd][1] << endl;
            //cout << "371 pipe read end: " << pipe_fd[now_cmd][0] << endl;
            //cout << "372 stdout is: " << pipe_fd[now_cmd][1] << endl;

            /* 設定 stdout */
            if(to_other_pipe){
                if(show_error)
                    dup2(pipe_fd[to_other_cmd_pipe][1],STDERR_FILENO);

                dup2(pipe_fd[to_other_cmd_pipe][1],STDOUT_FILENO);
                close(pipe_fd[to_other_cmd_pipe][1]);

            }
            else{
                if(show_error)
                    dup2(pipe_fd[now_cmd][1],STDERR_FILENO);

                dup2(pipe_fd[now_cmd][1],STDOUT_FILENO);
            //cout << "376 close pipe_fd[" << now_cmd << "][1]:"  << pipe_fd[now_cmd][1] << endl;
                close(pipe_fd[now_cmd][1]);
            }

        }

        for(int i=0; i<MAX_PIPE; i++){
            if(pipe_input[i][2]==1){
                int n = pipe_input[i][0];
                close(pipe_fd[n][0]);
                close(pipe_fd[n][1]); 
                //cout << "#387 close " << i << " pipe" << endl;
                //cout << "write end is:" << pipe_fd[n][1] << endl;
                //cout << "read end is:" << pipe_fd[n][0] << endl;

            }
        }


        // redirect to file
        if(tofile){
            int fd = open(filename,O_WRONLY|O_CREAT|O_TRUNC, 0666);
            dup2(fd,STDOUT_FILENO);
            close(fd);
        }
        if(appendfile){
            int fd = open(filename, O_WRONLY | O_APPEND |O_CREAT, 0666);
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }


        /* exexute the command */
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
    stdin_copy = dup(0);
    stdout_copy = dup(2);
    close(fileno(stdout));
    dup2(sockfd,STDOUT_FILENO);
    dup2(sockfd,STDERR_FILENO);
    // close(sockfd);
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
    // //cout << msg << ", errno = " << errno << endl;
        exit(1);
    }
