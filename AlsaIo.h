#pragma once

#include <zita-alsa-pcmi.h>
#include <libraries/Pipe/Pipe.h>
#include "CircularBuffer.h"
#include <Bela.h> // for AuxiliaryTask, which should anyhow be replaced

class AlsaIo
{
public:
	bool setup(const char* device, unsigned int fromHostChannels, unsigned int toHostChannels, unsigned int newBlockSize, unsigned int bufferSize, float rate);
	void cleanup();
	void sendReceive(float* fromHost, float* toHost, int samples);
	static void task(void* nil);
private:
	void sendToRt(float* toRt, int samples);
	int receiveFromRt(float* fromRt, int samples);
	void hostIo();
	enum { numBufs = 2 };
	CircularBuffer<float> hostIoInputBuffer;
	CircularBuffer<float> fromHostBuffer;
	CircularBuffer<float> toHostBuffer;
	float *buf;
	const char* playdev;
	const char* captdev;
	int blockSize;
	Alsa_pcmi  *D;
	AuxiliaryTask auxTask;
	Pipe toHostPipe;
	Pipe fromHostPipe;
};


