#pragma once

#include <zita-alsa-pcmi.h>
#include "Pipe.h"
#include "CircularBuffer.h"
#include <Bela.h> // for AuxiliaryTask, which should anyhow be replaced
//#define ONE_THREAD

class AlsaIo
{
public:
	bool setup(const char* device, unsigned int fromHostChannels, unsigned int toHostChannels, unsigned int newBlockSize, unsigned int bufferSize, float rate);
	void cleanup();
	void sendReceive(float* fromHost, float* toHost, int samples);
#ifdef ONE_THREAD
	static void task(void* nil);
#else // ONE_THREAD
	static void inputTask(void* nil);
	static void outputTask(void* nil);
#endif // ONE_THREAD
	static Alsa_pcmi* createAlsaDevice(const char *play_name,
			const char *capt_name,
			const char *ctrl_name,
			unsigned int samplingRate,
			unsigned int blockSize,
			unsigned int numBlocks,
			unsigned int debug);
	void logBuffers();
private:
	void sendToRt(float* toRt, int samples);
	int receiveFromRt(float* fromRt, int samples);
#ifdef ONE_THREAD
	void hostIo();
	CircularBuffer<float> hostIoInputBuffer;
#else // ONE_THREAD
	void hostInput();
	void hostOutput();
#endif // ONE_THREAD
	CircularBuffer<float> fromHostBuffer;
	CircularBuffer<float> toHostBuffer;
	int blockSize;
#ifdef ONE_THREAD
	Alsa_pcmi *D;
	AuxiliaryTask auxTask;
#else // ONE_THREAD
	Alsa_pcmi *inputDevice;
	Alsa_pcmi *outputDevice;
	AuxiliaryTask auxTaskInput;
	AuxiliaryTask auxTaskOutput;
#endif // ONE_THREAD
	Pipe toHostPipe;
	Pipe fromHostPipe;
};


