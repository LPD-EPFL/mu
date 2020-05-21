#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <unistd.h>
#include <errno.h>

#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netdb.h>	//hostent

#include "../../exported/include/timers.h"

int KEY_SIZE = 0;
int VALUE_SIZE = 0;

// #define UDS

#ifdef MEDIAN_SAMPLE_SIZE
#  undef MEDIAN_SAMPLE_SIZE
#endif
#define MEDIAN_SAMPLE_SIZE 500000
// WARNING: Also change the define MEDIAN_SAMPLE_SIZE in redis

#ifdef UDS
#  define SV_SOCK_PATH "/path/to/redis.sock"
#endif

// #define REDIS_IP "127.0.0.1"
// #define REDIS_PORT 8888


TIMESTAMP_T write_timestamps[MEDIAN_SAMPLE_SIZE+1];
TIMESTAMP_T read_timestamps[MEDIAN_SAMPLE_SIZE+1];
uint64_t elapsed_times[MEDIAN_SAMPLE_SIZE];

static volatile int can_write[16] = {1};
static volatile int can_read[16];

void dump_times() {
    __sync_synchronize();

    // // Skip the first measurement, because it is too slow.
    // before_timestamps++;
    // after_timestamps++;
    // int SIZE = MEDIAN_SAMPLE_SIZE - 1;
    int SIZE = MEDIAN_SAMPLE_SIZE;


    for (int i = 0; i < SIZE; i++) {
      elapsed_times[i] = ELAPSED_NSEC(write_timestamps[i], read_timestamps[i]);
    }

    // char name[64];
    // snprintf(name, 64, "dump-redis-%d.txt", (int)getpid());
    const char *name = "redis-cli.txt";

    FILE *fptr = fopen(name, "w");
    if (fptr == NULL) {
        fprintf(stderr, "Could not open file\n");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < SIZE; i++) {
        fprintf(fptr, "%ld\n", elapsed_times[i]);
    }
    fprintf(fptr, "\n");
    fclose(fptr);
}

void mkrndstr_ipa(int length, char *randomString) {
    static char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

    if (length) {
        if (randomString) {
            int l = (int) (sizeof(charset) - 1);
            for (int n = 0; n < length; n++) {
                int key = rand() % l;
                randomString[n] = charset[key];
            }

            randomString[length] = '\0';
        }
    }
}

int stick_this_thread_to_core(int core_id) {
   int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
   if (core_id < 0 || core_id >= num_cores)
      return EINVAL;

   cpu_set_t cpuset;
   CPU_ZERO(&cpuset);
   CPU_SET(core_id, &cpuset);

   pthread_t current_thread = pthread_self();
   return pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
}

void *reader(void *ptr) {
    int sockfd = *(int *)ptr;
    int read_val = 0;

    char tmp[1024];

    for (int i = 0; i < MEDIAN_SAMPLE_SIZE; i++) {
        // Can I read?
        while(can_read[0] == read_val);
        read_val++;

        int ret = read(sockfd, tmp, 1024);
        // tmp[ret] = 0;
        // printf("%s", tmp);

        // You can write
        __sync_fetch_and_add(can_write, 1);

        (void) ret;
    }

    for (int i = 0; i < MEDIAN_SAMPLE_SIZE; i++) {
        // Can I read?
        while(can_read[0] == read_val);
        read_val++;

        int ret = read(sockfd, tmp, 1024);
        GET_TIMESTAMP(read_timestamps[i]);

        // tmp[ret] = 0;
        // printf("%s", tmp);

        // You can write
        __sync_fetch_and_add(can_write, 1);

        (void) ret;
    }

    __sync_synchronize();

    return NULL;
}

