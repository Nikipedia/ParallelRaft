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
	struct request {
	public:
		char number = 0;
		char type = 0;
		int* lookBehind;
		int modifiedCell = 0;
		char* data;
		request() {
			//lookBehind = (int*)calloc(2, 4);
			//data = (char*)calloc(32, 1);
			data = new char[32];
			lookBehind = new int[2];
		}
		request(const request& obj) {
			//lookBehind = (int*)calloc(2, 4);
			//data = (char*)calloc(32, 1);
			data = new char[32];
			lookBehind = new int[2];
			number = obj.number;
			type = obj.type;
			std::copy(obj.lookBehind, obj.lookBehind + 1, lookBehind);
			modifiedCell = obj.modifiedCell;
			std::copy(obj.data, obj.data + 31, data);
		}
		~request() {
			//free(lookBehind);
			//free(data);
			delete data;
			delete lookBehind;
		}
		request(request&&) = default;
		request& operator=(const request&) = default;
	};


};




	// polls the IO queue that takes in requests from the InputThread / PolarSwitch

	/*void listener(BlockingCollection<request> queueRead, std::shared_ptr<IOPolling::request[]> bufferWrite, vector<IOPolling::request> requestQueue) {
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
		}
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
	}*/
