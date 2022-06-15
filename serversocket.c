#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/wait.h>

//sqlite db
#include <sqlite3.h>

#define buffersize 1024

char pcr_rowid;

void sig_fork(int signo) {
    int stat;
    waitpid(0, &stat, WNOHANG);
    return;
}

int callback(
    void *NotUsed,
    int argc,
    char **argv, 
    char **azColName)
{    
    NotUsed = 0;
    
    for (int i = 0; i < argc; i++)
    {
        printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
        //rowid => 2

        if (i == 2)
          pcr_rowid = *argv[i];
    }
    
    printf("\n");
    
    return 0;
}

int main(){
    signal(SIGCHLD, sig_fork); 

    //TPM PCR MANAGEMENT
    printf("PCR MANAGEMENT TEST SERVER - sqlite : %s\n", sqlite3_libversion());
    sqlite3 *db;
    char *err_msg = 0;

    int rc = sqlite3_open("test.db", &db);

    if (rc != SQLITE_OK)
    {
      fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
      sqlite3_close(db);
        
      return 1;
    }

    char *sql_init = "DROP TABLE IF EXISTS HOSTINFO;" 
                     "CREATE TABLE HOSTINFO(PID INT PRIMARY KEY, QEMU_IP TEXT);";

    rc = sqlite3_exec(db, sql_init, 0, 0, &err_msg);

    if (rc != SQLITE_OK)
    {
      fprintf(stderr, "SQL error: %s\n", err_msg);

      sqlite3_free(err_msg);
      sqlite3_close(db);

      return 1;
    }

    struct timeval timeout;
    timeout.tv_sec = 5; // sec
    timeout.tv_usec = 0; // ms

    int serv_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;  
    //serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    serv_addr.sin_addr.s_addr = inet_addr("172.25.244.104");  
    serv_addr.sin_port = htons(5566);  
    bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr));

    listen(serv_sock, 1);

    struct sockaddr_in clnt_addr;
    socklen_t clnt_addr_size = sizeof(clnt_addr);

    while (1) {
      int clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &clnt_addr_size);
      // set recv and send timeout
      if( setsockopt (clnt_sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0 )
        printf( "setsockopt fail\n" );
      if( setsockopt (clnt_sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) < 0 )
        printf( "setsockopt fail\n" ) ;
      
      int pid = fork();
      if (pid == -1) {
        printf( "ERROR pid\n" );
      } 
       
      else if (pid == 0) {
        printf("SERVER PID = %d\n", getpid()) ;
        //char *buffer = (char*) calloc(buffersize, sizeof(char));
        int *buffer = (int*) calloc(buffersize, sizeof(int));
        recv(clnt_sock, buffer, buffersize, 0) ;
        //printf("Server receive:%s\n", buffer) ;
        //printf("HOST QEMU PID:%d\n", *buffer) ;

        //int total = (int)(atoi(buffer)*atoi(buffer)) ;

        //TPM PCR DB ACCESS 
        //char *sql_VMINFO_INSERT = "";
        //chrt *sql_VMINFO_CHECK = "";

        char IP_ADDRESS[50] = {"\0"};
        char sql_command[buffersize] = {"\0"};
        //*buffer -> HOST QEMU PID
        //char *sql_insert = "INSERT INTO HOSTINFO (PID, QEMU_IP) VALUES (";
        char *sql_insert = "INSERT OR IGNORE INTO HOSTINFO (PID, QEMU_IP) VALUES (";
        char *sql_insert2 = ",'";
        char *sql_insert3 = "');";

        char PIDBUF[BUFSIZ];
        sprintf(PIDBUF, "%d", *buffer);

        inet_ntop(AF_INET, &clnt_addr.sin_addr, IP_ADDRESS, INET_ADDRSTRLEN);
        printf("connected host IP Address : %s | HOST QEMU PID : %d\n", IP_ADDRESS, *buffer);

        strcat(sql_command, sql_insert);
        strcat(sql_command, PIDBUF);
        strcat(sql_command, sql_insert2);
        strcat(sql_command, IP_ADDRESS);
        strcat(sql_command, sql_insert3);

        rc = sqlite3_exec(db, sql_command, 0, 0, &err_msg);

        if (rc != SQLITE_OK )
        {
          fprintf(stderr, "SQL error: %s\n", err_msg);
        
          sqlite3_free(err_msg);        
          sqlite3_close(db);
        
          return 1;
        } 

        char sql_command2[buffersize] = {"\0"};
        char *sql_get_rowid = "SELECT *, ROWID FROM HOSTINFO WHERE PID = ";
        char *sql_get_rowid2 = ";";

        strcat(sql_command2, sql_get_rowid);
        strcat(sql_command2, PIDBUF);
        strcat(sql_command2, sql_get_rowid2);

        rc = sqlite3_exec(db, sql_command2, callback, 0, &err_msg);

        if (rc != SQLITE_OK )
        {
          fprintf(stderr, "Failed to select data\n");
          fprintf(stderr, "SQL error: %s\n", err_msg);

          sqlite3_free(err_msg);
          sqlite3_close(db);
        
          return 1;
        } 

        uint64_t PCR_VALUE = 0x00;
        int PCR_VALUE_TMP = pcr_rowid - 48;

        PCR_VALUE = (uint64_t)PCR_VALUE_TMP;

        printf("Server return PCR VALUE:%lu , tmp = %d\n", PCR_VALUE, PCR_VALUE_TMP);

        //uint64_t *returnvalue = (uint64_t*) calloc(1, sizeof(uint64_t));
        //char *returnvalue = (char*) calloc(buffersize, sizeof(char));
        //sprintf(returnvalue, "%lu", PCR_VALUE ) ;
        //send(clnt_sock, returnvalue, buffersize, 0) ;
        send(clnt_sock, &PCR_VALUE, 1, 0) ;
        close(clnt_sock) ;
        close(serv_sock) ;
        return 0 ;
      } 
      else {
        close(clnt_sock);
      }

    }    
       
    close(serv_sock);
    return 0;
}
