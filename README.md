This project is an implementation of the protocol ParallelRaft described in an industry paper on PolarFS and ParallelRaft, with further explanations and results in the Seminar_Paper.pdf.

Its goal is to provide a way to parallelize the exchange of acknowledgements and commits between multiple redundant database servers by allowing out-of-order writing for non-colliding operations and increasing the throughput of the distributed database system.

The main method and all of the thread implementations are in Main.cpp

Request.cpp contains the class "Request" passed between the threads

BlockingCollection.h defines the thread-safe queues, taken from https://github.com/CodeExMachina/BlockingCollection on 14.10.2020
