#define _POSIX_SOURCE
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#define MAX_TOKENS 31



struct process {
    int pid;
    char **args;
    int numArgs;
    int input;
    int output;    //input, output, and error are changed for file redirection
    int error;
};

typedef struct process Process;

struct jobList {
    Process *p1;
    Process *p2; //in case of a pipe
    char command[2000];
    int unknownStatus;
    int unknownStatus2;
    int status;
    int jobNumber;
    struct jobList *next;
};

typedef struct jobList List;

List *head;
List *tail;

void parseLine(char* str,char **tokens, int *numTokens); 
void simpleCommands(Process *p, int flag);
void fd(char **left, char * cmd, char *arg);  //file redirection
void pipeExec(Process *pl, Process *pr, int flag); //pipes
void interpreter(char *tokens[], int numTokens);
void getPipeOperands(char *tokens[], int numTokens, int index); //tokens are all the tokens, numTokens is the total number of tokens, and index is the index of the pipe
static void sigChld_Handler(int sig);
void add(List *n);
void removeFromList(int groupId);
int findInList(int gpid);
void fg();
void bg(int flag);
void jobs();
List* getRecentJob(int length);
int getLength();


char cmd[2000];


int main() {

    char str[2000];
    char *tokens[31];
    int status;
    int num;
    char pOm;
    int numTokens = 0;
    signal(SIGINT,SIG_IGN);
    signal (SIGTSTP, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGCONT,SIG_IGN);
    tail = head = NULL;
    //signal(SIGCHLD, sigChld_Handler);

    while(1) { //ask for command
        printf("# ");
        if(fgets(str,2000,stdin) == NULL) {break;}
        if(strcmp(str,"\n")) {
            parseLine(str,tokens,&numTokens);
            for(int x = 0; x<numTokens;x++) {
                free(tokens[x]);
            }
            numTokens = 0;
        }
        num = 1;
        List *temp = head;
        while(temp != NULL) {
            
            if(temp->p2 == NULL) {
                waitpid(-temp->jobNumber,&temp->unknownStatus,WNOHANG);
                if(WIFEXITED(temp->unknownStatus)) {
                    if(temp->next == NULL) {
                        pOm = '+';
                    } else {
                        pOm = '-';
                    }
                    printf("[%d]%c   Done   %s\n",num,pOm,temp->command);
                    removeFromList(temp->jobNumber);
                }
            } else {
                waitpid(temp->p1->pid,&temp->unknownStatus,WNOHANG);
                waitpid(temp->p2->pid,&temp->unknownStatus2, WNOHANG);
                if(WIFEXITED(temp->unknownStatus) && WIFEXITED(temp->unknownStatus2)) {
                    if(temp->next == NULL) {
                        pOm = '+';
                    } else {
                        pOm = '-';
                    }
                    printf("[%d]%c   Done   %s\n",num,pOm,temp->command);
                    removeFromList(temp->jobNumber);
                }
            }
            temp = temp->next;
            num++;
        }

        
    }

    return 0;
}

void parseLine(char* str,char* tokens[], int *numTokens) {
    char *line = strtok(str, "\n");
    if(line[strlen(line)-1] == '&') {
        strcpy(cmd,line);
        strcpy(cmd,strtok(cmd,"&"));
    } else {
        strcpy(cmd,line);
    }
    //strcpy(cmd,line); 
    const char s[2] = " ";
    
    char* segment = strtok(line,s);
    int index = 0;
    while(segment != NULL) {
        tokens[index] = (char *)malloc((strlen(segment)+1) *sizeof(char));
        (*numTokens)++;
        strcpy(tokens[index],segment);
        index++;
        segment = strtok(NULL,s);
        
    } 
        tokens[index] = NULL; //AT THIS POINT, TOKENS HAS ALL THE TOKENS
        interpreter(tokens, *numTokens);
}


