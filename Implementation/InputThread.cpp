#include <thread>
#include <memory>
#include <random>
#include "IOPolling.cpp"
#include "BlockingCollection.h"
#include <fstream>
#include <iostream>
#include <string>
#include <set>


using namespace code_machina;


class Threading {
	//serves as the PolarSwitch that simulates lots of IO requests
public:
	//packages are sent every 50ms 
	int packageMillis = 50;

	// this is the IO stack depth, the amount of IO requests at once
	float RequestsPerTick = 1;

	//this is the mutex for the local vector containing all requests waiting for acknowledgements
	std::mutex reqMutex;

	static std::chrono::time_point<std::chrono::steady_clock> begin;
	//std::vector<IOPolling::request> waiting;

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

	/*void threadedRequests(BlockingCollection<IOPolling::request*> collection)
	{
		srand(time(NULL));
		//infinite loop of request sending by writing into the shared buffer
		char totalCount = 0;
		while (true) {
			std::this_thread::sleep_for(std::chrono::milliseconds(packageMillis));
			for (int m = 0; m < RequestsPerTick; m++) {
				IOPolling::request r = IOPolling::request();
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
	}*/

	void producer_thread(BlockingCollection<IOPolling::request*>* requestQueue) {
		int messageLimit = 0;
		char totalCount = 0;
		requestQueue->attach_producer();
		begin = std::chrono::high_resolution_clock::now();
		while (messageLimit < 100)
		{
			for (int i = 0; i < RequestsPerTick; i++) {
				IOPolling::request r = IOPolling::request();
				r.number = totalCount;
				// i for input, w for write, a for acknowledge, c for commit
				r.type = 'i';
				// data block "index" to edit
				r.modifiedCell = (char)(rand() % 255 + 1);
				// small letter to write into the data blocks (assuming chars or strings as data)
				r.data[0] = (char)(rand() % 26 + 65);

				//collection.add(r);
				//std::cout << "request " << (int)totalCount << " written\n";
				totalCount = (totalCount + 1) % 256;

				// blocks if collection.size() == collection.bounded_capacity()
				requestQueue->add(&r);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(packageMillis));
			messageLimit += RequestsPerTick;
		}
		requestQueue->complete_adding();
	}

	//a separate thread to simulate network delay of 10-40 ms
	void addToQueue(BlockingCollection<IOPolling::request*> * queue, IOPolling::request * request) {
		srand(time(NULL));
		std::this_thread::sleep_for(std::chrono::milliseconds(rand() % 30 + 10));
		if (!queue->is_completed()) {
			queue->add(request);
			
		}
	}


