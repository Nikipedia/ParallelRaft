#include <thread>
#include <memory>
#include <random>


	//packages are sent every 50ms 
	int packageMillis = 50;

	// this is the IO stack depth, the amount of IO requests at once
	float RequestsPerTick = 1;
	
	//acts as polarswitch sending multiple requests
	void threadedRequests(std::shared_ptr<char> leaderPointer)
	{
		if (leaderPointer == nullptr) {
			return;
		}
		srand(time(NULL));

		//infinite loop of request sending by writing into the shared buffer leaderPointer
		while (true) {
			std::this_thread::sleep_for(std::chrono::milliseconds(packageMillis));
			int counter = 0;
			for (int m = 0; m < RequestsPerTick; m++) {
				// r for read, w for write
				*(leaderPointer.get() + counter) = 'w';
				counter++;
				// data block "index" to edit
				*(leaderPointer.get() + counter) = (char)(rand() % 255 + 1);
				counter++;
				// small letter to write into the data blocks (assuming chars or strings as data)
				*(leaderPointer.get() + counter) = (char)(rand() % 26 + 49);
				counter++;
				// 0 to end the request (could contain more characters)
				*(leaderPointer.get() + counter) = (char)0;
				counter++;
			}
		}
	}

	int main() {
		std::thread standardThread(threadedRequests, nullptr);
	}
