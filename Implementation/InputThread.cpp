#include <thread>
#include <memory>
#include <random>
#include "IOPolling.cpp"
#include "BlockingCollection.h"
#include <fstream>
#include <iostream>
using namespace code_machina;


class Threading {
	//serves as the PolarSwitch that simulates lots of IO requests
public:
		//packages are sent every 50ms 
	int packageMillis = 50;

	// this is the IO stack depth, the amount of IO requests at once
	float RequestsPerTick = 3;

	//this is the mutex for the local vector containing all requests waiting for acknowledgements
	std::mutex reqMutex;

	std::vector<IOPolling::request> waiting;

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

	void threadedRequests(BlockingCollection<IOPolling::request*> collection)
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
				r.data[1] = (char)(rand() % 26 + 65);
				//collection.add(r);
				std::cout << "request written\n";
				totalCount = (totalCount + 1) % 32;
			}
		}
	}

	void producer_thread(BlockingCollection<IOPolling::request*> * requestQueue) {
		int messageLimit = 0;
		char totalCount = 0;
		while (messageLimit < 1000)
		{
			for (int i = 0; i < RequestsPerTick; i++) {
				IOPolling::request* r = new IOPolling::request;
				r->number = totalCount;
				// i for input, w for write, a for acknowledge, c for commit
				r->type = 'i';
				// data block "index" to edit
				r->modifiedCell = (char)(rand() % 255 + 1);
				// small letter to write into the data blocks (assuming chars or strings as data)
				r->data[0] = (char)(rand() % 26 + 65);
				//collection.add(r);
				std::cout << "request " << (int)totalCount << " written\n";
				totalCount = (totalCount + 1) % 32;

				// blocks if collection.size() == collection.bounded_capacity()
				requestQueue->add(r);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(packageMillis));
		}
		requestQueue->complete_adding();
	}


	void consumer_thread(BlockingCollection<IOPolling::request*> * input, BlockingCollection<IOPolling::request*> * write, std::list<IOPolling::request*> * wait, BlockingCollection<IOPolling::request*> * leaderQueue = NULL, std::vector<BlockingCollection<IOPolling::request*>> followers = std::vector<BlockingCollection < IOPolling::request*>>()) {
		int recentNumber = 0;
		int lookBehindOne = 0;
		int lookBehindTwo = 0;
		bool leader = false;

		if (followers.size() > 0) {
			leader = true;

		}
		while (!input->is_completed())
		{
			IOPolling::request* data;

			// take will block if there is no data to be taken
			auto status = input->take(data);

			if (status == BlockingCollectionStatus::Ok)
			{
				if (data->type == 'i') {
					if (data->number > recentNumber + 2) {
						input->add(data);
					}
					else {
						recentNumber = data->number;
						std::cout << "Leader received " << (int)data->number << "\n";
						data->type = 'w';
						data->lookBehind[0] = lookBehindOne;
						data->lookBehind[1] = lookBehindTwo;
						lookBehindTwo = lookBehindOne;
						lookBehindOne = data->modifiedCell;

						//add copies to other queues

						write->add(data);

						if (leader) {
							for (BlockingCollection<IOPolling::request*>& follower : followers) {
								IOPolling::request copy = IOPolling::request(*data);
								follower.add(&copy);
							}
						}

						IOPolling::request copy = IOPolling::request(*data);
						reqMutex.lock();
						wait->emplace_back(&copy);
						reqMutex.unlock();
					}
				}
				else if (data->type == 'w') {
					if (data->number > recentNumber + 2) {
						input->add(data);
					}
					else {
						recentNumber = data->number;
						std::cout << "Follower Received " << (int)data->number << "\n";

						write->add(data);

						if (!leader) {
							IOPolling::request copy = IOPolling::request(*data);
							copy.type = 'a';
							leaderQueue->add(&copy);
						}

						IOPolling::request copy = IOPolling::request(*data);
						reqMutex.lock();
						wait->emplace_back(&copy);
						reqMutex.unlock();
					}
				}
				else if (data->type == 'a') {
					reqMutex.lock();
					IOPolling::request* deleted;
					for (IOPolling::request *r : *wait) {
						if (r->number == data->number) {
							r->data[0]++;
							if (r->data[0] > 1) {
								deleted = r;
							}
							break;
						}
					}
					if (deleted != nullptr) {
						wait->remove(deleted);
					}
					reqMutex.unlock();

					//write commit to hard drive
					IOPolling::request copy2 = IOPolling::request(*data);
					copy2.type = 'c';
					write->add(&copy2);

					//send commit to all followers
					for (BlockingCollection<IOPolling::request*>& follower : followers) {
						IOPolling::request copy = IOPolling::request(copy2);
						follower.add(&copy);
					}
				}
				else if (data->type == 'c') {

					reqMutex.lock();
					IOPolling::request* deleted;
					for (IOPolling::request* r : *wait) {
						if (r->number == data->number) {
							deleted = r;
							break;
						}
					}
					if (deleted != nullptr) {
						wait->remove(deleted);
					}
					reqMutex.unlock();

					write->add(data);
				}
			}
		}
	}


	void writer_thread(BlockingCollection<IOPolling::request*>* writingQueue) {
		fstream myfile;
		myfile.open("log.txt");
		while (!writingQueue->is_completed()) {
			IOPolling::request* data;
			// take will block if there is no data to be taken
			auto status = writingQueue->take(data);

			if (status == BlockingCollectionStatus::Ok)
			{
				myfile << data->number << data->type << data->modifiedCell << data->data << "\n";
			}
		}
		myfile.close();
	}

	
};

