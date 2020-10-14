#include <memory>
#include <algorithm>
#include <iostream>
#include <thread>
#include <queue>
#include "BlockingCollection.h"

using namespace std;
using namespace code_machina;


class IOPolling {
public:
	bool parallelRaft;

	struct request {
		char number;
		char type;
		int lookBehind[2];
		int modifiedCell;
		char data[32];
	};




	// polls the IO queue that takes in requests from the InputThread / PolarSwitch

	void listener(BlockingCollection<request> queueRead, std::shared_ptr<IOPolling::request[]> bufferWrite, vector<IOPolling::request> requestQueue) {
		int recentNumber = 0;
		int counter = 0;
		int lookBehindOne = 0;
		int lookBehindTwo = 0;
		while (!queueRead.is_completed()) {
			request r;
			auto status = queueRead.take(r);
			if (status == BlockingCollectionStatus::Ok) {
				recentNumber = r.number;
				std::cout << "Read " << (int)r.number << "\n";

				r.lookBehind[0] = lookBehindOne;
				r.lookBehind[1] = lookBehindTwo;
				lookBehindTwo = lookBehindOne;
				lookBehindOne = r.modifiedCell;

				//*(bufferWrite.get() + counter) = &r;
				requestQueue.emplace_back(r);

				counter = (counter + 1) % 32;

			}


			/*	bool bothFound = false;
			char* endPtr;
			while (!bothFound) {
				bool oneZeroFound = false;
				int i = 0;
				while (*(bufferRead.get() + i) != 0) {
					i++;
				}
				oneZeroFound = true;
				i++;
				if (*(bufferRead.get() + i) != 0) oneZeroFound = false;
				else {
					bothFound = true;
					endPtr = bufferRead.get() + i;
				}
			}
			copy(bufferRead.get(), endPtr, bufferWrite.get());

			char* deletePointer = bufferRead.get();
			while (&deletePointer != &endPtr) {
				*deletePointer = '0';
				deletePointer++;
			}
		}*/
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
	}
};