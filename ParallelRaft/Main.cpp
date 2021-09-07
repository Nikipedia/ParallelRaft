#include <thread>
#include <memory>
#include <random>
#include "BlockingCollection.h"
#include <fstream>
#include <iostream>
#include <string>
#include <set>
#include <cstring>
#include <list>
#include "Request.cpp"
using namespace std;


using namespace code_machina;

/// <summary>
/// Main class that simulates IO requests and either manages them in order (Raft) or parallel/out of order (ParallelRaft)
/// </summary>
class Threading {
	
public:

	//packages are sent every x ms 
	int packageMillis = 1;

	// this is the IO stack depth, the amount of IO requests at once
	int RequestsPerTick = 32;

	int totalRequests = 32000;

	int randomSpread = 255;

	// any blocking and pending management has to be under lock
	std::mutex blockingMutex;
	std::vector<std::set<int>> pendingNumbers = std::vector<std::set<int>>(256);
	std::vector < std::list<IO::request*>> pendingRequests = std::vector<std::list<IO::request*>>(256);
	std::vector<bool> blockedCells = vector<bool>(256);

	//this is the mutex for the local vector containing all requests waiting for acknowledgements
	std::mutex reqMutex;

	static std::chrono::time_point<std::chrono::steady_clock> begin;

	int latency = 0;
	int latencyMeasures = 0;

	static int networkConditions;

	unsigned int recentNumber = 0;

	std::mutex numberMutex;

	fstream myfile;

