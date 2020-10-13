#include <thread>
#include <memory>
#include <random>
#include "IOPolling.cpp"

//serves as the PolarSwitch that simulates lots of IO requests

	//packages are sent every 50ms 
	int packageMillis = 50;

	// this is the IO stack depth, the amount of IO requests at once
	float RequestsPerTick = 1;
	
	//acts as polarswitch sending multiple requests
	/*void threadedRequests(std::shared_ptr<IOPolling::request[]> leaderRequests)
	{
		srand(time(NULL));

		//infinite loop of request sending by writing into the shared buffer
		char totalCount = 0;
		while (true) {
			std::this_thread::sleep_for(std::chrono::milliseconds(packageMillis));
			for (int m = 0; m < RequestsPerTick; m++) {
				IOPolling::request r;
				// requests are numbered from 1 to 255
				r.number = totalCount;
				// r for read, w for write, could contain more for e.g. new insertion of data
				r.type = 'w';
				// data block "index" to edit
				r.modifiedCell = (char)(rand() % 255 + 1);
				// small letter to write into the data blocks (assuming chars or strings as data)
				r.data[0] = (char)(rand() % 26 + 65);
				*(leaderRequests.get() + totalCount) = r;
				std::cout << "request written\n";
				totalCount = (totalCount + 1) % 32;
			}
		}
	}*/

	void threadedRequests(std::shared_ptr<vector<IOPolling::request>> leaderRequests)
	{
		srand(time(NULL));
		std::cout << &leaderRequests;
		//infinite loop of request sending by writing into the shared buffer
		char totalCount = 0;
		while (true) {
			std::this_thread::sleep_for(std::chrono::milliseconds(packageMillis));
			for (int m = 0; m < RequestsPerTick; m++) {
				IOPolling::request r;
				// requests are numbered from 1 to 255
				r.number = totalCount;
				// r for read, w for write, could contain more for e.g. new insertion of data
				r.type = 'w';
				// data block "index" to edit
				r.modifiedCell = (char)(rand() % 255 + 1);
				// small letter to write into the data blocks (assuming chars or strings as data)
				r.data[0] = (char)(rand() % 26 + 65);
				leaderRequests->push_back(r);
				std::cout << "request written\n";
				totalCount = (totalCount + 1) % 32;
			}
		}
	}

	int main() {
		IOPolling leader;
		std::shared_ptr<vector<IOPolling::request>> requestPointer;
		std::shared_ptr<IOPolling::request[]> writingPointer(new IOPolling::request[32]);
		std::vector<IOPolling::request> queuePointer;
		std::thread inputThread(threadedRequests, requestPointer);
		std::thread leaderReadThread(&IOPolling::listener, &leader, requestPointer, writingPointer, queuePointer);
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		inputThread.detach();
		inputThread.~thread();
		leaderReadThread.detach();
		leaderReadThread.~thread();
		IOPolling::request r = *(writingPointer.get());

	}

	