void *writer(void *ptr) {
    int sockfd = *(int *)ptr;
    int write_val = 0;
    char tmp[1024];

    char key[KEY_SIZE+1], value[VALUE_SIZE+1];

    srand(997);
    for (int i = 0; i < MEDIAN_SAMPLE_SIZE; i++) {
        mkrndstr_ipa(KEY_SIZE, key);
        mkrndstr_ipa(VALUE_SIZE, value);
        int len = sprintf(tmp, "*3\r\n$3\r\nSET\r\n$%d\r\n%s\r\n$%d\r\n%s\r\n",
            KEY_SIZE,
            key,
            VALUE_SIZE,
            value);

        // Can I write ?
        while(can_write[0] == write_val);
        write_val++;

        // You can read
        __sync_fetch_and_add(can_read, 1);

        // printf("Writing %s\n", tmp);
        int written = write(sockfd, tmp, len);
        if (written != len) {
            fprintf(stderr, "Failed to write everything\n");
        }

        // usleep(1000 * 50);
    }

    srand(997);
    for (int i = 0; i < MEDIAN_SAMPLE_SIZE; i++) {
        mkrndstr_ipa(KEY_SIZE, key);
        mkrndstr_ipa(VALUE_SIZE, value);

        int len = 0;
        if (i % 2 == 0) {
            len = sprintf(tmp, "*2\r\n$3\r\nGET\r\n$%d\r\n%s\r\n",
                KEY_SIZE,
                key);
        } else {
            len = sprintf(tmp, "*3\r\n$3\r\nSET\r\n$%d\r\n%s\r\n$%d\r\n%s\r\n",
                KEY_SIZE,
                key,
                VALUE_SIZE,
                value);
        }

        // printf("Writing %s\n", tmp);

        // Can I write ?
        while(can_write[0] == write_val);
        write_val++;

        // You can read
        __sync_fetch_and_add(can_read, 1);

        GET_TIMESTAMP(write_timestamps[i]);
        int written = write(sockfd, tmp, len);
        if (written != len) {
            fprintf(stderr, "Failed to write everything\n");
        }

        // usleep(1000 * 50);
    }

    __sync_synchronize();

    return NULL;
}

int hostname_to_ip(char * hostname , char* ip) {
	struct hostent *he;
	struct in_addr **addr_list;
	int i;

	if ( (he = gethostbyname( hostname ) ) == NULL)
	{
		// get the host info
		herror("gethostbyname");
		return 1;
	}

	addr_list = (struct in_addr **) he->h_addr_list;

	for(i = 0; addr_list[i] != NULL; i++)
	{
		//Return the first one;
		strcpy(ip , inet_ntoa(*addr_list[i]) );
		return 0;
	}

	return 1;
}

int main(int argc, char *argv[]) {
    KEY_SIZE = atoi(argv[1]);
    VALUE_SIZE = atoi(argv[2]);
    printf("Key+Value size = %d\n", KEY_SIZE + VALUE_SIZE);

    TIMESTAMP_INIT
    memset(write_timestamps, 0, sizeof(write_timestamps));
    memset(read_timestamps, 0, sizeof(read_timestamps));

    // Connect to redis
    int sockfd;

    #ifdef UDS
    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    #else
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    #endif

    if (sockfd == -1) {
        fprintf(stderr, "Socket creation failed\n");
        return EXIT_FAILURE;
    } else {
        printf("Socket successfully created\n");
    }

    #ifdef UDS
    struct sockaddr_un servaddr;
    #else
    struct sockaddr_in servaddr;
	char ip[100];
    #endif

    bzero(&servaddr, sizeof(servaddr));

    #ifdef UDS
    servaddr.sun_family = AF_UNIX;
    if (sizeof(servaddr.sun_path) - 1 < sizeof(SV_SOCK_PATH) ) {
        fprintf(stderr, "SV_SOCK_PATH is too long\n");
        return EXIT_FAILURE;
    }
    strncpy(servaddr.sun_path, SV_SOCK_PATH, sizeof(servaddr.sun_path) - 1);
    #else
    if (argc != 5) {
        fprintf(stderr, "Provide redis ip and redis port as arguments\n");
        exit(EXIT_FAILURE);
    }
    hostname_to_ip(argv[3], ip);
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(ip);
    servaddr.sin_port = htons(atoi(argv[4]));
    #endif

    // connect the client socket to server socket
    if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) != 0) {
        fprintf(stderr, "Connection with the server failed\n");
        return EXIT_FAILURE;
    } else {
        printf("Connected to Redis\n");
    }


    // Spawn the reader/writer threads
    pthread_t reader_thread, writer_thread;
    int ret;
    ret = pthread_create(&reader_thread, NULL, reader, &sockfd);
    if (ret) {
        fprintf(stderr, "Error creating reader thread\n");
        return EXIT_FAILURE;
    }

    ret = pthread_create(&writer_thread, NULL, writer, &sockfd);
    if (ret) {
        fprintf(stderr, "Error creating writer thread\n");
        return EXIT_FAILURE;
    }

    pthread_join(reader_thread, NULL);
    pthread_join(writer_thread, NULL);

    dump_times();


    return 0;
}