	void consumer_thread(BlockingCollection<IOPolling::request*> * input, BlockingCollection<IOPolling::request*> * write, std::set<IOPolling::request*> * wait, std::vector<BlockingCollection<IOPolling::request*>*> * followers, BlockingCollection<IOPolling::request*> * leaderQueue) {
		int recentNumber = 0;
		int lookBehindOne = 0;
		int lookBehindTwo = 0;
		bool leader = false;

		if (followers->size() > 0) {
			leader = true;
			for (BlockingCollection<IOPolling::request*>* follower : *followers) {
				follower->attach_producer();
			}
		}
		input->attach_consumer();
		write->attach_producer();
		if (leaderQueue != nullptr) {
			leaderQueue->attach_producer();
		}
		while (!input->is_completed())
		{
			IOPolling::request* currentDat;

			// take will block if there is no data to be taken
			auto status = input->take(currentDat);

			if (status == BlockingCollectionStatus::Ok)
			{
				IOPolling::request data = *currentDat;
				// if the data type is "input" we are the leader and send a write request to the followers
				if (data.type == 'i') {
					if (data.number > recentNumber + 2) {
						//std::cout << "hole found for " << (int)data.number << "\n";
						//input->add(&data);
					}
					else {
						if (wait->size() == 1) {
							std::cout << "big list";
						}
						recentNumber = data.number;
						//std::cout << "Leader received " << (int)data.number << "\n";
						data.type = 'w';

						data.lookBehind[0] = lookBehindOne;
						data.lookBehind[1] = lookBehindTwo;

						lookBehindTwo = lookBehindOne;
						lookBehindOne = data.modifiedCell;

						//add copies to other queues

						IOPolling::request copy2 = IOPolling::request(data);
						write->add(&copy2);

						if (leader) {
							for (BlockingCollection<IOPolling::request*>* follower : *followers) {
								IOPolling::request copy = IOPolling::request(data);
								//follower->add(&copy);
								std::thread queueAdder(&Threading::addToQueue, this, &(*follower), &copy);
								queueAdder.detach();
							}
						}

						IOPolling::request* copy = new IOPolling::request(data);
						reqMutex.lock();
					
						wait->emplace(copy);
						reqMutex.unlock();
					}
				}
				// if the data type is write we are a follower and should answer with an acknowledgement
				else if (data.type == 'w') {
					if (data.number > recentNumber + 2) {
						input->add(&data);
					}
					else {
						recentNumber = data.number;
						//std::cout << "Follower Received " << (int)data.number << "\n";
						IOPolling::request copy2 = IOPolling::request(data);
						write->add(&copy2);

						if (!leader) {
							IOPolling::request copy = IOPolling::request(data);
							copy.type = 'a';
							if (!leaderQueue->is_adding_completed()) {
								//leaderQueue->add(&copy);
								std::thread queueAdder(&Threading::addToQueue, this, &(*leaderQueue), &copy);
								queueAdder.detach();
							}
						}

						IOPolling::request copy = IOPolling::request(data);
						reqMutex.lock();
						wait->emplace(&copy);
						reqMutex.unlock();
					}
				}
				// the leader receives 3 acknowledgements so it sends commit as soon as the counter reaches 2.
				else if (data.type == 'a') {
					reqMutex.lock();
					std::cout << wait->size() << "\n";
					IOPolling::request* deleted = nullptr;
					for (IOPolling::request* r : *wait) {
						if (r != nullptr && r->number == data.number) {
							r->timesUsed++;
							
							if (r->timesUsed > 1) {
								deleted = r;
							}
							break;
						}
					}
					if (deleted != nullptr) {
						std::cout << "commit " << (int)deleted->number << "\n";
						wait->erase(deleted);
					}
					reqMutex.unlock();

					if (deleted != nullptr) {
						//write commit to hard drive
						IOPolling::request copy2 = IOPolling::request(data);
						copy2.type = 'c';
						write->add(&copy2);

						//send commit to all followers
						for (BlockingCollection<IOPolling::request*>* follower : *followers) {
							IOPolling::request copy = IOPolling::request(copy2);
							//follower->add(&copy);
							std::thread queueAdder(&Threading::addToQueue, this, &(*follower), &copy);
							queueAdder.detach();
						}

					}
				}
				// receiving commits means we write the data to disk and the commit to the log
				else if (data.type == 'c') {
					reqMutex.lock();
					IOPolling::request* deleted = nullptr;
					for (IOPolling::request* r : *wait) {
						if (r != nullptr && r->number == data.number) {
							deleted = r;
							break;
						}
					}
					if (deleted != nullptr) {
						wait->erase(deleted);
					}
					reqMutex.unlock();

					IOPolling::request copy2 = IOPolling::request(data);
					write->add(&copy2);
				}
			}
		}
		write->complete_adding();
		for (BlockingCollection<IOPolling::request*>* follower : *followers) {
			follower->complete_adding();
		}
	}

	void writer_thread(BlockingCollection<IOPolling::request*> * writingQueue, int nodeNumber) {
		std::string s = std::to_string(nodeNumber);
		fstream myfile;
		string x = "log" + s;
		x.append(".txt");
		std::cout << x;
		myfile.open(x, fstream::out);
		writingQueue->attach_consumer();
		while (!writingQueue->is_completed()) {
			IOPolling::request* currentDat;

			// take will block if there is no data to be taken
			auto status = writingQueue->take(currentDat);
			int u = 0;
			
			if (status == BlockingCollectionStatus::Ok)
			{
				IOPolling::request data = *currentDat;
				if (data.type == 'c')
					myfile << (int)data.number << " " << data.type << " "<< data.modifiedCell << "\n";
				//std::cout << (int)data.number << " " << data.type << "\n";
			}
		}
		myfile.close();
	}


};

///taken from https://stackoverflow.com/questions/865668/how-to-parse-command-line-arguments-in-c
class InputParser {
public:
	InputParser(int& argc, char** argv) {
		for (int i = 1; i < argc; ++i)
			this->tokens.push_back(std::string(argv[i]));
	}
	/// @author iain
	const std::string& getCmdOption(const std::string & option) const {
		std::vector<std::string>::const_iterator itr;
		itr = std::find(this->tokens.begin(), this->tokens.end(), option);
		if (itr != this->tokens.end() && ++itr != this->tokens.end()) {
			return *itr;
		}
		static const std::string empty_string("");
		return empty_string;
	}
	/// @author iain
	bool cmdOptionExists(const std::string & option) const {
		return std::find(this->tokens.begin(), this->tokens.end(), option)
			!= this->tokens.end();
	}
private:
	std::vector <std::string> tokens;
};

std::chrono::time_point<std::chrono::steady_clock> Threading::begin = std::chrono::high_resolution_clock::now();

