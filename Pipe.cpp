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
#include "test_utilities.h"
#include <Bela.h>  // auxiliaryTask

static int testDone;

static void nonRtThread(void* arg)
{
	Pipe* pipe = (Pipe*)arg;
	std::array<float, 1024> rec;
	int count;
	int read;

	count = 123;

	printf("waiting for %d\n", count);
	read = pipe->readNonRt(rec.data(), count);
	printf("received %d\n", read);

	printf("waiting for %d\n", count);
	read = pipe->readNonRt(rec.data(), count);
	printf("received %d\n", read);

	testDone++;
}

static void rtThread(void* arg)
{
	Pipe* pipe = (Pipe*)arg;
	std::array<float, 1024> send;
	int count, ret;
	count = 123;
	printf("sending %d\n", count);
	ret = pipe->writeRt(send.data(), count);
	printf("sent: %d\n", ret);
	usleep(100000);

	count = 100;
	printf("sending %d\n", count);
	ret = pipe->writeRt(send.data(), count);
	printf("sent: %d\n", ret);
	testDone++;
}

int testPipe()
{
	Pipe pipe("assaaa", 8192, false);
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
	Pipe bpipe("blocking", 8192, true);
	testDone = 0;
	Bela_scheduleAuxiliaryTask( Bela_createAuxiliaryTask(nonRtThread, 50, "nonrt", &bpipe));
	Bela_scheduleAuxiliaryTask( Bela_createAuxiliaryTask(rtThread, 50, "rt", &bpipe));

	while(testDone != 2)
		usleep(100000);
	printf("Test for Pipe successful\n");
	exit (0);
}
#endif

