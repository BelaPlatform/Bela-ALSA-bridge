#include "AlsaIo.h"

#include <array>

extern "C" {
extern int volatile gShouldStop;
};

bool AlsaIo::setup(const char* device, unsigned int fromHostChannels, unsigned int toHostChannels, unsigned int newBlockSize, unsigned int circularBufferSize, float rate)
{
	toHostPipe.setup("UACToHost", 8192*16, false);
	fromHostPipe.setup("UACFromHost", 8192*16, false);
	blockSize = newBlockSize;
	hostIoInputBuffer.resize(newBlockSize * toHostChannels * 4);
	fromHostBuffer.resize(circularBufferSize * toHostChannels);
	toHostBuffer.resize(circularBufferSize * toHostChannels);
	// dummy write to start with the buffer half full (or half empty)
	hostIoInputBuffer.write(fromHostBuffer.data(), hostIoInputBuffer.size()/2);
	fromHostBuffer.write(hostIoInputBuffer.data(), fromHostBuffer.size()/2);
	toHostBuffer.write(hostIoInputBuffer.data(), toHostBuffer.size()/2);
	playdev = device;
	captdev = device;
	int fsamp = (int)(rate + 0.5f);
	int nfrags = 2;

	// Interleaved buffer for channels 1 and 2.
	buf = new float [blockSize * 2];

	// Create and initialise the audio device.
	D = new Alsa_pcmi (playdev, captdev, 0, fsamp, blockSize, nfrags, 0);
	printf("D:%p, this: %p\n", D, this);
	if (D->state ()) 
	{
		fprintf (stderr, "Can't open ALSA device\n");
		delete D;
		exit (1);
	}
	if ((D->ncapt () < 2) || (D->nplay () < 2))
	{
		fprintf (stderr, "Expected a stereo device.\n");
		delete D;
		exit (1);
	}
	D->printinfo ();

	// Start the audio device.
	D->pcm_start ();
	auxTask = Bela_createAuxiliaryTask(task, 94, "pcm_task", this);
	return true;
}

void AlsaIo::sendReceive(float* fromHost, float* toHost, int samples)
{
	Bela_scheduleAuxiliaryTask(auxTask);
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

void AlsaIo::sendToRt(float* toRt, int samples)
{
	fromHostPipe.writeNonRt(toRt, samples);
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
			printf("fromHostBuffer %.3f(%5d), toHostBuffer %.3f(%5d), hostIoInputBuffer has %.3f(%5d)\r",
				fromHostBuffer.available(),
				fromHostBuffer.availableToRead(),
				toHostBuffer.available(),
				toHostBuffer.availableToRead(),
				hostIoInputBuffer.available(),
				hostIoInputBuffer.availableToRead()
			      );
			fflush(stdout);
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

void AlsaIo::cleanup()
{
	// Stop the audio device.
	D->pcm_stop ();

	delete D;
	delete[] buf;
}

