#include <stdio.h>      /* for printf() and fprintf() */
#include <sys/socket.h> /* for socket() and bind() */
#include <arpa/inet.h>  /* for sockaddr_in and inet_ntoa() */
#include <stdlib.h>     /* for atoi() and exit() */
#include <string.h>     /* for memset() */
#include <unistd.h>     /* for close() */

#define ECHOMAX 50000     /* Longest string to echo */

void DieWithError(char *errorMessage);                      /* External error handling function */
struct ACKPacket createACKPacket (int ack_type, int base);  /* Creates a ACK packet to be sent */
int is_lost(float loss_rate);                               /* Given function for lose rate */


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

int main(int argc, char *argv[])
{
    int sock;                        /* Socket */
    struct sockaddr_in echoServAddr; /* Local address */
    struct sockaddr_in echoClntAddr; /* Client address */
    unsigned int cliAddrLen;         /* Length of incoming message */
    char echoBuffer[ECHOMAX];        /* Buffer for echo string */
    unsigned short echoServPort;     /* Server port */
    int recvMsgSize;                 /* Size of received message */
    int chunkSize;                   /* Size of chunks to send */
    float loss_rate = 0;             /* lose rate range from 0.0 -> 1.0, initialized to zero b/c lose rate is optional */

    /* random generator seeding */
    srand48(2345);

    if (argc < 3 || argc > 4)         /* Test for correct number of parameters */
    {
        fprintf(stderr,"Usage:  %s <UDP SERVER PORT> <CHUNK SIZE> [<LOSS RATE>]\n", argv[0]);
        exit(1);
    }

    /* Set arguments to appropriate values */
    echoServPort = atoi(argv[1]);  /* First arg:  local port */
    chunkSize = atoi(argv[2]);  /* Second arg:  size of chunks */

    /* loss rate is option, thus muct check for 4th argv */
    if(argc == 4){
        loss_rate = atof(argv[3]);

    }

    /* Create socket for sending/receiving datagrams */
    if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
        DieWithError("socket() failed");

    /* Construct local address structure */
    memset(&echoServAddr, 0, sizeof(echoServAddr));   /* Zero out structure */
    echoServAddr.sin_family = AF_INET;                /* Internet address family */
    echoServAddr.sin_addr.s_addr = htonl(INADDR_ANY); /* Any incoming interface */
    echoServAddr.sin_port = htons(echoServPort);      /* Local port */

    /* Bind to the local address */
    if (bind(sock, (struct sockaddr *) &echoServAddr, sizeof(echoServAddr)) < 0)
        DieWithError("bind() failed");

    /* Initialize variables to their needed start values */
    char dataBuffer[8192];
    int base = -2;
    int seqNumber = 0;

    for (;;) /* Run forever */
    {
        /* Set the size of the in-out parameter */
        cliAddrLen = sizeof(echoClntAddr);

        /* struct for incoming datapacket */
        struct segmentPacket dataPacket;

        /* struct for outgoing ACK */
        struct ACKPacket ack;

        /* Block until receive message from a client */
        if ((recvMsgSize = recvfrom(sock, &dataPacket, sizeof(dataPacket), 0,
            (struct sockaddr *) &echoClntAddr, &cliAddrLen)) < 0)
            DieWithError("recvfrom() failed");

            seqNumber = dataPacket.seq_no;

        /* Add random packet lose, if lost dont process */
        if(!is_lost(loss_rate)){
            /* If seq is zero start new data collection */
            if(dataPacket.seq_no == 0 && dataPacket.type == 1)
            {
                printf("Recieved Initial Packet from %s\n", inet_ntoa(echoClntAddr.sin_addr));
                memset(dataBuffer, 0, sizeof(dataBuffer));
                strcpy(dataBuffer, dataPacket.data);
                base = 0;
                ack = createACKPacket(2, base);
            } else if (dataPacket.seq_no == base + 1) /* if base+1 then its a subsequent in order packet */
            {
                /* then concatinate the data sent to the recieving buffer */
                printf("Recieved  Subseqent Packet #%d\n", dataPacket.seq_no);
                strcat(dataBuffer, dataPacket.data);
                base = dataPacket.seq_no;
                ack = createACKPacket(2, base);
            } else if (dataPacket.type == 1 && dataPacket.seq_no != base + 1)
            {
                /* if recieved out of sync packet, send ACK with old base */
                printf("Recieved Out of Sync Packet #%d\n", dataPacket.seq_no);
                /* Resend ACK with old base */
                ack = createACKPacket(2, base);
            }

            /* type 4 means that the packet recieved is a termination packet */
            if(dataPacket.type == 4 && seqNumber == base ){
                base = -1;
                /* create an ACK packet with terminal type 8 */
                ack = createACKPacket(8, base);
            }

            /* Send ACK for Packet Recieved */
            if(base >= 0){
                printf("------------------------------------  Sending ACK #%d\n", base);
                if (sendto(sock, &ack, sizeof(ack), 0,
                     (struct sockaddr *) &echoClntAddr, sizeof(echoClntAddr)) != sizeof(ack))
                    DieWithError("sendto() sent a different number of bytes than expected");
            } else if (base == -1) {
                printf("Recieved Teardown Packet\n");
                printf("Sending Terminal ACK\n", base);
                if (sendto(sock, &ack, sizeof(ack), 0,
                     (struct sockaddr *) &echoClntAddr, sizeof(echoClntAddr)) != sizeof(ack))
                    DieWithError("sendto() sent a different number of bytes than expected");
            }

            /* if data packet is terminal packet, display and clear the recieved message */
            if(dataPacket.type == 4 && base == -1){
                printf("\n MESSAGE RECIEVED\n %s\n\n", dataBuffer);
                memset(dataBuffer, 0, sizeof(dataBuffer));
            }

        } else {
            printf("SIMULATED LOSE\n");
        }

    }
    /* NOT REACHED */
}

/* If this is called there was an error, Printed and then the process exits */
void DieWithError(char *errorMessage)
{
    perror(errorMessage);
    exit(1);
}

/* Creates and returns a segment Packet */
struct ACKPacket createACKPacket (int ack_type, int base){
        struct ACKPacket ack;
        ack.type = ack_type;
        ack.ack_no = base;
        return ack;
}

/* The given lost rate function */
int is_lost(float loss_rate) {
    double rv;
    rv = drand48();
    if (rv < loss_rate)
    {
        return(1);
    } else {
        return(0);
    }
}