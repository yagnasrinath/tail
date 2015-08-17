#include<iostream>
#include<sstream>
#include<vector>
#include<string>
#include<queue>
#include<map>
#include<list>
#include<cstring>
#include<cstdlib>
#include<cstdio>
#include<climits>
#include<unistd.h>
#include<sys/types.h>
#include<sys/inotify.h>
#include<sys/epoll.h>
#include<sys/timerfd.h>

#define max_line_size 20000
#define BUF_LEN (10 * (sizeof(struct inotify_event) + NAME_MAX + 1))

using namespace std;

static int no_lines;
static int no_files;


struct File
{
	FILE* curr;
	char* filename;
	bool follow;
	File():curr(NULL),follow(0){};
	File(FILE* file,bool f,char*fname):curr(file),follow(f),filename(fname),dirname(NULL),basename(NULL){};
	char* dirname;
	char* basename;
};

list<File> vf;

void printUsage()
{
	cout<<
		"Usage: tail [OPTION]... [FILE]...\n"
		"Print the last 10 lines of each FILE to standard output.\n"
		"With more than one FILE, precede each with a header giving the file name.\n"
		"\n"
		"Mandatory arguments to long options are mandatory for short options too.\n"
		"  -f, --follow[={name|descriptor}]\n"
		"                           output appended data as the file grows;\n"
		"                             an absent option argument means 'descriptor'\n"
		"  -n                       output the last K lines, instead of the last 10;\n"
		"                             or use -n +K to output starting with the Kth\n"
		"  -h,  --help              display this help and exit\n"
		"\n"
		"With --follow (-f), tail defaults to following the file descriptor, which\n"
		"means that even if a tail'ed file is renamed, tail will continue to track\n"
		"its end.  This default behavior is not desirable when you really want to\n"
		"track the actual name of the file, not the file descriptor (e.g., log\n"
		"rotation).  Use --follow=name in that case.  That causes tail to track the\n"
		"named file in a way that accommodates renaming, removal and creation.\n"
		<<endl;
}

FILE *openFile(const char *filePath)
{
	FILE *file;
	file= fopen(filePath, "r");
	if(file == NULL)
	{
		cerr<<"Error opening file: "<<filePath<<endl;
		exit(errno);
	}
	return(file);
}

static void  displayInotifyEvent(struct inotify_event *i)
{
	printf("    wd =%2d; ", i->wd);
	if (i->cookie > 0)
		printf("cookie =%4d; ", i->cookie);

	printf("mask = ");
	if (i->mask & IN_ACCESS)        printf("IN_ACCESS ");
	if (i->mask & IN_ATTRIB)        printf("IN_ATTRIB ");
	if (i->mask & IN_CLOSE_NOWRITE) printf("IN_CLOSE_NOWRITE ");
	if (i->mask & IN_CLOSE_WRITE)   printf("IN_CLOSE_WRITE ");
	if (i->mask & IN_CREATE)        printf("IN_CREATE ");
	if (i->mask & IN_DELETE)        printf("IN_DELETE ");
	if (i->mask & IN_DELETE_SELF)   printf("IN_DELETE_SELF ");
	if (i->mask & IN_IGNORED)       printf("IN_IGNORED ");
	if (i->mask & IN_ISDIR)         printf("IN_ISDIR ");
	if (i->mask & IN_MODIFY)        printf("IN_MODIFY ");
	if (i->mask & IN_MOVE_SELF)     printf("IN_MOVE_SELF ");
	if (i->mask & IN_MOVED_FROM)    printf("IN_MOVED_FROM ");
	if (i->mask & IN_MOVED_TO)      printf("IN_MOVED_TO ");
	if (i->mask & IN_OPEN)          printf("IN_OPEN ");
	if (i->mask & IN_Q_OVERFLOW)    printf("IN_Q_OVERFLOW ");
	if (i->mask & IN_UNMOUNT)       printf("IN_UNMOUNT ");
	printf("\n");

	if (i->len > 0)
		printf("        name = %s\n", i->name);
}

void parseArgs(int argc,char* argv[],map<int,File> &m,int ifd)
{
	no_files=0;
	no_lines=10;
	int i=1;//skipping the file name
	while(i<argc)
	{
		if(strncmp((const char*)argv[i],"-n",2)==0)
		{
			stringstream ss;
			ss<<argv[i+1];
			if(ss.fail())
			{
				cerr<<"Bad argument. -n should be followed by a number."<<endl;
				exit(1);
			}		
			ss>>no_lines;
			i+=2;
		}
		else if ((strncmp((const char*)argv[i],"-f",2)==0)||(strncmp((const char*)argv[i],"--follow",8)==0))
		{
			//cout<<"setting events"<<endl;
			int wd = inotify_add_watch(ifd,argv[i+1],IN_MODIFY|IN_MOVE_SELF|IN_DELETE_SELF);
			File* f = new File(openFile(argv[i+1]),true,argv[i+1]);
			m[wd] = *f;
			no_files++;
			i+=2;
		}
		else 
		{
			int wd = inotify_add_watch(ifd,argv[i], IN_MODIFY);
			File* f = new File(openFile(argv[i]),false,argv[i]);
			m[wd] = *f;
			no_files++;
			i++;
		}
	}
}