void interpreter(char *tokens[], int numTokens) {  //in charge of processing the input, and recognizing operators
    char **args = (char **)malloc((numTokens + 1) * sizeof(char *) );
    int idx = 0;
    int argIdx = 0;
    int pipeFl = 0;
    Process *p = (Process *)malloc(sizeof(Process));
    p->input = STDIN_FILENO;
    p->output = STDOUT_FILENO;
    p->error = STDERR_FILENO;
    while(idx < numTokens) {

        if(!strcmp(tokens[idx],"fg")) {
            fg();
            return;

        }
        if(!strcmp(tokens[idx],"bg")) {
            bg(0);
            return;
        }
        if(!strcmp(tokens[idx],"jobs")) {
            jobs();
            return;
        }
        if(strcmp(tokens[idx],"<") == 0 || strcmp(tokens[idx],">") == 0 || strcmp(tokens[idx],"2>") == 0 || strcmp(tokens[idx],"|") == 0 || strcmp(tokens[idx],"&") == 0) {

            
            if(!strcmp(tokens[idx],"&")) { //& is always the last thing in the line
                if(idx + 1 != numTokens) {
                    printf("Incorrect use of &\n");
                    return;
                }
                args[argIdx] = NULL;
                p->args = args;
                simpleCommands(p,1);
                return;
            }

            if(!strcmp(tokens[idx],"<")) { //file needs to exist for this to work
                p->input = open(tokens[idx+1],O_RDONLY);
                if(access(tokens[idx+1],F_OK) == -1) {
                    printf("Invalid use of <\n");
                    for(int a = 0; a < numTokens; a++) {
                        free(args[a]);
                    }
                    free(args);
                    free(p);
                    return;
                }
                idx++;
            }
            if(!strcmp(tokens[idx],">")) { //file does not need to exist for this to work
                p->output = open(tokens[idx+1], O_WRONLY|O_CREAT|O_TRUNC, 0600);
                idx++;
            }
            if(!strcmp(tokens[idx],"2>")) { 
                p->error = open(tokens[idx+1], O_WRONLY|O_CREAT|O_TRUNC, 0600);
                idx++;
            }
            if(!strcmp(tokens[idx],"|")) { 
                pipeFl = 1;
                //for(int x = 0; x < MAX_TOKENS; x++) {
                 //   free(args[x]);
                //}
                //free(p);
                break;
            }

        } else {
            args[argIdx] = (char *)malloc(strlen(tokens[idx]) * sizeof(char));
            strcpy(args[argIdx],tokens[idx]);
            //printf("%s\n",args[argIdx]);
            argIdx++;
        }
        idx++;
       
    }
        args[argIdx] = NULL;
        p->args = args;
        //p->numArgs = argIdx + 1;
    if(pipeFl == 1) {
        getPipeOperands(tokens,numTokens,idx);
    } else {
        simpleCommands(p,0);
    }
    
}//interpreter



void simpleCommands(Process *p, int flag) {

    int status;
    int cpid = fork();
    p->pid = cpid;
    setpgid(p->pid,p->pid);
    if(cpid == 0) {
        
        signal (SIGINT, SIG_DFL);
        signal (SIGQUIT, SIG_DFL);
        signal (SIGTSTP, SIG_DFL);
        signal (SIGTTIN, SIG_DFL);
        signal (SIGTTOU, SIG_DFL);
        signal (SIGCHLD, SIG_DFL);
        signal(SIGCONT, SIG_DFL);
        if(p->input != STDIN_FILENO) {
            dup2(p->input,STDIN_FILENO);
        }
        if(p->output != STDOUT_FILENO) {
            int x = dup2(p->output,STDOUT_FILENO);
            close(p->output);
        }   
        if(p->error != STDERR_FILENO) {
            dup2(p->error,STDERR_FILENO);
            close(p->error);
        }
        execvp(p->args[0], p->args);

        
    } else {
        if(flag) {
            //printf("it gets here\n");
            if(!findInList(p->pid)) {
                List *node = (List *)malloc(sizeof(List));
                node->p1 = p;
                strcpy(node->command,cmd);
                node->status = 0; //1 corresponds to a stopped state
                node->unknownStatus = 83; //arbitrary number
                node->jobNumber = p->pid;
                add(node);
            }
        } else {
        tcsetpgrp(STDIN_FILENO,p->pid);
        waitpid(p->pid,&status,WUNTRACED);
        if(WIFSTOPPED(status)) {
            printf("it gets here\n");
            if(!findInList(p->pid)) {
                List *node = (List *)malloc(sizeof(List));
                node->p1 = p;
                strcpy(node->command,cmd);
                node->status = 1; //1 corresponds to a stopped state
                node->unknownStatus = status;
                node->jobNumber = p->pid;
                add(node);
            }
        }
        tcsetpgrp(STDIN_FILENO,__getpgid(getpid()));
        /*
        free(p);
        for(int x = 0; x < numArgs; x++) {              //FOR NOW
            free(args[x]);
        }
        free(args);
        */
        }
    }
     
}

