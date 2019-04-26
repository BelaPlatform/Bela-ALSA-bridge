#include "AlsaIo.h"

#include <array>

extern "C" {
extern int volatile gShouldStop;
};

bool AlsaIo::setup(const char* device, unsigned int fromHostChannels, unsigned int toHostChannels, unsigned int newBlockSize, unsigned int circularBufferSize, float rate)
{
#ifdef ONE_THREAD
	bool blockingPipe = false;
#else // ONE_THREAD
	bool blockingPipe = true;
#endif // ONE_THREAD
	toHostPipe.setup("UACToHost", 8192*16, blockingPipe);
	fromHostPipe.setup("UACFromHost", 8192*16, blockingPipe);
	// never block on the RT side
	toHostPipe.setRtBlocking(false);
	fromHostPipe.setRtBlocking(false);

	blockSize = newBlockSize;
#ifdef ONE_THREAD
	hostIoInputBuffer.resize(newBlockSize * toHostChannels * 4);
#else // ONE_THREAD
	// this only ever needs one buffer worth of space, because of how we
	// handle that in outputTask
	//hostIoInputBuffer.resize(newBlockSize * toHostChannels);
#endif // ONE_THREAD
	fromHostBuffer.resize(circularBufferSize * toHostChannels);
	toHostBuffer.resize(circularBufferSize * toHostChannels);
	// dummy write to start with the buffer half full (or half empty)
#ifdef ONE_THREAD
	hostIoInputBuffer.write(hostIoInputBuffer.data(), hostIoInputBuffer.size()/2);
#endif // ONE_THREAD
	fromHostBuffer.write(fromHostBuffer.data(), fromHostBuffer.size()/2);
	toHostBuffer.write(toHostBuffer.data(), toHostBuffer.size()/2);
	int samplingRate = (int)(rate + 0.5f);
	int numBlocks = 2;

	// Create and initialise the audio device.
#ifdef ONE_THREAD
	D = createAlsaDevice(device, device, NULL, samplingRate, blockSize, numBlocks, 1);
#else // ONE_THREAD
	inputDevice = createAlsaDevice(NULL, device, NULL, samplingRate, blockSize, numBlocks, 15);
	outputDevice = createAlsaDevice(device, NULL, NULL, samplingRate, blockSize, numBlocks, 15);
#endif // ONE_THREAD

#ifdef ONE_THREAD
	auxTask = Bela_createAuxiliaryTask(task, 94, "alsa_task", this);
#else // ONE_THREAD
	auxTaskInput = Bela_createAuxiliaryTask(inputTask, 94, "alsa_input", this);
	auxTaskOutput = Bela_createAuxiliaryTask(outputTask, 94, "alsa_output", this);
#endif // ONE_THREAD
	return true;
}

Alsa_pcmi* AlsaIo::createAlsaDevice(const char *play_name,
		const char *capt_name,
		const char *ctrl_name,
		unsigned int samplingRate,
		unsigned int blockSize,
		unsigned int numBlocks,
		unsigned int debug)
{
	Alsa_pcmi* D = new Alsa_pcmi (play_name, capt_name, ctrl_name, samplingRate, blockSize, numBlocks, debug);
	if(D->state())
	{
		fprintf (stderr, "Can't open ALSA device\n");
		delete D;
		exit (1);
	}
	if((capt_name && D->ncapt() != 2) || (play_name && D->nplay() != 2))
	{
		fprintf (stderr, "Expected a stereo device.\n");
		delete D;
		exit (1);
	}
	D->printinfo();
	// Start the audio device.
	D->pcm_start();
	return D;
}

