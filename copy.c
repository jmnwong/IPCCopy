/*
Author: Justin Wong
*/

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <unistd.h>         
#include <sys/wait.h>       
#include <sys/ipc.h>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#define TRUE 1
#define S_MODE S_IRUSR|S_IWUSR

typedef struct {
	char byte;
	int offset;
} element;

typedef struct {
	sem_t semBuf;
	sem_t *semFile, *semCopy, *semWtrLog, *semRdrLog;
	int bufSize;
	int in;
	int out;
	int fileSize;
	int rc;
	int wc;
	element buffer[];
} item;

void *reader(int n, int fdFile, int fdRdrLog, item *p, int size);
void *writer(int n, int fdCopy, int fdWtrLog, item *p, int size);

/* Reader Function */
void *reader(int n, int fdFile, int fdRdrLog, item *p, int size) {
	srand(getpid());
	struct timespec tim;
	tim.tv_sec = 0;
	tim.tv_nsec = (double) 10000000 * rand() / RAND_MAX;
	int offset = 0;
	int index;
	char buf[50];
	char bufR[1];

	while(p->rc < size && ((p->in+1)%p->bufSize != p->out)) {
		/* ---------------- Read from File ---------------- */
		if(nanosleep(&tim, NULL) == -1){perror("nanosleep failed");exit(1);}
		if(sem_wait(p->semFile) == -1) {perror("failed to lock semFile");exit(1);}
		/* ==== Critical Section ====*/
		offset = lseek(fdFile, 0, SEEK_CUR);
		if(offset < size && p->buffer[p->in].byte == '\0') {
			read(fdFile, bufR, 1);
		}
		/*==== Remainder Section ====*/
		if(sem_post(p->semFile) == -1) {perror("failed to unlock semFile");exit(1);}


		/* ---------------- Write to Buffer ---------------- */
		if(nanosleep(&tim, NULL) == -1){perror("nanosleep failed");exit(1);}
		if(sem_wait(&p->semBuf) == -1) {perror("failed to lock semBuf");exit(1);}
		/* ==== Critical Section ====*/
		if(offset < size && p->buffer[p->in].byte == '\0') {
			p->buffer[p->in].byte = *bufR;
			p->buffer[p->in].offset = offset;
			index = p->in;
			p->in = (p->in+1) % p->bufSize;
			p->rc++;
		}
		/*==== Remainder Section ====*/
		if(sem_post(&p->semBuf) == -1) {perror("failed to unlock semBuf");exit(1);}


		/* ---------------- Write to Log ---------------- */
		//if(nanosleep(&tim, NULL) == -1){perror("nanosleep failed");exit(1);}
		if(sem_wait(p->semRdrLog) == -1) {perror("failed to lock semRdrLog");exit(1);}
		/* ==== Critical Section ====*/
		if(offset < size && p->buffer[p->in].byte == '\0') {
			sprintf(buf, "%d %d %d\n", n, offset, index);
			write(fdRdrLog, buf, strlen(buf)); 
		}
		/*==== Remainder Section ====*/
		if(sem_post(p->semRdrLog) == -1) {perror("failed to unlock semRdrLog");exit(1);}

	}
	return 0;
}

/* Writer Function */
void *writer(int n, int fdCopy, int fdWtrLog, item *p, int size) {
	srand(getpid());
	struct timespec tim;
	tim.tv_sec = 0;
	tim.tv_nsec = (double) 10000000 * rand() / RAND_MAX;
	int offset = 0;
	int index;
	char buf[50], local[1];


	while((p->wc < size)) {
		while(p->out < p->in && p->wc < size)
			;
		/* ---------------- Read from Buffer ---------------- */
		if(nanosleep(&tim, NULL) == -1){perror("nanosleep failed");exit(1);}
		if(sem_wait(&p->semBuf) == -1) {perror("failed to lock semBuf");exit(1);}  
		/* ==== Critical Section ====*/
		local[0] = p->buffer[p->out].byte;
		if (local[0] != '\0') {
		offset = p->buffer[p->out].offset;
			p->buffer[p->out].byte = '\0';		//nullifying
			index = p->out;
			p->out = (p->out + 1) % p->bufSize;
		}
		/*==== Remainder Section ====*/
		if(sem_post(&p->semBuf) == -1) {perror("failed to unlock semBuf");exit(1);}
	

		/* ---------------- Write to Copy ---------------- */
		if(nanosleep(&tim, NULL) == -1){perror("nanosleep failed");exit(1);}
		if(sem_wait(p->semCopy) == -1) {perror("failed to lock semCopy");exit(1);}
		/* ==== Critical Section ====*/
		if (local[0] != '\0') {
			lseek(fdCopy, offset, SEEK_SET);
			write(fdCopy, local, 1);
			p->wc++;
		}
		/*==== Remainder Section ====*/
		if(sem_post(p->semCopy) == -1) {perror("failed to unlock semCopy");exit(1);}


		/* ---------------- Write to Log ---------------- */
		//if(nanosleep(&tim, NULL) == -1){perror("nanosleep failed");exit(1);}
		if(sem_wait(p->semWtrLog) == -1) {perror("failed to lock semWtrLog");exit(1);}
		/* ==== Critical Section ====*/
		if (local[0] != '\0') {
			sprintf(buf, "%d %d %d\n", n, offset, index);
			write(fdWtrLog, buf, strlen(buf)); 
		}
		/*==== Remainder Section ====*/
		if(sem_post(p->semWtrLog) == -1) {perror("failed to unlock semWtrLog");exit(1);}
	}

	return 0;
}

