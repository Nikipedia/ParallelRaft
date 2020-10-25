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
	int RequestsPerTick = 16;

	int totalRequests = 500;

	int randomSpread = 250;

	//this is the mutex for the local vector containing all requests waiting for acknowledgements
	std::mutex reqMutex;

	static std::chrono::time_point<std::chrono::steady_clock> begin;

	unsigned int recentNumber = 0;

	std::mutex numberMutex;

	bool parallelExecution = true;

	std::mutex parallelMutex;

	unsigned int waitForNumber = 0;

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
		unsigned int totalCount = 0;
		requestQueue->attach_producer();
		begin = std::chrono::high_resolution_clock::now();
		int lookBehindOne = 0;
		int lookBehindTwo = 0;
		while (messageLimit < totalRequests)
		{
			if (requestQueue->size() < 50) {
				for (int i = 0; i < RequestsPerTick; i++) {
					IOPolling::request* r = new IOPolling::request();
					r->number = totalCount;
					// i for input, w for write, a for acknowledge, c for commit
					r->type = 'i';
					// data block "index" to edit
					r->modifiedCell = (char)((rand() % 255) % randomSpread + 1);
					// small letter to write into the data blocks (assuming chars or strings as data)
					r->data[0] = (char)(rand() % 26 + 65);
					r->lookBehind[0] = lookBehindOne;
					r->lookBehind[1] = lookBehindTwo;

					lookBehindTwo = lookBehindOne;
					lookBehindOne = r->modifiedCell;
					//collection.add(r);
					//std::cout << "request " << (int)totalCount << " written\n";
					totalCount = totalCount + 1;

					if (totalCount == 0) {
						IOPolling::request* s = new IOPolling::request();
						s->type = 's';
						requestQueue->add(s);
					}
					// blocks if collection.size() == collection.bounded_capacity()
					requestQueue->add(r);
				}
				messageLimit += RequestsPerTick;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(packageMillis));
			
		}
		// send a special message to confirm end of requests
		IOPolling::request * r = new IOPolling::request();
		r->number = totalCount;
		// i for input, w for write, a for acknowledge, c for commit
		r->type = 'e';
		// blocks if collection.size() == collection.bounded_capacity()
		requestQueue->add(r);

		while (!requestQueue->is_empty()) {
			std::cout << "not finished yet, still size: " << requestQueue->size() << "\n";
			std::this_thread::sleep_for(std::chrono::milliseconds(packageMillis));
		}
	}

	//a separate thread to simulate network delay of 10-40 ms
	void addToQueue(BlockingCollection<IOPolling::request*> * queue, IOPolling::request * request) {
		srand(time(NULL));
		std::this_thread::sleep_for(std::chrono::milliseconds(rand() % 30 + 10));
		if (!queue->is_completed()) {
			queue->add(request);
		}
	}

	//Raft version can only take the elements in correct order
	void consumer_thread(BlockingCollection<IOPolling::request*> * input, BlockingCollection<IOPolling::request*> * write, std::set<IOPolling::request*> * wait, std::vector<BlockingCollection<IOPolling::request*>*> * followers, BlockingCollection<IOPolling::request*> * leaderQueue) {
		//recentNumber = 0;
		bool leader = false;
		bool waitingForFinish = false;
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
		while ((leader && (!input->is_empty() || !waitingForFinish)) || (!leader && !input->is_completed()))
		{
			IOPolling::request* currentDat;

			// take will block if there is no data to be taken
			auto status = input->take(currentDat);

			if (status == BlockingCollectionStatus::Ok)
			{
				IOPolling::request data = *currentDat;
				if (data.type == 'e') {
					if (leader) {
						waitingForFinish = true;
						for (BlockingCollection<IOPolling::request*>* follower : *followers) {
							IOPolling::request* copy = new IOPolling::request(data);
							//follower->add(&copy);
							std::thread queueAdder(&Threading::addToQueue, this, &(*follower), copy);
							queueAdder.detach();
						}
					}
				}
				else {
					if (leader) numberMutex.lock();
					if (data.number != recentNumber)
					{
						//if(leader) std::cout << "Leader falsely received " << (int)data.number << "\n";
						//if ((0 < (int)(data.number) - recentNumber && (int)(data.number) - recentNumber < 5) || ((int)(data.number) - recentNumber > 250)) {
						if (data.type != 'a') {
							//std::cout << "Readded " << (int)data.number << " with " << data.type << "\n";
							//std::thread queueAdder(&Threading::addToQueue, this, input, new IOPolling::request(data));
							//queueAdder.detach();
							input->add(new IOPolling::request(data));
						}
					}
					else {
						// if the data type is "input" we are the leader and send a write request to the followers
						if (data.type == 'i') {

							/*if (wait->size() == 1) {
								std::cout << "big list";
							}*/

							//std::cout << "Leader received " << (int)data.number << "\n";
							data.type = 'w';

							//add copies to other queues

							IOPolling::request* copy2 = new IOPolling::request(data);
							write->add(copy2);

							if (leader) {
								for (BlockingCollection<IOPolling::request*>* follower : *followers) {
									IOPolling::request* copy = new IOPolling::request(data);
									//follower->add(&copy);
									std::thread queueAdder(&Threading::addToQueue, this, &(*follower), copy);
									queueAdder.detach();
								}
							}

							IOPolling::request* copy = new IOPolling::request(data);
							reqMutex.lock();

							wait->emplace(copy);
							reqMutex.unlock();

						}
						// if the data type is write we are a follower and should answer with an acknowledgement
						else if (data.type == 'w') {


							//if (data.number % 2 == 0) {
								//std::cout << "Follower Received " << (int)data.number << "\n";
							//}

							IOPolling::request* copy2 = new IOPolling::request(data);
							write->add(copy2);

							if (!leader) {
								IOPolling::request* copy = new IOPolling::request(data);
								copy->type = 'a';
								if (!leaderQueue->is_adding_completed()) {
									//leaderQueue->add(&copy);
									std::thread queueAdder(&Threading::addToQueue, this, &(*leaderQueue), copy);
									queueAdder.detach();
								}
							}

							IOPolling::request* copy = new IOPolling::request(data);
							reqMutex.lock();
							wait->emplace(copy);
							reqMutex.unlock();
						}

						// the leader receives 3 acknowledgements so it sends commit as soon as the counter reaches 2.
						else if (data.type == 'a') {
							reqMutex.lock();
							//if (leader)std::cout << "list size" << wait->size() << "\n";
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
								//std::cout << "commit " << (int)deleted->number << "\n";
								wait->erase(deleted);
							}
							reqMutex.unlock();

							if (deleted != nullptr) {
								recentNumber = data.number + 1;
								//write commit to hard drive
								IOPolling::request * copy2 = new IOPolling::request(data);
								copy2->type = 'c';
								write->add(copy2);

								//send commit to all followers
								for (BlockingCollection<IOPolling::request*>* follower : *followers) {
									IOPolling::request* copy = new IOPolling::request(data);
									copy->type = 'c';
									//follower->add(&copy);
									std::thread queueAdder(&Threading::addToQueue, this, &(*follower), copy);
									queueAdder.detach();
								}

							}
						}
						// receiving commits means we write the data to disk and the commit to the log
						else if (data.type == 'c') {
							recentNumber = data.number + 1;
							reqMutex.lock();
							IOPolling::request * deleted = nullptr;
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

							IOPolling::request* copy2 = new IOPolling::request(data);
							write->add(copy2);
						}
					}
					if (leader) numberMutex.unlock();
				}
				delete currentDat;
			}

		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		write->complete_adding();
		if (leader) {

			for (BlockingCollection<IOPolling::request*>* follower : *followers) {
				follower->complete_adding();
			}
			//remove this for tests
			reqMutex.lock();
			for (IOPolling::request* req : *wait) {
				std::cout << "still in waiting: " << (int)req->number << " with " << req->timesUsed << " commit messages" << "\n";
			}
			reqMutex.unlock();
		}
	}

	//Parallel Raft version
	void consumer_thread_parallel(BlockingCollection<IOPolling::request*> * input, BlockingCollection<IOPolling::request*> * write, std::set<IOPolling::request*> * wait, std::vector<BlockingCollection<IOPolling::request*>*> * followers, BlockingCollection<IOPolling::request*> * leaderQueue) {
		
		bool leader = false;
		bool waitingForFinish = false;
		bool allowed = false;
		bool reset = false;
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
		while ((leader && (!input->is_empty() || !waitingForFinish)) || (!leader && !input->is_completed()))
		{
			IOPolling::request* currentDat;

			// take will block if there is no data to be taken
			auto status = input->take(currentDat);

			if (status == BlockingCollectionStatus::Ok)
			{
				IOPolling::request data = *currentDat;
				// end of input 
				allowed = false;
				if (data.type == 'e') {
					if (leader) {
						waitingForFinish = true;
						for (BlockingCollection<IOPolling::request*>* follower : *followers) {
							IOPolling::request* copy = new IOPolling::request(data);
							//follower->add(&copy);
							std::thread queueAdder(&Threading::addToQueue, this, &(*follower), copy);
							queueAdder.detach();
						}
					}
				}
				// reset the message numbering / inserting a checkpoint
				else if (data.type == 's') {
					reset = true;
				}
				else {
					int rct = recentNumber;
					if (leader) {
						// if data is filling a hole or doesnt conflict, we continue parallel execution, if theres a conflict we go into the strict mode
						if (parallelExecution) {
							if (data.number <= rct && rct - data.number < 100) allowed = true;
							else if (data.number <= rct + 2) {
								if (data.modifiedCell != data.lookBehind[0] && data.modifiedCell != data.lookBehind[1]) {
									allowed = true;
								}
								else {
									allowed = false;
									//std::cout << "didnt work for number " << data.number << ", going into serial mode\n";
									parallelMutex.lock();
									parallelExecution = false;
									parallelMutex.unlock();
									if (data.number >= waitForNumber) {
										waitForNumber = data.number;
									}
								}
							}
						}
						else {
							if (rct == data.number) {
								allowed = true;
							}
						}
					}
					else allowed = true;

					if (allowed) {
						//if(leader)std::cout << "is in strict mode: " << !parallelExecution <<"\n";
						// if the data type is "input" we are the leader and send a write request to the followers
						if (data.type == 'i') {

							/*if (wait->size() == 1) {
								std::cout << "big list";
							}*/

							//std::cout << "Leader received " << (int)data.number << "\n";
							data.type = 'w';

							//add copies to other queues

							IOPolling::request* copy2 = new IOPolling::request(data);
							write->add(copy2);

							if (leader) {
								for (BlockingCollection<IOPolling::request*>* follower : *followers) {
									IOPolling::request* copy = new IOPolling::request(data);
									//follower->add(&copy);
									std::thread queueAdder(&Threading::addToQueue, this, &(*follower), copy);
									queueAdder.detach();
								}
							}

							IOPolling::request* copy = new IOPolling::request(data);
							reqMutex.lock();

							wait->emplace(copy);
							reqMutex.unlock();

						}
						// if the data type is write we are a follower and should answer with an acknowledgement
						else if (data.type == 'w') {


							//if (data.number % 2 == 0) {
								//std::cout << "Follower Received " << (int)data.number << "\n";
							//}

							IOPolling::request* copy2 = new IOPolling::request(data);
							write->add(copy2);

							if (!leader) {
								IOPolling::request* copy = new IOPolling::request(data);
								copy->type = 'a';
								if (!leaderQueue->is_adding_completed()) {
									//leaderQueue->add(&copy);
									std::thread queueAdder(&Threading::addToQueue, this, &(*leaderQueue), copy);
									queueAdder.detach();
								}
							}

							IOPolling::request* copy = new IOPolling::request(data);
							reqMutex.lock();
							wait->emplace(copy);
							reqMutex.unlock();
						}

						// the leader receives 3 acknowledgements so it sends commit as soon as the counter reaches 2.
						else if (data.type == 'a') {
							reqMutex.lock();
							//if (leader)std::cout << "list size" << wait->size() << "\n";
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
								//std::cout << "commit " << (int)deleted->number << "\n";
								wait->erase(deleted);
							}
							reqMutex.unlock();

							if (deleted != nullptr) {
								parallelMutex.lock();
								if (!parallelExecution && data.number >= waitForNumber - 1) {
									parallelExecution = true;
									//std::cout << "turned on parallel\n";
								}
								parallelMutex.unlock();
								numberMutex.lock();
								if(data.number + 1 > recentNumber) recentNumber = data.number + 1;
								numberMutex.unlock();
								//write commit to hard drive
								IOPolling::request * copy2 = new IOPolling::request(data);
								copy2->type = 'c';
								write->add(copy2);

								//send commit to all followers
								for (BlockingCollection<IOPolling::request*>* follower : *followers) {
									IOPolling::request* copy = new IOPolling::request(data);
									copy->type = 'c';
									//follower->add(&copy);
									std::thread queueAdder(&Threading::addToQueue, this, &(*follower), copy);
									queueAdder.detach();
								}

							}
						}
						// receiving commits means we write the data to disk and the commit to the log
						else if (data.type == 'c') {
							numberMutex.lock();
							recentNumber = data.number + 1;
							numberMutex.unlock();
							reqMutex.lock();
							IOPolling::request * deleted = nullptr;
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

							IOPolling::request* copy2 = new IOPolling::request(data);
							write->add(copy2);
						}
					}
					//if(leader) std::cout << "Leader falsely received " << (int)data.number << "\n";
						//if ((0 < (int)(data.number) - recentNumber && (int)(data.number) - recentNumber < 5) || ((int)(data.number) - recentNumber > 250)) {
					else if (data.type != 'a' || !parallelExecution || data.number >= rct) {
						//std::cout << "Readded " << (int)data.number << " with " << data.type << "\n";
						//std::thread queueAdder(&Threading::addToQueue, this, input, new IOPolling::request(data));
						//queueAdder.detach();
						input->add(new IOPolling::request(data));
						
					}
				}
				if (wait->size() == 0 && leader && reset) {

					reset = false;
					//recentNumber = 0;
				}
				delete currentDat;
			}

		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		write->complete_adding();
		if (leader) {

			for (BlockingCollection<IOPolling::request*>* follower : *followers) {
				follower->complete_adding();
			}
			//remove this for tests
			reqMutex.lock();
			for (IOPolling::request* req : *wait) {
				std::cout << "still in waiting: " << (int)req->number << " with " << req->timesUsed << " commit messages" << "\n";
			}
			reqMutex.unlock();
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
					myfile << (int)data.number << " " << data.type << " " << data.modifiedCell << "\n";
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
	bool parallelRaft = false;
	if (input.cmdOptionExists("-help")) {
		std::cout << "Usage: -p for parallelRaft instead of Raft,\n-d followed by the IO stack depth,\n-c followed by the amount of IO threads per node\n";
	}

	if (input.cmdOptionExists("-p")) {
		parallelRaft = true;
	}

	//amount of I/O threads on each node
	int threadCount = 2;
	Threading leaderThread;
	Threading followerThread1;
	Threading followerThread2;
	Threading followerThread3;

	const std::string& inputStackDepth = input.getCmdOption("-d");
	if (!inputStackDepth.empty()) {
		leaderThread.RequestsPerTick = std::stoi(inputStackDepth);
	}
	const std::string& thrC = input.getCmdOption("-c");
	if (!inputStackDepth.empty()) {
		threadCount = std::stoi(thrC);
	}





	//IOPolling leader;
	BlockingCollection<IOPolling::request*> requestQueue(200);
	BlockingCollection<IOPolling::request*> fromFollToLeaderQueue(120);
	std::set<IOPolling::request*> leaderWaiting;
	std::set<IOPolling::request*> follower1Waiting;
	std::set<IOPolling::request*> follower2Waiting;
	std::set<IOPolling::request*> follower3Waiting;
	BlockingCollection<IOPolling::request*> follower1Input(60);
	BlockingCollection<IOPolling::request*> follower2Input(60);
	BlockingCollection<IOPolling::request*> follower3Input(60);

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

	std::vector<std::shared_ptr<std::thread>> threads;

	//the for loop produces crashes because the BlockingCollection doesnt operate correctly when its not referenced in main anymore.
	//for (int i = 0; i < threadCount; i++) {
	int i = 0;
	BlockingCollection<IOPolling::request*> leaderWrite(60);
	BlockingCollection<IOPolling::request*> follower1Write(60);
	BlockingCollection<IOPolling::request*> follower2Write(60);
	BlockingCollection<IOPolling::request*> follower3Write(60);
	if (!parallelRaft) {
		std::unique_ptr<std::thread> leader_Consumer (new std::thread(&Threading::consumer_thread, &leaderThread, &requestQueue, &leaderWrite, &leaderWaiting, &followers, nullptr));
		std::unique_ptr<std::thread> leader_Consum2(new std::thread(&Threading::consumer_thread, &leaderThread, &fromFollToLeaderQueue, &leaderWrite, &leaderWaiting, &followers, nullptr));
		std::unique_ptr<std::thread> leader_Writer(new std::thread(&Threading::writer_thread, &leaderThread, &leaderWrite, i));

		std::unique_ptr<std::thread> follower1_Consumer(new std::thread(&Threading::consumer_thread, &followerThread1, &follower1Input, &follower1Write, &follower1Waiting, &std::vector<BlockingCollection<IOPolling::request*>*>(), &fromFollToLeaderQueue));
		std::unique_ptr<std::thread> follower2_Consumer(new std::thread(&Threading::consumer_thread, &followerThread2, &follower2Input, &follower2Write, &follower2Waiting, &std::vector<BlockingCollection<IOPolling::request*>*>(), &fromFollToLeaderQueue));
		std::unique_ptr<std::thread> follower3_Consumer(new std::thread(&Threading::consumer_thread, &followerThread3, &follower3Input, &follower3Write, &follower3Waiting, &std::vector<BlockingCollection<IOPolling::request*>*>(), &fromFollToLeaderQueue));

		std::unique_ptr<std::thread> follower1_Write(new std::thread(&Threading::writer_thread, &followerThread1, &follower1Write, ++i));
		std::unique_ptr<std::thread> follower2_Write(new std::thread(&Threading::writer_thread, &followerThread2, &follower2Write, ++i));
		std::unique_ptr<std::thread> follower3_Write(new std::thread(&Threading::writer_thread, &followerThread3, &follower3Write, ++i));
		prod.detach();
		leader_Consumer->detach();
		leader_Consum2->detach();
		leader_Writer->detach();
		follower1_Consumer->detach();
		follower2_Consumer->detach();
		follower3_Consumer->detach();
		follower1_Write->detach();
		follower2_Write->detach();
		follower3_Write->detach();

		//threads.emplace(leader_Consumer);
		//threads.emplace(leader_Consum2);
		//threads.emplace(leader_Writer);
		//threads.emplace(follower1_Consumer);
		//threads.emplace(follower2_Consumer);
		//threads.emplace(follower3_Consumer);
		//threads.emplace(follower1_Write);
		//threads.emplace(follower2_Write);
		//threads.emplace(follower3_Write);

		while (!follower1Write.is_completed() || !follower2Write.is_completed() || !follower3Write.is_completed()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		std::chrono::time_point<std::chrono::steady_clock> end = std::chrono::high_resolution_clock::now();
		std::cout << "Execution time of " << std::chrono::duration_cast<std::chrono::milliseconds>(end - Threading::begin).count() << "ms\nfor Raft with "
			<< leaderThread.RequestsPerTick << " requests every " << leaderThread.packageMillis << "milliseconds:\n"
			<< "Total requests: " << leaderThread.totalRequests << " , total producing time: " << (leaderThread.totalRequests / leaderThread.RequestsPerTick) * leaderThread.packageMillis << "ms" << std::endl;

		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}
	else {
		prod.detach();
		for (int n = 0; n < threadCount; n++) {
			std::shared_ptr<std::thread> leader_Consumer(new std::thread(&Threading::consumer_thread_parallel, &leaderThread, &requestQueue, &leaderWrite, &leaderWaiting, &followers, nullptr));
			std::shared_ptr<std::thread> leader_Consum2(new std::thread(&Threading::consumer_thread_parallel, &leaderThread, &fromFollToLeaderQueue, &leaderWrite, &leaderWaiting, &followers, nullptr));
			std::shared_ptr<std::thread> leader_Writer(new std::thread(&Threading::writer_thread, &leaderThread, &leaderWrite, ++i));
				
			std::shared_ptr<std::thread> follower1_Consumer(new std::thread(&Threading::consumer_thread_parallel, &followerThread1, &follower1Input, &follower1Write, &follower1Waiting, &std::vector<BlockingCollection<IOPolling::request*>*>(), &fromFollToLeaderQueue));
			std::shared_ptr<std::thread> follower2_Consumer(new std::thread(&Threading::consumer_thread_parallel, &followerThread2, &follower2Input, &follower2Write, &follower2Waiting, &std::vector<BlockingCollection<IOPolling::request*>*>(), &fromFollToLeaderQueue));
			std::shared_ptr<std::thread> follower3_Consumer(new std::thread(&Threading::consumer_thread_parallel, &followerThread3, &follower3Input, &follower3Write, &follower3Waiting, &std::vector<BlockingCollection<IOPolling::request*>*>(), &fromFollToLeaderQueue));
				 
			std::shared_ptr<std::thread> follower1_Write(new std::thread(&Threading::writer_thread, &followerThread1, &follower1Write, ++i));
			std::shared_ptr<std::thread> follower2_Write(new std::thread(&Threading::writer_thread, &followerThread2, &follower2Write, ++i));
			std::shared_ptr<std::thread> follower3_Write(new std::thread(&Threading::writer_thread, &followerThread3, &follower3Write, ++i));
			
			leader_Consumer->detach();
			leader_Consum2->detach();
			leader_Writer->detach();
			follower1_Consumer->detach();
			follower2_Consumer->detach();
			follower3_Consumer->detach();
			follower1_Write->detach();
			follower2_Write->detach();
			follower3_Write->detach();

			threads.push_back(leader_Consumer);
			threads.push_back(leader_Consum2);
			threads.push_back(leader_Writer);
			threads.push_back(follower1_Consumer);
			threads.push_back(follower2_Consumer);
			threads.push_back(follower3_Consumer);
			threads.push_back(follower1_Write);
			threads.push_back(follower2_Write);
			threads.push_back(follower3_Write);
		}
		while (!follower1Write.is_completed() || !follower2Write.is_completed() || !follower3Write.is_completed()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		threads.clear();
		std::chrono::time_point<std::chrono::steady_clock> end = std::chrono::high_resolution_clock::now();
		std::cout << "Execution time of " << std::chrono::duration_cast<std::chrono::milliseconds>(end - Threading::begin).count() << "ms\nfor ParallelRaft with " 
			<< leaderThread.RequestsPerTick << " requests every " << leaderThread.packageMillis << "milliseconds:\n" 
			<< "Total requests: " << leaderThread.totalRequests << " , total producing time: " << (leaderThread.totalRequests / leaderThread.RequestsPerTick) * leaderThread.packageMillis << "ms" << std::endl;
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}
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