void printBeginningTailFile(File& f)
{
	queue<string> lineq;
	char *c = NULL;
	FILE* ff=f.curr;
	size_t max_line_size_;
	while(lineq.size()!=no_lines)
	{
		if(getline(&c,&max_line_size_,ff)!=-1)
		{
			lineq.push(string(c));	
		}
		else
		{
			break;
		}
	}
	while(getline(&c,&max_line_size_,ff)!=-1)
	{
		lineq.pop();
		lineq.push(string(c));
	}
	while(lineq.size()!=0)
	{
		cout<<lineq.front();
		lineq.pop();
	}
}

void printBeginningTail(map<int,File>& m)
{
	if(m.size()==1)
	{
		printBeginningTailFile((m.begin())->second);
	}
	else
	{
		for(auto i=m.begin();i!=m.end();++i)
		{
			cout<<" ==> "<<(i->second).filename<<" <=="<<endl;
			printBeginningTailFile(i->second);
		}
	}
}

void printTail(map<int,File>& m,int ifd)
{
	char *p;
	struct inotify_event *event;
	ssize_t numRead;
	char buf[BUF_LEN] __attribute__ ((aligned(8)));
	numRead = read(ifd, buf, BUF_LEN);
	if (numRead == 0)
		perror("read() from inotify fd returned 0!");

	if (numRead == -1)
		perror("read");

	/* Process all of the events in buffer returned by read() */

	for (p = buf; p < buf + numRead; ) 
	{
		event = (struct inotify_event *) p;
		if ((event->mask & IN_MOVE_SELF)||(event->mask & IN_DELETE_SELF))
		{
			//displayInotifyEvent(event);
			File &f=m[event->wd];
			m.erase(event->wd);
			inotify_rm_watch( ifd, event->wd );
			vf.push_back(f);
			fclose(f.curr);
		}
		else if ( event->mask & IN_MODIFY ) 
		{
			//displayInotifyEvent(event);
			char *c = NULL;
			FILE* ff=m[event->wd].curr;
			size_t max_line_size_;
			bool firstTime=true;
			while(getline(&c,&max_line_size_,ff)!=-1)
			{
				if(firstTime && no_files!=1)
				{
					firstTime=false;	
					cout<<" ==> "<<m[event->wd].filename<<" <=="<<endl;
				}
				cout<<c;
			}
		}
		p += sizeof(struct inotify_event) + event->len;
	}
}

void checkDeletedFiles(map<int,File>& m,int ifd)
{
	auto it=vf.begin();
	while(it!=vf.end())
	{
		FILE *file;
		file= fopen(it->filename, "r");
		if(file == NULL)
		{
			++it;
			continue;
		}
		else
		{
			it->curr=file;
			int wd = inotify_add_watch(ifd,it->filename,IN_MODIFY|IN_MOVE_SELF|IN_DELETE_SELF);
			m[wd]= *it;
			vf.erase(it++);
		}
	}
}

int main(int argc, char* argv[])
{
	if(argc<=1||(strcmp((const char*)argv[1],"-h")==0)||(strcmp((const char*)argv[1],"--help")==0))
	{
		printUsage();
		exit(1);
	}

	int tfd = timerfd_create (CLOCK_MONOTONIC, TFD_NONBLOCK);
	if (tfd == -1) 
	{
		std::cerr << strerror (errno);
		return 1;
	}

	int flags = 0;
	itimerspec new_timer;
	// 1-second periodic timer
	new_timer.it_interval.tv_sec = 1;
	new_timer.it_interval.tv_nsec = 0;
	new_timer.it_value.tv_sec = 1;
	new_timer.it_value.tv_nsec = 0;
	itimerspec old_timer;
	int i = timerfd_settime (tfd, flags, &new_timer, &old_timer);
	if (i == -1) {
		std::cerr << strerror (errno) << "\n";
		close (tfd);
		return 1;
	}

	map<int,File> m;
	int ifd;
	ifd = inotify_init();
	if ( ifd < 0 ) 
	{
		perror( "inotify_init" );
	}
	parseArgs(argc,argv,m,ifd);
	printBeginningTail(m);
	int epfd;
	epfd = epoll_create (2); /* plan to watch ~100 fds */
	if (epfd < 0)
	{
		perror ("epoll_create"); 
	}

	//struct epoll_event *revents;
	struct epoll_event *events = (epoll_event*)malloc(2*sizeof(epoll_event)) ;
	int ret;

	events[0].data.fd = ifd; /* return the fd to us later */
	events[0].events = EPOLLIN | EPOLLOUT;

	events[1].events = EPOLLIN; // notification is a read event
	events[1].data.fd = tfd; // user data
	ret = epoll_ctl (epfd, EPOLL_CTL_ADD, ifd, events);
	if (ret)
	{
		perror ("epoll_ctl"); 
	}
	ret = epoll_ctl (epfd, EPOLL_CTL_ADD, tfd, events+1);
	if (ret)
	{
		perror ("epoll_ctl"); 
	}
	while(1)
	{
		int nr_events = epoll_wait (epfd, events, 2, -1);
		if (nr_events < 0) 
		{
			perror ("epoll_wait");
			return 1;
		}
		int i=0;
		while(i<nr_events)
		{
			if(events[i].data.fd==ifd)
			{
				printTail(m,ifd);
			}
			else if(events[i].data.fd==tfd)
			{
				checkDeletedFiles(m,ifd);
			}
			i++;
		}
	} 
}