void AlsaIo::sendReceive(float* fromHost, float* toHost, int samples)
{
#ifdef ONE_THREAD
	Bela_scheduleAuxiliaryTask(auxTask);
#else // ONE_THREAD
	Bela_scheduleAuxiliaryTask(auxTaskInput);
	Bela_scheduleAuxiliaryTask(auxTaskOutput);
#endif // ONE_THREAD
	const int overrunThreshold = fromHostBuffer.size() * 0.2;
	const int underrunThreshold = fromHostBuffer.size() * 0.2;
	const int overrunCompensation = fromHostBuffer.size() * 0.3f;
	const int underrunCompensation = fromHostBuffer.size() * 0.3f;

	std::array<float, 4096> tmp;

	// buffer new output
	int written;
	if((written = toHostBuffer.write(toHost, samples)) < samples)
	{
		rt_printf("BAD: toHostBuffer was full upon write: only could fit %d out of %d\n", written, samples);
	}

	// always retrieve all the data available
	// and send back the same amount
	int read;
	while((read = fromHostPipe.readRt(tmp.data(), tmp.size())) > 0)
	{
		// buffer the received data
		if(read != fromHostBuffer.write(tmp.data(), read))
		{
			rt_printf("BAD: fromHostBuffer was full upon write\n");
		}
		// send the same amount of data back
		if(read != toHostBuffer.read(tmp.data(), read))
		{
			rt_printf("BAD: toHostBuffer was empty upon read\n");
		}
		toHostPipe.writeRt(tmp.data(), read);
	}

	// check space available in the local buffer
	bool underrun = false;
	bool overrun = false;
	int avaWrite = fromHostBuffer.availableToWrite();
	if(avaWrite < overrunThreshold)
	{
		rt_printf("\nAbout to overrun: only %d samples to write\n", avaWrite);
		overrun = true;
	}
	int avaRead = fromHostBuffer.availableToRead();
	if(avaRead < underrunThreshold)
	{
		rt_printf("\nAbout to underrun: only %d samples to read\n", avaRead);
		underrun = true;
	}

	if(overrun)
	{
		// when overruning the input buffer, we should:
		// - free some space in the input buffer
		fromHostBuffer.read(tmp.data(), overrunCompensation);
		// - add some padding to the output buffer
		toHostBuffer.write(tmp.data(), overrunCompensation);
	} else if(underrun) {
		// when underrunning the output buffer, we should:
		// - add some padding to the input buffer
		fromHostBuffer.write(tmp.data(), underrunCompensation);
		// - free some space in the output buffer
		toHostBuffer.read(tmp.data(), underrunCompensation);
	}

	// prepare the result for the caller
	if(underrun)
	{
		return;
	}
	read = fromHostBuffer.read(fromHost, samples);
	if(read != samples)
	{
		rt_printf("\nBAD: fromHostBuffer was empty upon read: %d out of %d\n", read, samples);
	}
}

void AlsaIo::sendToRt(float* toRt, int samples)
{
	if(!fromHostPipe.writeNonRt(toRt, samples))
		printf("BAD: sendToRt failed\n");
}

#ifdef ONE_THREAD
int AlsaIo::receiveFromRt(float* fromRt, int samples)
{
	// always read everything that is available from the rt into the buffer
	std::array<float, 1024> tmp;
	int read;
	while((read = toHostPipe.readNonRt(tmp.data(), tmp.size())) > 0)
	{
		int ret;
		ret = hostIoInputBuffer.write(tmp.data(), read);
		if(ret != read)
		{
			printf("\nBAD: hostIoInputBuffer was full upon write: wrote %d out of %d\n", ret, read);
		}
	}

	if(hostIoInputBuffer.availableToRead() < samples)
	{
		printf("\nUNDERRUN hostIoInputBuffer only had %d out of %d for read\n", hostIoInputBuffer.availableToRead(), samples);
		return samples;
	}
	// only provide the desired number of samples
	return hostIoInputBuffer.read(fromRt, samples);
}

void AlsaIo::task(void* obj)
{
	AlsaIo* that = (AlsaIo*)obj;
	that->hostIo();
}

