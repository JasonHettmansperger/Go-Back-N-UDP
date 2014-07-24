Jason Hettmansperger
CS485 Networking
Project #2

Description: For this project I implemented a Go-Back-N UDP service utilizing c sockets.
I started for the suggested UDPEcho server and client codes. One would start with the
message that would be sending, this is called the sender: udpSender.c. It first reads in
arguments from the command line. It then takes these arguments and creates a socket. From
these arguments we are able to determine the size of the chunks, the number of segments
and the window size. It will then send segment chunks of data numbered up to the window size.
It will then wait for ACKs. When the window has an opening it will then send another segment.
If a packet is lost it will wait 3 seconds for the ACK, if one is not received it will go back
to the base and resend all segments. The receiver will constantly wait for incoming packets.
when it receives and initial packet is will start data collection. It will send ACKs for each
segment recieved.

Limitations:
Loss Rate parameter range: 0.0 -> 1.0;

To run:
Use any Unix system. Open two terminal tabs.

In the first tab, compile both the sender and receiver code with commands:
gcc -o sender udpSender.c
gcc -o receiver udpReciever.c

In the first tab, start the reciever:
./receiver 2734 12 .1  (set the lose rate to what you want it)

In the second tab, run the Sender:
./sender 127.0.0.1 2734 12 6

You will be able to watch the output in the terminal tabs in real time.
When the transmition is finished the message will be displayed in the reciever tab
and the receiver will wait for more senders.