void getPipeOperands(char *tokens[], int numTokens, int index) {
    char **left = (char **)malloc((numTokens + 1) * sizeof(char *) );
    char **right = (char **)malloc((numTokens + 1) * sizeof(char *) );
    int idx = 0;
    int leftIdx = 0;
    int rightIdx = 0;
    Process *pl = (Process *)malloc(sizeof(Process));
    pl->input = STDIN_FILENO;
    pl->output = STDOUT_FILENO;
    pl->error = STDERR_FILENO;
    while(idx < index) { //feeding the left
        if(strcmp(tokens[idx],"<") == 0 || strcmp(tokens[idx],">") == 0 || strcmp(tokens[idx],"2>") == 0 || strcmp(tokens[idx],"&") == 0) {

            
            if(!strcmp(tokens[idx],"<")) { //file needs to exist for this to work
                pl->input = open(tokens[idx+1],O_RDONLY);
                if(access(tokens[idx+1],F_OK) == -1) {
                    printf("Invalid use of <\n");
                    return;
                }
                idx++;
            }
            if(!strcmp(tokens[idx],">")) { //file does not need to exist for this to work
                pl->output = open(tokens[idx+1], O_WRONLY|O_CREAT|O_TRUNC, 0600);
                idx++;
            }
            if(!strcmp(tokens[idx],"2>")) { 
                pl->error = open(tokens[idx+1], O_WRONLY|O_CREAT|O_TRUNC, 0600);
                idx++;
            }

        } else {
            left[leftIdx] = (char *)malloc(sizeof(tokens[idx]));
            strcpy(left[leftIdx],tokens[idx]);
            //printf("%s\n",args[argIdx]);
            leftIdx++;
        }
        idx++;
    }//FEEDING THE LEFT WHILE LOOP

    left[leftIdx] = NULL;
    leftIdx++;
    pl->args = left;
    pl->numArgs = leftIdx +1;


    idx = index + 1; //start of the second process
    Process *pr = (Process *)malloc(sizeof(Process));
    pr->input = STDIN_FILENO;
    pr->output = STDOUT_FILENO;
    pr->error = STDERR_FILENO;

    while(idx < numTokens) {
        if(strcmp(tokens[idx],"<") == 0 || strcmp(tokens[idx],">") == 0 || strcmp(tokens[idx],"2>") == 0 || strcmp(tokens[idx],"&") == 0) {

            if(!strcmp(tokens[idx],"&")) { //& is always the last thing in the lihe
                if(idx + 1 != numTokens) {
                    printf("Incorrect use of &\n");
                    return;
                }
                right[rightIdx] = NULL;
                pr->args = right;
                pr->numArgs = rightIdx;
                pipeExec(pl,pr,1);
                return;
            }

            if(!strcmp(tokens[idx],"<")) { //file needs to exist for this to work
                pr->input = open(tokens[idx+1],O_RDONLY);
                if(access(tokens[idx+1],F_OK) == -1) {
                    printf("Invalid use of <\n");
                    return;
                }
                idx++;
            }
            if(!strcmp(tokens[idx],">")) { //file does not need to exist for this to work
                pr->output = open(tokens[idx+1], O_WRONLY|O_CREAT|O_TRUNC, 0600);
                idx++;
            }
            if(!strcmp(tokens[idx],"2>")) { 
                pr->error = open(tokens[idx+1], O_WRONLY|O_CREAT|O_TRUNC, 0600);
                idx++;
            }

        } else {
            right[rightIdx] = (char *)malloc(sizeof(tokens[idx]));
            strcpy(right[rightIdx],tokens[idx]);
            //printf("%s\n",args[argIdx]);
            rightIdx++;
        }
        idx++;
    }

    right[rightIdx] = NULL;
    rightIdx++;
    pr->args = right;
    pr->numArgs = rightIdx +1;

    pipeExec(pl, pr, 0);


}