void AlsaIo::hostIo()
{
//#define GET_TIME
#ifdef GET_TIME
	struct timespec tp1, tp2;
	int ret = clock_gettime(CLOCK_REALTIME, &tp1);
	printf("It \n");
#endif /* GET_TIME */
	printf("hostIo started\n");
	int k = D->pcm_wait();
	do
	{
#ifdef DEBUG
		gpio2.set();
#endif /* DEBUG */
		static int count = 0;
		++count;
		if(count % 100 == 0)
		{
			logBuffers();
		}
		if (k < blockSize)
		{
			// Normally we shouldn't do this in a real-time context.
			fprintf (stderr, "\nError: pcm_wait returned %d.\n", k);
			continue;
		}
		while (k >= blockSize)
		{
			//printf("waited for %d\n", k);
			std::array<float, 1024> tmp1;
			std::array<float, 1024> tmp2;
			float* toHost = tmp1.data();
			float* fromHost = tmp2.data();
			int numSamples = 2 * blockSize;

			int received = receiveFromRt(toHost, numSamples);
			if(received != numSamples)
			{
				printf("\nBAD: hostIo has %d samples to write instead of %d\n", received, numSamples);
			}

			D->capt_init (blockSize);
			D->capt_chan (0, fromHost + 0, blockSize, 2);
			D->capt_chan (1, fromHost + 1, blockSize, 2);
			//D->capt_chan (1, buf + 0, blockSize, 2); // loopback
			D->capt_done (blockSize);

			D->play_init (blockSize);
			//D->play_chan (1, buf + 0, blockSize, 2); //loopback
			D->play_chan (0, toHost + 0, blockSize, 2); // send audioOut   
			D->play_chan (1, toHost + 1, blockSize, 2); // send audioOut     

			// Clear all other outputs.
			for (int i = 2; i < D->nplay (); i++) D->clear_chan (i, blockSize);
			D->play_done (blockSize);
			
			sendToRt(fromHost, numSamples);
			k -= blockSize;
		}
#ifdef GET_TIME
		ret = clock_gettime(CLOCK_REALTIME, &tp2);
		if(ret){
			fprintf(stderr, "\nFailed to read clock\n");
		} else {
			long long int ns1 = tp1.tv_sec * 1000000000ULL + tp1.tv_nsec;
			long long int ns2 = tp2.tv_sec * 1000000000ULL + tp2.tv_nsec;
			long long int delta = ns2 - ns1;
			printf("\nTook %lld\n", delta);
		}
#endif /* GET_TIME */
#ifdef DEBUG
		gpio2.clear();
#endif /* DEBUG */
	}
	while((k = D->pcm_wait()) && !gShouldStop);
	printf("\nhostIo finished: k is %d\n", k);
}
#else // ONE_THREAD
int AlsaIo::receiveFromRt(float* fromRt, int samples)
{
	// block on the pipe (possibly multiple times) till we get enough samples
	int totalRead = 0;
	while(samples)
	{
		printf("toHostPipe reading %d\n", samples);
		int read = toHostPipe.readNonRt(fromRt, samples);
		printf("toHostPipe got %d\n", read);
		if(read < 0)
		{
			printf("BAD: receiveFromRt toHostPipe read an error\n");
			usleep(1000);
			continue;
		} else if (0 == read) {
			printf("BAD: receiveFromRt toHostPipe read a 0\n");
			usleep(1000);
			continue;
		}
		totalRead += read;
		samples -= read;
	}
	return totalRead;
}
void AlsaIo::inputTask(void* obj)
{
	AlsaIo* that = (AlsaIo*)obj;
	that->hostInput();
}
void AlsaIo::outputTask(void* obj)
{
	AlsaIo* that = (AlsaIo*)obj;
	that->hostOutput();
}
void AlsaIo::hostInput()
{
	printf("hostInput started\n");
	std::array<float,4096> inputArray;
	float* input = inputArray.data();
	while(!gShouldStop)
	{
		int k = inputDevice->pcm_wait();
		if(k <= 0)
		{
			printf("BAD: input k is %d\n", k);
			usleep(1000);
			continue;
		}
		//printf(": input k was %d\n", k);
		// TODO: check that k is smaller than inputArray.size()
		inputDevice->capt_init(k);
		inputDevice->capt_chan(0, input + 0, k, 2);
		inputDevice->capt_chan(1, input + 1, k, 2);
		inputDevice->capt_done(k);
		sendToRt(input, k);
	}
	printf("hostInput stopped\n");
}
void AlsaIo::hostOutput()
{
	printf("hostOutput started\n");
	std::array<float,4096> outputArray;
	float* output = outputArray.data();
	while(!gShouldStop)
	{
		logBuffers();
		int k = outputDevice->pcm_wait(); 
		if(k <= 0)
		{
			printf("BAD: output k is %d\n", k);
			usleep(1000);
			continue;
		}
		// printf(": output k was %d\n", k);
		// TODO: check that k is smaller than outputArray.size()
		receiveFromRt(output, k);
		outputDevice->play_init (k);
		outputDevice->play_chan (0, output + 0, k, 2);
		outputDevice->play_chan (1, output + 1, k, 2);
		outputDevice->play_done (k);
		// Clear all other outputs.
		for (int i = 2; i < outputDevice->nplay (); i++)
			outputDevice->clear_chan (i, k);
	}
	printf("hostOutput stopped\n");
}
#endif // ONE_THREAD

void AlsaIo::logBuffers()
{
	printf("fromHostBuffer %.3f(%5d), toHostBuffer %.3f(%5d), "
#ifdef ONE_THREAD
			"hostIoInputBuffer has %.3f(%5d)"
#endif // ONE_THREAD
			"\n",
		fromHostBuffer.available(),
		fromHostBuffer.availableToRead(),
		toHostBuffer.available(),
		toHostBuffer.availableToRead()
#ifdef ONE_THREAD
		,hostIoInputBuffer.available(),
		hostIoInputBuffer.availableToRead()
#endif // ONE_THREAD
	      );
}

void AlsaIo::cleanup()
{
#ifdef ONE_THREAD
	D->pcm_stop();
	delete D;
#else // ONE_THREAD
	inputDevice->pcm_stop();
	delete inputDevice;
	outputDevice->pcm_stop();
	delete outputDevice;
#endif // ONE_THREAD
}