int main(int argc, char** argv) {
	InputParser input(argc, argv);
	if (input.cmdOptionExists("-h")) {
		// Do stuff
	}
	const std::string& filename = input.getCmdOption("-f");
	if (!filename.empty()) {
		// Do interesting things ...
	}

	//amount of I/O threads on each node
	int threadCount = 1;

	Threading leaderThread;
	Threading followerThread1;
	Threading followerThread2;
	Threading followerThread3;

	//IOPolling leader;
	BlockingCollection<IOPolling::request*> requestQueue(30);

	std::set<IOPolling::request*> leaderWaiting;
	std::set<IOPolling::request*> follower1Waiting;
	std::set<IOPolling::request*> follower2Waiting;
	std::set<IOPolling::request*> follower3Waiting;
	BlockingCollection<IOPolling::request*> follower1Input(30);
	BlockingCollection<IOPolling::request*> follower2Input(30);
	BlockingCollection<IOPolling::request*> follower3Input(30);

	std::vector<BlockingCollection<IOPolling::request*>*> followers;
	followers.push_back(&follower1Input);
	followers.push_back(&follower2Input);
	followers.push_back(&follower3Input);

	//testing:
	/*IOPolling::request r;
	std::thread tester(&Threading::addToQueue, &leaderThread, &requestQueue, &r);
	tester.detach();


	BlockingCollection<IOPolling::request*> leaderWrite(10);
	if (r.number != 1) {

		std::thread leader_Consumer(&Threading::consumer_thread, &leaderThread, &requestQueue, &leaderWrite, &leaderWaiting, &followers, nullptr);
		leader_Consumer.detach();
	}
	*/
	std::thread prod(&Threading::producer_thread, &leaderThread, &requestQueue);



	//the for loop produces crashes because the BlockingCollection doesnt operate correctly when its not referenced in main anymore.
	//for (int i = 0; i < threadCount; i++) {
	int i = 0;
	BlockingCollection<IOPolling::request*> leaderWrite(30);
	std::thread leader_Consumer(&Threading::consumer_thread, &leaderThread, &requestQueue, &leaderWrite, &leaderWaiting, &followers, nullptr);
	std::thread leader_Writer(&Threading::writer_thread, &leaderThread, &leaderWrite, i);

	BlockingCollection<IOPolling::request*> follower1Write(30);
	BlockingCollection<IOPolling::request*> follower2Write(30);
	BlockingCollection<IOPolling::request*> follower3Write(30);

	std::thread follower1_Consumer(&Threading::consumer_thread, &followerThread1, &follower1Input, &follower1Write, &follower1Waiting, &std::vector<BlockingCollection<IOPolling::request*>*>(), &requestQueue);

	std::thread follower2_Consumer(&Threading::consumer_thread, &followerThread2, &follower2Input, &follower2Write, &follower2Waiting, &std::vector<BlockingCollection<IOPolling::request*>*>(), &requestQueue);

	std::thread follower3_Consumer(&Threading::consumer_thread, &followerThread3, &follower3Input, &follower3Write, &follower3Waiting, &std::vector<BlockingCollection<IOPolling::request*>*>(), &requestQueue);

	std::thread follower1_Write(&Threading::writer_thread, &followerThread1, &follower1Write, ++i);
	std::thread follower2_Write(&Threading::writer_thread, &followerThread2, &follower2Write, ++i);
	std::thread follower3_Write(&Threading::writer_thread, &followerThread3, &follower3Write, ++i);
	prod.detach();
	leader_Consumer.detach();
	leader_Writer.detach();
	follower1_Consumer.detach();
	follower2_Consumer.detach();
	follower3_Consumer.detach();
	follower1_Write.detach();
	follower2_Write.detach();
	follower3_Write.detach();
//}


	while (!follower1Write.is_completed() //|| !follower2Write.is_completed() || !follower3Write.is_completed()) {
		){
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	std::chrono::time_point<std::chrono::steady_clock> end = std::chrono::high_resolution_clock::now();
	std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(end - Threading::begin).count() << "ms" << std::endl;
	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	std::this_thread::sleep_for(std::chrono::milliseconds(1000));

	return 0;
}
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
	//std::shared_ptr<IOPolling::request[]> writingPointer(new IOPolling::request[32]);
	//std::vector<IOPolling::request> queuePointer;
	//std::thread inputThread(threadedRequests, requestQueue);
	//std::thread leaderReadThread(&IOPolling::listener, &leader, requestQueue, writingPointer, queuePointer);

	//inputThread.detach();
	//inputThread.~thread();
	//leaderReadThread.detach();
	//leaderReadThread.~thread();
	//IOPolling::request r = *(writingPointer.get());


