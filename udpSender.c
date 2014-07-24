#include <stdio.h>      /* for printf() and fprintf() */
#include <sys/socket.h> /* for socket(), connect(), sendto(), and recvfrom() */
#include <arpa/inet.h>  /* for sockaddr_in and inet_addr() */
#include <stdlib.h>     /* for atoi() and exit() */
#include <string.h>     /* for memset() */
#include <unistd.h>     /* for close() and alarm() */
#include <errno.h>      /* for errno and EINTR */
#include <signal.h>     /* for sigaction() */

#define DATALIMIT       511     /* Longest string to echo */
#define TIMEOUT_SECS    3       /* Seconds between retransmits */
#define DATAMSG         1       /* Message of Data type */
#define TEARDOWNMSG     4       /* Message of Tear Down type */
#define MAXTRIES        16      /* Number of times it can resend */


char* dataBuffer = "A man stood upon a railroad bridge in northern Alabama, looking down into the swift water twenty feet below. The man's hands were behind his back, the wrists bound with a cord. A rope closely encircled his neck. It was attached to a stout cross-timber above his head and the slack fell to the level of his knees. Some loose boards laid upon the sleepers supporting the metals of the railway supplied a footing for him and his executioners--two private soldiers of the Federal army, directed by a sergeant who in civil life may have been a deputy sheriff. At a short remove upon the same temporary platform was an officer in the uniform of his rank, armed. He was a captain. A sentinel at each end of the bridge stood with his rifle in the position known as support, that is to say, vertical in front of the left shoulder, the hammer resting on the forearm thrown straight across the chest--a formal and unnatural position, enforcing an erect carriage of the body. It did not appear to be the duty of these two men to know what was occurring at the center of the bridge; they merely blockaded the two ends of the foot planking that traversed it. Beyond one of the sentinels nobody was in sight; the railroad ran straight away into a forest for a hundred yards, then, curving, was lost to view. Doubtless there was an outpost farther along. The other bank of the stream was open ground--a gentle acclivity topped with a stockade of vertical tree trunks, loopholed for rifles, with a single embrasure through which protruded the muzzle of a brass cannon commanding the bridge. Midway of the slope between the bridge and fort were the spectators--a single company of infantry in line, at parade rest, the butts of the rifles on the ground, the barrels inclining slightly backward against the right shoulder, the hands crossed upon the stock. A lieutenant stood at the right of the line, the point of his sword upon the ground, his left hand resting upon his right. Excepting the group of four at the center of the bridge, not a man moved. The company faced the bridge, staring stonily, motionless. The sentinels, facing the banks of the stream, might have been statues to adorn the bridge. The captain stood with folded arms, silent, observing the work of his subordinates, but making no sign. Death is a dignitary who when he comes announced is to be received with formal manifestations of respect, even by those most familiar with him. In the code of military etiquette silence and fixity are forms of deference.";

int tries=0;   /* Count of times sent - GLOBAL for signal-handler access */

void DieWithError(char *errorMessage);   /* Error handling function */
void CatchAlarm(int ignored);            /* Handler for SIGALRM */

/* Structure of the segmentPacket for recieving from sender */
struct segmentPacket {
    int type;
    int seq_no;
    int length;
    char data[512];
};

/* Structure of the ACK Packet that is returned to sender */
struct ACKPacket {
    int type;
    int ack_no;
};

/* Function headers used to create packets */
struct segmentPacket createDataPacket (int seq_no, int length, char* data);
struct segmentPacket createTerminalPacket (int seq_no, int length);