void pipeExec(Process *pl, Process *pr, int flag) {

    int pipefd[2], status1,status2;
    List *node;
    pipe(pipefd);
    pid_t cpid;

    cpid = fork();
    pl->pid = cpid;
    setpgid(pl->pid,pl->pid);
    if(cpid == 0) {
        signal(SIGINT,SIG_DFL);
        signal(SIGTSTP,SIG_DFL);
        close(pipefd[0]);
        dup2(pipefd[1],STDOUT_FILENO);
        if(pl->input != STDIN_FILENO) {
            dup2(pl->input,STDIN_FILENO);
        }
        if(pl->output != STDOUT_FILENO) {
            int x = dup2(pl->output,STDOUT_FILENO);
            close(pl->output);
        }
        if(pl->error != STDERR_FILENO) {
            dup2(pl->error,STDERR_FILENO);
            close(pl->error);
        }
        execvp(pl->args[0],pl->args);
    }
    cpid = fork();
    pr->pid = cpid;
    setpgid(pr->pid,pl->pid);
    if(cpid == 0) {
        signal(SIGINT,SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        close(pipefd[1]);
        dup2(pipefd[0],STDIN_FILENO);
        if(pr->input != STDIN_FILENO) {
            dup2(pr->input,STDIN_FILENO);
        }
        if(pr->output != STDOUT_FILENO) {
            int x = dup2(pr->output,STDOUT_FILENO);
            close(pr->output);
        }
        if(pr->error != STDERR_FILENO) {
            dup2(pr->error,STDERR_FILENO);
            close(pr->error);
        }
        execvp(pr->args[0],pr->args);
    }
    close(pipefd[0]);
    close(pipefd[1]);
    if(!flag) {
        tcsetpgrp(STDIN_FILENO,pl->pid);
        waitpid(pl->pid,&status1,WUNTRACED);
        if(WIFSTOPPED(status1)) {
                printf("1st process stop handler\n");
                if(!findInList(pl->pid)) {
                    node = (List *)malloc(sizeof(List));
                    node->p1 = pl;
                    node->p2 = pr;
                    strcpy(node->command,cmd);
                    node->unknownStatus = status1;
                    node->unknownStatus2 = status2;
                    node->status = 1;
                    node->jobNumber = pl->pid;
                    add(node);
                } else {
                    node->unknownStatus = status1;
                }
        }   
        waitpid(pr->pid,&status2,WUNTRACED);
        if(WIFSTOPPED(status2)) {
                printf("it gets here\n");
                if(!findInList(pl->pid)) {
                    node = (List *)malloc(sizeof(List));
                    node->p1 = pl;
                    node->p2 = pr;
                    strcpy(node->command,cmd);
                    node->unknownStatus = status1;
                    node->unknownStatus2 = status2;
                    node->status = 1;
                    node->jobNumber = pl->pid;
                    add(node);
                } else {
                    node->unknownStatus2 = status2;
                }
            }
        tcsetpgrp(STDIN_FILENO,__getpgid(getpid()));
        /*
        free(pl);
        free(pr);
        for(int x = 0; x < numLeft; x++) {
            free(left[x]);                                 //FOR NOW
        }
        free(left);
        for(int y = 0; y < numRight; y++) {
            free(right[y]);
        }
        free(right);
        */
    } else {
        if(!findInList(pl->pid)) {
            node = (List *)malloc(sizeof(List));
            node->p1 = pl;
            node->p2 = pr;
            strcpy(node->command,cmd);
            node->unknownStatus = status1+45;
            node->unknownStatus2 = status2+42;
            node->status = 0;
            node->jobNumber = pl->pid;
            add(node);
        }
    }
}

static void sigChld_Handler(int sig) {

}

void add(List *n) {

    if(head == NULL) {
        head = n;
        tail = n;
    } else {
        tail->next = n;
        tail = n;
        tail->next = NULL;
    }

}

void removeFromList(int groupId) {
    
    List* temp = head;
    List* previous = NULL;
    while (temp != NULL)
    {
        if (temp->jobNumber == groupId)
        {
            if(temp->next == NULL) {
                tail = previous;
            }
            if (previous == NULL)
            {
                head = temp->next;
            }
            else
            {
                previous->next = temp->next;
            }
            return;
        }
        previous = temp;
        temp = temp->next;
    }
}

int findInList(int gpid) {
    List *temp = head;
    while(temp != NULL) {
        if(temp->jobNumber == gpid) {
            return 1;
        }
        temp = temp->next;
    }
    return 0;
}

void fg() {


    List *temp = head;
    if(temp == NULL) {
        printf("No jobs to fg\n");
        return;
    }
    while(temp->next != NULL) {
        temp = temp->next;
    }
    printf("%s\n",temp->command);
    tcsetpgrp(STDIN_FILENO,temp->jobNumber);
    //if(WIFSTOPPED(temp->unknownStatus) || WIFSTOPPED(temp->unknownStatus2)) {
        kill(-temp->jobNumber,SIGCONT);
    //}

    if(temp->p2 != NULL) {

        waitpid(temp->p1->pid,&temp->unknownStatus, WUNTRACED);
        waitpid(temp->p2->pid,&temp->unknownStatus2, WUNTRACED);
        if(WIFSTOPPED(temp->unknownStatus2) || WIFSTOPPED(temp->unknownStatus)) {
            temp->status = 1; //represents stopped state
        }
        if(WIFSIGNALED(temp->unknownStatus2) || WIFSIGNALED(temp->unknownStatus)) {
            removeFromList(temp->jobNumber);
        }
        if(WIFEXITED(temp->unknownStatus2) && WIFEXITED(temp->unknownStatus)) {
            removeFromList(temp->jobNumber);
        }
    } else {

        waitpid(temp->jobNumber,&temp->unknownStatus, WUNTRACED);
    
        if(WIFSTOPPED(temp->unknownStatus)) {
            temp->status = 1; //represents stopped state
        }
        if(WIFEXITED(temp->unknownStatus) || WIFSIGNALED(temp->unknownStatus)) {
            removeFromList(temp->jobNumber);
        }
    }
    tcsetpgrp(STDIN_FILENO,__getpgid(getpid()));
    
    
    
}

void bg(int flag) {
    int status;
    List *temp = head;
    if(temp == NULL) {
        printf("No jobs to bg\n");
        return;
    }
    int length = getLength();
    temp = getRecentJob(length);
    if(temp == NULL) {
        printf("No jobs to bg\n");
        return;
    }
    int num = 1;
    List *faker = head;
    while(faker->jobNumber != temp->jobNumber) {
        faker = faker->next;
        num++;
    }
    printf("[%d]+   ", num);
    printf("%s & \n",temp->command);
    if(!flag) {
        if(kill(-temp->jobNumber,SIGCONT) < 0 ) {
            printf("Signal not sent\n");
        }
    }
    //waitpid(-1,&status, WUNTRACED);
    temp->status = 0; //set to running
}

void jobs() {
    List *temp = head;
    int num = 1;
    while(temp != NULL) {
        printf("[%d]", num);
        if(temp->next == NULL) {
            printf("+    ");
        } else {
            printf("-    ");
        }
        if(temp->status == 0) {
            printf("Running    ");
        } else if(temp->status == 1) {
            printf("Stopped    ");
        } else if(temp->status == 2) {
            printf("Done    ");
        }
        printf("%s\n",temp->command);
        temp = temp->next;
        num++;
    }
}

List * getRecentJob(int length) {
    if(length == 0) {
        return NULL;
    }
    List *temp = head;
    for(int x = 0; x < length - 1; x++) {
        temp = temp->next;
    }
    if(temp->status == 1) {
        return temp;
    } else {
        return getRecentJob(length - 1);
    }
    

}

int getLength() {
    List *temp = head;
    int length = 0;
    while(temp != NULL) {
        length++;
        temp = temp->next;
    }
    return length;
}