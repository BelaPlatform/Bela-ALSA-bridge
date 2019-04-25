#include "Pipe.h"
#include <xenomai_wraps.h>

std::string Pipe::defaultName;

bool Pipe::setup(const std::string& pipeName, size_t size, bool blocking)
{
	pipeSize = size;

	name = "p_" + pipeName;
	int ret = createXenomaiPipe(name.c_str(), pipeSize);
	if(ret < 0)
	{
		fprintf(stderr, "Unable to create pipe %s with %u bytes: (%i) %s\n", name.c_str(), pipeSize, ret, strerror(ret));
		return false;
	}
	pipeSocket = ret;
	setRtBlocking(blocking);
	path = "/proc/xenomai/registry/rtipc/xddp/" + name;
	// no idea why a usleep(0) is needed. Give it a bit more time,
	// just in case
	usleep(10000);
	unsigned int blockingFlag = blocking ? 0 : O_NONBLOCK;
	fd = open(path.c_str(), O_RDWR | blockingFlag);
	if(fd < 0)
	{
		fprintf(stderr, "Unable to open pipe %s: (%i) %s\n", path.c_str(), errno, strerror(errno));
		//TODO: close the pipe
		return false;
	
	}
	return true;
}

void Pipe::setRtBlocking(bool blocking)
{
	int flags = __wrap_fcntl(pipeSocket, F_GETFL);
	if(blocking)
	{
		flags ^= O_NONBLOCK;
	} else {
		flags |= O_NONBLOCK;
	}
	if(int ret = __wrap_fcntl(pipeSocket, F_SETFL, flags))
	{
		fprintf(stderr, "Unable to set socket non blocking\n");
	}
}
void Pipe::cleanup()
{
	close(fd);
	__wrap_close(pipeSocket);
}

bool Pipe::_writeNonRt(void* ptr, size_t size)
{
	ssize_t ret = write(fd, (void*)ptr, size);
	if(ret < 0 || ret != size)
	{
		return false;
	}
	return true;
}

bool Pipe::_writeRt(void* ptr, size_t size)
{
	ssize_t ret = __wrap_send(pipeSocket, (void*)ptr, size, 0);
	if(ret < 0 || ret != size)
	{
		return false;
	}
	return true;
}

ssize_t Pipe::_readRt(void* ptr, size_t size)
{
	return __wrap_recv(pipeSocket, ptr, size, 0);
}

ssize_t Pipe::_readNonRt(void* ptr, size_t size)
{
	return read(fd, ptr, size);
}

#if 1
// tests
#include <stdlib.h>
#include <array>
#undef NDEBUG
#include <assert.h>

template <typename T1, typename T2>
static bool areEqual(const T1& vec1, const T2& vec2)
{
	if(vec1.size() != vec2.size())
		return false;	
	for(unsigned int n = 0; n < vec1.size(); ++n)
	{
		if(vec1[n] != vec2[n])
		{
			return false;
		}
	}
	return true;
}

template <typename T>
static void scramble(T& vec)
{
	for(unsigned int n = 0; n < vec.size(); ++n)
	{
		vec[n] = rand();
	}
}

int testPipe()
{
	Pipe pipe("assaaa", 8192, false);
	pipe.setRtBlocking(true);
	std::array<float, 1000> payload;
	{
		//Rt to NonRt
		scramble(payload);
		bool success = pipe.writeRt(payload);
		assert(success);
		std::array<float, payload.size()> rec;
		int ret = pipe.readNonRt(rec.data(), rec.size());
		assert(ret == rec.size());
		assert(areEqual(payload, rec));
		ret = pipe.readNonRt(rec.data(), rec.size());
		assert(areEqual(payload, rec));
		assert(ret != rec.size());
		assert(ret < 0);
	}
	{
		//NonRt to Rt
		scramble(payload);
		bool success = pipe.writeNonRt(payload);
		assert(success);
		std::array<float, payload.size()> rec;
		int ret = pipe.readRt(rec.data(), rec.size());
		assert(ret == rec.size());
		assert(areEqual(payload, rec));
		ret = pipe.readRt(rec.data(), rec.size());
		assert(areEqual(payload, rec));
		assert(ret != rec.size());
		assert(ret < 0);
	}

	printf("Test for Pipe successful\n");
	exit (0);
}
#endif