	/// <summary>
	/// the producer thread creates requests according to the parameters and puts them into the request queue. The cells these requests want to edit are chosen randomly (with .
	/// </summary>
	/// <param name="requestQueue"></param>
	void producer_thread(BlockingCollection<IO::request*>* requestQueue) {
		int messageLimit = 0;
		unsigned int totalCount = 0;
		requestQueue->attach_producer();
		std::this_thread::sleep_for(std::chrono::milliseconds(packageMillis));
		begin = std::chrono::steady_clock::now();
		int lookBehindOne = -1;
		int lookBehindTwo = -1;
		while (messageLimit < totalRequests)
		{
			
			if (requestQueue->size() < 30) {
				for (int i = 0; i < RequestsPerTick; i++) {
					IO::request* r = new IO::request();
					r->number = totalCount;
					// i for input, w for write, a for acknowledge, c for commit
					r->type = 'i';

					// track the time of some packets to get a representative average
					if (totalCount % 100 == 0 && totalCount != 0 && totalCount != totalRequests) {
						r->sentTime = std::chrono::steady_clock::now();
					}
					// data block "index" to edit
					r->modifiedCell = abs(rand() % randomSpread) + 1;
					// small letter to write into the data blocks (assuming chars or strings as data)
					r->data[0] = (char)(rand() % 26 + 65);

					// track the data blocks edited by previous packets (that might not arrive earlier)
					r->lookBehind[0] = lookBehindOne;
					r->lookBehind[1] = lookBehindTwo;

					lookBehindTwo = lookBehindOne;
					lookBehindOne = r->modifiedCell;
					totalCount = totalCount + 1;

					// blocks if collection.size() == collection.bounded_capacity()
					requestQueue->add(r);
				}
				messageLimit += RequestsPerTick;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(packageMillis));

		}
		// send a special message to confirm end of requests
		IO::request* r = new IO::request();
		r->number = totalCount;
		// i for input, w for write, a for acknowledge, c for commit, e for end of transmission
		r->type = 'e';
		// blocks if collection.size() == collection.bounded_capacity()
		requestQueue->add(r);

		while (!requestQueue->is_empty()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(packageMillis));
		}
	}

	/// <summary>
	/// created as a thread to parallelize the sending of acknowledgements, could be further optimized using multiple worker threads instead of repeated thread creation
	/// </summary>
	/// <param name="leaderQueue"></param>
	/// <param name="request"></param>
	void writeAndAck(BlockingCollection<IO::request*> * leaderQueue, IO::request * request) {
		IO::request* copy = new IO::request(request);
		copy->type = 'a';
		if (!leaderQueue->is_adding_completed()) {
			leaderQueue->add(copy);
		}
		delete request;
	}

	//Raft version can only take the elements in correct order
	void consumer_thread(BlockingCollection<IO::request*> * input, BlockingCollection<IO::request*> * write, std::set<IO::request*> * wait, std::vector<BlockingCollection<IO::request*>*> * followers, BlockingCollection<IO::request*> * leaderQueue) {
		bool leader = false;
		bool waitingForFinish = false;
		if (followers->size() > 0) {
			leader = true;
			for (BlockingCollection<IO::request*>* follower : *followers) {
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
			IO::request* currentDat;

			// take will block if there is no data to be taken
			auto status = input->take(currentDat);

			if (status == BlockingCollectionStatus::Ok)
			{
				// end of transmission gets broadcasted to followers
				if (currentDat->type == 'e') {
					if (leader) {
						waitingForFinish = true;
						for (BlockingCollection<IO::request*>* follower : *followers) {
							IO::request* copy = new IO::request(currentDat);
							if (!follower->is_adding_completed()) {
								follower->add(copy);
							}
						}
					}
				}
				else {
					if (leader) numberMutex.lock();
					if (currentDat->number != recentNumber)
					{
						if (currentDat->type != 'a') {
							input->add(new IO::request(currentDat));
						}
					}
					else {
						// if the data type is "input" we are the leader and send a write request to the followers
						if (currentDat->type == 'i') {

							currentDat->type = 'w';

							//add copies to other queues

							IO::request* copy2 = new IO::request(currentDat);
							write->add(copy2);

							if (leader) {
								for (BlockingCollection<IO::request*>* follower : *followers) {
									IO::request* copy = new IO::request(currentDat);
									if (!follower->is_adding_completed()) {
										follower->add(copy);
									}
								}
							}

							IO::request* copy = new IO::request(currentDat);
							reqMutex.lock();

							wait->emplace(copy);
							reqMutex.unlock();

						}
						// if the data type is write we are a follower and should answer with an acknowledgement
						else if (currentDat->type == 'w') {

							IO::request* copy2 = new IO::request(currentDat);
							std::thread WriteAndAck(&Threading::writeAndAck, this, &(*leaderQueue), copy2);
							WriteAndAck.detach();

							IO::request* copy = new IO::request(currentDat);
							reqMutex.lock();
							wait->emplace(copy);
							reqMutex.unlock();
						}

						// the leader receives 3 acknowledgements so it sends commit as soon as the counter reaches 2.
						else if (currentDat->type == 'a') {
							reqMutex.lock();
							IO::request* deleted = nullptr;
							for (IO::request* r : *wait) {
								if (r != nullptr && r->number == currentDat->number) {
									r->timesUsed++;

									if (r->timesUsed > 1) {
										deleted = r;
									}
									break;
								}
							}
							if (deleted != nullptr) {
								wait->erase(deleted);
							}
							reqMutex.unlock();

							if (deleted != nullptr) {
								recentNumber = currentDat->number + 1;
								//write commit to hard drive
								IO::request* copy2 = new IO::request(currentDat);
								copy2->type = 'c';
								write->add(copy2);

								//send commit to all followers
								for (BlockingCollection<IO::request*>* follower : *followers) {
									IO::request* copy = new IO::request(currentDat);
									copy->type = 'c';
									if (!follower->is_adding_completed()) {
										follower->add(copy);
									}
								}
								if (currentDat->number % 100 == 0 && currentDat->number != 0 && currentDat->number != totalRequests)
								{
									std::chrono::time_point<std::chrono::steady_clock> end = std::chrono::steady_clock::now();
									latency += std::chrono::duration_cast<std::chrono::microseconds>(end - currentDat->sentTime).count();
									latencyMeasures++;
								}
								delete deleted;
							}
						}
						// receiving commits means we write the data to disk and the commit to the log
						else if (currentDat->type == 'c') {

							reqMutex.lock();
							IO::request* deleted = nullptr;
							for (IO::request* r : *wait) {
								if (r != nullptr && r->number == currentDat->number) {
									deleted = r;
									break;
								}
							}
							if (deleted != nullptr) {
								wait->erase(deleted);
							}
							reqMutex.unlock();
							if (deleted != nullptr) {
								delete deleted;
								recentNumber = currentDat->number + 1;
								IO::request* copy2 = new IO::request(currentDat);
								write->add(copy2);
							}
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

			for (BlockingCollection<IO::request*>* follower : *followers) {
				follower->complete_adding();
			}
			//remove this for tests
			reqMutex.lock();
			for (IO::request* req : *wait) {
				std::cout << "still in waiting: " << (int)req->number << " with " << req->timesUsed << " commit messages" << "\n";
			}
			reqMutex.unlock();
		}
	}

	//Parallel Raft version
	void consumer_thread_parallel(BlockingCollection<IO::request*> * input, BlockingCollection<IO::request*> * write, std::set<IO::request*> * wait, std::vector<BlockingCollection<IO::request*>*> * followers, BlockingCollection<IO::request*> * leaderQueue) {
		bool removeCellBlocker = false;
		bool leader = false;
		bool waitingForFinish = false;
		bool allowed = false;
		bool reset = false;
		unsigned int rct = 0;
		bool deleteCurrent = true;
		if (followers->size() > 0) {
			leader = true;
			for (BlockingCollection<IO::request*>* follower : *followers) {
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
			IO::request* currentDat;

			// take will block if there is no data to be taken
			auto status = input->take(currentDat);

			if (status == BlockingCollectionStatus::Ok)
			{
				allowed = false;
				deleteCurrent = true;
				// end of transmission gets broadcasted to followers
				if (currentDat->type == 'e') {
					if (leader) {
						waitingForFinish = true;
						for (BlockingCollection<IO::request*>* follower : *followers) {
							IO::request* copy = new IO::request(currentDat);
							if (!follower->is_adding_completed()) {
								follower->add(copy);
							}
						}
					}
				}
				else {
					blockingMutex.lock();
					rct = recentNumber;
					// lock to prevent e.g. unnecessarily getting put into pending
					// data is filling a hole
					if (currentDat->number < rct) {
						// there are no holes modifying this cell
						if (blockedCells[currentDat->modifiedCell] == false) {
							// only happens to the third ack message, can be thrown away
							allowed = false;
						}
						// we are the lowest hole with that cell
						else if (pendingNumbers[currentDat->modifiedCell].empty() || *pendingNumbers[currentDat->modifiedCell].begin() >= currentDat->number) {
							removeCellBlocker = true;
							allowed = true;
						}
						// we have to wait for an earlier request of the same cell
						else {
							deleteCurrent = false;
							pendingRequests[currentDat->modifiedCell].push_back(currentDat);
							allowed = false;
						}
					}
					else if (currentDat->number <= rct + 2) {
						// add the hole cells into the waiting queue
						if (currentDat->number == rct + 1 && currentDat->type != 'a' && currentDat->type != 'c') {
							blockedCells[currentDat->lookBehind[0]] = true;
							pendingNumbers[currentDat->lookBehind[0]].insert(rct);
						}
						else if (currentDat->number == rct + 2 && currentDat->type != 'a' && currentDat->type != 'c') {
							blockedCells[currentDat->lookBehind[1]] = true;
							blockedCells[currentDat->lookBehind[0]] = true;
							pendingNumbers[currentDat->lookBehind[1]].insert(rct);
							pendingNumbers[currentDat->lookBehind[0]].insert(rct + 1);
						}
						if (!blockedCells[currentDat->modifiedCell]) {
							allowed = true;
						}
						else if (pendingNumbers[currentDat->modifiedCell].empty() || *pendingNumbers[currentDat->modifiedCell].begin() >= currentDat->number) {
							removeCellBlocker = true;
							allowed = true;
						}
						// we have to wait for an earlier request of the same cell
						else {
							bool isInPending = false;
							for (int x : pendingNumbers[currentDat->modifiedCell]) {
								if (x == currentDat->number) {
									isInPending = true;
									break;
								}
							}
							deleteCurrent = false;
							pendingRequests[currentDat->modifiedCell].push_back(currentDat);
							if (!isInPending) pendingNumbers[currentDat->modifiedCell].insert(currentDat->number);
							allowed = false;
						}
					}
					else if (currentDat->type != 'a') {
						input->add(currentDat);
						deleteCurrent = false;
					}
					blockingMutex.unlock();
					if (allowed) {
						// if the data type is "input" we are the leader and send a write request to the followers
						if (currentDat->type == 'i') {

							currentDat->type = 'w';

							//add copies to other queues
							IO::request* copy2 = new IO::request(currentDat);
							write->add(copy2);

							if (leader) {
								for (BlockingCollection<IO::request*>* follower : *followers) {
									IO::request* copy = new IO::request(currentDat);
									if (!follower->is_adding_completed()) {
										follower->add(copy);
									}
								}
							}
							deleteCurrent = false;
							reqMutex.lock();
							wait->emplace(currentDat);
							reqMutex.unlock();

						}
						// if the data type is write we are a follower and should answer with an acknowledgement
						else if (currentDat->type == 'w') {

							IO::request* copy2 = new IO::request(currentDat);

							std::thread WriteAndAck(&Threading::writeAndAck, this, &(*leaderQueue), copy2);
							WriteAndAck.detach();
							
							deleteCurrent = false;
							reqMutex.lock();
							wait->emplace(currentDat);
							reqMutex.unlock();
						}

						// the leader receives 3 acknowledgements so it sends commit as soon as the counter reaches 2.
						else if (currentDat->type == 'a') {
							
							reqMutex.lock();
							IO::request* deleted = nullptr;
							for (IO::request* r : *wait) {
								if (r != nullptr && r->number == currentDat->number) {
									r->timesUsed++;

									if (r->timesUsed > 1) {
										deleted = r;
									}
									break;
								}
							}
							if (deleted != nullptr) {
								wait->erase(deleted);
							}
							reqMutex.unlock();

							if (deleted != nullptr) {
								
								//write commit to hard drive
								IO::request * copy2 = new IO::request(currentDat);
								copy2->type = 'c';
								write->add(copy2);

								//send commit to all followers
								for (BlockingCollection<IO::request*>* follower : *followers) {
									IO::request* copy = new IO::request(currentDat);
									copy->type = 'c';
									if (!follower->is_adding_completed()) {
										follower->add(copy);
									}
								}
								if (currentDat->number % 100 == 0 && currentDat->number != 0 && currentDat->number != totalRequests) {
									std::chrono::time_point<std::chrono::steady_clock> end = std::chrono::steady_clock::now();
									latency += std::chrono::duration_cast<std::chrono::microseconds>(end - currentDat->sentTime).count();
									latencyMeasures++;
								}
								blockingMutex.lock();
								numberMutex.lock();
								if (currentDat->number + 1 > recentNumber) recentNumber = currentDat->number + 1;
								numberMutex.unlock();
								if (removeCellBlocker || blockedCells[currentDat->modifiedCell]) {
									pendingNumbers[currentDat->modifiedCell].erase(currentDat->number);
									// there are no remaining holes with that cell
									if (pendingNumbers[currentDat->modifiedCell].size() == 0) {
										blockedCells[currentDat->modifiedCell] = false;
									}
									// if there is a pending request of the lowest hole, insert into queue
									else {
										IO::request* y = nullptr;
										for (IO::request* x : pendingRequests[currentDat->modifiedCell])
										{
											if (x->number == *pendingNumbers[currentDat->modifiedCell].begin()) {
												input->add(x);
											}
										}
										int f = *pendingNumbers[currentDat->modifiedCell].begin();
										pendingRequests[currentDat->modifiedCell].remove_if([f](IO::request * x) {return x->number == f; });
									}
								}
								blockingMutex.unlock();
								delete deleted;

							}
							
						}
						// receiving commits means we write the data to disk and the commit to the log
						else if (currentDat->type == 'c') {

							reqMutex.lock();
							IO::request* deleted = nullptr;
							for (IO::request* r : *wait) {
								if (r != nullptr && r->number == currentDat->number) {
									deleted = r;
									break;
								}
							}
							if (deleted != nullptr) {
								wait->erase(deleted);
							}
							reqMutex.unlock();

							if (deleted != nullptr) {
								blockingMutex.lock();
								if (currentDat->number + 1 > recentNumber) recentNumber = currentDat->number + 1;
								if (removeCellBlocker || blockedCells[currentDat->modifiedCell]) {
									pendingNumbers[currentDat->modifiedCell].erase(currentDat->number);
									// there are no remaining holes with that cell
									if (pendingNumbers[currentDat->modifiedCell].size() == 0) {
										blockedCells[currentDat->modifiedCell] = false;
									}
									// if there is a pending request of the lowest hole, insert into queue
									else {
										for (IO::request* x : pendingRequests[currentDat->modifiedCell])
										{
											if (x->number == *pendingNumbers[currentDat->modifiedCell].begin()) {
												input->add(x);
											}
										}
										int f = *pendingNumbers[currentDat->modifiedCell].begin();
										pendingRequests[currentDat->modifiedCell].remove_if([f](IO::request * x) {return x->number == f; });
									}

								}
								blockingMutex.unlock();
								deleteCurrent = false;
								write->add(currentDat);

								delete deleted;
							}
							else {
								deleteCurrent = false;
								blockingMutex.lock();
								pendingRequests[currentDat->modifiedCell].push_back(currentDat);
								pendingNumbers[currentDat->modifiedCell].insert(currentDat->number);
								blockedCells[currentDat->modifiedCell] = true;
								blockingMutex.unlock();
							}
						}
					}					
				}
				if (deleteCurrent) delete currentDat;
			}

		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		write->complete_adding();
		if (leader) {
			for (BlockingCollection<IO::request*>* follower : *followers) {
				follower->complete_adding();
			}
		}
	}


	/// <summary>
	/// this thread logs all writes and commits into a log file that could be used for information recovery
	/// </summary>
	/// <param name="writingQueue">requests that should be logged </param>
	/// <param name="nodeNumber">unique identifier for the file name </param>
	void writer_thread(BlockingCollection<IO::request*> * writingQueue, int nodeNumber) {
		std::string s = std::to_string(nodeNumber);
		string x = "log" + s;
		x.append(".txt");
		myfile.open(x, fstream::out);
		writingQueue->attach_consumer();
		while (!writingQueue->is_completed()) {
			IO::request* currentDat;

			// take will block if there is no data to be taken
			auto status = writingQueue->take(currentDat);

			if (status == BlockingCollectionStatus::Ok)
			{
				//IO::request r = *currentDat;
				if (currentDat->type == 'c' || currentDat->type == 'w') {
					myfile << (int)currentDat->number << " " << currentDat->type << " " << currentDat->modifiedCell << "\n";
				}
				delete currentDat;
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

std::chrono::time_point<std::chrono::steady_clock> Threading::begin = std::chrono::steady_clock::now();
int Threading::networkConditions = 1;

int main(int argc, char** argv) {
	InputParser input(argc, argv);
	bool parallelRaft = false;
	bool testing = false;
	if (input.cmdOptionExists("-help")) {
		std::cout << "Usage: -t for testing mode, -p for parallelRaft instead of Raft,\n-d followed by the IO stack depth,\n-c followed by the amount of IO threads per node\n" <<
			"-n followed by the amount of requests, -m for the milliseconds between each package, -r for random range of cells\n";
	}
	if (input.cmdOptionExists("-t")) {
		testing = true;
	}
	if (input.cmdOptionExists("-p")) {
		parallelRaft = true;
	}

	//amount of I/O threads on each node
	int threadCount = 1;
	Threading leaderThread;
	Threading followerThread1;
	Threading followerThread2;
	Threading followerThread3;

	const std::string& inputString = input.getCmdOption("-d");
	if (!inputString.empty()) {
		int stackDepth = std::stoi(inputString);
		if (stackDepth > 0 && stackDepth < 500)
			leaderThread.RequestsPerTick = stackDepth;
		else leaderThread.RequestsPerTick = 4;
	}
	const std::string& inputString2 = input.getCmdOption("-c");
	if (!inputString2.empty()) {
		int thrC = std::stoi(inputString2);
		if (thrC > 0 && thrC < 21)
			threadCount = thrC;
	}
	const std::string& inputString3 = input.getCmdOption("-n");
	if (!inputString3.empty()) {
		int count = std::stoi(inputString3);
		if (count > 0 && count < 1000000)
			leaderThread.totalRequests = count;
		else leaderThread.totalRequests = 5000;
	}
	const std::string& inputString4 = input.getCmdOption("-m");
	if (!inputString4.empty()) {
		int count = std::stoi(inputString4);
		if (count > 0 && count < 100)
			leaderThread.packageMillis = count;
		else leaderThread.packageMillis = 5;
	}
	const std::string& inputString5 = input.getCmdOption("-r");
	if (!inputString5.empty()) {
		int count = std::stoi(inputString5);
		if (count > 1 && count < 256)
			leaderThread.randomSpread = count;
		else leaderThread.randomSpread = 255;
	}

	for (int i = 0; i < 256; i++) {
		leaderThread.blockedCells[i] = false;
		followerThread1.blockedCells[i] = false;
		followerThread2.blockedCells[i] = false;
		followerThread3.blockedCells[i] = false;
	}


	//IO leader;
	BlockingCollection<IO::request*> requestQueue(400);
	BlockingCollection<IO::request*> fromFollToLeaderQueue(400);
	std::set<IO::request*> leaderWaiting;
	std::set<IO::request*> follower1Waiting;
	std::set<IO::request*> follower2Waiting;
	std::set<IO::request*> follower3Waiting;
	BlockingCollection<IO::request*> follower1Input(400);
	BlockingCollection<IO::request*> follower2Input(400);
	BlockingCollection<IO::request*> follower3Input(400);

	std::vector<BlockingCollection<IO::request*>*> followers;
	followers.push_back(&follower1Input);
	followers.push_back(&follower2Input);
	followers.push_back(&follower3Input);


	
	std::thread prod(&Threading::producer_thread, &leaderThread, &requestQueue);

	std::vector<std::shared_ptr<std::thread>> threads;

	int i = 0;
	BlockingCollection<IO::request*> leaderWrite(400);
	BlockingCollection<IO::request*> follower1Write(400);
	BlockingCollection<IO::request*> follower2Write(400);
	BlockingCollection<IO::request*> follower3Write(400);
	std::vector<BlockingCollection<IO::request*>*> dummyFollowers;
	if (!parallelRaft) {
		std::unique_ptr<std::thread> leader_Consumer(new std::thread(&Threading::consumer_thread, &leaderThread, &requestQueue, &leaderWrite, &leaderWaiting, &followers, nullptr));
		std::unique_ptr<std::thread> leader_Consum2(new std::thread(&Threading::consumer_thread, &leaderThread, &fromFollToLeaderQueue, &leaderWrite, &leaderWaiting, &followers, nullptr));
		std::unique_ptr<std::thread> leader_Writer(new std::thread(&Threading::writer_thread, &leaderThread, &leaderWrite, i));

		std::unique_ptr<std::thread> follower1_Consumer(new std::thread(&Threading::consumer_thread, &followerThread1, &follower1Input, &follower1Write, &follower1Waiting, &dummyFollowers, &fromFollToLeaderQueue));
		std::unique_ptr<std::thread> follower2_Consumer(new std::thread(&Threading::consumer_thread, &followerThread2, &follower2Input, &follower2Write, &follower2Waiting, &dummyFollowers, &fromFollToLeaderQueue));
		std::unique_ptr<std::thread> follower3_Consumer(new std::thread(&Threading::consumer_thread, &followerThread3, &follower3Input, &follower3Write, &follower3Waiting, &dummyFollowers, &fromFollToLeaderQueue));

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

		while (!follower1Write.is_completed() || !follower2Write.is_completed() || !follower3Write.is_completed()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		std::chrono::time_point<std::chrono::steady_clock> end = std::chrono::steady_clock::now();
		if (!testing) {
			std::cout << "Execution time of " << std::chrono::duration_cast<std::chrono::milliseconds>(end - Threading::begin).count() << "ms\nfor Raft with "
				<< leaderThread.RequestsPerTick << " requests every " << leaderThread.packageMillis << "milliseconds:\n"
				<< "Total requests: " << leaderThread.totalRequests << " , total producing time: " << (leaderThread.totalRequests / leaderThread.RequestsPerTick) * leaderThread.packageMillis << "ms"
				<< " latency: " << ((float)leaderThread.latency) / leaderThread.latencyMeasures << "us" << std::endl;

			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		}
		else
		{
			std::cout << (1000 * ((long)leaderThread.totalRequests)) / std::chrono::duration_cast<std::chrono::milliseconds>(end - Threading::begin).count() << ", " << ((float)leaderThread.latency) / leaderThread.latencyMeasures << "\t";
		}
	}
	else {
		prod.detach();

		std::shared_ptr<std::thread> leader_Writer(new std::thread(&Threading::writer_thread, &leaderThread, &leaderWrite, ++i));
		leader_Writer->detach();
		threads.push_back(leader_Writer);

		std::shared_ptr<std::thread> follower1_Write(new std::thread(&Threading::writer_thread, &followerThread1, &follower1Write, ++i));
		std::shared_ptr<std::thread> follower2_Write(new std::thread(&Threading::writer_thread, &followerThread2, &follower2Write, ++i));
		std::shared_ptr<std::thread> follower3_Write(new std::thread(&Threading::writer_thread, &followerThread3, &follower3Write, ++i));
		follower1_Write->detach();
		follower2_Write->detach();
		follower3_Write->detach();
		threads.push_back(follower1_Write);
		threads.push_back(follower2_Write);
		threads.push_back(follower3_Write);

		for (int n = 0; n < threadCount; n++) {
			std::shared_ptr<std::thread> leader_Consumer(new std::thread(&Threading::consumer_thread_parallel, &leaderThread, &requestQueue, &leaderWrite, &leaderWaiting, &followers, nullptr));
			std::shared_ptr<std::thread> leader_Consum2(new std::thread(&Threading::consumer_thread_parallel, &leaderThread, &fromFollToLeaderQueue, &leaderWrite, &leaderWaiting, &followers, nullptr));

			std::shared_ptr<std::thread> follower1_Consumer(new std::thread(&Threading::consumer_thread_parallel, &followerThread1, &follower1Input, &follower1Write, &follower1Waiting, &dummyFollowers, &fromFollToLeaderQueue));
			std::shared_ptr<std::thread> follower2_Consumer(new std::thread(&Threading::consumer_thread_parallel, &followerThread2, &follower2Input, &follower2Write, &follower2Waiting, &dummyFollowers, &fromFollToLeaderQueue));
			std::shared_ptr<std::thread> follower3_Consumer(new std::thread(&Threading::consumer_thread_parallel, &followerThread3, &follower3Input, &follower3Write, &follower3Waiting, &dummyFollowers, &fromFollToLeaderQueue));

			leader_Consumer->detach();
			leader_Consum2->detach();

			follower1_Consumer->detach();
			follower2_Consumer->detach();
			follower3_Consumer->detach();

			threads.push_back(leader_Consumer);
			threads.push_back(leader_Consum2);

			threads.push_back(follower1_Consumer);
			threads.push_back(follower2_Consumer);
			threads.push_back(follower3_Consumer);
		}
		while (!follower1Write.is_completed() || !follower2Write.is_completed() || !follower3Write.is_completed()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		threads.clear();
		std::chrono::time_point<std::chrono::steady_clock> end = std::chrono::steady_clock::now();
		if (!testing) {
			std::cout << "Execution time of " << std::chrono::duration_cast<std::chrono::milliseconds>(end - Threading::begin).count() << "ms\nfor ParallelRaft with "
				<< leaderThread.RequestsPerTick << " requests every " << leaderThread.packageMillis << "milliseconds:\n"
				<< "Total requests: " << leaderThread.totalRequests << " , total producing time: " << (leaderThread.totalRequests / leaderThread.RequestsPerTick) * leaderThread.packageMillis << "ms"
				<< " latency: " << ((float)leaderThread.latency) / leaderThread.latencyMeasures << "us, I/O threads: " << threadCount << std::endl;
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		}
		else
		{
			std::cout << (1000 * ((long)leaderThread.totalRequests)) / std::chrono::duration_cast<std::chrono::milliseconds>(end - Threading::begin).count() << ", " << ((float)leaderThread.latency) / leaderThread.latencyMeasures << "\t";
		}
	}
	if(!testing)system("pause");
	return 0;

}