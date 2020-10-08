#include <memory>

std::shared_ptr<char> loggingBuffer;
int counter;

char* getBufferPointer() {
	return loggingBuffer.get() + counter;
}