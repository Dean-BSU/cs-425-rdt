# Project 2 - Reliable Data Transfer

- Name: Dean Cunningham
- Email: deancunningham@u.boisestate.edu
- Class: CS425-001

This project focuses on Reliable Data Transfer. We are implementing this over an unreliable protocol. The principles of reliable data transfer we are using are based on Go-Back-N protocol, and includes a checksum, sequence numbers, cumulative acknowledgments, a timer, and a window size.

## Known Bugs or Issues

Testing random seeds with a 20% drop/corrupt/duplicate rate exits with timeout about
30% of the time. I believe this is expected due to there being a 10 timeout limit and
20% being a high error rate.

## Experience

This project gave me less of a hard time than P1, partially thanks to the textbook chapter being easier to follow (albiet still very dense). One thing that was a bit confusing was packet structure, so I started the project with scaffholding for the Go-Back-N protocol. Once I split the design into three layers, I implemented the packet and filled in the rest of gbn and the socket layer. 

Writing tests and debugging was the majority of time spent on the project. Writing a project which runs through a python file relay was a bit confusing, but tests clarified what exactly I was supposed to be doing. I got GCC coverage to 100% last, which caught last-minute errors.

## Design

`main.c` parses command line arguments for errors and and passes them to `rdt.c`.
`rdt.c` parses command line arguments with getopt for sender/receiver.

The protocol logic is split into three layers, this is done so that protocol logic does not touch a socket, a clock, or a file. 

Layer 1 is packets `packet.c`. This layer just has pure functions, encodes packet into buffer, decodes buffer into packet. 

Layer 2 is Go-Back-N state machine logic `gbn.c`. This contains the sender and receiver machines, each a struct plus the functions that feed it events.

Layer 3 is the socket `io.c`. The only layer that is aware of the socket, relay, clock, or file.

## Analysis

| Scenario | Test 1 | Test 2 | Test 3 | Avg |
|---|---:|---:|---:|---:|
| 1 Window 0 Loss | 105.089847 s, 9.744 KiB/s | 105.043694 s, 9.748 KiB/s | 105.037403 s, 9.749 KiB/s | **105.056981 s, 9.747 KiB/s** |
| 16 Window 0 Loss | 6.794327 s, 150.714 KiB/s | 6.689488 s, 153.076 KiB/s | 6.682659 s, 153.232 KiB/s | **6.722158 s, 152.341 KiB/s** |
| 1 Window 0.05 Loss | 130.886912 s, 7.824 KiB/s | 130.949677 s, 7.820 KiB/s | 129.394535 s, 7.914 KiB/s | **130.410375 s, 7.852 KiB/s** |
| 16 Window 0.05 Loss | 31.379098 s, 32.633 KiB/s | 29.033622 s, 35.269 KiB/s | 29.680176 s, 34.501 KiB/s | **30.030965 s, 34.135 KiB/s** |

The round trip takes 100ms, and 1025 packets were transfered. For 1 window under perfect conditions, this is expected to have taken 102.5 s. The extra time is likely computational from my program.

16 Windows is about 15.6x faster than the 1 window scenario. With a larger window size, the sender can send more packets without waiting for an ACK from the receiver, saving the transmission time of 15 50ms ACKs assuming no loss. More processing can occur before the window moves.

The amount of time saved with 0.05 Loss is a lot less, because as window size increases the cost of retransmitting packets upon timeout increases. On each timeout, each packet within the window needs to be resubmitted.