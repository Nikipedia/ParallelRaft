#include <thread>
#include <memory>
#include <random>
#include "IOPolling.cpp"
#include "BlockingCollection.h"
#include <fstream>
#include <iostream>
#include <string>
#include <set>
#include <cstring>


using namespace code_machina;


class Threading {
	//serves as the PolarSwitch that simulates lots of IO requests
public:

	//packages are sent every 50ms 
	int packageMillis = 5;

	// this is the IO stack depth, the amount of IO requests at once
	int RequestsPerTick = 64;

	int totalRequests = 32000;

	int randomSpread = 250;

	//std::vector< std::list< int > > pendingQueue;
	std::mutex pendingQueueMutex;
	std::vector<std::set<int>> pendingNumbers = std::vector<std::set<int>>(256);
	std::vector < std::list<IOPolling::request*>> pendingRequests = std::vector<std::list<IOPolling::request*>>(256);
	std::vector<bool> blockedCells = vector<bool>(256);

	//this is the mutex for the local vector containing all requests waiting for acknowledgements
	std::mutex reqMutex;

	static std::chrono::time_point<std::chrono::steady_clock> begin;

	int latency = 0;
	int latencyMeasures = 0;

	static int networkConditions;

	unsigned int recentNumber = 0;

	std::mutex numberMutex;

	//bool parallelExecution = true;

	//std::mutex parallelMutex;

	//unsigned int waitForNumber = 0;

	fstream myfile;

	~Threading() {
		//delete[]blockedCells;
		//delete[]pendingNumbers;
		//delete[]pendingRequests;
	}

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
		std::this_thread::sleep_for(std::chrono::milliseconds(packageMillis));
		begin = std::chrono::steady_clock::now();
		int lookBehindOne = -1;
		int lookBehindTwo = -1;
		while (messageLimit < totalRequests)
		{
			if (requestQueue->size() < 30) {
				for (int i = 0; i < RequestsPerTick; i++) {
					IOPolling::request* r = new IOPolling::request();
					r->number = totalCount;
					// i for input, w for write, a for acknowledge, c for commit
					r->type = 'i';
					if (totalCount % 100 == 0 && totalCount != 0 && totalCount != totalRequests) {
						r->sentTime = std::chrono::steady_clock::now();
					}
					// data block "index" to edit
					r->modifiedCell = (unsigned char)((rand() % 255) % randomSpread + 1);
					// small letter to write into the data blocks (assuming chars or strings as data)
					r->data[0] = (char)(rand() % 26 + 65);
					r->lookBehind[0] = lookBehindOne;
					r->lookBehind[1] = lookBehindTwo;

					lookBehindTwo = lookBehindOne;
					lookBehindOne = r->modifiedCell;
					//collection.add(r);
					//std::cout << "request " << (int)totalCount << " written\n";
					totalCount = totalCount + 1;

					// blocks if collection.size() == collection.bounded_capacity()
					requestQueue->add(r);
				}
				messageLimit += RequestsPerTick;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(packageMillis));

		}
		// send a special message to confirm end of requests
		IOPolling::request* r = new IOPolling::request();
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

	//a separate thread to simulate network delay
	void addToQueue(BlockingCollection<IOPolling::request*> * queue, IOPolling::request * request) {
		srand(time(NULL));
		switch (networkConditions) {
		case 1: //std::this_thread::sleep_for(std::chrono::milliseconds(rand() % 5 + 5)); 
			break;
		case 2: std::this_thread::sleep_for(std::chrono::milliseconds(rand() % 10 + 15)); break;
		case 3: std::this_thread::sleep_for(std::chrono::milliseconds(rand() % 20 + 20 + (rand() % 2) * 60)); break;
		default: std::this_thread::sleep_for(std::chrono::milliseconds(rand() % 10 + 15)); break;
		}

		if (!queue->is_adding_completed()) {
			queue->add(request);
		}
	}