int main(int argc, char *argv[]) {
	if(argc != 8) {
        perror("USAGE:./copy <nReaders> <nWriters> <file> <copy> <bufsize> <readerLog> <writerLog>\n");
        exit(1);
    }
	int nReaders = atoi(argv[1]);
	int nWriters = atoi(argv[2]);
	char *file = argv[3];
	char *copy = argv[4];
	int bufSize = atoi(argv[5]);
	char *rdrLog = argv[6];
	char *wtrLog = argv[7];

	char *nameFile = "semFile";
	char *nameCopy = "semCopy";
	char *namedWtr = "semWtr";
	char *namedRdr = "semRdr";

	/* ---------- Create File Descriptors ------------- */
	int fdFile = open(file, O_RDWR, S_MODE);
	if(fdFile==-1) { perror("error on fdFile"); exit(1); }
	int fdCopy = open(copy, O_CREAT | O_RDWR | O_TRUNC, S_MODE);
	if(fdCopy==-1) { perror("error on fdCopy"); exit(1); }
	int fdWtrLog = open(wtrLog, O_CREAT | O_RDWR | O_TRUNC, S_MODE);
	if(fdWtrLog==-1) { perror("error on fdWtrLog"); exit(1); }
	int fdRdrLog = open(rdrLog, O_CREAT | O_RDWR | O_TRUNC, S_MODE);
	if(fdRdrLog==-1) { perror("error on fdRdrLog"); exit(1); }

	/* ---------- Create shared memory ---------------- */
	item *p;
    	int shmid = shmget(IPC_PRIVATE, sizeof(item), S_IRUSR | S_IWUSR);	//allocate shm
	if(shmid == -1) {perror("shmget: shmget failed");exit(1);}
	if((p = (item *)shmat(shmid, NULL, 0)) == (item *) -1) { perror("shmat error"); exit(1); }
	p->in = 0;
	p->out = 0;
	p->bufSize = bufSize;
	p->fileSize = lseek(fdFile, 0, SEEK_END);
	lseek(fdFile, 0, SEEK_SET);
	p->rc = 0;
	p->wc = 0;

	/* ---------- Create unnamed semaphores --------------------*/
	if(sem_init(&p->semBuf, 1, 1) == -1) {perror("failed to init semBuf");exit(1);}

	/* ---------- Create named semaphores --------------------*/
	if((p->semFile = sem_open(nameFile, O_CREAT|O_EXCL, S_MODE, 1)) == (void *)-1) {perror("failed to open semFile");exit(1);}
	if((p->semCopy = sem_open(nameCopy, O_CREAT|O_EXCL, S_MODE, 1)) == (void *)-1) {perror("failed to open semCopy");exit(1);}
	if((p->semWtrLog = sem_open(namedWtr, O_CREAT|O_EXCL, S_MODE, 1)) == (void *)-1) {perror("failed to open semWtrLog");exit(1);}
	if((p->semRdrLog = sem_open(namedRdr, O_CREAT|O_EXCL, S_MODE, 1)) == (void *)-1) {perror("failed to open semRdrLog");exit(1);}

	/* ----------- Create child processes -------------- */
	int x,n;
	for(n = 1; n <= nReaders + nWriters; n++) {
		switch(x=fork()) {
			case -1: {
				perror("fork error");
				exit(1);
			}
			case 0: {
				if(n<=nReaders) {
					reader(n, fdFile, fdRdrLog, p, p->fileSize-1);
				} else {
					writer(n, fdCopy, fdWtrLog, p, p->fileSize-1);
				}
				exit(0);
			}
		}
	}

	/*--------- Waits for all children to finish ---------*/
	while(wait(NULL) != -1) {
    		if(errno == ECHILD) {
        		printf("all children have not ended\n");
			break;
    		}
	}
	
	/*--------- Add 'DONE' to end of file ------------*/
	write(fdCopy, "DONE\n", 5);

	/*---------- Close file descriptors ------------*/
	close(fdFile);
	close(fdCopy);
	close(fdWtrLog);
	close(fdRdrLog);

	/* --------- Remove shared memory and destroy semaphores ----------- */
	if(sem_destroy(&p->semBuf) == -1) {perror("can't destroy semBuf");exit(1);}
	if(sem_close(p->semFile) == -1) {perror("can't destroy semFile");exit(1);}
	if(sem_close(p->semCopy) == -1) {perror("can't destroy semCopy");exit(1);}
	if(sem_close(p->semWtrLog) == -1) {perror("can't destroy semWtrLog");exit(1);}
	if(sem_close(p->semRdrLog) == -1) {perror("can't destroy semRdrLog");exit(1);}
	if(sem_unlink(nameFile) == -1) {perror("can't unlink semFile");exit(1);}
	if(sem_unlink(nameCopy) == -1) {perror("can't  unlink semCopy");exit(1);}
	if(sem_unlink(namedWtr) == -1) {perror("can't  unlink semWtrLog");exit(1);}
	if(sem_unlink(namedRdr) == -1) {perror("can't  unlink semRdrLog");exit(1);}
    	if(shmdt(p)==-1) { perror("shmdt error"); exit(1); }
	if(shmctl(shmid, IPC_RMID, NULL)==-1) { perror("shmctl error"); exit(1); }
	
	return 0;
}
