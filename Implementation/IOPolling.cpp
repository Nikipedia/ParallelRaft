#include <memory>


void listener(std::shared_ptr<char> bufferRead, LogWriter[] writers) {
	while (true) {
		// w for writing, r for reading
		int counter = 0;
		if (*bufferRead.get() == 'w' || *bufferRead.get() == 'r') {
			if(bufferWrite)
		}
	}
}