int main(int argc, char *argv[])
{
    int sock;                               /* Socket descriptor */
    struct sockaddr_in recievingServer;     /* Receiving server address */
    struct sockaddr_in fromAddr;            /* Source address of echo */
    unsigned short recievingServerPort;     /* Receiving server port */
    unsigned int fromSize;                  /* In-out of address size for recvfrom() */
    struct sigaction myAction;              /* For setting signal handler */
    char *servIP;                           /* IP address of server */
    int respStringLen;                      /* Size of received datagram */

    /* Variables used to control the flow of data */
    int chunkSize;                          /* number of bits of data sent with each segment */
    int windowSize;                         /* Number of segments in limbo */
    int tries =0;                           /* Number of tries itterator */

    if (argc != 5)    /* Test for correct number of arguments */
    {
        fprintf(stderr,"Usage: %s <Server IP> <Server Port> <Chunk Size> <Window Size>\n You gave %d Arguments\n", argv[0], argc);
        exit(1);
    }

    /* Set the corresponding agrs to their respective variables */
    servIP = argv[1];                          /* First arg:  server IP address (dotted quad) */
    recievingServerPort = atoi(argv[2]);       /* Second arg: string to echo */
    chunkSize = atoi(argv[3]);                 /* Third arg: Size of chunks being sent */
    windowSize = atoi(argv[4]);                /* Fourth arg: Size of Segment window */

    /* Print out of initial connection data */
    printf("Attempting to Send to: \n");
    printf("IP:          %s\n", servIP);
    printf("Port:        %d\n", recievingServerPort);

    /* Check to make sure appropriate chunk size was entered */
    if(chunkSize > DATALIMIT){
        fprintf(stderr, "Error: Chunk Size is too large. Must be < 512 bytes\n");
        exit(1);
    }

    /* Create a best-effort datagram socket using UDP */
    if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
        DieWithError("socket() failed");


    /* Construct the server address structure */
    memset(&recievingServer, 0, sizeof(recievingServer));    /* Zero out structure */
    recievingServer.sin_family = AF_INET;
    recievingServer.sin_addr.s_addr = inet_addr(servIP);  /* Server IP address */
    recievingServer.sin_port = htons(recievingServerPort);       /* Server port */


    /* Calculate number of Segments */
    int dataBufferSize = strlen(dataBuffer);
    int numOfSegments = dataBufferSize / chunkSize;
    /* Might have left overs */
    if (strlen(dataBuffer) % chunkSize > 0){
        numOfSegments++;
    }

    /* Set seqNumber, base and ACK to 0 */
    int base = -1;           /* highest segments AKC recieved */
    int seqNumber = 0;      /* highest segment sent, reset by base */
    int dataLength = 0;     /* Chunk size */

    /* Print out of data stats */
    printf("Window Size: %d\n", windowSize);
    printf("Chunk Size:  %d\n", chunkSize);
    printf("Chunks:      %d\n", numOfSegments);

    /* Set signal handler for alarm signal */
    myAction.sa_handler = CatchAlarm;
    if (sigemptyset(&myAction.sa_mask) < 0) /* block everything in handler */
        DieWithError("sigfillset() failed");
    myAction.sa_flags = 0;

    if (sigaction(SIGALRM, &myAction, 0) < 0)
        DieWithError("sigaction() failed for SIGALRM");

    /* bool used to keep the program running until teardown ack has been recieved */
    int noTearDownACK = 1;
    while(noTearDownACK){

        /* Send chunks from base up to window size */
        while(seqNumber <= numOfSegments && (seqNumber - base) <= windowSize){
            struct segmentPacket dataPacket;

            if(seqNumber == numOfSegments){
                /* Reached end, create terminal packet */
                dataPacket = createTerminalPacket(seqNumber, 0);
                printf("Sending Terminal Packet\n");
            } else {
                /* Create Data Packet Struct */
                char seg_data[chunkSize];
                strncpy(seg_data, (dataBuffer + seqNumber*chunkSize), chunkSize);

                dataPacket = createDataPacket(seqNumber, dataLength, seg_data);
                printf("Sending Packet: %d\n", seqNumber);
                //printf("Chunk: %s\n", seg_data);
            }

            /* Send the constructed data packet to the receiver */
            if (sendto(sock, &dataPacket, sizeof(dataPacket), 0,
                 (struct sockaddr *) &recievingServer, sizeof(recievingServer)) != sizeof(dataPacket))
                DieWithError("sendto() sent a different number of bytes than expected");
            seqNumber++;
        }


        /* Set Timer */
        alarm(TIMEOUT_SECS);        /* Set the timeout */

        /* IF window is full alert that it is waiting */
        printf("Window full: waiting for ACKs\n");

        /* Listen for ACKs, get highest ACK, reset base */
        struct ACKPacket ack;
        while ((respStringLen = recvfrom(sock, &ack, sizeof(ack), 0,
               (struct sockaddr *) &fromAddr, &fromSize)) < 0)
        {
            if (errno == EINTR)     /* Alarm went off  */
            {
                /* reset the seqNumber back to one ahead of the last recieved ACK */
                seqNumber = base + 1;

                printf("Timeout: Resending\n");
                if(tries >= 10){
                    printf("Tries exceeded: Closing\n");
                    exit(1);
                }
                else {
                    alarm(0);

                    while(seqNumber <= numOfSegments && (seqNumber - base) <= windowSize){
                        struct segmentPacket dataPacket;

                        if(seqNumber == numOfSegments){
                            /* Reached end, create terminal packet */
                            dataPacket = createTerminalPacket(seqNumber, 0);
                            printf("Sending Terminal Packet\n");
                        } else {
                            /* Create Data Packet Struct */
                            char seg_data[chunkSize];
                            strncpy(seg_data, (dataBuffer + seqNumber*chunkSize), chunkSize);

                            dataPacket = createDataPacket(seqNumber, dataLength, seg_data);
                            printf("Sending Packet: %d\n", seqNumber);
                            //printf("Chunk: %s\n", seg_data);
                        }

                        /* Send the constructed data packet to the receiver */
                        if (sendto(sock, &dataPacket, sizeof(dataPacket), 0,
                             (struct sockaddr *) &recievingServer, sizeof(recievingServer)) != sizeof(dataPacket))
                            DieWithError("sendto() sent a different number of bytes than expected");
                        seqNumber++;
                    }
                    alarm(TIMEOUT_SECS);
                }
                tries++;
            }
            else
            {
                DieWithError("recvfrom() failed");
            }
        }

        /* 8 is teardown ack */
        if(ack.type != 8){
            printf("----------------------- Recieved ACK: %d\n", ack.ack_no);
            if(ack.ack_no > base){
                /* Advances the sending, reset tries */
                base = ack.ack_no;
            }
        } else {
            printf("Recieved Terminal ACK\n");
            noTearDownACK = 0;
        }

        /* recvfrom() got something --  cancel the timeout, reset tries */
        alarm(0);
        tries = 0;

    }


    close(sock);
    exit(0);
}

void CatchAlarm(int ignored)     /* Handler for SIGALRM */
{
    //printf("In Alarm\n");
}

/* If this is called there was an error, Printed and then the process exits */
void DieWithError(char *errorMessage)
{
    perror(errorMessage);
    exit(1);
}

/* Creates and returns a segment Packet */
struct segmentPacket createDataPacket (int seq_no, int length, char* data){

    struct segmentPacket pkt;

    pkt.type = 1;
    pkt.seq_no = seq_no;
    pkt.length = length;
    memset(pkt.data, 0, sizeof(pkt.data));
    strcpy(pkt.data, data);

    return pkt;
}

/* Creates and returns a terminal segment Packet */
struct segmentPacket createTerminalPacket (int seq_no, int length){

    struct segmentPacket pkt;

    pkt.type = 4;
    pkt.seq_no = seq_no;
    pkt.length = 0;
    memset(pkt.data, 0, sizeof(pkt.data));

    return pkt;
}
