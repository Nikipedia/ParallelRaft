#include <memory>
#include <algorithm>
#include <iostream>
#include <thread>
#include <queue>
#include "BlockingCollection.h"
#include <cstring>

using namespace std;
using namespace code_machina;


class IO {
public:
	class request {
	public:
		unsigned int number = 0;
		char type = 0;
		int *lookBehind;
		int modifiedCell = 0;
		int timesUsed = 0;
		char *data;
		std::chrono::time_point<std::chrono::steady_clock> sentTime;
		request() {
			//lookBehind = (int*)calloc(2, 4);
			//data = (char*)calloc(32, 1);
			data = new char[32];
			lookBehind = new int[2];
			memset(data, 0, sizeof data - 1);
			memset(lookBehind, 0, sizeof lookBehind - 1);
		}
		request(const request& obj) {
			//lookBehind = (int*)calloc(2, 4);
			//data = (char*)calloc(32, 1);
			data = new char[32];
			lookBehind = new int[2];
			sentTime = obj.sentTime;
			number = obj.number;
			type = obj.type;
			timesUsed = obj.timesUsed;
			std::copy(obj.lookBehind, obj.lookBehind + 2, lookBehind);
			//memset(data, 0, sizeof data - 1);
			modifiedCell = obj.modifiedCell;
			std::copy(obj.data, obj.data + 32, data);
			
		}
		request(const request* obj) {
			data = new char[32];
			lookBehind = new int[2];
			sentTime = obj->sentTime;
			number = obj->number;
			type = obj->type;
			timesUsed = obj->timesUsed;
			std::copy(obj->lookBehind, obj->lookBehind + 2, lookBehind);
			//memset(data, 0, sizeof data - 1);
			modifiedCell = obj->modifiedCell;
			std::copy(obj->data, obj->data + 32, data);
		}
		~request() {
			//free(lookBehind);
			//free(data);
			delete []data;
			delete []lookBehind;
		}
		request(request&&) = default;
		request& operator=(const request&) = default;
	};


};
