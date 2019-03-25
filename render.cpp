#include <vector>
#include <array>
#define USE_ZITA
unsigned int sentSamples;
extern "C" {
extern int volatile gShouldStop;
};

#ifdef USE_ASOUND // broken, so please don't
#include <alsa/asoundlib.h>
class AlsaIo
{
public:
	bool setup(const char* device, unsigned int fromHostChannels, unsigned int toHostChannels, float rate);
	void send(float* buf, int count);
	void cleanup();
private:
	snd_pcm_t *playback_handle = nullptr;
	snd_pcm_t *capture_handle = nullptr;
	unsigned int buf_frames;
	unsigned int toHostFramesAvailable;
	unsigned int fromHostFramesAvailable;
	std::vector<int16_t> toHostBuffer;
	std::vector<int16_t> fromHostBuffer;
};

bool AlsaIo::setup(const char* device, unsigned int fromHostChannels, unsigned int toHostChannels, float rate)
{
	toHostBuffer.resize(toHostChannels);
	fromHostBuffer.resize(fromHostChannels);
	toHostFramesAvailable = 0;
	fromHostFramesAvailable = 0;
	snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;
	rate = 44100;
	fromHostChannels = 2;
	toHostChannels = 2;
	unsigned int nchannels = 2;
	buf_frames = 128;
	int err;

	if ((err = snd_pcm_open (&playback_handle, device, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
		fprintf (stderr, "cannot open audio device %s (%s)\n", device, snd_strerror (err)); exit (1);
	}

	if ((err = snd_pcm_set_params(playback_handle, format,  SND_PCM_ACCESS_RW_INTERLEAVED, nchannels, rate, 1, 500000)) < 0) {   /* 0.5sec */
		fprintf(stderr, "Playback open error: %s\n", snd_strerror(err)); exit(1);
	}

	if ((err = snd_pcm_open (&capture_handle, device, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
	fprintf (stderr, "cannot open audio device %s (%s)\n", device, snd_strerror (err)); exit (1);
	}

	if ((err = snd_pcm_set_params(capture_handle, format,  SND_PCM_ACCESS_RW_INTERLEAVED, nchannels, rate, 1, 500000)) < 0) {   /* 0.5sec */
		fprintf(stderr, "capture open error: %s\n", snd_strerror(err)); exit(1);
	}

	return true;
}

void AlsaIo::send(float* buf, int count)
{
	static int recSamples = 0;
	recSamples += count;
	unsigned int inThePipe = sentSamples - recSamples;
	if(inThePipe > 512)
	{
		//printf("%d %d\n", (inThePipe)*4, inThePipe);
		printf("Received %d bytes (%d samples) in total, sent %d bytes (%d samples) in total. In the pipe: %d bytes (%d samples), toHostAvailable: %d\n", recSamples*4, recSamples, sentSamples*4, sentSamples, (inThePipe)*4, inThePipe, toHostFramesAvailable);
	}
	int16_t* toHost = toHostBuffer.data();
	for(unsigned int n = 0; n < count; ++n)
	{
		toHost[n + toHostFramesAvailable] = buf[n] * 32768.f;
	}
	toHostFramesAvailable += count;
	//printf("toHostFramesAvailable: %d, buf_frames: %d\n", toHostFramesAvailable, buf_frames);
	if(1)
	while(toHostFramesAvailable >= buf_frames)
	{
		for(unsigned int n = 0; n < buf_frames; ++n)
		{
			//printf("[%3d] %d\n", n, toHost[n]);
		}
		int err;
		struct timespec tp1, tp2;
		int ret = clock_gettime(CLOCK_REALTIME, &tp1);
		printf("It \n");
		if ((err = snd_pcm_writei (playback_handle, toHost, buf_frames)) != buf_frames)
		{
		    fprintf (stderr, "write to audio interface failed (%s)\n",
				    snd_strerror (err)); exit (1);
		}
		ret = clock_gettime(CLOCK_REALTIME, &tp2);
		if(ret){
			fprintf(stderr, "Failed to read clock\n");
		} else {
			long long int ns1 = tp1.tv_sec * 1000000000ULL + tp1.tv_nsec;
			long long int ns2 = tp2.tv_sec * 1000000000ULL + tp2.tv_nsec;
			long long int delta = ns2 - ns1;
			printf("Took %lld\n", delta);
		}
		toHostFramesAvailable -= buf_frames;
	}
}
void AlsaIo::cleanup()
{
	snd_pcm_close (playback_handle);
	snd_pcm_close (capture_handle);
}
#endif /* USE_ASOUND */
#ifdef USE_ZITA
#include <zita-alsa-pcmi.h>
#include <AuxTaskNonRT.h>
#include <inttypes.h>
class AlsaIo
{
public:
	bool setup(const char* device, unsigned int fromHostChannels, unsigned int toHostChannels, unsigned int blockSize, float rate);
	void sendReceive(float* toHost, float* fromHost, int samples);
	void cleanup();
	static void alsaIoCallback(void* data, int size);
private:
	void hostIo(unsigned int hostCurrentBuffer, int samples, uint64_t count);
	AuxTaskNonRT outPipe;
	unsigned int buf_frames;
	unsigned int toHostFramesAvailable;
	unsigned int fromHostFramesAvailable;
	enum { numBufs = 2 };
	std::array<std::vector<float>, numBufs> toHostBuffer;
	std::array<std::vector<float>, numBufs> fromHostBuffer;
	int localCurrentBuffer;
	float *buf;
	const char* playdev;
	const char* captdev;
	int fsamp;
	int frsize;
	int nfrags;
	Alsa_pcmi  *D;
	int localPtr;
	struct toHostMsg_t {
		uint64_t count;
		AlsaIo* alsaIo;
		int samples;
		unsigned int hostBuffer;
	};
};

void AlsaIo::alsaIoCallback(void* data, int size)
{
	size_t msgSize = sizeof(struct toHostMsg_t);
	struct toHostMsg_t* msg = (struct toHostMsg_t*)data;
	while(size >= msgSize)
	{
		msg->alsaIo->hostIo(msg->hostBuffer, msg->samples, msg->count);
		++msg;
		size -= msgSize;
	}
}

bool AlsaIo::setup(const char* device, unsigned int fromHostChannels, unsigned int toHostChannels, unsigned int blockSize, float rate)
{
	outPipe.create("ALSA-usb", alsaIoCallback);
	localCurrentBuffer = 0;
	for(unsigned int n = 0; n < numBufs; ++n)
	{
		toHostBuffer[n].resize(toHostChannels * blockSize);
		fromHostBuffer[n].resize(fromHostChannels * blockSize);
	}
	buf_frames = toHostChannels * blockSize;
	playdev = device;
	captdev = device;
	fsamp = (int)(rate + 0.5f);
	frsize = blockSize;
	nfrags = 2;

	// Interleaved buffer for channels 1 and 2.
	buf = new float [frsize * 2];

	// Create and initialise the audio device.
	D = new Alsa_pcmi (playdev, captdev, 0, fsamp, frsize, nfrags, 0);
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
	return true;
}

void AlsaIo::sendReceive(float* toHost, float* fromHost, int samples)
{
	memcpy(toHostBuffer[localCurrentBuffer].data() + localPtr, toHost, samples * sizeof(toHost[0]));
	memcpy(fromHost, fromHostBuffer[localCurrentBuffer].data() + localPtr, samples * sizeof(fromHost[0]));
	localPtr += samples;
	if(localPtr == toHostBuffer[0].size())
	{
		struct toHostMsg_t msg;
		msg.alsaIo = this;
		msg.samples = localPtr;
		msg.count = 0;
		msg.hostBuffer = localCurrentBuffer;
		outPipe.schedule(&msg, sizeof(msg));
		++localCurrentBuffer;
		if(localCurrentBuffer >= numBufs)
			localCurrentBuffer = 0;
		localPtr = 0;
	}
	assert(localPtr < toHostBuffer[0].size());
}

void AlsaIo::hostIo(unsigned int hostCurrentBuffer, int samples, uint64_t count)
{
	static int recSamples = 0;
	recSamples += count;
	unsigned int inThePipe = sentSamples - recSamples;
	if(inThePipe > 512)
	{
		//printf("%d %d\n", (inThePipe)*4, inThePipe);
		printf("Received %d bytes (%d samples) in total, sent %d bytes (%d samples) in total. In the pipe: %d bytes (%d samples), toHostAvailable: %d\n", recSamples*4, recSamples, sentSamples*4, sentSamples, (inThePipe)*4, inThePipe, toHostFramesAvailable);
	}
	float* fromHost = fromHostBuffer[hostCurrentBuffer].data();
	float* toHost = toHostBuffer[hostCurrentBuffer].data();
	toHostFramesAvailable = samples;

	while(toHostFramesAvailable >= buf_frames)
	{
		int err;
//#define GET_TIME
#ifdef GET_TIME
		struct timespec tp1, tp2;
		int ret = clock_gettime(CLOCK_REALTIME, &tp1);
		printf("It \n");
#endif /* GET_TIME */
		int k = D->pcm_wait();  
		if (k < frsize)
		{
			// Normally we shouldn't do this in a real-time context.
			fprintf (stderr, "Error: pcm_wait returned %d.\n", k);
		}
		while (k >= frsize)
		{
			// Copy the first 2 inputs to the first two outputs.
			// Clear all other outputs.
			D->capt_init (frsize);
			D->capt_chan (0, fromHost + 0, frsize, 2);
			D->capt_chan (1, buf + 1, frsize, 2); // loopback
			D->capt_done (frsize);

			D->play_init (frsize);
			//D->play_chan (1, buf + 0, frsize, 2); //loopback
			D->play_chan (0, toHost + 0, frsize, 2); // send audioOut     
			D->play_chan (1, buf + 1, frsize, 2); // loopback
			for (int i = 2; i < D->nplay (); i++) D->clear_chan (i, frsize);
			D->play_done (frsize);

			k -= frsize;
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
		toHostFramesAvailable -= buf_frames;
	}
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

AlsaIo alsaIo;

bool setup(BelaContext* context, void*)
{
	unsigned int alsaBlockSize = 128;
	alsaIo.setup("hw:CARD=UAC2Gadget,DEV=0", context->audioInChannels, context->audioOutChannels, alsaBlockSize, context->audioSampleRate);
	return true;
}

void render(BelaContext* context, void*)
{
	static float phase;
	static int count = 0;
	for(unsigned int n = 0; n < context->audioFrames; ++n)
	{
		phase += 300.f * 2.f * (float)M_PI / context->audioSampleRate;
		if(phase > M_PI)
			phase -= 2.f * (float)M_PI;
		audioWrite(context, n, 0, sinf(phase));
		audioWrite(context, n, 1, sinf(phase+0.5f*M_PI));
		//audioWrite(context, n, 0, count);
		//audioWrite(context, n, 1, count);
		count++;
	}
	alsaIo.sendReceive(context->audioOut, (float*)context->audioIn, context->audioInChannels * context->audioFrames); // audioIn is overwritten but unused
}

void cleanup(BelaContext* context, void*)
{

}
