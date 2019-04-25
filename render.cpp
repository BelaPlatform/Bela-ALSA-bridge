#include <vector>
#include <array>
#include <Bela.h> // AuxiliaryTask

//#define TESTPIPE
#define USE_ZITA
#define DEBUG

#include "Pipe.h"

#ifdef DEBUG
#include <Gpio.h>
Gpio gpio0;
Gpio gpio1;
Gpio gpio2;
#endif /* DEBUG */

unsigned int sentSamples;
extern "C" {
extern int volatile gShouldStop;
};

#ifdef USE_ZITA
#include <zita-alsa-pcmi.h>
#include <AuxTaskNonRT.h>
#include <inttypes.h>
class AlsaIo
{
public:
	bool setup(const char* device, unsigned int fromHostChannels, unsigned int toHostChannels, unsigned int newBlockSize, float rate);
	void receive(float* fromHost, int samples);
	void send(float* toHost, int samples);
	void cleanup();
	static void task(void* nil);
private:
	void hostIo();
	enum { numBufs = 2 };
	std::array<std::vector<float>, numBufs> toHostBuffer;
	std::array<std::vector<float>, numBufs> fromHostBuffer;
	float *buf;
	const char* playdev;
	const char* captdev;
	int blockSize;
	Alsa_pcmi  *D;
	AuxiliaryTask auxTask;
	Pipe toHostPipe;
	Pipe fromHostPipe;
};

bool AlsaIo::setup(const char* device, unsigned int fromHostChannels, unsigned int toHostChannels, unsigned int newBlockSize, float rate)
{
	toHostPipe.setup("UACToHost", 8192*16, false);
	fromHostPipe.setup("UACFromHost", 8192*16, false);
	blockSize = newBlockSize;
	for(unsigned int n = 0; n < numBufs; ++n)
	{
		toHostBuffer[n].resize(toHostChannels * blockSize);
		fromHostBuffer[n].resize(fromHostChannels * blockSize);
	}
	playdev = device;
	captdev = device;
	int fsamp = (int)(rate + 0.5f);
	int nfrags = 2;

	// Interleaved buffer for channels 1 and 2.
	buf = new float [blockSize * 2];

	// Create and initialise the audio device.
	D = new Alsa_pcmi (playdev, captdev, 0, fsamp, blockSize, nfrags, 0);
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
	Bela_scheduleAuxiliaryTask(auxTask);
	return true;
}

void AlsaIo::send(float* toHost, int samples)
{
	toHostPipe.writeRt(toHost, samples);
}

void AlsaIo::receive(float* fromHost, int samples)
{
	// always read the latest block available from the host
	int ret = fromHostPipe.readRt(fromHost, samples);
	if(ret == samples)
	{
		// there were indeed some samples available
		// let's make sure we have the most recent block by trying again
		// until we dry the pipe
		int count = 0;
		while(samples == fromHostPipe.readRt(fromHost, samples))
			rt_printf("render is drying the pipe %d \n", count++);
	} else {
		// otherwise, we did not receive anything. We should block
		// until the next block becomes available
		//rt_printf("render is waiting for samples %d\n", ret);
		fromHostPipe.setRtBlocking(true);
		int ret = fromHostPipe.readRt(fromHost, samples);
		if(ret != samples)
		{
			rt_printf("render did receive something, but %d\n", ret);
		}
		fromHostPipe.setRtBlocking(false);
	}
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
		//rt_printf("pcm_wait_done\n");
#ifdef DEBUG
		gpio2.set();
#endif /* DEBUG */
		if (k < blockSize)
		{
			// Normally we shouldn't do this in a real-time context.
			fprintf (stderr, "Error: pcm_wait returned %d.\n", k);
			continue;
		}
		int bufferCount = 0;
		while (k >= blockSize)
		{
			// for now, let's just use one of these buffers as scratchpads.
			auto& toHostVec = toHostBuffer[0];
			float* toHost = toHostVec.data();
			auto& fromHostVec = fromHostBuffer[0];
			float* fromHost = fromHostVec.data();

			toHostPipe.readNonRt(toHost, toHostVec.size());
			if(k == blockSize)
			{
				// this is the last buffer we have to write now: let's dry out the pipe
				// read the most recent buffer available (discard unused ones)
				while(toHostPipe.readNonRt(toHost, toHostVec.size()) == toHostVec.size())
					printf("Read another buffer: %d\n", bufferCount++);
			}
			//rt_printf("finished readingNonRt\n");

			// Copy the first 2 inputs to the first two outputs.
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
			
			fromHostPipe.writeNonRt(fromHost, fromHostVec.size());
			//rt_printf("finished writingNonRt\n");
			k -= blockSize;
		}
#ifdef GET_TIME
		ret = clock_gettime(CLOCK_REALTIME, &tp2);
		if(ret){
			fprintf(stderr, "Failed to read clock\n");
		} else {
			long long int ns1 = tp1.tv_sec * 1000000000ULL + tp1.tv_nsec;
			long long int ns2 = tp2.tv_sec * 1000000000ULL + tp2.tv_nsec;
			long long int delta = ns2 - ns1;
			printf("Took %lld\n", delta);
		}
#endif /* GET_TIME */
#ifdef DEBUG
		gpio2.clear();
#endif /* DEBUG */
	}
	while((k = D->pcm_wait()));
	printf("hostIo finished: k is %d\n", k);
}

void AlsaIo::cleanup()
{
	// Stop the audio device.
	D->pcm_stop ();

	delete D;
	delete[] buf;
}

#endif /* USE_ZITA */

#include <Bela.h>
#include <math.h>
#include <Scope.h>
#include "Pipe.h"

AlsaIo alsaIo;
Scope scope;
#ifdef TESTPIPE
void testPipe();
#endif

bool setup(BelaContext* context, void*)
{
#ifdef TESTPIPE
	testPipe();
#endif
#ifdef DEBUG
	gpio0.open(30, OUTPUT);
	gpio1.open(31, OUTPUT);
	gpio2.open(23, OUTPUT);
#endif /* DEBUG */
	unsigned int alsaBlockSize = 128;
	if(context->audioFrames != alsaBlockSize)
	{
		fprintf(stderr, "You should run this example with %d samples per block.\n", alsaBlockSize);
		return false;
	}
	alsaIo.setup("hw:CARD=UAC2Gadget,DEV=0", context->audioInChannels, context->audioOutChannels, alsaBlockSize, context->audioSampleRate);
	scope.setup(3, context->audioSampleRate);
	return true;
}

void render(BelaContext* context, void*)
{
	alsaIo.send((float*)context->audioIn, context->audioInChannels * context->audioFrames);
	alsaIo.receive((float*)context->audioOut, context->audioOutChannels * context->audioFrames);
	return;

	static float phase;
	static int count = 0;
	for(unsigned int n = 0; n < context->audioFrames; ++n)
	{
		phase += 1.f * 2.f * (float)M_PI / context->audioSampleRate;
		if(phase > M_PI)
			phase -= 2.f * (float)M_PI;
		audioWrite(context, n, 0, sinf(phase) * 0.2 + 0.5);
		audioWrite(context, n, 1, sinf(phase+0.5f*M_PI) * 0.2 + 0.5);
		count++;
	}
	for(unsigned int n = 0; n < context->audioFrames; ++n)
	{
		for(unsigned int ch = 0; ch < context->audioInChannels; ++ch)
			audioWrite(context, n, ch, audioRead(context, n, ch));
		scope.log(audioRead(context, n , -1), audioRead(context, n,1));
	}
}

void cleanup(BelaContext* context, void*)
{

}