int main() {
	Threading leaderThread;
	Threading followerThread1;
	Threading followerThread2;
	Threading followerThraed3;

	//IOPolling leader;
	BlockingCollection<IOPolling::request*> requestQueue(10);

	BlockingCollection<IOPolling::request*> leaderWrite(10);
	BlockingCollection<IOPolling::request*> follower1Write(10);
	BlockingCollection<IOPolling::request*> follower2Write(10);
	BlockingCollection<IOPolling::request*> follower3Write(10);
	list<IOPolling::request*> leaderWaiting(30);
	list<IOPolling::request*> follower1Waiting(30);
	list<IOPolling::request*> follower2Waiting(30);
	list<IOPolling::request*> follower3Waiting(30);
	BlockingCollection<IOPolling::request*> follower1Input(10);
	BlockingCollection<IOPolling::request*> follower2Input(10);
	BlockingCollection<IOPolling::request*> follower3Input(10);

	std::vector<BlockingCollection<IOPolling::request*>> followers = std::vector<BlockingCollection < IOPolling::request*>>();
	followers.push_back(follower1Input);
	followers.push_back(follower2Input);
	followers.push_back(follower3Input);

	std::thread prod(&Threading::producer_thread, &leaderThread, &requestQueue);
	std::thread leader_Consumer(&Threading::consumer_thread, &leaderThread, &requestQueue, &leaderWrite, &leaderWaiting, followers);
	std::thread leader_Writer(&Threading::writer_thread, &leaderThread, &leaderWrite);

	/*std::thread producer_thread([&requestQueue]() {
		int messageLimit = 0;
		char totalCount = 0;
		while (messageLimit < 1000)
		{
			for (int i = 0; i < RequestsPerTick; i++) {
				IOPolling::request* r = new IOPolling::request;
				r->number = totalCount;
				// r for read, w for write, could contain more for e.g. new insertion of data
				r->type = 'w';
				// data block "index" to edit
				r->modifiedCell = (char)(rand() % 255 + 1);
				// small letter to write into the data blocks (assuming chars or strings as data)
				r->data[0] = (char)(rand() % 26 + 65);
				//collection.add(r);
				std::cout << "request " << (int)totalCount << " written\n";
				totalCount = (totalCount + 1) % 32;

				// blocks if collection.size() == collection.bounded_capacity()
				requestQueue.add(r);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(packageMillis));
		}
		requestQueue.complete_adding();
		});

	std::thread consumer_thread([&requestQueue, &leaderWaiting]() {
		int recentNumber = 0;
		int counter = 0;
		int lookBehindOne = 0;
		int lookBehindTwo = 0;
		while (!requestQueue.is_completed())
		{
			IOPolling::request* data;

			// take will block if there is no data to be taken
			auto status = requestQueue.take(data);

			if (status == BlockingCollectionStatus::Ok)
			{
				recentNumber = data->number;
				std::cout << "Thread 1 Read " << (int)data->number << "\n";

				data->lookBehind[0] = lookBehindOne;
				data->lookBehind[1] = lookBehindTwo;
				lookBehindTwo = lookBehindOne;
				lookBehindOne = data->modifiedCell;


				counter = (counter + 1) % 32;
			}
		}
		});

	std::thread consumer2_thread([&requestQueue]() {
		int recentNumber = 0;
		int counter = 0;
		int lookBehindOne = 0;
		int lookBehindTwo = 0;
		while (!requestQueue.is_completed())
		{
			IOPolling::request* data;

			// take will block if there is no data to be taken
			auto status = requestQueue.take(data);

			if (status == BlockingCollectionStatus::Ok)
			{
				recentNumber = data->number;
				std::cout << "Thread 2 Read " << (int)data->number << "\n";

				data->lookBehind[0] = lookBehindOne;
				data->lookBehind[1] = lookBehindTwo;
				lookBehindTwo = lookBehindOne;
				lookBehindOne = data->modifiedCell;


				counter = (counter + 1) % 32;
			}
		}
		});

	std::thread consumer3_thread([&requestQueue]() {
		int recentNumber = 0;
		int counter = 0;
		int lookBehindOne = 0;
		int lookBehindTwo = 0;
		while (!requestQueue.is_completed())
		{
			IOPolling::request* data;

			// take will block if there is no data to be taken
			auto status = requestQueue.take(data);

			if (status == BlockingCollectionStatus::Ok)
			{
				recentNumber = data->number;
				std::cout << "Thread 3 Read " << (int)data->number << "\n";

				data->lookBehind[0] = lookBehindOne;
				data->lookBehind[1] = lookBehindTwo;
				lookBehindTwo = lookBehindOne;
				lookBehindOne = data->modifiedCell;


				counter = (counter + 1) % 32;
			}
		}
		});
		*/

		//std::shared_ptr<vector<IOPolling::request>> requestPointer;
	std::shared_ptr<IOPolling::request[]> writingPointer(new IOPolling::request[32]);
	std::vector<IOPolling::request> queuePointer;
	//std::thread inputThread(threadedRequests, requestQueue);
	//std::thread leaderReadThread(&IOPolling::listener, &leader, requestQueue, writingPointer, queuePointer);
	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	//inputThread.detach();
	//inputThread.~thread();
	//leaderReadThread.detach();
	//leaderReadThread.~thread();
	IOPolling::request r = *(writingPointer.get());

}
	