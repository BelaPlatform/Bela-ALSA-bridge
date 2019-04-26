#include <vector>
/// A non-thread safe circular buffer

template <class T> class CircularBuffer : public std::vector<T>
{
public:
	CircularBuffer<T>(){};
	CircularBuffer<T>(int count)
	{ setup(count); }
	void setup(int length);
	void reset();
	int write(T* dataToWrite, int count);
	int read(T* dataToRead, int count);
	int availableToWrite();
	int availableToRead();
	float available();
	T* getReadPointer(int& count);
	T* getWritePointer(int& count);
private:
	int readPtr;
	int writePtr;
	bool full;
};

template <class T>
void CircularBuffer<T>::setup(int count)
{
	std::vector<T>::resize(count);
	reset();
}

template <class T>
void CircularBuffer<T>::reset()
{
	readPtr = 0;
	writePtr = 0;
	full = false;
}

#undef NDEBUG
#include <assert.h>
#include <string.h>
template <class T>
int CircularBuffer<T>::write(T* dataToWrite, int count)
{
	int size = std::vector<T>::size();
	T* data = std::vector<T>::data();
	int ava = availableToWrite();
	int written = std::min(count, ava);
	count = written;
	while(count)
	{
		int toWrite = std::min(size - writePtr, count);
		toWrite = std::min(toWrite, availableToWrite());
		memcpy(data + writePtr, dataToWrite, toWrite * sizeof(*data));
		count -= toWrite;
		dataToWrite += toWrite;
		writePtr += toWrite;
		if(writePtr == size)
			writePtr = 0;
		assert(writePtr < size);
	}
	if(ava == written)
		full = true;
	return written;
}

template <class T>
int CircularBuffer<T>::read(T* dataToRead, int count)
{
	int size = std::vector<T>::size();
	T* data = std::vector<T>::data();
	int read = std::min(count, availableToRead());
	count = read;
	while(count)
	{
		int toRead = std::min(size - readPtr, count);
		toRead = std::min(toRead, availableToRead());
		memcpy(dataToRead, data + readPtr, toRead * sizeof(*data));
		count -= toRead;
		dataToRead += toRead;
		readPtr += toRead;
		if(readPtr == size)
			readPtr = 0;
		assert(readPtr < size);
	}
	if(full && read)
		full = false;
	return read;
}

template <class T>
int CircularBuffer<T>::availableToWrite()
{
	if(full)
		return 0;
	int size = std::vector<T>::size();
	int count = (readPtr - writePtr + size);
	if(count > size)
		count -= size;
	return count;
}
template <class T>
int CircularBuffer<T>::availableToRead()
{
	int size = std::vector<T>::size();
	if(full)
		return size;
	return (writePtr - readPtr + size) % size;
}

template <class T>
float CircularBuffer<T>::available()
{
	return availableToRead() / (float)std::vector<T>::size();
}
#if 1
#include "test_utilities.h"
static int testCircularBuffer()
{
	CircularBuffer<float> circ;
	int circLen = 4096;
	circ.setup(circLen);
	assert(circ.availableToRead() == 0);
	assert(circ.availableToWrite() == circLen);

	// fill it in and make sure the availabilities are still right
	std::vector<float> dummy(circLen);
	assert(circ.write(dummy.data(), dummy.size()));
	assert(circ.availableToRead() == circLen);
	assert(circ.availableToWrite() == 0);

	assert(!circ.write(dummy.data(), dummy.size()));
	assert(circ.availableToRead() == circLen);
	assert(circ.availableToWrite() == 0);

	std::vector<float> in(100);
	for(auto & val : in)
		val = rand();
	std::vector<float> out(in.size());
	
	int count = in.size();
	int written = 0;
	int read = 0;
	int inCirc = 0;
	circ.reset();
	for(int val = 0; val < 2; ++val)
	{
		for(int n = 0; n < 1000; ++n)
		{
			int nWrite = 1;
			// read and write asymmetrically
			if((n & 1) == val)
				nWrite = 4;
			int nRead = 1;
			if((n & 1) == !val)
				nRead = 4;
			for(int c = 0; c < nWrite; ++c)
			{
				int ret = circ.write(in.data(), count);
				assert(ret == count);
				written += ret;
				inCirc = written - read;
				assert(circ.availableToRead() == inCirc);
				assert(circ.availableToWrite() == circLen - inCirc);
			}

			for(int c = 0; c < nRead; ++c)
			{
				read += circ.read(out.data(), count);
				inCirc = written - read;
				assert(circ.availableToRead() == inCirc);
				assert(circ.availableToWrite() == circLen - inCirc);

				assert(areEqual(out, in));
			}
		}
	}
	
	printf("testCircularBuffer() was successful\n");
	return 0;
}
# endif
