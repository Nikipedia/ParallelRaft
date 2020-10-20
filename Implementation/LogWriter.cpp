#include <memory>
#include <fstream>
#include <iostream>
#include <queue>
#include "IOPolling.cpp"
using namespace std;

std::queue<IOPolling::request> loggingBuffer;

// checks the input buffer for something to write and does formatting if required
/*void writeToFile(bool parallelRaft, std::queue<char> ackQueue) {
	while (true) {
		if (!loggingBuffer.empty()) {
			fstream myfile;
			myfile.open("log.txt");
			while (!loggingBuffer.empty()) {
				IOPolling::request r = loggingBuffer.front();
				loggingBuffer.pop();
				myfile << r.number << r.type << r.modifiedCell << r.data << "\n";
			}
			myfile.close();
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
}*/