	void writeAndAck(BlockingCollection<IOPolling::request*> * leaderQueue, IOPolling::request * request) {
		//myfile << (int)request->number << " " << request->type << " " << request->modifiedCell << "\n";
		IOPolling::request* copy = new IOPolling::request(request);
		copy->type = 'a';
		if (!leaderQueue->is_adding_completed()) {
			leaderQueue->add(copy);
		}
		delete request;
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
				//IOPolling::request data = *currentDat;
				if (currentDat->type == 'e') {
					if (leader) {
						waitingForFinish = true;
						for (BlockingCollection<IOPolling::request*>* follower : *followers) {
							IOPolling::request* copy = new IOPolling::request(currentDat);
							//follower->add(&copy);
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
						//if(leader) std::cout << "Leader falsely received " << (int)currentDat->number << "\n";
						//if ((0 < (int)(data.number) - recentNumber && (int)(data.number) - recentNumber < 5) || ((int)(data.number) - recentNumber > 250)) {
						if (currentDat->type != 'a') {
							//std::cout << "Readded " << (int)data.number << " with " << data.type << "\n";
							//std::thread queueAdder(&Threading::addToQueue, this, input, new IOPolling::request(data));
							//queueAdder.detach();
							input->add(new IOPolling::request(currentDat));
						}
					}
					else {
						// if the data type is "input" we are the leader and send a write request to the followers
						if (currentDat->type == 'i') {

							/*if (wait->size() == 1) {
								std::cout << "big list";
							}*/

							//std::cout << "Leader received " << (int)currentDat->number << "\n";
							currentDat->type = 'w';

							//add copies to other queues

							IOPolling::request* copy2 = new IOPolling::request(currentDat);
							write->add(copy2);

							if (leader) {
								for (BlockingCollection<IOPolling::request*>* follower : *followers) {
									IOPolling::request* copy = new IOPolling::request(currentDat);
									//follower->add(&copy);
									if (!follower->is_adding_completed()) {
										follower->add(copy);
									}
								}
							}

							IOPolling::request* copy = new IOPolling::request(currentDat);
							reqMutex.lock();

							wait->emplace(copy);
							reqMutex.unlock();

						}
						// if the data type is write we are a follower and should answer with an acknowledgement
						else if (currentDat->type == 'w') {


							//if (currentDat->number % 2 == 0) {
								//std::cout << "Follower Received " << (int)currentDat->number << "\n";
							//}

							IOPolling::request* copy2 = new IOPolling::request(currentDat);
							write->add(copy2);
							//writeAndAck(leaderQueue, copy2);

							IOPolling::request* copy = new IOPolling::request(currentDat);
							reqMutex.lock();
							wait->emplace(copy);
							reqMutex.unlock();
						}

						// the leader receives 3 acknowledgements so it sends commit as soon as the counter reaches 2.
						else if (currentDat->type == 'a') {
							reqMutex.lock();
							//if (leader)std::cout << "list size" << wait->size() << "\n";
							IOPolling::request* deleted = nullptr;
							for (IOPolling::request* r : *wait) {
								if (r != nullptr && r->number == currentDat->number) {
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
								recentNumber = currentDat->number + 1;
								//write commit to hard drive
								IOPolling::request* copy2 = new IOPolling::request(currentDat);
								copy2->type = 'c';
								write->add(copy2);

								//send commit to all followers
								for (BlockingCollection<IOPolling::request*>* follower : *followers) {
									IOPolling::request* copy = new IOPolling::request(currentDat);
									copy->type = 'c';
									//follower->add(&copy);
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
							IOPolling::request* deleted = nullptr;
							for (IOPolling::request* r : *wait) {
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
								IOPolling::request* copy2 = new IOPolling::request(currentDat);
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
		bool removeCellBlocker = false;
		bool leader = false;
		bool waitingForFinish = false;
		bool allowed = false;
		bool reset = false;
		int rct = 0;
		bool deleteCurrent = true;
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
				/*IOPolling::request data = *currentDat;
				data.lookBehind = currentDat->lookBehind;
				data.data = currentDat->data;
				if (data.lookBehind[1] == currentDat->lookBehind[1]) {

				}*/
				// end of input 
				allowed = false;
				deleteCurrent = true;
				if (currentDat->type == 'e') {
					if (leader) {
						waitingForFinish = true;
						for (BlockingCollection<IOPolling::request*>* follower : *followers) {
							IOPolling::request* copy = new IOPolling::request(currentDat);
							//follower->add(&copy);
							if (!follower->is_adding_completed()) {
								follower->add(copy);
							}
						}
					}
				}
				else {
					pendingQueueMutex.lock();
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
							//std::cout << "blocked cell " << currentDat->lookBehind[0] << " for req " << rct << "\n";
						}
						else if (currentDat->number == rct + 2 && currentDat->type != 'a' && currentDat->type != 'c') {
							blockedCells[currentDat->lookBehind[1]] = true;
							blockedCells[currentDat->lookBehind[0]] = true;
							pendingNumbers[currentDat->lookBehind[1]].insert(rct);
							pendingNumbers[currentDat->lookBehind[0]].insert(rct + 1);
							//std::cout << "blocked cell " << currentDat->lookBehind[1] << " for req " << rct << "\n";
							//std::cout << "blocked cell " << currentDat->lookBehind[0] << " for req " << rct + 1 << "\n";
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
					pendingQueueMutex.unlock();
					if (allowed) {
						//if(leader)std::cout << "is in strict mode: " << !parallelExecution <<"\n";
						// if the data type is "input" we are the leader and send a write request to the followers
						if (currentDat->type == 'i') {

							//std::cout << "Leader received " << (int)currentDat->number << "\n";
							currentDat->type = 'w';

							//add copies to other queues
							IOPolling::request* copy2 = new IOPolling::request(currentDat);
							write->add(copy2);

							if (leader) {
								for (BlockingCollection<IOPolling::request*>* follower : *followers) {
									IOPolling::request* copy = new IOPolling::request(currentDat);
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

							IOPolling::request* copy2 = new IOPolling::request(currentDat);

							//std::thread WriteAndAck(&Threading::writeAndAck, this, &(*leaderQueue), copy2);
							//WriteAndAck.detach();
							//writeAndAck(leaderQueue, copy2);

							write->add(copy2);

							// keep the cell locked but remove it from pendingNumbers as indicator that commit is possible
							/*if (removeCellBlocker) {
								pendingQueueMutex.lock();
								pendingNumbers[currentDat->modifiedCell].erase(currentDat->number);
								pendingQueueMutex.unlock();
							}*/

							deleteCurrent = false;
							reqMutex.lock();
							wait->emplace(currentDat);
							reqMutex.unlock();
						}

						// the leader receives 3 acknowledgements so it sends commit as soon as the counter reaches 2.
						else if (currentDat->type == 'a') {
							reqMutex.lock();
							//if (leader)std::cout << "list size" << wait->size() << "\n";
							IOPolling::request* deleted = nullptr;
							for (IOPolling::request* r : *wait) {
								if (r != nullptr && r->number == currentDat->number) {
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
								
								numberMutex.lock();
								if (currentDat->number + 1 > recentNumber) recentNumber = currentDat->number + 1;
								numberMutex.unlock();
								//write commit to hard drive
								IOPolling::request * copy2 = new IOPolling::request(currentDat);
								copy2->type = 'c';
								write->add(copy2);

								//send commit to all followers
								for (BlockingCollection<IOPolling::request*>* follower : *followers) {
									IOPolling::request* copy = new IOPolling::request(currentDat);
									copy->type = 'c';
									//follower->add(&copy);
									if (!follower->is_adding_completed()) {
										follower->add(copy);
									}
								}
								if (currentDat->number % 100 == 0 && currentDat->number != 0 && currentDat->number != totalRequests) {
									std::chrono::time_point<std::chrono::steady_clock> end = std::chrono::steady_clock::now();
									latency += std::chrono::duration_cast<std::chrono::microseconds>(end - currentDat->sentTime).count();
									latencyMeasures++;
								}
								pendingQueueMutex.lock();
								if (removeCellBlocker || blockedCells[currentDat->modifiedCell]) {
									pendingNumbers[currentDat->modifiedCell].erase(currentDat->number);
									// there are no remaining holes with that cell
									if (pendingNumbers[currentDat->modifiedCell].size() == 0) {
										//std::cout << "unblocked cell " << currentDat->modifiedCell << " for req " << currentDat->number << "\n";
										blockedCells[currentDat->modifiedCell] = false;
									}
									// if there is a pending request of the lowest hole, insert into queue
									else {
										IOPolling::request* y = nullptr;
										for (IOPolling::request* x : pendingRequests[currentDat->modifiedCell])
										{
											if (x->number == *pendingNumbers[currentDat->modifiedCell].begin()) {
												input->add(x);
											}
										}
										int f = *pendingNumbers[currentDat->modifiedCell].begin();
										pendingRequests[currentDat->modifiedCell].remove_if([f](IOPolling::request* x) {return x->number == f; });
									}
								}
								pendingQueueMutex.unlock();
								delete deleted;

							}
						}
						// receiving commits means we write the data to disk and the commit to the log
						else if (currentDat->type == 'c') {
							
							reqMutex.lock();
							IOPolling::request* deleted = nullptr;
							for (IOPolling::request* r : *wait) {
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
								numberMutex.lock();
								if (currentDat->number + 1 > recentNumber) recentNumber = currentDat->number + 1;
								numberMutex.unlock();
								pendingQueueMutex.lock();
								if (removeCellBlocker || blockedCells[currentDat->modifiedCell]) {
									pendingNumbers[currentDat->modifiedCell].erase(currentDat->number);
									// there are no remaining holes with that cell
									if (pendingNumbers[currentDat->modifiedCell].size() == 0) {
										blockedCells[currentDat->modifiedCell] = false;
									}
									// if there is a pending request of the lowest hole, insert into queue
									else {
										for (IOPolling::request* x : pendingRequests[currentDat->modifiedCell])
										{
											if (x->number == *pendingNumbers[currentDat->modifiedCell].begin()) {
												input->add(x);
											}
										}
										int f = *pendingNumbers[currentDat->modifiedCell].begin();
										pendingRequests[currentDat->modifiedCell].remove_if([f](IOPolling::request * x) {return x->number == f; });
									}
									
								}
								pendingQueueMutex.unlock();
								deleteCurrent = false;
								write->add(currentDat);

								delete deleted;
							}
							else {
								deleteCurrent = false;
								pendingQueueMutex.lock();
								pendingRequests[currentDat->modifiedCell].push_back(currentDat);
								pendingNumbers[currentDat->modifiedCell].insert(currentDat->number);
								blockedCells[currentDat->modifiedCell] = true;
								pendingQueueMutex.unlock();
							}
						}
					}
					//if(leader) std::cout << "Leader falsely received " << (int)data.number << "\n";
						//if ((0 < (int)(data.number) - recentNumber && (int)(data.number) - recentNumber < 5) || ((int)(data.number) - recentNumber > 250)) {
					/*else if (currentDat->type != 'a' || currentDat->number >= rct) {
						//std::cout << "Readded " << (int)data.number << " with " << data.type << "\n";
						//std::thread queueAdder(&Threading::addToQueue, this, input, new IOPolling::request(data));
						//queueAdder.detach();
						input->add(new IOPolling::request(currentDat));

					}*/
				}
				/*data.data = nullptr;
				data.lookBehind = nullptr;*/
				if(deleteCurrent) delete currentDat;
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

	void writer_thread(BlockingCollection<IOPolling::request*> * writingQueue, BlockingCollection<IOPolling::request*>* leaderQueue, bool leader, int nodeNumber) {
		std::string s = std::to_string(nodeNumber);
		string x = "log" + s;
		x.append(".txt");
		myfile.open(x, fstream::out);
		writingQueue->attach_consumer();
		std::cout << x;
		while (!writingQueue->is_completed()) {
			IOPolling::request* currentDat;

			// take will block if there is no data to be taken
			auto status = writingQueue->take(currentDat);
			int u = 0;

			if (status == BlockingCollectionStatus::Ok)
			{
				IOPolling::request r = *currentDat;
				if (currentDat->type == 'c' || currentDat->type == 'w') {
					myfile << (int)currentDat->number << " " << currentDat->type << " " << currentDat->modifiedCell << "\n";
					//if(nodeNumber==2)std::cout << (int)currentDat->number << " " << currentDat->type << "\n";

				}
				if (currentDat->type == 'w' && !leader) {
					//myfile << (int)currentDat->number << " " << currentDat->type << " " << currentDat->modifiedCell << "\n";
					IOPolling::request* copy = new IOPolling::request(r);
					copy->type = 'a';
					if (!leaderQueue->is_adding_completed()) {
						leaderQueue->add(copy);
					}
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
	if (input.cmdOptionExists("-help")) {
		std::cout << "Usage: -p for parallelRaft instead of Raft,\n-d followed by the IO stack depth,\n-c followed by the amount of IO threads per node\n" <<
			"-n followed by the amount of requests\n";
	}

	if (input.cmdOptionExists("-p")) {
		parallelRaft = true;
	}
	else parallelRaft = false;

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

	for (int i = 0; i < 256; i++) {
		leaderThread.blockedCells[i] = false;
		followerThread1.blockedCells[i] = false;
		followerThread2.blockedCells[i] = false;
		followerThread3.blockedCells[i] = false;
		//leaderThread.pendingRequests[i] = std::list<IOPolling::request*>();
		//followerThread1.pendingRequests[i] = std::list<IOPolling::request*>();
		//followerThread2.pendingRequests[i] = std::list<IOPolling::request*>();
		//followerThread3.pendingRequests[i] = std::list<IOPolling::request*>();
	}


	//IOPolling leader;
	BlockingCollection<IOPolling::request*> requestQueue(400);
	BlockingCollection<IOPolling::request*> fromFollToLeaderQueue(400);
	std::set<IOPolling::request*> leaderWaiting;
	std::set<IOPolling::request*> follower1Waiting;
	std::set<IOPolling::request*> follower2Waiting;
	std::set<IOPolling::request*> follower3Waiting;
	BlockingCollection<IOPolling::request*> follower1Input(400);
	BlockingCollection<IOPolling::request*> follower2Input(400);
	BlockingCollection<IOPolling::request*> follower3Input(400);

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
	BlockingCollection<IOPolling::request*> leaderWrite(400);
	BlockingCollection<IOPolling::request*> follower1Write(400);
	BlockingCollection<IOPolling::request*> follower2Write(400);
	BlockingCollection<IOPolling::request*> follower3Write(400);
	std::vector<BlockingCollection<IOPolling::request*>*> dummyFollowers;
	if (!parallelRaft) {
		std::unique_ptr<std::thread> leader_Consumer(new std::thread(&Threading::consumer_thread, &leaderThread, &requestQueue, &leaderWrite, &leaderWaiting, &followers, nullptr));
		std::unique_ptr<std::thread> leader_Consum2(new std::thread(&Threading::consumer_thread, &leaderThread, &fromFollToLeaderQueue, &leaderWrite, &leaderWaiting, &followers, nullptr));
		std::unique_ptr<std::thread> leader_Writer(new std::thread(&Threading::writer_thread, &leaderThread, &leaderWrite, nullptr, true, i));

		std::unique_ptr<std::thread> follower1_Consumer(new std::thread(&Threading::consumer_thread, &followerThread1, &follower1Input, &follower1Write, &follower1Waiting, &dummyFollowers, &fromFollToLeaderQueue));
		std::unique_ptr<std::thread> follower2_Consumer(new std::thread(&Threading::consumer_thread, &followerThread2, &follower2Input, &follower2Write, &follower2Waiting, &dummyFollowers, &fromFollToLeaderQueue));
		std::unique_ptr<std::thread> follower3_Consumer(new std::thread(&Threading::consumer_thread, &followerThread3, &follower3Input, &follower3Write, &follower3Waiting, &dummyFollowers, &fromFollToLeaderQueue));

		std::unique_ptr<std::thread> follower1_Write(new std::thread(&Threading::writer_thread, &followerThread1, &follower1Write, &fromFollToLeaderQueue, false, ++i));
		std::unique_ptr<std::thread> follower2_Write(new std::thread(&Threading::writer_thread, &followerThread2, &follower2Write, &fromFollToLeaderQueue, false, ++i));
		std::unique_ptr<std::thread> follower3_Write(new std::thread(&Threading::writer_thread, &followerThread3, &follower3Write, &fromFollToLeaderQueue, false, ++i));
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
		std::chrono::time_point<std::chrono::steady_clock> end = std::chrono::steady_clock::now();
		std::cout << "Execution time of " << std::chrono::duration_cast<std::chrono::milliseconds>(end - Threading::begin).count() << "ms\nfor Raft with "
			<< leaderThread.RequestsPerTick << " requests every " << leaderThread.packageMillis << "milliseconds:\n"
			<< "Total requests: " << leaderThread.totalRequests << " , total producing time: " << (leaderThread.totalRequests / leaderThread.RequestsPerTick) * leaderThread.packageMillis << "ms"
			<< " latency: " << ((float)leaderThread.latency) / leaderThread.latencyMeasures << "us" << std::endl;

		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}
	else {
		prod.detach();

		std::shared_ptr<std::thread> leader_Writer(new std::thread(&Threading::writer_thread, &leaderThread, &leaderWrite, nullptr, true, ++i));
		leader_Writer->detach();
		threads.push_back(leader_Writer);

		std::shared_ptr<std::thread> follower1_Write(new std::thread(&Threading::writer_thread, &followerThread1, &follower1Write, &fromFollToLeaderQueue, false, ++i));
		std::shared_ptr<std::thread> follower2_Write(new std::thread(&Threading::writer_thread, &followerThread2, &follower2Write, &fromFollToLeaderQueue, false, ++i));
		std::shared_ptr<std::thread> follower3_Write(new std::thread(&Threading::writer_thread, &followerThread3, &follower3Write, &fromFollToLeaderQueue, false, ++i));
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
		std::cout << "Execution time of " << std::chrono::duration_cast<std::chrono::milliseconds>(end - Threading::begin).count() << "ms\nfor ParallelRaft with "
			<< leaderThread.RequestsPerTick << " requests every " << leaderThread.packageMillis << "milliseconds:\n"
			<< "Total requests: " << leaderThread.totalRequests << " , total producing time: " << (leaderThread.totalRequests / leaderThread.RequestsPerTick) * leaderThread.packageMillis << "ms"
			<< " latency: " << ((float)leaderThread.latency) / leaderThread.latencyMeasures << "us, I/O threads: " << threadCount << std::endl;
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}
	system("pause");
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


